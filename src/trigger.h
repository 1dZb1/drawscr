#pragma once
#include <windows.h>
#include <string>

namespace drawscr {
constexpr UINT WM_DRAW_TRIGGER = WM_APP + 30;
enum class TriggerKind { Keyboard = 0, Mouse = 1 };
enum class MouseButton : UINT { Left = 1, Right = 2, Middle = 3, X1 = 4, X2 = 5 };
struct Trigger {
    TriggerKind kind = TriggerKind::Keyboard;
    UINT modifiers = 0, code = VK_SNAPSHOT;
    bool override_printscreen = false;
};
enum class BindError {
    None,
    Invalid,
    SnippingConflict,
    Occupied,
    RegistrationUnavailable,
    HookUnavailable,
    StorageUnavailable
};
enum class SnippingState { Disabled, Enabled, Unknown };
bool same_trigger(const Trigger& a, const Trigger& b);
bool valid_trigger(const Trigger& trigger);
bool mouse_matches(const Trigger& trigger, UINT message, DWORD mouse_data, UINT held,
                   bool* down = nullptr);
SnippingState snipping_state();
BindError preliminary_conflict(const Trigger& trigger, SnippingState state);
std::wstring trigger_name(const Trigger& trigger);
struct TriggerOps {
    SnippingState (*snipping)();
    decltype(&RegisterHotKey) register_key;
    decltype(&UnregisterHotKey) unregister_key;
    decltype(&SetWindowsHookExW) set_hook;
    decltype(&UnhookWindowsHookEx) unhook;
};
class TriggerManager {
  public:
    using CanFire = bool (*)(void*);
    TriggerManager(HWND target, CanFire can_fire, void* context, const TriggerOps* ops = nullptr);
    ~TriggerManager();
    bool initialize(const Trigger& trigger, BindError& error);
    bool bind(const Trigger& trigger, BindError& error);
    bool resume(BindError& error);
    bool restore(const Trigger& trigger, BindError& error);
    void suspend();
    bool active() const {
        return active_;
    }
    DWORD last_error() const {
        return last_error_;
    }
    bool mouse_hooked() const {
        return hook_ != nullptr;
    }
    bool keyboard_hooked() const {
        return keyboard_hook_ != nullptr;
    }
    const Trigger& current() const {
        return current_;
    }

  private:
    static LRESULT CALLBACK mouse_proc(int, WPARAM, LPARAM);
    static LRESULT CALLBACK keyboard_proc(int, WPARAM, LPARAM);
    static TriggerManager* mouse_owner_;
    static TriggerManager* keyboard_owner_;
    bool install(const Trigger& trigger, BindError& error);
    const TriggerOps* ops_ = nullptr;
    DWORD last_error_ = 0;
    HWND target_ = nullptr;
    CanFire can_fire_ = nullptr;
    void* context_ = nullptr;
    Trigger current_{};
    HHOOK hook_ = nullptr;
    HHOOK keyboard_hook_ = nullptr;
    bool suppress_snapshot_ = false;
    bool active_ = false;
    UINT suppress_button_ = 0;
};
}
