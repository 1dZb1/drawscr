#include "shortcut_ui.h"
#include "paint_buffer.h"
#include "theme.h"
#include "ui_type.h"
#include "tooltips.h"
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>
using namespace Gdiplus;

namespace drawscr {
static constexpr UINT WM_BIND_KEY = WM_APP + 61;
static int sx(int v, UINT dpi) {
    return MulDiv(v, static_cast<int>(dpi), 96);
}
static void round_rect(Graphics& g, RectF r, Color fill, Color edge = Color(0, 0, 0, 0)) {
    float d = 12;
    GraphicsPath p;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
    SolidBrush b(fill);
    g.FillPath(&b, &p);
    if (edge.GetAlpha()) {
        Pen pen(edge, 1);
        g.DrawPath(&pen, &p);
    }
}
static void text(Graphics& g, const wchar_t* value, RectF r, float size, Color color,
                 int style = FontStyleRegular, bool centered = false) {
    UiTypeface family;
    Font font(style == FontStyleBold ? family.regular() : family.medium(), ui_size(size),
              style == FontStyleBold ? FontStyleBold : family.medium_style(), UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    if (centered) {
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
    }
    g.DrawString(value, -1, &font, r, &format, &brush);
}
static UINT held_modifiers() {
    UINT mods = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        mods |= MOD_CONTROL;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        mods |= MOD_SHIFT;
    if (GetAsyncKeyState(VK_MENU) & 0x8000)
        mods |= MOD_ALT;
    if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000)
        mods |= MOD_WIN;
    return mods;
}
static bool modifier_key(UINT key) {
    return key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL || key == VK_SHIFT ||
           key == VK_LSHIFT || key == VK_RSHIFT || key == VK_MENU || key == VK_LMENU ||
           key == VK_RMENU || key == VK_LWIN || key == VK_RWIN;
}
struct SettingsWindow {
    HWND window = nullptr;
    UINT dpi = 96;
    bool accepted = false, owned_by_window = false, conflict = false, start_minimized = false;
    int hover = -1, focus = 2;
    bool tracking = false;
    Tooltips tips;
    Trigger keyboard{}, mouse{TriggerKind::Mouse, 0, 0};
    TriggerKind kind = TriggerKind::Keyboard;
    StartupStore* startup = nullptr;
    SettingsActions actions;
    StartupState startup_state = StartupState::Off;
    std::wstring notice, config_tip;
    HHOOK capture_hook = nullptr;
    std::array<bool, 256> swallowed{};
    PaintBuffer buffer;
    static SettingsWindow* capture_owner;
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK capture_proc(int, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    void paint(HDC);
    Trigger& draft() {
        return kind == TriggerKind::Keyboard ? keyboard : mouse;
    }
    void stop_capture() {
        if (capture_hook) {
            UnhookWindowsHookEx(capture_hook);
            capture_hook = nullptr;
        }
        if (capture_owner == this)
            capture_owner = nullptr;
        swallowed.fill(false);
    }
    void sync_capture() {
        bool wanted = window && kind == TriggerKind::Keyboard && focus == 2 &&
                      GetFocus() == window && GetForegroundWindow() == window;
        if (!wanted) {
            stop_capture();
            return;
        }
        if (capture_hook)
            return;
        if (capture_owner && capture_owner != this) {
            notice = L"Keyboard capture is unavailable. Close the other settings window.";
            return;
        }
        capture_owner = this;
        capture_hook =
            SetWindowsHookExW(WH_KEYBOARD_LL, capture_proc, GetModuleHandleW(nullptr), 0);
        if (!capture_hook) {
            capture_owner = nullptr;
            notice = L"Keyboard capture is unavailable. Choose another trigger.";
        }
    }
    void refresh_conflict() {
        notice.clear();
        const Trigger& t = draft();
        conflict = t.kind == TriggerKind::Keyboard && t.code == VK_SNAPSHOT && !t.modifiers &&
                   !t.override_printscreen;
        std::wstring title = conflict ? L"Drawscr settings - Print Screen in use"
                                      : L"Drawscr settings - " + trigger_name(draft());
        SetWindowTextW(window, title.c_str());
    }
    int hit(POINT p) {
        p.x = MulDiv(p.x, 96, dpi);
        p.y = MulDiv(p.y, 96, dpi);
        if (p.x >= 410 && p.x < 448 && p.y >= 10 && p.y < 46)
            return 7;
        if (p.y >= 63 && p.y < 95)
            return p.x >= 20 && p.x < 225 ? 0 : p.x >= 235 && p.x < 440 ? 1 : -1;
        if (p.y >= 105 && p.y < 149 && p.x >= 20 && p.x < 440)
            return 2;
        if (p.y >= 205 && p.y < 249 && p.x >= 20 && p.x < 440)
            return 3;
        if (p.y >= 319 && p.y < 353 && p.x >= 20 && p.x < 440) {
            int i = (p.x - 20) / 106;
            int local = (p.x - 20) % 106;
            if (i < 4 && local < 102)
                return 8 + i;
        }
        if (p.y >= 390 && p.y < 426)
            return p.x >= 20 && p.x < 225 ? 4 : p.x >= 235 && p.x < 440 ? 5 : -1;
        if (p.y >= 478 && p.y < 502 && p.x >= 335 && p.x < 440)
            return 12;
        if (p.y >= 253 && p.y < 291 && p.x >= 20 && p.x < 440)
            return 13;
        if (p.y >= 20 && p.y < 49 && p.x >= 330 && p.x < 400)
            return 6;
        return -1;
    }
    void bind() {
        Trigger selected = draft();
        if (conflict)
            selected.override_printscreen = true;
        BindError result = actions.bind(selected);
        if (result == BindError::None) {
            accepted = true;
            PostMessageW(window, WM_CLOSE, 0, 0);
            return;
        }
        if (result == BindError::SnippingConflict ||
            (result == BindError::Occupied && selected.kind == TriggerKind::Keyboard &&
             selected.code == VK_SNAPSHOT && !selected.modifiers &&
             !selected.override_printscreen)) {
            conflict = true;
            notice.clear();
            SetWindowTextW(window, L"Drawscr settings - Print Screen in use");
        } else if (result == BindError::Occupied) {
            notice = L"That shortcut is already in use. Choose another.";
            SetWindowTextW(window, L"Drawscr settings - shortcut in use");
        } else if (result == BindError::RegistrationUnavailable)
            notice = L"Windows could not register that shortcut (error " +
                     std::to_wstring(actions.trigger_error ? actions.trigger_error() : 0) + L").";
        else if (result == BindError::HookUnavailable)
            notice = L"Windows could not install the trigger hook (error " +
                     std::to_wstring(actions.trigger_error ? actions.trigger_error() : 0) + L").";
        else if (result == BindError::Invalid)
            notice = L"Use Ctrl, Shift or Alt. Left and right mouse need a modifier.";
        else if (result == BindError::StorageUnavailable)
            notice = L"Could not save settings (Windows error " +
                     std::to_wstring(actions.storage_error ? actions.storage_error() : 0) +
                     L"). Check the settings file path below.";
        else
            notice = L"Windows could not enable that trigger. Choose another.";
        InvalidateRect(window, nullptr, FALSE);
    }
    void choose(int n) {
        if (n == 7 || n == 5) {
            PostMessageW(window, WM_CLOSE, 0, 0);
            return;
        }
        focus = n;
        if (n == 0 || n == 1) {
            kind = n ? TriggerKind::Mouse : TriggerKind::Keyboard;
            focus = 2;
            refresh_conflict();
        } else if (n == 2) {
            focus = 2;
            notice.clear();
        } else if (n == 3) {
            std::wstring error;
            if (!toggle_startup(*startup, startup_state, error))
                notice = error;
            else
                notice.clear();
        } else if (n == 4) {
            sync_capture();
            bind();
        } else if (n == 6) {
            kind = TriggerKind::Keyboard;
            keyboard = {TriggerKind::Keyboard, 0, VK_SNAPSHOT};
            focus = 2;
            refresh_conflict();
        } else if (n >= 8 && n <= 11) {
            ThemeId id = static_cast<ThemeId>(n - 8);
            if (!actions.set_theme || !actions.set_theme(id))
                notice = L"Could not save the theme. Check that the folder is writable.";
            else
                notice.clear();
        } else if (n == 12) {
            if (!actions.open_site || !actions.open_site())
                notice = L"Could not open dzrobo.com in your browser.";
            else
                notice.clear();
        } else if (n == 13) {
            bool desired = !start_minimized;
            if (!actions.set_start_minimized || !actions.set_start_minimized(desired))
                notice = L"Could not save startup preference (Windows error " +
                         std::to_wstring(actions.storage_error ? actions.storage_error() : 0) +
                         L").";
            else {
                start_minimized = desired;
                notice.clear();
            }
        }
        sync_capture();
        InvalidateRect(window, nullptr, FALSE);
    }
    void capture_mouse(UINT button) {
        if (kind != TriggerKind::Mouse || focus != 2)
            return;
        mouse = {TriggerKind::Mouse, held_modifiers(), button};
        refresh_conflict();
        InvalidateRect(window, nullptr, FALSE);
    }
};
SettingsWindow* SettingsWindow::capture_owner = nullptr;
LRESULT CALLBACK SettingsWindow::capture_proc(int code, WPARAM w, LPARAM l) {
    SettingsWindow* s = capture_owner;
    if (code != HC_ACTION || !s)
        return CallNextHookEx(nullptr, code, w, l);
    auto* data = reinterpret_cast<KBDLLHOOKSTRUCT*>(l);
    UINT key = data->vkCode;
    if (key >= s->swallowed.size())
        return CallNextHookEx(nullptr, code, w, l);
    bool down = w == WM_KEYDOWN || w == WM_SYSKEYDOWN, up = w == WM_KEYUP || w == WM_SYSKEYUP;
    if (up && s->swallowed[key]) {
        s->swallowed[key] = false;
        return 1;
    }
    if (down && s->swallowed[key])
        return 1;
    if (!down || s->kind != TriggerKind::Keyboard || s->focus != 2 ||
        GetForegroundWindow() != s->window || GetFocus() != s->window || modifier_key(key))
        return CallNextHookEx(nullptr, code, w, l);
    UINT held = held_modifiers();
    if (!held && (key == VK_ESCAPE || key == VK_TAB))
        return CallNextHookEx(nullptr, code, w, l);
    s->swallowed[key] = true;
    PostMessageW(s->window, WM_BIND_KEY, held, key);
    return 1;
}
void SettingsWindow::paint(HDC dc) {
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.ScaleTransform(dpi / 96.0f, dpi / 96.0f);
    g.Clear(Color(255, 27, 32, 34));
    round_rect(g, {.5f, .5f, 459, 509}, Color(255, 27, 32, 34), Color(255, 62, 73, 73));
    text(g, L"Settings", {20, 14, 295, 28}, 20, Color(255, 240, 243, 237), FontStyleBold);
    RectF def{330, 20, 70, 29};
    round_rect(g, def, hover == 6 ? Color(255, 52, 62, 63) : Color(255, 37, 45, 46));
    text(g, L"Default", def, 11, Color(255, 225, 235, 228), FontStyleRegular, true);
    RectF close{410, 10, 38, 36};
    round_rect(g, close, hover == 7 ? Color(255, 52, 62, 63) : Color(255, 27, 32, 34));
    Pen xpen(Color(255, 220, 232, 225), 1.7f);
    xpen.SetStartCap(LineCapRound);
    xpen.SetEndCap(LineCapRound);
    g.DrawLine(&xpen, 423, 21, 435, 33);
    g.DrawLine(&xpen, 435, 21, 423, 33);
    text(g, L"Capture trigger", {20, 43, 300, 20}, 12, Color(255, 166, 180, 176));
    for (int i = 0; i < 2; ++i) {
        RectF r{20.0f + i * 215, 63, 205, 32};
        bool selected = (i == 0) == (kind == TriggerKind::Keyboard);
        round_rect(g, r,
                   selected     ? ui_color(current_theme().accent)
                   : hover == i ? Color(255, 52, 62, 63)
                                : Color(255, 37, 45, 46));
        text(g, i ? L"Mouse" : L"Keyboard", r, 12,
             selected ? ui_color(current_theme().ink) : Color(255, 230, 236, 230), FontStyleRegular,
             true);
    }
    RectF field{20, 105, 420, 44};
    round_rect(g, field, Color(255, 37, 45, 46),
               focus == 2 ? ui_color(current_theme().border) : Color(255, 62, 73, 73));
    std::wstring value = trigger_name(kind == TriggerKind::Keyboard ? keyboard : mouse);
    text(g, value.c_str(), {32, 117, 396, 25}, 14, Color(255, 235, 241, 234));
    if (conflict)
        text(
            g,
            L"Windows may handle Print Screen outside Drawscr. Assign anyway intercepts it only while Drawscr is running.",
            {20, 156, 420, 41}, 11, Color(255, 239, 196, 153));
    else {
        text(g,
             kind == TriggerKind::Keyboard
                 ? L"Click the field, then press the keys."
                 : L"Click the field, then press a mouse button with Ctrl, Shift or Alt.",
             {20, 157, 420, 23}, 11, Color(255, 166, 180, 176));
        text(g,
             kind == TriggerKind::Keyboard ? L"Print Screen is the default."
                                           : L"Only the selected mouse gesture is captured.",
             {20, 176, 420, 22}, 10, Color(255, 148, 165, 158));
    }
    Pen separator(Color(255, 52, 63, 61), 1);
    g.DrawLine(&separator, 20, 199, 440, 199);
    text(g, L"Launch at Windows startup", {20, 208, 310, 24}, 13, Color(255, 233, 239, 232),
         FontStyleBold);
    const wchar_t* status =
        startup_state == StartupState::On      ? L"On  •  starts quietly in the tray"
        : startup_state == StartupState::Off   ? L"Off  •  starts quietly if enabled"
        : startup_state == StartupState::Other ? L"Click to enable startup for this app"
                                               : L"Windows startup settings unavailable";
    text(g, status, {20, 230, 330, 20}, 10, Color(255, 166, 180, 176));
    RectF toggle{370, 209, 70, 32};
    round_rect(g, toggle,
               startup_state == StartupState::On ? ui_color(current_theme().accent)
                                                 : Color(255, 37, 45, 46));
    text(g,
         startup_state == StartupState::On      ? L"On"
         : startup_state == StartupState::Error ? L"Error"
                                                : L"Off",
         toggle, 12,
         startup_state == StartupState::On ? ui_color(current_theme().ink)
                                           : Color(255, 230, 236, 230),
         FontStyleRegular, true);
    text(g, L"Start minimized to tray", {20, 257, 320, 24}, 13, Color(255, 233, 239, 232),
         FontStyleBold);
    text(g, L"Ordinary launch stays in the tray", {20, 278, 330, 18}, 10,
         Color(255, 166, 180, 176));
    RectF check{415, 260, 20, 20};
    round_rect(g, check,
               start_minimized ? ui_color(current_theme().accent) : Color(255, 37, 45, 46),
               Color(255, 90, 110, 104));
    if (start_minimized) {
        Pen p(ui_color(current_theme().ink), 1.7f);
        p.SetStartCap(LineCapRound);
        p.SetEndCap(LineCapRound);
        g.DrawLine(&p, 419, 270, 423, 274);
        g.DrawLine(&p, 423, 274, 431, 265);
    }
    text(g, L"Theme", {20, 296, 150, 20}, 12, Color(255, 192, 207, 199));
    for (int i = 0; i < 4; ++i) {
        const auto& t = theme_for(static_cast<ThemeId>(i));
        RectF r{20.0f + i * 106, 319, 102, 34};
        bool selected = current_theme_id() == static_cast<ThemeId>(i);
        round_rect(g, r,
                   selected         ? ui_color(t.soft)
                   : hover == 8 + i ? Color(255, 52, 62, 63)
                                    : Color(255, 37, 45, 46),
                   selected ? ui_color(t.border) : Color(255, 62, 73, 73));
        SolidBrush dot(ui_color(t.accent));
        g.FillEllipse(&dot, r.X + 9, r.Y + 11, 12.0f, 12.0f);
        text(g, t.name, {r.X + 27, r.Y + 7, 71, 22}, 11, Color(255, 230, 236, 230));
    }
    if (!notice.empty())
        text(g, notice.c_str(), {20, 359, 420, 29}, 10, Color(255, 239, 188, 143));
    RectF save{20, 390, 205, 36}, cancel{235, 390, 205, 36};
    round_rect(g, save, ui_color(current_theme().accent));
    round_rect(g, cancel, hover == 5 ? Color(255, 52, 62, 63) : Color(255, 37, 45, 46));
    text(g, conflict ? L"Assign anyway" : L"Save trigger", save, 13, ui_color(current_theme().ink),
         FontStyleRegular, true);
    text(g, L"Close", cancel, 13, Color(255, 230, 236, 230), FontStyleRegular, true);
    std::wstring config =
        L"Settings file: " + (actions.config_path ? actions.config_path() : L"(portable folder)");
    text(g, config.c_str(), {20, 433, 420, 35}, 10, Color(255, 166, 180, 176));
    text(g, L"by DZRobo&SI", {20, 481, 180, 20}, 11, Color(255, 166, 180, 176));
    text(g, L"dzrobo.com", {335, 479, 105, 22}, 11, ui_color(current_theme().frame),
         FontStyleRegular, true);
    if (window && GetFocus() == window && focus >= 0 && focus <= 13) {
        RectF r[] = {{20, 63, 205, 32},
                     {235, 63, 205, 32},
                     field,
                     {20, 205, 420, 44},
                     save,
                     cancel,
                     def,
                     close,
                     {20, 319, 102, 34},
                     {126, 319, 102, 34},
                     {232, 319, 102, 34},
                     {338, 319, 102, 34},
                     {335, 478, 105, 24},
                     {20, 253, 420, 38}};
        Pen p(ui_color(current_theme().focus), 1);
        g.DrawRectangle(&p, r[focus].X + 2, r[focus].Y + 2, r[focus].Width - 4,
                        r[focus].Height - 4);
    }
}
LRESULT CALLBACK SettingsWindow::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* s = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        s = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        s->window = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (m == WM_NCDESTROY && s) {
        s->stop_capture();
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        s->window = nullptr;
        if (s->owned_by_window) {
            auto closed = std::move(s->actions.closed);
            bool accepted = s->accepted;
            delete s;
            if (closed)
                closed(accepted);
        }
        return DefWindowProcW(h, m, w, l);
    }
    return s ? s->message(m, w, l) : DefWindowProcW(h, m, w, l);
}
LRESULT SettingsWindow::message(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        RECT r{};
        GetClientRect(window, &r);
        HDC buffered = buffer.prepare(dc, r.right, r.bottom);
        if (buffered) {
            paint(buffered);
            GdiFlush();
            buffer.present(dc, ps.rcPaint);
        } else
            paint(dc);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        paint(reinterpret_cast<HDC>(w));
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
        sync_capture();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_KILLFOCUS:
        stop_capture();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        int n = hit({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (n != hover) {
            hover = n;
            InvalidateRect(window, nullptr, FALSE);
        }
        if (!tracking) {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, window, 0};
            TrackMouseEvent(&t);
            tracking = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking = false;
        hover = -1;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            POINT p{};
            GetCursorPos(&p);
            ScreenToClient(window, &p);
            if (hit(p) == 12) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        break;
    case WM_LBUTTONDOWN: {
        int n = hit({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (n == 2) {
            focus = 2;
            SetFocus(window);
            sync_capture();
            if (kind == TriggerKind::Mouse && held_modifiers())
                capture_mouse(static_cast<UINT>(MouseButton::Left));
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int n = hit({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (n >= 0) {
            if (n != 2 || kind != TriggerKind::Mouse || !held_modifiers())
                choose(n);
            if (n != 12)
                SetFocus(window);
        }
        return 0;
    }
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN: {
        if (hit({GET_X_LPARAM(l), GET_Y_LPARAM(l)}) == 2 && kind == TriggerKind::Mouse) {
            focus = 2;
            UINT button = m == WM_RBUTTONDOWN     ? static_cast<UINT>(MouseButton::Right)
                          : m == WM_MBUTTONDOWN   ? static_cast<UINT>(MouseButton::Middle)
                          : HIWORD(w) == XBUTTON1 ? static_cast<UINT>(MouseButton::X1)
                                                  : static_cast<UINT>(MouseButton::X2);
            capture_mouse(button);
            return m == WM_XBUTTONDOWN ? TRUE : 0;
        }
        break;
    }
    case WM_XBUTTONUP:
        if (kind == TriggerKind::Mouse)
            return TRUE;
        break;
    case WM_BIND_KEY:
        if (kind == TriggerKind::Keyboard && focus == 2) {
            keyboard = {TriggerKind::Keyboard, static_cast<UINT>(w), static_cast<UINT>(l)};
            refresh_conflict();
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        UINT held = held_modifiers();
        if (w == VK_ESCAPE && !held) {
            PostMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        if (w == VK_TAB && !(held & (MOD_CONTROL | MOD_ALT | MOD_WIN))) {
            const int order[] = {0, 1, 2, 3, 13, 8, 9, 10, 11, 12, 4, 5, 6, 7};
            int pos = 0;
            while (pos < 14 && order[pos] != focus)
                ++pos;
            if (pos == 14)
                pos = 0;
            int step = (held & MOD_SHIFT) ? -1 : 1;
            focus = order[(pos + 14 + step) % 14];
            sync_capture();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if ((w == VK_RETURN || w == VK_SPACE) && !held && focus != 2) {
            choose(focus);
            return 0;
        }
        return 0;
    }
    case WM_SYSKEYUP:
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        stop_capture();
        tips.reset();
        return 0;
    }
    return DefWindowProcW(window, m, w, l);
}
HWND show_settings(HWND owner, const Trigger& current, StartupStore& startup,
                   SettingsActions actions, BindError initial_error, bool start_minimized) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = SettingsWindow::proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Drawscr.Settings";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    auto s = std::make_unique<SettingsWindow>();
    s->kind = current.kind;
    if (current.kind == TriggerKind::Keyboard)
        s->keyboard = current;
    else
        s->mouse = current;
    s->startup = &startup;
    s->actions = std::move(actions);
    s->notice = s->actions.initial_notice;
    s->start_minimized = start_minimized;
    s->startup_state = startup.query();
    s->dpi = GetDpiForWindow(owner);
    if (!s->dpi)
        s->dpi = 96;
    bool bare_print = current.kind == TriggerKind::Keyboard && current.code == VK_SNAPSHOT &&
                      !current.modifiers && !current.override_printscreen;
    s->conflict = bare_print;
    if (initial_error == BindError::Occupied && !s->conflict)
        s->notice = L"That shortcut is already in use. Choose another.";
    HWND h = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName,
        s->conflict ? L"Drawscr settings - Print Screen in use" : L"Drawscr settings", WS_POPUP, 0,
        0, sx(460, s->dpi), sx(510, s->dpi), owner, nullptr, wc.hInstance, s.get());
    if (!h)
        return nullptr;
    SettingsWindow* state = s.release();
    state->owned_by_window = true;
    RECT anchor{};
    GetWindowRect(owner, &anchor);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &mi);
    int width = sx(460, state->dpi), height = sx(510, state->dpi),
        x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - width) / 2,
        y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - height) / 2;
    SetWindowPos(h, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    HRGN region = CreateRoundRectRgn(0, 0, width, height, sx(16, state->dpi), sx(16, state->dpi));
    if (!SetWindowRgn(h, region, TRUE))
        DeleteObject(region);
    SetWindowDisplayAffinity(h, 0x11);
    if (state->tips.create(h)) {
        state->config_tip = state->actions.config_path ? state->actions.config_path() : L"";
        state->tips.add(15, {20, 433, 440, 468}, state->config_tip.c_str(), state->dpi);
        const RECT rects[] = {{20, 63, 225, 95},    {235, 63, 440, 95},   {20, 105, 440, 149},
                              {20, 205, 440, 249},  {20, 390, 225, 426},  {235, 390, 440, 426},
                              {330, 20, 400, 49},   {410, 10, 448, 46},   {20, 319, 122, 353},
                              {126, 319, 228, 353}, {232, 319, 334, 353}, {338, 319, 440, 353},
                              {335, 478, 440, 502}, {20, 253, 440, 291}};
        const wchar_t* help[] = {L"Choose a keyboard shortcut",
                                 L"Choose a mouse shortcut",
                                 L"Press the keys or mouse button for capture",
                                 L"Toggle launch at Windows startup",
                                 L"Save the capture trigger",
                                 L"Close settings",
                                 L"Restore Print Screen as the default",
                                 L"Close settings",
                                 L"Use the mint theme",
                                 L"Use the sky blue theme",
                                 L"Use the amber theme",
                                 L"Use the violet theme",
                                 L"Open dzrobo.com in your browser",
                                 L"Start normal launches minimized to the tray"};
        for (UINT i = 0; i < 14; ++i)
            state->tips.add(i + 1, rects[i], help[i], state->dpi);
    }
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetFocus(h);
    state->sync_capture();
    return h;
}
}
