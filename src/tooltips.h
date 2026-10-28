#pragma once
#include <windows.h>
#include <commctrl.h>

namespace drawscr {
class Tooltips {
  public:
    Tooltips() = default;
    Tooltips(const Tooltips&) = delete;
    Tooltips& operator=(const Tooltips&) = delete;
    ~Tooltips() {
        reset();
    }
    bool create(HWND owner) {
        INITCOMMONCONTROLSEX init{sizeof(init), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&init);
        owner_ = owner;
        window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
                                  TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, owner,
                                  nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window_)
            return false;
        if (!SetWindowSubclass(window_, tooltip_proc, 1, 0)) {
            DestroyWindow(window_);
            window_ = owner_ = nullptr;
            return false;
        }
        DWORD affinity = 0;
        excluded_ = SetWindowDisplayAffinity(window_, 0x11) != FALSE &&
                    GetWindowDisplayAffinity(window_, &affinity) != FALSE && affinity == 0x11;
        SendMessageW(window_, TTM_SETMAXTIPWIDTH, 0, 360);
        return true;
    }
    void add(UINT id, RECT rect, const wchar_t* text, UINT dpi) {
        if (!window_)
            return;
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_SUBCLASS;
        tool.hwnd = owner_;
        tool.uId = id;
        tool.lpszText = const_cast<LPWSTR>(text);
        tool.rect = {MulDiv(rect.left, dpi, 96), MulDiv(rect.top, dpi, 96),
                     MulDiv(rect.right, dpi, 96), MulDiv(rect.bottom, dpi, 96)};
        SendMessageW(window_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    void update(UINT id, RECT rect, UINT dpi) {
        if (!window_)
            return;
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.hwnd = owner_;
        tool.uId = id;
        tool.rect = {MulDiv(rect.left, dpi, 96), MulDiv(rect.top, dpi, 96),
                     MulDiv(rect.right, dpi, 96), MulDiv(rect.bottom, dpi, 96)};
        SendMessageW(window_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    void set_text(UINT id, const wchar_t* text) {
        if (!window_)
            return;
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.hwnd = owner_;
        tool.uId = id;
        tool.lpszText = const_cast<LPWSTR>(text);
        SendMessageW(window_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    void pop() {
        if (window_)
            SendMessageW(window_, TTM_POP, 0, 0);
    }
    void activate(bool enabled) {
        if (window_)
            SendMessageW(window_, TTM_ACTIVATE, enabled ? TRUE : FALSE, 0);
    }
    bool excluded() const {
        return excluded_;
    }
    void reset() {
        if (window_ && IsWindow(window_))
            DestroyWindow(window_);
        window_ = owner_ = nullptr;
        excluded_ = false;
    }

  private:
    static LRESULT CALLBACK tooltip_proc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                         DWORD_PTR) {
        if (message == WM_NCHITTEST)
            return HTTRANSPARENT;
        if (message == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(hwnd, tooltip_proc, id);
        return DefSubclassProc(hwnd, message, w, l);
    }
    HWND owner_ = nullptr, window_ = nullptr;
    bool excluded_ = false;
};
} // namespace drawscr
