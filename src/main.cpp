#include "session.h"
#include "resource.h"
#include "theme.h"
#include "website.h"
#include "tooltips.h"
#include "ui_type.h"
#include "shortcut_ui.h"
#include "preferences.h"
#include "launcher_view.h"
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <memory>
#include <string>
#include <thread>
using namespace Gdiplus;
using namespace drawscr;

static constexpr wchar_t app_class[] = L"Drawscr.Launcher";
static constexpr UINT WM_TRAY = WM_APP + 1;
static constexpr int TRAY_ID = 1;
static constexpr UINT MENU_AREA = 301, MENU_MONITOR = 302, MENU_SHORTCUT = 303, MENU_SHOW = 304,
                      MENU_EXIT = 305;

static HICON load_app_icon(HINSTANCE instance, int width, int height) {
    HICON icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_DRAWSCR), IMAGE_ICON,
                                               width, height, LR_DEFAULTCOLOR));
    return icon ? icon : CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
}
static INT_PTR shell_open_site(HWND owner, const wchar_t* url, void*) {
    return reinterpret_cast<INT_PTR>(
        ShellExecuteW(owner, L"open", url, nullptr, nullptr, SW_SHOWNORMAL));
}
class App;

class App {
  public:
    ~App() { if (font_warmup_.joinable()) font_warmup_.join(); }
    bool create(HINSTANCE instance, bool quiet);
    int run();
    BindError change_trigger(const Trigger& trigger);

