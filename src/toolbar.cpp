#include "toolbar.h"
#include "session.h"
#include "theme.h"
#include "ui_type.h"
#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
using namespace Gdiplus;

namespace drawscr {
static constexpr wchar_t class_name[] = L"Drawscr.Toolbar";
static constexpr COLORREF colors[] = {RGB(244, 83, 81),   RGB(255, 193, 80),  RGB(86, 208, 179),
                                      RGB(100, 168, 255), RGB(245, 245, 242), RGB(20, 24, 26)};
static constexpr float widths[] = {2, 4, 7, 12};
static int sx(int v, UINT dpi) {
    return MulDiv(v, static_cast<int>(dpi), 96);
}
static void rounded(Graphics& g, const RectF& r, float radius, const Color& fill,
                    const Color& stroke = Color(0, 0, 0, 0)) {
    GraphicsPath p;
    float d = radius * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
    SolidBrush b(fill);
    g.FillPath(&b, &p);
    if (stroke.GetAlpha()) {
        Pen pen(stroke, 1);
        g.DrawPath(&pen, &p);
    }
}
static void icon(Graphics& g, Command cmd, float x, float y, Color c, COLORREF selected) {
    GraphicsState state = g.Save();
    g.TranslateTransform(x, y);
    Pen p(c, 1.65f);
    p.SetLineJoin(LineJoinRound);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    SolidBrush fill(c);
    switch (cmd) {
    case Command::Screenshot:
        g.DrawRectangle(&p, 2.5f, 5.0f, 14.0f, 10.0f);
        g.DrawLine(&p, 5.0f, 5.0f, 6.5f, 3.2f);
        g.DrawLine(&p, 6.5f, 3.2f, 10.5f, 3.2f);
        g.DrawEllipse(&p, 7.0f, 7.0f, 5.0f, 5.0f);
        break;
    case Command::Live:
        g.DrawEllipse(&p, 2.5f, 2.5f, 13.0f, 13.0f);
        g.DrawLine(&p, 4.8f, 9.2f, 7.0f, 9.2f);
        g.DrawLine(&p, 7.0f, 9.2f, 8.1f, 6.5f);
        g.DrawLine(&p, 8.1f, 6.5f, 10.0f, 11.6f);
        g.DrawLine(&p, 10.0f, 11.6f, 11.2f, 8.6f);
        g.DrawLine(&p, 11.2f, 8.6f, 13.2f, 8.6f);
        break;
    case Command::Interact:
    case Command::Pen: {
        PointF pts[] = {{3, 13}, {4.3f, 9.8f}, {12.9f, 2.0f}, {16, 5.1f}, {7.2f, 13.4f}, {3, 15}};
        g.DrawPolygon(&p, pts, 6);
        g.DrawLine(&p, 4.7f, 10.0f, 7.8f, 13.0f);
        break;
    }
    case Command::Marker: {
        PointF body[] = {{5.2f, 9.6f}, {11.8f, 2.8f}, {15.5f, 6.5f}, {8.9f, 13.3f}};
        g.DrawPolygon(&p, body, 4);
        g.DrawLine(&p, 5.2f, 9.6f, 3.1f, 13.7f);
        g.DrawLine(&p, 3.1f, 13.7f, 7.2f, 14.8f);
        g.DrawLine(&p, 7.2f, 14.8f, 8.9f, 13.3f);
        g.DrawLine(&p, 2.0f, 16.0f, 7.4f, 16.0f);
        break;
    }
    case Command::Record: {
        SolidBrush red(Color(255, 235, 103, 102));
        g.DrawEllipse(&p, 2.6f, 2.6f, 12.8f, 12.8f);
        g.FillEllipse(&red, 6.4f, 6.4f, 5.2f, 5.2f);
        break;
    }
    case Command::Select: {
        PointF pts[] = {{4, 2.5f},      {4, 15.0f}, {7.0f, 12.0f}, {9.1f, 16.0f},
                        {11.0f, 15.1f}, {9, 11},    {13.5f, 10.5f}};
        g.DrawPolygon(&p, pts, 7);
        break;
    }
    case Command::Arrow:
        g.DrawLine(&p, 3.0f, 14.5f, 14.2f, 3.5f);
        g.DrawLine(&p, 14.2f, 3.5f, 14.0f, 8.0f);
        g.DrawLine(&p, 14.2f, 3.5f, 9.5f, 3.5f);
        break;
    case Command::Line:
        g.DrawLine(&p, 3.0f, 14.0f, 15.0f, 4.0f);
        break;
    case Command::Rectangle:
        g.DrawRectangle(&p, 3.0f, 4.0f, 12.0f, 10.0f);
        break;
    case Command::Text:
        g.DrawLine(&p, 3, 4, 15, 4);
        g.DrawLine(&p, 9, 4, 9, 15);
        g.DrawLine(&p, 6.5f, 15.0f, 11.5f, 15.0f);
        break;
    case Command::Redact:
        g.DrawRectangle(&p, 2.5f, 4.0f, 13.0f, 10.0f);
        g.FillRectangle(&fill, 5.0f, 7.5f, 8.0f, 3.0f);
        break;
    case Command::Color: {
        SolidBrush swatch(
            Color(255, GetRValue(selected), GetGValue(selected), GetBValue(selected)));
        g.DrawRectangle(&p, 2.5f, 2.5f, 12.0f, 12.0f);
        g.FillRectangle(&swatch, 4.8f, 4.8f, 7.4f, 7.4f);
        g.DrawLine(&p, 13.5f, 15.5f, 16.0f, 15.5f);
        break;
    }
    case Command::Width:
        g.DrawLine(&p, 3, 4, 15, 4);
        p.SetWidth(2.6f);
        g.DrawLine(&p, 3, 9, 15, 9);
        p.SetWidth(4);
        g.DrawLine(&p, 3, 14, 15, 14);
        break;
    case Command::Undo:
        g.DrawArc(&p, 4.0f, 4.0f, 11.0f, 10.0f, 205.0f, 245.0f);
        g.DrawLine(&p, 3.0f, 7.0f, 3.0f, 3.5f);
        g.DrawLine(&p, 3.0f, 7.0f, 6.5f, 7.0f);
        break;
    case Command::Redo:
        g.DrawArc(&p, 3.0f, 4.0f, 11.0f, 10.0f, 90.0f, 245.0f);
        g.DrawLine(&p, 15.0f, 7.0f, 15.0f, 3.5f);
        g.DrawLine(&p, 15.0f, 7.0f, 11.5f, 7.0f);
        break;
    case Command::Copy:
        g.DrawRectangle(&p, 5.0f, 5.0f, 9.0f, 10.0f);
        g.DrawLine(&p, 3.0f, 12.0f, 3.0f, 3.0f);
        g.DrawLine(&p, 3.0f, 3.0f, 12.0f, 3.0f);
        break;
    case Command::Save:
        g.DrawLine(&p, 9.0f, 2.5f, 9.0f, 10.5f);
        g.DrawLine(&p, 9.0f, 10.5f, 5.5f, 7.5f);
        g.DrawLine(&p, 9.0f, 10.5f, 12.5f, 7.5f);
        g.DrawLine(&p, 3, 12, 3, 15);
        g.DrawLine(&p, 3, 15, 15, 15);
        g.DrawLine(&p, 15, 15, 15, 12);
        break;
    case Command::Close:
        g.DrawLine(&p, 4, 4, 14, 14);
        g.DrawLine(&p, 14, 4, 4, 14);
        break;
    default:
        break;
    }
    g.Restore(state);
}
Toolbar::Toolbar(Session& s) : session_(s) {}
Toolbar::~Toolbar() {
    if (hwnd_ && IsWindow(hwnd_))
        DestroyWindow(hwnd_);
}
bool Toolbar::create(HWND owner, RECT selection, RECT avoid) {
    selection_ = selection;
    avoid_ = avoid;
    dpi_ = GetDpiForWindow(owner);
    if (!dpi_)
        dpi_ = 96;
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = class_name;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&selection_, MONITOR_DEFAULTTONEAREST), &monitor);
    bar_width_ = std::min(
        526, MulDiv(monitor.rcMonitor.right - monitor.rcMonitor.left, 96, static_cast<int>(dpi_)));
    layout();
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, class_name, L"Drawscr tools",
                            WS_POPUP, 0, 0, sx(bar_width_, dpi_), sx(bar_height_, dpi_), owner,
                            nullptr, wc.hInstance, this);
    if (!hwnd_)
        return false;
    if (tips_.create(hwnd_)) {
        for (size_t i = 0; i < buttons_.size(); ++i)
            tips_.add(static_cast<UINT>(i + 1), buttons_[i].rect, buttons_[i].tip, dpi_);
        const wchar_t* color_help[] = {L"Use red ink",  L"Use amber ink", L"Use mint ink",
                                       L"Use blue ink", L"Use white ink", L"Use black ink"};
        const wchar_t* width_help[] = {L"Use a thin stroke", L"Use a medium stroke",
                                       L"Use a thick stroke", L"Use the thickest stroke"};
        for (UINT i = 0; i < 6; ++i)
            tips_.add(100 + i, {}, color_help[i], dpi_);
        for (UINT i = 0; i < 4; ++i)
            tips_.add(110 + i, {}, width_help[i], dpi_);
    }
    DWORD affinity = 0;
    capture_excluded_ = SetWindowDisplayAffinity(hwnd_, 0x00000011) != FALSE &&
                        GetWindowDisplayAffinity(hwnd_, &affinity) != FALSE &&
                        affinity == 0x00000011;
    reposition();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    return true;
}
void Toolbar::layout() {
    bar_height_ = 94;
    auto g = wide_toolbar_geometry();
    buttons_ = {
        {Command::Mode, g.mode, L"Screenshot", L"Switch to Live"},
        {Command::Interact, g.interact, L"Draw", L"Switch between drawing and interacting"},
        {Command::Record, g.record, L"Record", L"Record the selected area as an animated GIF"},
        {Command::Copy, g.copy, L"", L"Copy image  Ctrl+C"},
        {Command::Save, g.save, L"", L"Save PNG or JPEG  Ctrl+S"},
        {Command::Close, g.close, L"", L"Close  Esc"}};
    const Command cmd[] = {Command::Select,    Command::Pen,    Command::Arrow, Command::Line,
                           Command::Rectangle, Command::Marker, Command::Text,  Command::Redact,
                           Command::Color,     Command::Width,  Command::Undo,  Command::Redo};
    const wchar_t* tips[] = {L"Select and move objects",
                             L"Pen",
                             L"Arrow",
                             L"Line",
                             L"Rectangle",
                             L"Marker",
                             L"Text",
                             L"Solid redaction",
                             L"Color",
                             L"Stroke width",
                             L"Undo  Ctrl+Z",
                             L"Redo  Ctrl+Y"};
    for (int i = 0; i < 12; ++i)
        buttons_.push_back({cmd[i], g.tools[static_cast<size_t>(i)], L"", tips[i]});
    if (bar_width_ < 526) {
        int x = 10, y = 8;
        for (auto& b : buttons_) {
            int w = b.rect.right - b.rect.left;
            if (x + w + 10 > bar_width_ && x > 10) {
                x = 10;
                y += 40;
            }
            b.rect = {x, y, x + w, y + 32};
            x += w + 4;
        }
        bar_height_ = y + 42;
    }
}
void Toolbar::reposition() {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&selection_, MONITOR_DEFAULTTONEAREST), &mi);
    SIZE z{sx(bar_width_, dpi_), sx(bar_height_ + (flyout_ ? 46 : 0), dpi_)};
    RECT r = place_toolbar(avoid_, mi.rcMonitor, z, sx(8, dpi_));
    SetWindowPos(hwnd_, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 SWP_NOACTIVATE);
    HRGN region =
        CreateRoundRectRgn(0, 0, r.right - r.left, r.bottom - r.top, sx(16, dpi_), sx(16, dpi_));
    if (!SetWindowRgn(hwnd_, region, TRUE))
        DeleteObject(region);
    sync_flyout_tooltips();
}
void Toolbar::set_selection(RECT selection, RECT avoid) {
    selection_ = selection;
    avoid_ = avoid;
    if (hwnd_)
        reposition();
}
void Toolbar::sync_flyout_tooltips() {
    RECT hidden{};
    for (int i = 0; i < 6; ++i) {
        LONG x = static_cast<LONG>(20 + i * std::min(39.0f, (bar_width_ - 48.0f) / 5.0f));
        tips_.update(100 + i,
                     flyout_ == 1 ? RECT{x, bar_height_ + 3, x + 28, bar_height_ + 36} : hidden,
                     dpi_);
    }
    for (int i = 0; i < 4; ++i) {
        LONG x = static_cast<LONG>(20 + i * std::min(52.0f, (bar_width_ - 60.0f) / 4.0f));
        tips_.update(110 + i,
                     flyout_ == 2 ? RECT{x, bar_height_ + 3, x + 45, bar_height_ + 36} : hidden,
                     dpi_);
    }
}
void Toolbar::refresh() {
    if (!hwnd_)
        return;
    bool recording = session_.is_recording(), live = session_.is_live();
    if (recording != tooltip_recording_ || live != tooltip_live_) {
        for (size_t i = 0; i < buttons_.size(); ++i) {
            UINT id = static_cast<UINT>(i + 1);
            switch (buttons_[i].command) {
            case Command::Mode:
                tips_.set_text(id, live ? L"Switch to Screenshot" : L"Switch to Live");
                break;
            case Command::Record:
                tips_.set_text(id, recording ? L"Stop recording or cancel GIF encoding"
                                             : L"Record the selected area as an animated GIF");
                break;
            case Command::Text:
                tips_.set_text(id, recording ? L"Text editing is unavailable during recording"
                                             : L"Text");
                break;
            default:
                break;
            }
        }
        tooltip_recording_ = recording;
        tooltip_live_ = live;
    }
    bool allowed = !recording || session_.is_encoding() || tips_.excluded();
    if (allowed != tooltip_allowed_) {
        tips_.activate(allowed);
        tooltip_allowed_ = allowed;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Toolbar::suspend_tooltips_for_capture() {
    if (!tips_.excluded()) {
        tips_.pop();
        tips_.activate(false);
    }
}
void Toolbar::resume_tooltips_after_capture() {
    if (!tips_.excluded() && !session_.is_recording())
        tips_.activate(true);
}
void Toolbar::show(bool v) {
    if (hwnd_)
        ShowWindow(hwnd_, v ? SW_SHOWNOACTIVATE : SW_HIDE);
}
void Toolbar::paint(HDC target) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    render(target, client.right, client.bottom);
}
void Toolbar::render(HDC target, int width, int height) {
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, width, height);
    HGDIOBJ old = SelectObject(dc, bmp);
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.ScaleTransform(dpi_ / 96.0f, dpi_ / 96.0f);
    g.Clear(Color(255, 27, 32, 34));
    rounded(g,
            RectF(.6f, .6f, static_cast<REAL>(bar_width_) - 1.2f,
                  static_cast<REAL>(bar_height_ + (flyout_ ? 46 : 0)) - 1.2f),
            8, Color(255, 27, 32, 34), Color(255, 62, 73, 73));
    UiTypeface family;
    Font font(family.medium(), 13, family.medium_style(), UnitPixel);
    bool live = session_.is_live();
    for (size_t i = 0; i < buttons_.size(); ++i) {
        auto b = buttons_[i];
        bool selected = b.command == Command::Mode ||
                        (b.command == Command::Interact && session_.is_interacting()) ||
                        session_.is_tool(b.command) ||
                        (b.command == Command::Color && flyout_ == 1) ||
                        (b.command == Command::Width && flyout_ == 2);
        bool hovered = static_cast<int>(i) == hover_;
        Color bg = selected  ? ui_color(current_theme().accent)
                   : hovered ? Color(255, 52, 62, 63)
                             : Color(255, 35, 42, 44);
        Color fg = selected ? ui_color(current_theme().ink) : Color(255, 233, 237, 232);
        if (b.command == Command::Record && session_.is_recording()) {
            bg = Color(255, 119, 49, 49);
            fg = Color(255, 255, 241, 238);
        }
        if ((b.command == Command::Interact && !live) ||
            (session_.is_recording() &&
             (b.command == Command::Mode || b.command == Command::Copy ||
              b.command == Command::Save || b.command == Command::Text))) {
            bg = Color(255, 35, 42, 44);
            fg = Color(255, 112, 124, 121);
        }
        rounded(g,
                RectF(static_cast<REAL>(b.rect.left), static_cast<REAL>(b.rect.top),
                      static_cast<REAL>(b.rect.right - b.rect.left),
                      static_cast<REAL>(b.rect.bottom - b.rect.top)),
                6, bg);
        if (hwnd_ && GetFocus() == hwnd_ && static_cast<int>(i) == focus_) {
            Pen focus(ui_color(current_theme().focus, 230), 1);
            g.DrawRectangle(&focus, static_cast<REAL>(b.rect.left + 2),
                            static_cast<REAL>(b.rect.top + 2),
                            static_cast<REAL>(b.rect.right - b.rect.left - 5),
                            static_cast<REAL>(b.rect.bottom - b.rect.top - 5));
        }
        float ix = static_cast<float>(b.rect.left +
                                      (b.label[0] ? 9 : ((b.rect.right - b.rect.left) - 18) / 2));
        icon(g,
             b.command == Command::Mode ? (live ? Command::Live : Command::Screenshot) : b.command,
             ix, static_cast<float>(b.rect.top + 7), fg, session_.color());
        if (b.label[0]) {
            const wchar_t* label = b.label;
            if (b.command == Command::Mode)
                label = live ? L"Live" : L"Screenshot";
            if (b.command == Command::Interact)
                label = session_.is_interacting() ? L"Draw" : L"Interact";
            std::wstring recording_label;
            if (b.command == Command::Record) {
                recording_label = session_.record_label();
                label = recording_label.c_str();
            }
            SolidBrush brush(fg);
            RectF r(static_cast<REAL>(b.rect.left + 31), static_cast<REAL>(b.rect.top),
                    static_cast<REAL>(b.rect.right - b.rect.left - 32), 32);
            StringFormat fmt;
            fmt.SetFormatFlags(StringFormatFlagsNoWrap);
            fmt.SetLineAlignment(StringAlignmentCenter);
            fmt.SetTrimming(StringTrimmingEllipsisCharacter);
            g.DrawString(label, -1, &font, r, &fmt, &brush);
        }
    }
    if (flyout_) {
        rounded(g,
                RectF(9.0f, static_cast<REAL>(bar_height_ + 3), static_cast<REAL>(bar_width_ - 18),
                      33.0f),
                6, Color(255, 35, 42, 44));
        if (flyout_ == 1)
            for (int i = 0; i < 6; ++i) {
                float gap = std::min(39.0f, (bar_width_ - 48.0f) / 5.0f);
                float x = 24.0f + i * gap;
                SolidBrush brush(
                    Color(255, GetRValue(colors[i]), GetGValue(colors[i]), GetBValue(colors[i])));
                g.FillEllipse(&brush, x, static_cast<REAL>(bar_height_ + 10), 18.0f, 18.0f);
                Pen outline(Color(255, 176, 190, 185), session_.color() == colors[i] ? 2.0f : 1.0f);
                g.DrawEllipse(&outline, x, static_cast<REAL>(bar_height_ + 10), 18.0f, 18.0f);
            }
        else
            for (int i = 0; i < 4; ++i) {
                Pen p(Color(255, 225, 232, 226), widths[i]);
                p.SetStartCap(LineCapRound);
                p.SetEndCap(LineCapRound);
                g.DrawLine(&p, 28.0f + i * std::min(52.0f, (bar_width_ - 60.0f) / 4.0f),
                           static_cast<REAL>(bar_height_ + 19),
                           54.0f + i * std::min(52.0f, (bar_width_ - 60.0f) / 4.0f),
                           static_cast<REAL>(bar_height_ + 19));
            }
    }
    g.Flush();
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
}
void Toolbar::choose(POINT p) {
    if (session_.text_edit_active()) {
        session_.hold_text_edit();
        return;
    }
    p.x = MulDiv(p.x, 96, static_cast<int>(dpi_));
    p.y = MulDiv(p.y, 96, static_cast<int>(dpi_));
    if (flyout_ && p.y >= bar_height_ + 3 && p.y < bar_height_ + 36) {
        if (flyout_ == 1)
            for (int i = 0; i < 6; ++i)
                if (p.x >= 20 + i * std::min(39.0f, (bar_width_ - 48.0f) / 5.0f) &&
                    p.x < 48 + i * std::min(39.0f, (bar_width_ - 48.0f) / 5.0f)) {
                    session_.set_color(colors[i]);
                    break;
                }
        if (flyout_ == 2)
            for (int i = 0; i < 4; ++i)
                if (p.x >= 20 + i * std::min(52.0f, (bar_width_ - 60.0f) / 4.0f) &&
                    p.x < 65 + i * std::min(52.0f, (bar_width_ - 60.0f) / 4.0f)) {
                    session_.set_width(widths[i]);
                    break;
                }
        flyout_ = 0;
        reposition();
        refresh();
        return;
    }
    for (size_t i = 0; i < buttons_.size(); ++i)
        if (PtInRect(&buttons_[i].rect, p)) {
            focus_ = static_cast<int>(i);
            auto command = buttons_[i].command;
            if ((command == Command::Interact && !session_.is_live()) ||
                (command == Command::Text && session_.is_recording())) {
                refresh();
                return;
            }
            if (command == Command::Color || command == Command::Width) {
                int next = command == Command::Color ? 1 : 2;
                flyout_ = (flyout_ == next ? 0 : next);
                reposition();
                refresh();
            } else {
                flyout_ = 0;
                reposition();
                session_.command(command);
                refresh();
            }
            return;
        }
}
LRESULT CALLBACK Toolbar::proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    Toolbar* self = reinterpret_cast<Toolbar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<Toolbar*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->message(msg, w, l) : DefWindowProcW(hwnd, msg, w, l);
}
LRESULT Toolbar::message(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
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
    case WM_MOUSEMOVE: {
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        p.x = MulDiv(p.x, 96, static_cast<int>(dpi_));
        p.y = MulDiv(p.y, 96, static_cast<int>(dpi_));
        int next = -1;
        for (size_t i = 0; i < buttons_.size(); ++i)
            if (PtInRect(&buttons_[i].rect, p)) {
                next = static_cast<int>(i);
                break;
            }
        if (next != hover_) {
            hover_ = next;
            refresh();
        }
        if (!tracking_) {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&t);
            tracking_ = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        hover_ = -1;
        refresh();
        return 0;
    case WM_LBUTTONUP:
        SetFocus(hwnd_);
        choose({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        return 0;
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) && w == 'A') {
            session_.command(Command::SelectAll);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && w == 'C') {
            session_.command(Command::Copy);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && w == 'S') {
            session_.command(Command::Save);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && w == 'Z') {
            session_.command(Command::Undo);
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && w == 'Y') {
            session_.command(Command::Redo);
            return 0;
        }
        if (w == VK_RIGHT || w == VK_TAB) {
            focus_ = (focus_ + 1) % static_cast<int>(buttons_.size());
            refresh();
            return 0;
        }
        if (w == VK_LEFT) {
            focus_ = (focus_ + static_cast<int>(buttons_.size()) - 1) %
                     static_cast<int>(buttons_.size());
            refresh();
            return 0;
        }
        if (w == VK_RETURN || w == VK_SPACE) {
            auto b = buttons_[static_cast<size_t>(focus_)];
            choose({sx((b.rect.left + b.rect.right) / 2, dpi_),
                    sx((b.rect.top + b.rect.bottom) / 2, dpi_)});
            return 0;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        refresh();
        break;
    case WM_DESTROY:
        tips_.reset();
        hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, w, l);
}
}
