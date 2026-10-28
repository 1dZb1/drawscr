#include "trigger.h"
#include <algorithm>
#include <cwchar>

namespace drawscr {
static constexpr UINT allowed = MOD_CONTROL | MOD_SHIFT | MOD_ALT;
bool same_trigger(const Trigger& a, const Trigger& b) {
    return a.kind == b.kind && a.modifiers == b.modifiers && a.code == b.code &&
           a.override_printscreen == b.override_printscreen;
}
bool valid_trigger(const Trigger& t) {
    if (t.modifiers & ~allowed)
        return false;
    if (t.override_printscreen &&
        (t.kind != TriggerKind::Keyboard || t.modifiers || t.code != VK_SNAPSHOT))
        return false;
    if (t.kind == TriggerKind::Keyboard) {
        if (!t.code || t.code > 255 || t.code == VK_CONTROL || t.code == VK_SHIFT ||
            t.code == VK_MENU || t.code == VK_LWIN || t.code == VK_RWIN || t.code == VK_F12)
            return false;
        if (!t.modifiers && t.code != VK_SNAPSHOT && (t.code < VK_F1 || t.code > VK_F24))
            return false;
        return true;
    }
    if (t.kind != TriggerKind::Mouse || t.code < 1 || t.code > 5)
        return false;
    if (!t.modifiers && (t.code == static_cast<UINT>(MouseButton::Left) ||
                         t.code == static_cast<UINT>(MouseButton::Right)))
        return false;
    return true;
}
static UINT button_for(UINT message, DWORD data, bool& down) {
    down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN ||
           message == WM_XBUTTONDOWN;
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        return static_cast<UINT>(MouseButton::Left);
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        return static_cast<UINT>(MouseButton::Right);
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        return static_cast<UINT>(MouseButton::Middle);
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        return HIWORD(data) == XBUTTON1   ? static_cast<UINT>(MouseButton::X1)
               : HIWORD(data) == XBUTTON2 ? static_cast<UINT>(MouseButton::X2)
                                          : 0;
    default:
        return 0;
    }
}
bool mouse_matches(const Trigger& trigger, UINT message, DWORD data, UINT held, bool* is_down) {
    bool down = false;
    UINT button = button_for(message, data, down);
    if (is_down)
        *is_down = down;
    return trigger.kind == TriggerKind::Mouse && button == trigger.code &&
           held == trigger.modifiers;
}
SnippingState snipping_state() {
    DWORD value = 0, size = sizeof(value), type = 0;
    LSTATUS status =
        RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Keyboard",
                     L"PrintScreenKeyForSnippingEnabled", RRF_RT_REG_DWORD, &type, &value, &size);
    if (status != ERROR_SUCCESS)
        return SnippingState::Unknown;
    return value ? SnippingState::Enabled : SnippingState::Disabled;
}
BindError preliminary_conflict(const Trigger& t, SnippingState state) {
    if (!valid_trigger(t))
        return BindError::Invalid;
    if (t.kind == TriggerKind::Keyboard && t.code == VK_SNAPSHOT && !t.modifiers &&
        !t.override_printscreen && state == SnippingState::Enabled)
        return BindError::SnippingConflict;
    return BindError::None;
}
std::wstring trigger_name(const Trigger& t) {
    std::wstring out;
    if (t.modifiers & MOD_CONTROL)
        out += L"Ctrl + ";
    if (t.modifiers & MOD_SHIFT)
        out += L"Shift + ";
    if (t.modifiers & MOD_ALT)
        out += L"Alt + ";
    if (t.kind == TriggerKind::Mouse) {
        switch (static_cast<MouseButton>(t.code)) {
        case MouseButton::Left:
            out += L"Left mouse";
            break;
        case MouseButton::Right:
            out += L"Right mouse";
            break;
        case MouseButton::Middle:
            out += L"Middle mouse";
            break;
        case MouseButton::X1:
            out += L"Mouse side 1";
            break;
        case MouseButton::X2:
            out += L"Mouse side 2";
            break;
        default:
            out += L"Choose a mouse button";
        }
        return out;
    }
    if (t.code == VK_SNAPSHOT) {
        out += L"Print Screen";
        return out;
    }
    UINT scan = MapVirtualKeyW(t.code, MAPVK_VK_TO_VSC);
    wchar_t name[64]{};
    if (GetKeyNameTextW(static_cast<LONG>(scan << 16), name, 64) > 0)
        out += name;
    else if (t.code)
        out += L"Key " + std::to_wstring(t.code);
    else
        out += L"Choose a key";
    return out;
}
TriggerManager* TriggerManager::mouse_owner_ = nullptr;
TriggerManager* TriggerManager::keyboard_owner_ = nullptr;
static const TriggerOps system_ops{snipping_state, RegisterHotKey, UnregisterHotKey,
                                   SetWindowsHookExW, UnhookWindowsHookEx};
TriggerManager::TriggerManager(HWND target, CanFire can_fire, void* context, const TriggerOps* ops)
    : ops_(ops ? ops : &system_ops), target_(target), can_fire_(can_fire), context_(context) {}