  private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);

    LRESULT message(UINT, WPARAM, LPARAM);
    void paint(HDC);
    void action(int);
    void capture(bool);
    void shortcut();
    void tray(bool);
    void menu();
    void show();
    void load_trigger();
    bool save_trigger(const Trigger& trigger);
    static bool trigger_allowed(void* context);
    void fire_trigger();
    std::wstring shortcut_name() const;
    std::thread font_warmup_;
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HICON icon_ = nullptr, tray_icon_ = nullptr;
    std::unique_ptr<Session> session_;
    HANDLE mutex_ = nullptr;
    std::unique_ptr<TriggerManager> trigger_;
    std::unique_ptr<RegistryStartupStore> startup_;
    BindError trigger_error_ = BindError::None;
    bool settings_open_ = false, shutting_down_ = false;
    HWND settings_window_ = nullptr;
    UINT dpi_ = 96, taskbar_created_ = 0;
    int hover_ = -1, focus_ = 0;
    bool tracking_ = false;
    Tooltips tips_;
    std::wstring ini_, executable_, config_notice_;
    DWORD config_error_ = 0;
};
bool App::create(HINSTANCE instance, bool quiet) {
    instance_ = instance;
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    executable_ = path;
    ini_ = settings_path_for_executable(executable_);
    bool start_quiet = quiet || load_start_minimized(ini_);
    mutex_ = CreateMutexW(nullptr, TRUE, L"Local\\Drawscr.Singleton");
    if (!mutex_)
        return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(app_class, nullptr);
        if (existing && !start_quiet)
            PostMessageW(existing, WM_APP + 2, 0, 0);
        CloseHandle(mutex_);
        mutex_ = nullptr;
        return false;
    }
    // Quiet startup can defer first GDI+ font use until the first hotkey.
    // Warm only private offscreen objects; never capture the user's screen here.
    try {
        font_warmup_ = std::thread([] {
            try {
                Bitmap bitmap(32, 24, PixelFormat32bppARGB);
                Graphics graphics(&bitmap);
                UiTypeface family;
                Font font(family.medium(), 12, family.medium_style(), UnitPixel);
                SolidBrush ink(Color(255, 255, 255, 255));
                graphics.DrawString(L"Aa", -1, &font, PointF(0, 0), &ink);
                graphics.Flush();
            } catch (...) { /* Optional warmup must not terminate the application. */ }
        });
    } catch (...) { /* Capture still works if optional warmup cannot start. */ }
    set_current_theme(load_theme(ini_));
    startup_ = std::make_unique<RegistryStartupStore>(path);
    icon_ = load_app_icon(instance_, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    tray_icon_ =
        load_app_icon(instance_, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = instance_;
    wc.lpszClassName = app_class;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = icon_;
    wc.hIconSm = tray_icon_;
    RegisterClassExW(&wc);
    UINT initial_dpi = GetDpiForSystem();
    int width = MulDiv(450, initial_dpi, 96), height = MulDiv(250, initial_dpi, 96);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + (work.right - work.left - width) / 2,
        y = work.top + (work.bottom - work.top - height) / 2;
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, app_class, L"Drawscr",
                            WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX, x, y, width, height, nullptr,
                            nullptr, instance_, this);
    if (!hwnd_) {
        if (icon_)
            DestroyIcon(icon_);
        if (tray_icon_)
            DestroyIcon(tray_icon_);
        icon_ = tray_icon_ = nullptr;
        return false;
    }
    dpi_ = GetDpiForWindow(hwnd_);
    {
        HRGN region =
            CreateRoundRectRgn(0, 0, width, height, MulDiv(16, dpi_, 96), MulDiv(16, dpi_, 96));
        if (!SetWindowRgn(hwnd_, region, TRUE))
            DeleteObject(region);
    }
    taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
    load_trigger();
    tray(true);
    if (tips_.create(hwnd_)) {
        const RECT rects[] = {{24, 122, 220, 162},  {230, 122, 426, 162}, {24, 174, 220, 210},
                              {230, 174, 426, 210}, {368, 12, 402, 48},   {406, 12, 438, 48}};
        const wchar_t* help[] = {L"Select an area to capture",
                                 L"Capture the current monitor",
                                 L"Choose shortcut, startup and theme",
                                 L"Hide Drawscr to the system tray",
                                 L"Minimize to the tray",
                                 L"Close to the tray"};
        for (UINT i = 0; i < 6; ++i)
            tips_.add(i + 1, rects[i], help[i], dpi_);
    }
    if (!start_quiet) {
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
    } else if (!trigger_->active() || !config_notice_.empty()) {
        NOTIFYICONDATAW n{};
        n.cbSize = sizeof(n);
        n.hWnd = hwnd_;
        n.uID = TRAY_ID;
        n.uFlags = NIF_INFO;
        wcscpy_s(n.szInfoTitle, L"Drawscr");
        wcscpy_s(n.szInfo,
                 L"Capture trigger unavailable. Open Drawscr Settings to choose another.");
        n.dwInfoFlags = NIIF_WARNING;
        Shell_NotifyIconW(NIM_MODIFY, &n);
    }
    return true;
}
int App::run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    session_.reset();
    trigger_.reset();
    tray(false);
    if (icon_)
        DestroyIcon(icon_);
    if (tray_icon_)
        DestroyIcon(tray_icon_);
    if (mutex_) {
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
    }
    return static_cast<int>(msg.wParam);
}
void App::load_trigger() {
    auto saved = load_trigger_config(ini_);
    if (saved.status == ConfigStatus::Invalid)
        config_notice_ = L"Saved trigger is invalid. Default selected; open Settings.";
    else if (saved.status == ConfigStatus::Unreadable)
        config_notice_ = L"Settings cannot be read (Windows error " + std::to_wstring(saved.error) +
                         L"). Default selected.";
    trigger_ = std::make_unique<TriggerManager>(hwnd_, trigger_allowed, this);
    trigger_->initialize(saved.trigger, trigger_error_);
}
bool App::save_trigger(const Trigger& trigger) {
    return save_trigger_config(ini_, trigger, config_error_);
}
BindError App::change_trigger(const Trigger& selected) {
    Trigger previous = trigger_->current();
    BindError error{};
    if (!trigger_->bind(selected, error)) {
        trigger_error_ = error;
        return error;
    }
    if (!save_trigger(selected)) {
        BindError ignored{};
        trigger_->restore(previous, ignored);
        trigger_error_ = ignored;
        return BindError::StorageUnavailable;
    }
    trigger_error_ = BindError::None;
    config_notice_.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return BindError::None;
}
bool App::trigger_allowed(void* context) {
    auto* app = static_cast<App*>(context);
    return !app->settings_open_ &&
           (!app->session_ || (app->session_->is_live() && app->session_->is_interacting()));
}
void App::fire_trigger() {
    if (settings_open_)
        return;
    if (session_) {
        if (session_->is_live() && session_->is_interacting())
            session_->restore_draw();
    } else
        capture(false);
}
std::wstring App::shortcut_name() const {
    if (!trigger_)
        return L"Capture trigger unavailable";
    std::wstring value = trigger_name(trigger_->current());
    if (!trigger_->active())
        value += L" unavailable  -  open Settings";
    else if (!config_notice_.empty())
        value += L"  -  check Settings";
    else if (trigger_->current().kind == TriggerKind::Keyboard &&
             trigger_->current().code == VK_SNAPSHOT && !trigger_->current().modifiers &&
             !trigger_->current().override_printscreen)
        value += L"  -  see Assign anyway in Settings";
    return value;
}

