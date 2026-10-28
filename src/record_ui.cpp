#include "record_ui.h"
#include "paint_buffer.h"
#include "theme.h"
#include "ui_type.h"
#include "tooltips.h"
#include "image.h"
#include <objidl.h>
#include <gdiplus.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <commdlg.h>
#include <windowsx.h>
#include <algorithm>
#include <vector>
using namespace Gdiplus;
using Microsoft::WRL::ComPtr;

namespace drawscr {
static int scale(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}
static void round_rect(Graphics& g, RectF r, Color fill, Color border = Color(0, 0, 0, 0)) {
    float d = 12;
    GraphicsPath p;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    p.CloseFigure();
    SolidBrush b(fill);
    g.FillPath(&b, &p);
    if (border.GetAlpha()) {
        Pen pen(border, 1);
        g.DrawPath(&pen, &p);
    }
}
static void label(Graphics& g, const wchar_t* text, float x, float y, float size, Color color,
                  int style = FontStyleRegular) {
    UiTypeface f;
    Font font(style == FontStyleBold ? f.regular() : f.medium(), ui_size(size),
              style == FontStyleBold ? FontStyleBold : f.medium_style(), UnitPixel);
    SolidBrush brush(color);
    g.DrawString(text, -1, &font, PointF(x, y), &brush);
}
static void center(Graphics& g, const wchar_t* text, RectF r, float size, Color color) {
    UiTypeface f;
    Font font(f.medium(), ui_size(size), f.medium_style(), UnitPixel);
    SolidBrush brush(color);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(text, -1, &font, r, &fmt, &brush);
}
static void set_position(HWND window, HWND owner, int width, int height, UINT dpi) {
    RECT anchor{};
    GetWindowRect(owner, &anchor);
    MONITORINFO m{sizeof(m)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &m);
    int w = scale(width, dpi), h = scale(height, dpi),
        x = m.rcWork.left + ((m.rcWork.right - m.rcWork.left) - w) / 2,
        y = m.rcWork.top + ((m.rcWork.bottom - m.rcWork.top) - h) / 2;
    SetWindowPos(window, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    HRGN region = CreateRoundRectRgn(0, 0, w + 1, h + 1, scale(16, dpi), scale(16, dpi));
    if (!SetWindowRgn(window, region, TRUE))
        DeleteObject(region);
}
static bool modal_loop(HWND owner, HWND window, bool& open) {
    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    SetFocus(window);
    MSG msg{};
    while (open) {
        int n = GetMessageW(&msg, nullptr, 0, 0);
        if (n <= 0) {
            if (n == 0)
                PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (IsWindow(window))
        DestroyWindow(window);
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return true;
}
struct Settings {
    HWND window = nullptr;
    UINT dpi = 96;
    SIZE selection{};
    bool open = true, accepted = false, expanded = false;
    int countdown = 3, hover = -1, focus = 1, fps = 8;
    GifCompression compression = GifCompression::Normal;
    PaintBuffer buffer;
    Tooltips tips;
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    void paint(HDC);
    bool can_start() const {
        return selection.cx > 0 && selection.cy > 0 &&
               valid_record_frame_size(selection.cx, selection.cy);
    }
    void sync_tips() {
        RECT hidden{};
        tips.update(7, expanded ? RECT{20, 213, 54, 245} : hidden, dpi);
        tips.update(8, expanded ? RECT{164, 213, 198, 245} : hidden, dpi);
        int y = expanded ? 318 : 274;
        tips.update(9, {20, y, 198, y + 36}, dpi);
        tips.update(10, {204, y, 382, y + 36}, dpi);
    }
    void choose(int index) {
        if (index >= 0 && index <= 2)
            compression = index == 0   ? GifCompression::Strong
                          : index == 1 ? GifCompression::Normal
                                       : GifCompression::Quality;
        else if (index == 3)
            countdown = 0;
        else if (index == 4)
            countdown = 3;
        else if (index == 5) {
            expanded = !expanded;
            if (!expanded && (focus == 6 || focus == 7))
                focus = 5;
            set_position(window, GetWindow(window, GW_OWNER), 402, expanded ? 370 : 326, dpi);
            sync_tips();
        } else if (index == 6)
            fps = std::max(1, fps - 1);
        else if (index == 7)
            fps = std::min(20, fps + 1);
        else if (index == 8) {
            if (can_start()) {
                accepted = true;
                open = false;
            }
        } else if (index == 9)
            open = false;
        InvalidateRect(window, nullptr, FALSE);
    }
    int hit(POINT p) {
        p.x = MulDiv(p.x, 96, dpi);
        p.y = MulDiv(p.y, 96, dpi);
        if (p.y >= 40 && p.y < 66 && p.x >= 320 && p.x < 382)
            return 5;
        if (p.y >= 123 && p.y < 155)
            return p.x >= 20 && p.x < 134    ? 0
                   : p.x >= 143 && p.x < 259 ? 1
                   : p.x >= 268 && p.x < 382 ? 2
                                             : -1;
        if (p.y >= 162 && p.y < 194)
            return p.x >= 126 && p.x < 244 ? 3 : p.x >= 252 && p.x < 382 ? 4 : -1;
        if (expanded && p.y >= 213 && p.y < 245)
            return p.x >= 20 && p.x < 54 ? 6 : p.x >= 164 && p.x < 198 ? 7 : -1;
        int action = expanded ? 318 : 274;
        if (p.y >= action && p.y < action + 36)
            return p.x >= 20 && p.x < 198 ? 8 : p.x >= 204 && p.x < 382 ? 9 : -1;
        return -1;
    }
};
void Settings::paint(HDC dc) {
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.ScaleTransform(dpi / 96.0f, dpi / 96.0f);
    g.Clear(Color(255, 27, 32, 34));
    round_rect(g, {.5f, .5f, 401, expanded ? 369.0f : 325.0f}, Color(255, 27, 32, 34),
               Color(255, 62, 73, 73));
    label(g, L"Record selected area", 20, 15, 20, Color(255, 240, 243, 237), FontStyleBold);
    wchar_t value[120]{};
    swprintf_s(value, L"Area: %ld x %ld px (original)", selection.cx, selection.cy);
    label(g, value, 20, 44, 12, Color(255, 225, 237, 230));
    label(g, L"The GIF uses the selected pixels without resizing.", 20, 64, 11,
          Color(255, 166, 180, 176));
    label(g, L"Live desktop in frame. Use Interact to click through.", 20, 82, 11,
          Color(255, 166, 180, 176));
    RectF more{320, 40, 62, 26};
    round_rect(g, more,
               expanded     ? ui_color(current_theme().soft)
               : hover == 5 ? Color(255, 52, 62, 63)
                            : Color(255, 37, 45, 46));
    center(g, expanded ? L"Less" : L"More", more, 11, Color(255, 225, 238, 230));
    label(g, L"Compression", 20, 105, 12, Color(255, 192, 207, 199));
    label(g, L"Protects fine detail", 146, 106, 10, Color(255, 166, 180, 176));
    const RectF cards[] = {{20, 123, 114, 32}, {143, 123, 116, 32}, {268, 123, 114, 32}};
    const wchar_t* names[] = {L"Strong", L"Normal", L"Quality"};
    for (int i = 0; i < 3; ++i) {
        bool chosen = (i == 0 && compression == GifCompression::Strong) ||
                      (i == 1 && compression == GifCompression::Normal) ||
                      (i == 2 && compression == GifCompression::Quality);
        round_rect(g, cards[i],
                   chosen       ? ui_color(current_theme().accent)
                   : hover == i ? Color(255, 52, 62, 63)
                                : Color(255, 37, 45, 46));
        center(g, names[i], cards[i], 12,
               chosen ? ui_color(current_theme().ink) : Color(255, 230, 236, 230));
    }
    label(g, L"Countdown", 20, 171, 12, Color(255, 192, 207, 199));
    for (int i = 0; i < 2; ++i) {
        RectF r{i ? 252.0f : 126.0f, 162, i ? 130.0f : 118.0f, 32};
        bool chosen = countdown == (i ? 3 : 0);
        round_rect(g, r,
                   chosen           ? ui_color(current_theme().accent)
                   : hover == i + 3 ? Color(255, 52, 62, 63)
                                    : Color(255, 37, 45, 46));
        center(g, i ? L"3 seconds" : L"None", r, 12,
               chosen ? ui_color(current_theme().ink) : Color(255, 230, 236, 230));
    }
    if (expanded) {
        label(g, L"Frames / second (1-20)", 20, 197, 11, Color(255, 192, 207, 199));
        const RectF buttons[] = {{20, 213, 34, 32}, {164, 213, 34, 32}};
        for (int i = 0; i < 2; ++i) {
            round_rect(g, buttons[i],
                       hover == 6 + i ? Color(255, 52, 62, 63) : Color(255, 37, 45, 46));
            Pen pen(Color(255, 231, 238, 232), 1.5f);
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);
            float x = buttons[i].X + 17, y = buttons[i].Y + 16;
            g.DrawLine(&pen, x - 5, y, x + 5, y);
            if (i)
                g.DrawLine(&pen, x, y - 5, x, y + 5);
        }
        swprintf_s(value, L"%d fps", fps);
        center(g, value, {56, 213, 106, 32}, 12, Color(255, 231, 238, 232));
    }
    float note = expanded ? 253.0f : 209.0f, action = expanded ? 318.0f : 274.0f;
    bool ready = can_start();
    if (ready) {
        double rate = static_cast<double>(selection.cx) * selection.cy * 4 * fps / 1048576.0;
        swprintf_s(value, L"Temporary storage: up to %.1f MiB/s at %d fps.", rate, fps);
        label(g, value, 20, note, 11, Color(255, 192, 207, 199));
        label(g, L"Stops if storage is low. Identical frames use less space.", 20, note + 17, 11,
              Color(255, 166, 180, 176));
    } else {
        label(g, L"Selection is too large. Resize the frame first.", 20, note, 11,
              Color(255, 242, 172, 152));
        label(g, L"Limit: 8192 px per side and 64 MiB per frame.", 20, note + 17, 11,
              Color(255, 166, 180, 176));
    }
    RectF start{20, action, 178, 36}, cancel{204, action, 178, 36};
    round_rect(g, start, ready ? ui_color(current_theme().accent) : Color(255, 52, 62, 63));
    round_rect(g, cancel, hover == 9 ? Color(255, 52, 62, 63) : Color(255, 37, 45, 46));
    center(g, L"Start recording", start, 13,
           ready ? ui_color(current_theme().ink) : Color(255, 133, 146, 141));
    center(g, L"Cancel", cancel, 13, Color(255, 230, 236, 230));
    if (window && GetFocus() == window) {
        RectF rects[] = {{20, 123, 114, 32},
                         {143, 123, 116, 32},
                         {268, 123, 114, 32},
                         {126, 162, 118, 32},
                         {252, 162, 130, 32},
                         more,
                         {20, 213, 34, 32},
                         {164, 213, 34, 32},
                         start,
                         cancel};
        auto r = rects[focus];
        Pen pen(ui_color(current_theme().focus), 1);
        g.DrawRectangle(&pen, r.X + 2, r.Y + 2, r.Width - 4, r.Height - 4);
    }
}
LRESULT CALLBACK Settings::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* s = reinterpret_cast<Settings*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        s = static_cast<Settings*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        s->window = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    return s ? s->message(m, w, l) : DefWindowProcW(h, m, w, l);
}
LRESULT Settings::message(UINT m, WPARAM w, LPARAM l) {
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
    case WM_MOUSEMOVE: {
        int n = hit({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (n != hover) {
            hover = n;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int n = hit({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (n >= 0) {
            focus = n;
            choose(n);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            open = false;
            return 0;
        }
        if (w == VK_TAB || w == VK_RIGHT || w == VK_LEFT) {
            const int order[] = {0, 1, 2, 5, 3, 4, 6, 7, 8, 9};
            int n = expanded ? 10 : 8;
            const int compact[] = {0, 1, 2, 5, 3, 4, 8, 9};
            const int* items = expanded ? order : compact;
            int pos = 0;
            while (pos < n && items[pos] != focus)
                ++pos;
            if (pos == n)
                pos = 0;
            int d = (w == VK_LEFT || (w == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000))) ? -1 : 1;
            focus = items[(pos + n + d) % n];
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (w == VK_RETURN || w == VK_SPACE) {
            choose(focus);
            return 0;
        }
        break;
    case WM_CLOSE:
        open = false;
        return 0;
    case WM_DESTROY:
        tips.reset();
        return 0;
    }
    return DefWindowProcW(window, m, w, l);
}
bool choose_recording(HWND owner, RecordOptions& out, SIZE selection) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = Settings::proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Drawscr.RecordSettings";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    Settings s;
    s.dpi = GetDpiForWindow(owner);
    if (!s.dpi)
        s.dpi = 96;
    s.selection = selection;
    s.fps = std::clamp(out.fps, 1, 20);
    s.countdown = out.countdown;
    s.compression = out.compression;
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName,
                             L"Record selected area", WS_POPUP, 0, 0, scale(402, s.dpi),
                             scale(326, s.dpi), owner, nullptr, wc.hInstance, &s);
    if (!h)
        return false;
    set_position(h, owner, 402, 326, s.dpi);
    SetWindowDisplayAffinity(h, 0x11);
    if (s.tips.create(h)) {
        const RECT rects[] = {{20, 123, 134, 155},
                              {143, 123, 259, 155},
                              {268, 123, 382, 155},
                              {126, 162, 244, 194},
                              {252, 162, 382, 194},
                              {320, 40, 382, 66},
                              {},
                              {},
                              {20, 274, 198, 310},
                              {204, 274, 382, 310}};
        const wchar_t* help[] = {L"Reduce size while protecting fine details",
                                 L"Balance color fidelity and file size",
                                 L"Refine the palette for color fidelity",
                                 L"Start without a countdown",
                                 L"Wait three seconds before recording",
                                 L"Adjust frame rate",
                                 L"Decrease the frame rate",
                                 L"Increase the frame rate",
                                 L"Start recording the selected area",
                                 L"Cancel and return to the selection"};
        for (UINT i = 0; i < 10; ++i)
            s.tips.add(i + 1, rects[i], help[i], s.dpi);
        s.sync_tips();
    }
    modal_loop(owner, h, s.open);
    if (s.accepted) {
        out.fps = s.fps;
        out.countdown = s.countdown;
        out.compression = s.compression;
    }
    return s.accepted;
}

static unsigned meta(IWICMetadataQueryReader* reader, const wchar_t* key, unsigned fallback) {
    PROPVARIANT p{};
    unsigned v = fallback;
    if (reader && SUCCEEDED(reader->GetMetadataByName(key, &p))) {
        if (p.vt == VT_UI1)
            v = p.bVal;
        else if (p.vt == VT_UI2)
            v = p.uiVal;
    }
    PropVariantClear(&p);
    return v;
}
static bool save_copy(const std::wstring& from, HWND owner, std::wstring& error) {
    wchar_t name[32768] = L"Drawscr.gif";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Animated GIF (*.gif)\0*.gif\0\0";
    ofn.lpstrFile = name;
    ofn.nMaxFile = 32768;
    ofn.lpstrDefExt = L"gif";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrTitle = L"Save GIF";
    if (!GetSaveFileNameW(&ofn))
        return false;
    GUID id{};
    CoCreateGuid(&id);
    wchar_t suffix[40]{};
    StringFromGUID2(id, suffix, 40);
    std::wstring dest = name, temp = dest + L".drawscr-" + suffix + L".tmp";
    if (!CopyFileW(from.c_str(), temp.c_str(), TRUE)) {
        error = L"Could not write the selected file.";
        return false;
    }
    if (!MoveFileExW(temp.c_str(), dest.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        error = L"Could not finish saving the selected file.";
        return false;
    }
    return true;
}
struct Preview {
    HWND window = nullptr;
    UINT dpi = 96;
    bool open = true, saved = false, failed = false, detail_guard = false, storage_limited = false;
    int hover = -1, focus = 0;
    std::wstring path, error;
    uint64_t bytes = 0;
    PaintBuffer buffer;
    Tooltips tips;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    Image canvas;
    UINT count = 0, index = 0;
    bool init() {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr))
            hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnLoad, &decoder);
        if (SUCCEEDED(hr))
            hr = decoder->GetFrameCount(&count);
        if (FAILED(hr) || !count)
            return false;
        ComPtr<IWICMetadataQueryReader> reader;
        hr = decoder->GetMetadataQueryReader(&reader);
        UINT w = meta(reader.Get(), L"/logscrdesc/Width", 0),
             h = meta(reader.Get(), L"/logscrdesc/Height", 0);
        if (!valid_record_frame_size(w, h))
            return false;
        canvas = Image(static_cast<int>(w), static_cast<int>(h));
        return canvas.valid();
    }
    bool advance() {
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICMetadataQueryReader> reader;
        ComPtr<IWICFormatConverter> converter;
        UINT w = 0, h = 0;
        HRESULT hr = decoder->GetFrame(index, &frame);
        if (SUCCEEDED(hr))
            hr = frame->GetSize(&w, &h);
        if (SUCCEEDED(hr))
            hr = frame->GetMetadataQueryReader(&reader);
        UINT x = meta(reader.Get(), L"/imgdesc/Left", 0),
             y = meta(reader.Get(), L"/imgdesc/Top", 0),
             delay = meta(reader.Get(), L"/grctlext/Delay", 10);
        if (FAILED(hr) || x + w > static_cast<UINT>(canvas.width()) ||
            y + h > static_cast<UINT>(canvas.height()))
            return false;
        if (SUCCEEDED(hr))
            hr = factory->CreateFormatConverter(&converter);
        if (SUCCEEDED(hr))
            hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                                       WICBitmapDitherTypeNone, nullptr, 0,
                                       WICBitmapPaletteTypeCustom);
        std::vector<BYTE> pixels(static_cast<size_t>(w) * h * 4);
        if (SUCCEEDED(hr))
            hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()),
                                       pixels.data());
        if (FAILED(hr))
            return false;
        if (!blit_gif_delta(canvas, pixels.data(), w, h, x, y))
            return false;
        index = (index + 1) % count;
        SetTimer(window, 1, std::max(20U, delay * 10), nullptr);
        InvalidateRect(window, nullptr, FALSE);
        return true;
    }
    void paint(HDC dc) {
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.ScaleTransform(dpi / 96.0f, dpi / 96.0f);
        g.Clear(Color(255, 27, 32, 34));
        round_rect(g, {.5f, .5f, 519, 415}, Color(255, 27, 32, 34), Color(255, 62, 73, 73));
        label(g, storage_limited ? L"Stopped: low storage" : L"Recording ready", 20, 14, 20,
              Color(255, 240, 243, 237), FontStyleBold);
        wchar_t details[200]{};
        const wchar_t* guard = detail_guard ? L"  |  Detail protected" : L"";
        if (bytes >= 1024 * 1024)
            swprintf_s(details, L"%.2f MiB  |  %d x %d  |  %u frames%s", bytes / 1048576.0,
                       canvas.width(), canvas.height(), count, guard);
        else
            swprintf_s(details, L"%.1f KiB  |  %d x %d  |  %u frames%s", bytes / 1024.0,
                       canvas.width(), canvas.height(), count, guard);
        label(g, details, 21, 45, 12, Color(255, 172, 190, 183));
        round_rect(g, {20, 70, 480, 270}, Color(255, 16, 20, 21));
        g.Flush();
        double factor = std::min(460.0 / canvas.width(), 250.0 / canvas.height());
        int w = scale(std::max(1, static_cast<int>(canvas.width() * factor)), dpi),
            h = scale(std::max(1, static_cast<int>(canvas.height() * factor)), dpi);
        int x = scale(260, dpi) - w / 2, y = scale(205, dpi) - h / 2;
        HDC src = canvas.begin();
        SetStretchBltMode(dc, HALFTONE);
        StretchBlt(dc, x, y, w, h, src, 0, 0, canvas.width(), canvas.height(), SRCCOPY);
        canvas.end(src);
        RectF save{20, 357, 232, 38}, discard{262, 357, 238, 38};
        round_rect(g, save, ui_color(current_theme().accent));
        round_rect(g, discard, hover == 1 ? Color(255, 52, 62, 63) : Color(255, 37, 45, 46));
        center(g, L"Save GIF", save, 13, ui_color(current_theme().ink));
        center(g, L"Discard", discard, 13, Color(255, 230, 236, 230));
        if (GetFocus() == window) {
            auto r = focus ? discard : save;
            Pen p(ui_color(current_theme().focus), 1);
            g.DrawRectangle(&p, r.X + 2, r.Y + 2, r.Width - 4, r.Height - 4);
        }
    }
    int hit(POINT p) {
        p.x = MulDiv(p.x, 96, dpi);
        p.y = MulDiv(p.y, 96, dpi);
        if (p.y >= 357 && p.y < 395)
            return p.x >= 20 && p.x < 252 ? 0 : p.x >= 262 && p.x < 500 ? 1 : -1;
        return -1;
    }
    void choose(int i) {
        if (i == 1) {
            open = false;
            return;
        }
        std::wstring e;
        if (save_copy(path, window, e)) {
            saved = true;
            open = false;
        } else if (!e.empty())
            MessageBoxW(window, e.c_str(), L"Drawscr", MB_ICONERROR);
    }
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
};
LRESULT CALLBACK Preview::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* s = reinterpret_cast<Preview*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE) {
        s = static_cast<Preview*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        s->window = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    return s ? s->message(m, w, l) : DefWindowProcW(h, m, w, l);
}
LRESULT Preview::message(UINT m, WPARAM w, LPARAM l) {
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
    case WM_TIMER:
        if (w == 1 && !advance()) {
            failed = true;
            open = false;
        }
        return 0;
    case WM_MOUSEMOVE: {
        int h = hit({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (h != hover) {
            hover = h;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int h = hit({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (h >= 0) {
            focus = h;
            choose(h);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            open = false;
            return 0;
        }
        if (w == VK_TAB || w == VK_LEFT || w == VK_RIGHT) {
            focus = 1 - focus;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (w == VK_RETURN || w == VK_SPACE) {
            choose(focus);
            return 0;
        }
        break;
    case WM_CLOSE:
        open = false;
        return 0;
    case WM_DESTROY:
        tips.reset();
        return 0;
    }
    return DefWindowProcW(window, m, w, l);
}
PreviewResult preview_recording(HWND owner, const std::wstring& path, uint64_t size,
                                bool detail_guard, bool storage_limited, std::wstring& error) {
    Preview p;
    p.path = path;
    p.bytes = size;
    p.detail_guard = detail_guard;
    p.storage_limited = storage_limited;
    p.dpi = GetDpiForWindow(owner);
    if (!p.dpi)
        p.dpi = 96;
    if (!p.init()) {
        error = L"Could not open the recording preview.";
        return PreviewResult::Failed;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = Preview::proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Drawscr.RecordPreview";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName,
                             L"Recording preview", WS_POPUP, 0, 0, scale(520, p.dpi),
                             scale(416, p.dpi), owner, nullptr, wc.hInstance, &p);
    if (!h) {
        error = L"Could not open the recording preview.";
        return PreviewResult::Failed;
    }
    set_position(h, owner, 520, 416, p.dpi);
    SetWindowDisplayAffinity(h, 0x11);
    if (p.tips.create(h)) {
        p.tips.add(1, {20, 357, 252, 395}, L"Save this GIF", p.dpi);
        p.tips.add(2, {262, 357, 500, 395}, L"Discard this recording and return to editing", p.dpi);
    }
    if (!p.advance()) {
        DestroyWindow(h);
        error = L"Could not decode the recording preview.";
        return PreviewResult::Failed;
    }
    modal_loop(owner, h, p.open);
    KillTimer(h, 1);
    if (p.failed) {
        error = L"Could not decode a recording frame.";
        return PreviewResult::Failed;
    }
    return p.saved ? PreviewResult::Saved : PreviewResult::Discarded;
}
}