TriggerManager::~TriggerManager() {
    suspend();
}
bool TriggerManager::install(const Trigger& t, BindError& error) {
    last_error_ = 0;
    error = preliminary_conflict(t, ops_->snipping());
    if (error != BindError::None)
        return false;
    if (t.kind == TriggerKind::Keyboard) {
        if (t.override_printscreen) {
            if (keyboard_owner_ && keyboard_owner_ != this) {
                error = BindError::HookUnavailable;
                return false;
            }
            keyboard_owner_ = this;
            keyboard_hook_ =
                ops_->set_hook(WH_KEYBOARD_LL, keyboard_proc, GetModuleHandleW(nullptr), 0);
            if (!keyboard_hook_) {
                last_error_ = GetLastError();
                keyboard_owner_ = nullptr;
                error = BindError::HookUnavailable;
                return false;
            }
            active_ = true;
            return true;
        }
        if (!ops_->register_key(target_, 1, t.modifiers | MOD_NOREPEAT, t.code)) {
            last_error_ = GetLastError();
            if (!last_error_)
                last_error_ = ERROR_GEN_FAILURE;
            error = last_error_ == ERROR_HOTKEY_ALREADY_REGISTERED
                        ? BindError::Occupied
                        : BindError::RegistrationUnavailable;
            return false;
        }
        active_ = true;
        return true;
    }
    if (mouse_owner_ && mouse_owner_ != this) {
        error = BindError::HookUnavailable;
        return false;
    }
    mouse_owner_ = this;
    hook_ = ops_->set_hook(WH_MOUSE_LL, mouse_proc, GetModuleHandleW(nullptr), 0);
    if (!hook_) {
        last_error_ = GetLastError();
        mouse_owner_ = nullptr;
        error = BindError::HookUnavailable;
        return false;
    }
    active_ = true;
    return true;
}
bool TriggerManager::initialize(const Trigger& t, BindError& error) {
    current_ = t;
    return install(t, error);
}
bool TriggerManager::bind(const Trigger& t, BindError& error) {
    if (active_ && same_trigger(current_, t)) {
        error = BindError::None;
        return true;
    }
    bool restore = active_;
    Trigger old = current_;
    if (active_)
        suspend();
    if (install(t, error)) {
        current_ = t;
        return true;
    }
    if (restore) {
        DWORD cause = last_error_;
        BindError ignored{};
        install(old, ignored);
        current_ = old;
        last_error_ = cause;
    }
    return false;
}
bool TriggerManager::resume(BindError& error) {
    if (active_) {
        error = BindError::None;
        return true;
    }
    return install(current_, error);
}
bool TriggerManager::restore(const Trigger& trigger, BindError& error) {
    suspend();
    current_ = trigger;
    return install(current_, error);
}
void TriggerManager::suspend() {
    if (hook_) {
        ops_->unhook(hook_);
        hook_ = nullptr;
        if (mouse_owner_ == this)
            mouse_owner_ = nullptr;
        suppress_button_ = 0;
    }
    if (keyboard_hook_) {
        ops_->unhook(keyboard_hook_);
        keyboard_hook_ = nullptr;
        if (keyboard_owner_ == this)
            keyboard_owner_ = nullptr;
        suppress_snapshot_ = false;
    }
    if (active_ && current_.kind == TriggerKind::Keyboard && !current_.override_printscreen)
        ops_->unregister_key(target_, 1);
    active_ = false;
}
static UINT async_modifiers() {
    UINT held = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        held |= MOD_CONTROL;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        held |= MOD_SHIFT;
    if (GetAsyncKeyState(VK_MENU) & 0x8000)
        held |= MOD_ALT;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000)
        held |= MOD_WIN;
    return held;
}
LRESULT CALLBACK TriggerManager::keyboard_proc(int code, WPARAM w, LPARAM l) {
    TriggerManager* owner = keyboard_owner_;
    if (code != HC_ACTION || !owner)
        return CallNextHookEx(nullptr, code, w, l);
    auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(l);
    if (info->vkCode != VK_SNAPSHOT)
        return CallNextHookEx(nullptr, code, w, l);
    bool down = w == WM_KEYDOWN || w == WM_SYSKEYDOWN, up = w == WM_KEYUP || w == WM_SYSKEYUP;
    if (up && owner->suppress_snapshot_) {
        owner->suppress_snapshot_ = false;
        return 1;
    }
    if (down && owner->suppress_snapshot_)
        return 1;
    if (down && owner->current_.override_printscreen && !async_modifiers() &&
        (!owner->can_fire_ || owner->can_fire_(owner->context_))) {
        owner->suppress_snapshot_ = true;
        PostMessageW(owner->target_, WM_DRAW_TRIGGER, 0, 0);
        return 1;
    }
    return CallNextHookEx(nullptr, code, w, l);
}
LRESULT CALLBACK TriggerManager::mouse_proc(int code, WPARAM w, LPARAM l) {
    TriggerManager* owner = mouse_owner_;
    if (code != HC_ACTION || !owner)
        return CallNextHookEx(nullptr, code, w, l);
    auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(l);
    bool down = false;
    UINT button = button_for(static_cast<UINT>(w), info->mouseData, down);
    if (!button)
        return CallNextHookEx(nullptr, code, w, l);
    if (!down && owner->suppress_button_ == button) {
        owner->suppress_button_ = 0;
        return 1;
    }
    if (!down || button != owner->current_.code)
        return CallNextHookEx(nullptr, code, w, l);
    UINT held = async_modifiers();
    if (mouse_matches(owner->current_, static_cast<UINT>(w), info->mouseData, held) &&
        (!owner->can_fire_ || owner->can_fire_(owner->context_))) {
        owner->suppress_button_ = button;
        PostMessageW(owner->target_, WM_DRAW_TRIGGER, 0, 0);
        return 1;
    }
    return CallNextHookEx(nullptr, code, w, l);
}
}