void App::tray(bool add) {
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = hwnd_;
    n.uID = TRAY_ID;
    if (!add) {
        Shell_NotifyIconW(NIM_DELETE, &n);
        return;
    }
    n.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    n.uCallbackMessage = WM_TRAY;
    n.hIcon = tray_icon_;
    wcscpy_s(n.szTip, L"Drawscr");
    Shell_NotifyIconW(NIM_ADD, &n);
}
void App::show() {
    if (session_ && session_->is_recording())
        return;
    ShowWindow(hwnd_, SW_SHOW);
    ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(settings_window_ ? settings_window_ : hwnd_);
}
void App::menu() {
    if (session_ && session_->is_recording())
        return;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, MENU_AREA, L"Capture area");
    AppendMenuW(m, MF_STRING, MENU_MONITOR, L"Capture monitor");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, MENU_SHORTCUT, L"Settings");
    AppendMenuW(m, MF_STRING, MENU_SHOW, L"Open Drawscr");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, MENU_EXIT, L"Exit");
    POINT p{};
    GetCursorPos(&p);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd_, nullptr);
    DestroyMenu(m);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
}
void App::capture(bool whole) {
    const ULONGLONG capture_started = GetTickCount64();
    if (session_)
        return;
    if (settings_window_)
        SendMessageW(settings_window_, WM_CLOSE, 0, 0);
    tips_.pop();
    ShowWindow(hwnd_, SW_HIDE);
    // Session performs the single compositor synchronization after all hides.
    session_ = std::make_unique<Session>(hwnd_, ini_, capture_started);
    if (!session_->begin(whole)) {
        session_.reset();
        show();
    }
}
void App::shortcut() {
    if ((session_ && session_->is_recording()) || !trigger_ || !startup_)
        return;
    if (settings_window_) {
        SetForegroundWindow(settings_window_);
        return;
    }
    settings_open_ = true;
    trigger_->suspend();
    SettingsActions actions;
    actions.bind = [this](const Trigger& selected) { return change_trigger(selected); };
    actions.set_theme = [this](ThemeId id) {
        SetLastError(0);
        if (!save_theme(ini_, id)) {
            config_error_ = GetLastError();
            if (!config_error_)
                config_error_ = ERROR_WRITE_FAULT;
            return false;
        }
        set_current_theme(id);
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (session_)
            session_->refresh_theme();
        return true;
    };
    actions.set_start_minimized = [this](bool value) {
        return save_start_minimized(ini_, value, config_error_);
    };
    actions.config_path = [this]() { return ini_; };
    actions.storage_error = [this]() { return config_error_; };
    actions.trigger_error = [this]() { return trigger_ ? trigger_->last_error() : 0; };
    actions.initial_notice = config_notice_;
    wchar_t temp[32768]{};
    DWORD temp_len = GetTempPathW(32768, temp);
    if (actions.initial_notice.empty() && temp_len && temp_len < 32768 &&
        executable_is_in_temp(executable_, temp))
        actions.initial_notice =
            L"Running from a temporary folder. Extract Drawscr to keep settings.";
    actions.open_site = [this]() { return open_website(hwnd_, shell_open_site); };
    actions.closed = [this](bool accepted) {
        settings_window_ = nullptr;
        settings_open_ = false;
        if (shutting_down_ || !trigger_)
            return;
        if (!accepted && !trigger_->active()) {
            BindError error{};
            trigger_->resume(error);
            trigger_error_ = error;
        } else if (accepted)
            trigger_error_ = BindError::None;
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
    settings_window_ = show_settings(hwnd_, trigger_->current(), *startup_, std::move(actions),
                                     trigger_error_, load_start_minimized(ini_));
    if (!settings_window_) {
        settings_open_ = false;
        BindError error{};
        trigger_->resume(error);
        trigger_error_ = error;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void App::action(int i) {
    if (i == 0)
        capture(false);
    else if (i == 1)
        capture(true);
    else if (i == 2)
        shortcut();
    else if (i >= 3 && i <= 5)
        SendMessageW(hwnd_, WM_CLOSE, 0, 0);
}
void App::paint(HDC target) {
    RECT r{};
    GetClientRect(hwnd_, &r);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, r.right, r.bottom);
    HGDIOBJ old = SelectObject(dc, bmp);
    paint_launcher(dc, dpi_, hover_, focus_, GetFocus() == hwnd_, shortcut_name());
    GdiFlush();
    BitBlt(target, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
}
LRESULT CALLBACK App::proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    App* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->message(msg, w, l) : DefWindowProcW(hwnd, msg, w, l);
}
LRESULT App::message(UINT msg, WPARAM w, LPARAM l) {
    if (msg == taskbar_created_ && taskbar_created_) {
        tray(true);
        return 0;
    }
    switch (msg) {
    case WM_APP + 2:
        show();
        return 0;
    case WM_DRAW_END:
        session_.reset();
        return 0;
    case WM_HOTKEY:
        if (!settings_open_ && trigger_ && trigger_->active() &&
            trigger_->current().kind == TriggerKind::Keyboard && w == 1)
            fire_trigger();
        return 0;
    case WM_DRAW_TRIGGER:
        fire_trigger();
        return 0;
    case WM_TRAY:
        if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU)
            menu();
        else if (l == WM_LBUTTONDBLCLK)
            show();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case MENU_AREA:
            capture(false);
            break;
        case MENU_MONITOR:
            capture(true);
            break;
        case MENU_SHORTCUT:
            shortcut();
            break;
        case MENU_SHOW:
            show();
            break;
        case MENU_EXIT:
            shutting_down_ = true;
            DestroyWindow(hwnd_);
            break;
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd_, &ps);
        paint(dc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        paint(reinterpret_cast<HDC>(w));
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST: {
        LRESULT h = DefWindowProcW(hwnd_, msg, w, l);
        if (h == HTCLIENT) {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(hwnd_, &p);
            if (MulDiv(p.y, 96, static_cast<int>(dpi_)) < 50 && launcher_hit(p, dpi_) < 0)
                return HTCAPTION;
        }
        return h;
    }
    case WM_MOUSEMOVE: {
        int next = launcher_hit({GET_X_LPARAM(l), GET_Y_LPARAM(l)}, dpi_);
        if (next != hover_) {
            hover_ = next;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        if (!tracking_) {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&t);
            tracking_ = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        hover_ = -1;
        tracking_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        int hit = launcher_hit({GET_X_LPARAM(l), GET_Y_LPARAM(l)}, dpi_);
        if (hit >= 0) {
            focus_ = hit;
            action(hit);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_TAB || w == VK_RIGHT) {
            int step = (w == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000)) ? 5 : 1;
            focus_ = (focus_ + step) % 6;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (w == VK_LEFT) {
            focus_ = (focus_ + 5) % 6;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (w == VK_RETURN || w == VK_SPACE) {
            action(focus_);
            return 0;
        }
        if (w == VK_ESCAPE) {
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
        }
        break;
    case WM_DPICHANGED:
        dpi_ = HIWORD(w);
        {
            const RECT rects[] = {{24, 122, 220, 162},  {230, 122, 426, 162}, {24, 174, 220, 210},
                                  {230, 174, 426, 210}, {368, 12, 402, 48},   {406, 12, 438, 48}};
            for (UINT i = 0; i < 6; ++i)
                tips_.update(i + 1, rects[i], dpi_);
            RECT* p = reinterpret_cast<RECT*>(l);
            SetWindowPos(hwnd_, nullptr, p->left, p->top, p->right - p->left, p->bottom - p->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            HRGN region = CreateRoundRectRgn(0, 0, p->right - p->left, p->bottom - p->top,
                                             MulDiv(16, dpi_, 96), MulDiv(16, dpi_, 96));
            if (!SetWindowRgn(hwnd_, region, TRUE))
                DeleteObject(region);
        }
        return 0;
    case WM_CLOSE:
        if (settings_window_)
            SendMessageW(settings_window_, WM_CLOSE, 0, 0);
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
    case WM_DESTROY:
        tips_.reset();
        shutting_down_ = true;
        if (settings_window_) {
            DestroyWindow(settings_window_);
            settings_window_ = nullptr;
        }
        session_.reset();
        trigger_.reset();
        tray(false);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput input;
    ULONG_PTR gdiplus = 0;
    if (GdiplusStartup(&gdiplus, &input, nullptr) != Ok) {
        if (SUCCEEDED(hr))
            CoUninitialize();
        return 1;
    }
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1 && wcscmp(argv[1], L"--recover") == 0) {
        int code = 1;
        if (argc != 4)
            MessageBoxW(nullptr, L"Usage: drawscr.exe --recover <recording-data> <new-output.gif>",
                        L"Drawscr recovery", MB_ICONINFORMATION);
        else if (GetFileAttributesW(argv[3]) != INVALID_FILE_ATTRIBUTES)
            MessageBoxW(nullptr, L"The output already exists. Choose a new output path.",
                        L"Drawscr recovery", MB_ICONWARNING);
        else {
            GUID id{};
            CoCreateGuid(&id);
            wchar_t suffix[40]{};
            StringFromGUID2(id, suffix, 40);
            std::wstring temp = std::wstring(argv[3]) + L".drawscr-" + suffix + L".tmp", error;
            if (recover_raw(argv[2], temp, error) &&
                MoveFileExW(temp.c_str(), argv[3], MOVEFILE_WRITE_THROUGH)) {
                MessageBoxW(nullptr, L"The recording was recovered.", L"Drawscr recovery",
                            MB_ICONINFORMATION);
                code = 0;
            } else {
                DeleteFileW(temp.c_str());
                if (error.empty())
                    error = L"Could not save the recovered GIF.";
                error += L"\n\nThe original recording data is unchanged.";
                MessageBoxW(nullptr, error.c_str(), L"Drawscr recovery", MB_ICONERROR);
            }
        }
        LocalFree(argv);
        GdiplusShutdown(gdiplus);
        if (SUCCEEDED(hr))
            CoUninitialize();
        return code;
    }
    bool quiet = argv && argc > 1 && wcscmp(argv[1], L"--startup") == 0;
    if (argv)
        LocalFree(argv);

    int result = 0;
    {
        App app;
        result = app.create(instance, quiet) ? app.run() : 0;
    } // Join font warmup and destroy all GDI+ owners before shutdown.
    GdiplusShutdown(gdiplus);
    if (SUCCEEDED(hr))
        CoUninitialize();
    return result;
}
