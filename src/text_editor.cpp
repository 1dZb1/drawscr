#include "text_editor.h"
#include "theme.h"
#include "ui_type.h"
#include <windowsx.h>
#include <imm.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
using namespace Gdiplus;
namespace drawscr {
namespace {
constexpr wchar_t editor_class[] = L"Drawscr.TextEditor";
constexpr COLORREF colors[] = {RGB(244, 83, 81),   RGB(255, 193, 80),  RGB(86, 208, 179),
                               RGB(100, 168, 255), RGB(245, 245, 242), RGB(20, 24, 26)};
constexpr RECT minus_rect{10, 6, 38, 36}, plus_rect{76, 6, 104, 36}, apply_rect{272, 6, 348, 36},
    cancel_rect{354, 6, 434, 36};
int px(int v, UINT dpi) {
    return MulDiv(v, static_cast<int>(dpi), 96);
}
void box(Graphics& g, const RectF& r, Color fill, Color edge = Color(0, 0, 0, 0)) {
    GraphicsPath path;
    constexpr float d = 10;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    SolidBrush b(fill);
    g.FillPath(&b, &path);
    if (edge.GetAlpha()) {
        Pen p(edge, 1);
        g.DrawPath(&p, &path);
    }
}
LRESULT CALLBACK suppress_input_paint(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id,
                                      DWORD_PTR) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_PRINT || message == WM_PRINTCLIENT)
        return 0;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, suppress_input_paint, id);
    return DefSubclassProc(hwnd, message, w, l);
}
}
HWND create_text_input_window(HWND parent, UINT dpi, const std::wstring& initial) {
    const int extent = px(2048, dpi ? dpi : 96), outside = -extent - 1;
    HWND edit = CreateWindowExW(
        0, L"EDIT", initial.c_str(),
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        outside, outside, extent, extent, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!edit)
        return nullptr;
    HRGN empty = CreateRectRgn(0, 0, 0, 0);
    if (!empty || !SetWindowRgn(edit, empty, FALSE)) {
        if (empty)
            DeleteObject(empty);
        DestroyWindow(edit);
        return nullptr;
    }
    HRGN actual = CreateRectRgn(0, 0, 0, 0);
    const bool clipped = actual && GetWindowRgn(edit, actual) == NULLREGION;
    if (actual)
        DeleteObject(actual);
    if (!clipped || !SetWindowSubclass(edit, suppress_input_paint, 1, 0)) {
        DestroyWindow(edit);
        return nullptr;
    }
    return edit;
}
TextEditor::~TextEditor() {
    tips_.reset();
    if (hwnd_ && IsWindow(hwnd_))
        DestroyWindow(hwnd_);
    if (font_)
        DeleteObject(font_);
}
bool TextEditor::create(HWND owner, POINT anchor, RECT selection, const std::wstring& initial,
                        COLORREF color, int size_dip, float existing_px, bool require_exclusion) {
    owner_ = owner;
    anchor_ = anchor;
    selection_ = selection;
    color_ = color;
    size_ = std::clamp(size_dip, min_text_size, max_text_size);
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = editor_class;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, editor_class, L"Drawscr text",
                            WS_POPUP, anchor.x, anchor.y, 1, 1, owner, nullptr, wc.hInstance, this);
    if (!hwnd_)
        return false;
    dpi_ = GetDpiForWindow(hwnd_);
    if (!dpi_)
        dpi_ = 96;
    if (existing_px > 0)
        size_ = std::clamp(MulDiv(static_cast<int>(existing_px), 96, static_cast<int>(dpi_)),
                           min_text_size, max_text_size);
    initial_size_ = size_;
    original_px_ = existing_px;
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &mi))
        return false;
    monitor_ = mi.rcMonitor;
    DWORD affinity = 0;
    bool excluded = SetWindowDisplayAffinity(hwnd_, 0x11) != FALSE &&
                    GetWindowDisplayAffinity(hwnd_, &affinity) != FALSE && affinity == 0x11;
    if (require_exclusion && !excluded)
        return false;
    edit_ = create_text_input_window(hwnd_, dpi_, initial);
    if (!edit_)
        return false;
    SendMessageW(edit_, EM_SETLIMITTEXT, 4096, 0);
    if (!SetWindowSubclass(edit_, edit_proc, 1, reinterpret_cast<DWORD_PTR>(this)))
        return false;
    update_font();
    refresh_draft();
    if (!view_.valid || !layout_controls())
        return false;
    if (tips_.create(hwnd_)) {
        tips_.add(1, minus_rect, L"Decrease text size", dpi_);
        tips_.add(2, plus_rect, L"Increase text size", dpi_);
        const wchar_t* names[] = {L"Use red text",  L"Use amber text", L"Use mint text",
                                  L"Use blue text", L"Use white text", L"Use black text"};
        for (UINT i = 0; i < 6; ++i)
            tips_.add(10 + i,
                      {112 + static_cast<LONG>(i) * 25, 6, 136 + static_cast<LONG>(i) * 25, 36},
                      names[i], dpi_);
        tips_.add(3, apply_rect, L"Apply text  Ctrl+Enter", dpi_);
        tips_.add(4, cancel_rect, L"Cancel text  Esc", dpi_);
        if (require_exclusion && !tips_.excluded())
            tips_.activate(false);
    }
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(edit_);
    if (GetFocus() != edit_)
        return false;
    SendMessageW(edit_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    refresh_draft();
    return true;
}
std::wstring TextEditor::text() const {
    if (!edit_)
        return {};
    int n = GetWindowTextLengthW(edit_);
    std::wstring value(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(edit_, value.data(), n + 1);
    value.resize(static_cast<size_t>(n));
    return value;
}
void TextEditor::update_font() {
    HFONT next = CreateFontW(-px(size_, dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (next) {
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(next), FALSE);
        if (font_)
            DeleteObject(font_);
        font_ = next;
    }
}
bool TextEditor::layout_controls() {
    if (!hwnd_ || !view_.valid)
        return false;
    RECT r = view_.placement.controls, current{};
    GetWindowRect(hwnd_, &current);
    if (!EqualRect(&r, &current))
        SetWindowPos(hwnd_, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOACTIVATE);
    SIZE size{r.right - r.left, r.bottom - r.top};
    if (size.cx != region_size_.cx || size.cy != region_size_.cy) {
        HRGN region =
            CreateRoundRectRgn(0, 0, size.cx + 1, size.cy + 1, px(12, dpi_), px(12, dpi_));
        if (!region)
            return false;
        if (!SetWindowRgn(hwnd_, region, TRUE)) {
            DeleteObject(region);
            return false;
        }
        region_size_ = size;
    }
    return true;
}
void TextEditor::refresh_draft() {
    if (!edit_)
        return;
    std::wstring value = text();
    DWORD first = 0, last = 0;
    SendMessageW(edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&first),
                 reinterpret_cast<LPARAM>(&last));
    size_t caret = last;
    POINT cursor{};
    if (first != last && GetFocus() == edit_ && GetCaretPos(&cursor)) {
        LRESULT hit = SendMessageW(edit_, EM_CHARFROMPOS, 0, MAKELPARAM(cursor.x, cursor.y));
        caret = std::min<size_t>(LOWORD(hit), value.size());
    }
    if (composing_ && !composition_.empty()) {
        size_t a = std::min(composition_start_, value.size()),
               b = std::min(std::max(composition_end_, a), value.size());
        value.replace(a, b - a, composition_);
        first = last = static_cast<DWORD>(a + composition_.size());
        caret = last;
    }
    view_ = make_inline_view(anchor_, monitor_, selection_, dpi_, size_, size_px(), color_, value,
                             first, last, caret);
    if (view_.valid) {
        layout_controls();
        InvalidateRect(hwnd_, nullptr, FALSE);
        position_ime();
    }
}
void TextEditor::position_ime() {
    if (!edit_ || !view_.valid)
        return;
    HIMC context = ImmGetContext(edit_);
    if (!context)
        return;
    RECT edit_rect{};
    GetWindowRect(edit_, &edit_rect);
    LONG x = std::clamp<LONG>(anchor_.x + view_.text.caret.x, monitor_.left, monitor_.right - 1),
         y = std::clamp<LONG>(anchor_.y + view_.text.caret.y, monitor_.top, monitor_.bottom - 1);
    POINT p{x - edit_rect.left, y - edit_rect.top};
    COMPOSITIONFORM form{};
    form.dwStyle = CFS_POINT;
    form.ptCurrentPos = p;
    ImmSetCompositionWindow(context, &form);
    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = {p.x, p.y + view_.text.line_height};
    ImmSetCandidateWindow(context, &candidate);
    ImmReleaseContext(edit_, context);
}
void TextEditor::focus(const wchar_t* notice) {
    if (notice) {
        notice_ = notice;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    SetForegroundWindow(hwnd_);
    SetFocus(edit_);
    position_ime();
}
void TextEditor::select_at(POINT screen, bool extend) {
    if (!edit_ || !view_.valid)
        return;
    POINT local{screen.x - anchor_.x, screen.y - anchor_.y};
    size_t index = text_index_at(text(), size_px(), local);
    if (!extend)
        mouse_anchor_ = index;
    SetFocus(edit_);
    SendMessageW(edit_, EM_SETSEL, static_cast<WPARAM>(mouse_anchor_), static_cast<LPARAM>(index));
    notice_.clear();
    refresh_draft();
}
void TextEditor::prepare_apply() {
    if (!edit_)
        return;
    HIMC context = ImmGetContext(edit_);
    if (context) {
        ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
        ImmReleaseContext(edit_, context);
    }
}
void TextEditor::paint(HDC dc) const {
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.ScaleTransform(dpi_ / 96.0f, dpi_ / 96.0f);
    g.Clear(Color(255, 27, 32, 34));
    bool overflow = view_.scroll.x || view_.scroll.y;
    box(g, RectF(.5f, .5f, 445, overflow ? 103.0f : 69.0f), Color(255, 27, 32, 34),
        Color(255, 79, 95, 92));
    UiTypeface family;
    Font normal(family.medium(), 13, family.medium_style(), UnitPixel),
        hint_font(family.medium(), 12, family.medium_style(), UnitPixel);
    SolidBrush white(Color(255, 235, 241, 236)), muted(Color(255, 164, 180, 175));
    box(g, RectF(10, 6, 28, 30), Color(255, 42, 51, 52));
    box(g, RectF(76, 6, 28, 30), Color(255, 42, 51, 52));
    Pen line(Color(255, 231, 237, 232), 1.5f);
    line.SetStartCap(LineCapRound);
    line.SetEndCap(LineCapRound);
    g.DrawLine(&line, 19, 21, 29, 21);
    g.DrawLine(&line, 85, 21, 95, 21);
    g.DrawLine(&line, 90, 16, 90, 26);
    std::wstring number = std::to_wstring(size_);
    StringFormat center;
    center.SetAlignment(StringAlignmentCenter);
    center.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(number.c_str(), -1, &normal, RectF(40, 6, 34, 30), &center, &white);
    for (int i = 0; i < 6; ++i) {
        REAL x = 116.0f + i * 25;
        SolidBrush fill(ui_color(colors[i]));
        g.FillEllipse(&fill, x, 13.0f, 16.0f, 16.0f);
        Pen outline(color_ == colors[i] ? ui_color(current_theme().focus)
                                        : Color(255, 127, 142, 138),
                    color_ == colors[i] ? 2.0f : 1.0f);
        g.DrawEllipse(&outline, x, 13.0f, 16.0f, 16.0f);
    }
    box(g, RectF(272, 6, 76, 30), ui_color(current_theme().accent));
    box(g, RectF(354, 6, 80, 30), Color(255, 43, 52, 53));
    Pen check(ui_color(current_theme().ink), 1.7f);
    check.SetStartCap(LineCapRound);
    check.SetEndCap(LineCapRound);
    g.DrawLine(&check, 281, 21, 285, 25);
    g.DrawLine(&check, 285, 25, 292, 17);
    Pen cross(Color(255, 231, 237, 232), 1.6f);
    cross.SetStartCap(LineCapRound);
    cross.SetEndCap(LineCapRound);
    g.DrawLine(&cross, 363, 17, 371, 25);
    g.DrawLine(&cross, 371, 17, 363, 25);
    SolidBrush dark(ui_color(current_theme().ink));
    g.DrawString(L"Apply", -1, &normal, PointF(297, 14), &dark);
    g.DrawString(L"Cancel", -1, &normal, PointF(377, 14), &white);
    const wchar_t* hint = !notice_.empty() ? notice_.c_str()
                          : overflow       ? L"Text clipped at edge; current line below"
                                     : L"Enter: new line  |  Ctrl+Enter: apply  |  Esc: cancel";
    g.DrawString(hint, -1, &hint_font, PointF(10, 48), &muted);
    if (overflow && !view_.text.lines.empty()) {
        size_t line_index = std::min<size_t>(
            static_cast<size_t>(view_.text.caret.y / std::max(1, view_.text.line_height)),
            view_.text.lines.size() - 1);
        auto info = view_.text.lines[line_index];
        std::wstring value = view_.mark.text.substr(info.start, info.end - info.start);
        size_t caret = view_.text.caret_index > info.start
                           ? std::min(view_.text.caret_index - info.start, value.size())
                           : 0;
        StringFormat format(StringFormat::GenericTypographic());
        format.SetFormatFlags(format.GetFormatFlags() | StringFormatFlagsNoWrap |
                              StringFormatFlagsMeasureTrailingSpaces);
        auto measure = [&](const std::wstring& text) {
            RectF bounds;
            if (!text.empty())
                g.MeasureString(text.c_str(), static_cast<INT>(text.size()), &normal, PointF(0, 0),
                                &format, &bounds);
            return text.empty() ? 0.0f : bounds.Width;
        };
        auto preview = fit_inline_line_preview(value, caret, 422.0f, measure);
        GraphicsState saved = g.Save();
        g.SetClip(RectF(10, 73, 426, 24), CombineModeIntersect);
        if (!preview.visible.empty())
            g.DrawString(preview.visible.c_str(), static_cast<INT>(preview.visible.size()), &normal,
                         PointF(10, 73), &format, &white);
        g.Restore(saved);
        REAL caret_x = 10 + measure(preview.visible.substr(0, preview.caret_offset));
        Pen caret_pen(ui_color(current_theme().focus), 1.3f);
        g.DrawLine(&caret_pen, caret_x, 74.0f, caret_x, 91.0f);
    }
    g.Flush();
}
void TextEditor::click(POINT p) {
    p.x = MulDiv(p.x, 96, static_cast<int>(dpi_));
    p.y = MulDiv(p.y, 96, static_cast<int>(dpi_));
    if (PtInRect(&apply_rect, p)) {
        PostMessageW(owner_, WM_DRAW_TEXT_APPLY, 0, 0);
        return;
    }
    if (PtInRect(&cancel_rect, p)) {
        PostMessageW(owner_, WM_DRAW_TEXT_CANCEL, 0, 0);
        return;
    }
    if (PtInRect(&minus_rect, p))
        size_ = std::max(min_text_size, size_ - 2);
    else if (PtInRect(&plus_rect, p))
        size_ = std::min(max_text_size, size_ + 2);
    else
        for (int i = 0; i < 6; ++i) {
            RECT r{112 + i * 25, 6, 136 + i * 25, 36};
            if (PtInRect(&r, p)) {
                color_ = colors[i];
                break;
            }
        }
    notice_.clear();
    update_font();
    SetFocus(edit_);
    refresh_draft();
    PostMessageW(owner_, WM_DRAW_TEXT_CHANGED, 0, 0);
}
LRESULT CALLBACK TextEditor::edit_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l, UINT_PTR id,
                                       DWORD_PTR data) {
    auto* self = reinterpret_cast<TextEditor*>(data);
    if (msg == WM_CHAR && (w == L'\r' || w == L'\n') && (GetKeyState(VK_CONTROL) & 0x8000))
        return 0;
    if (msg == WM_KEYDOWN && w == VK_ESCAPE) {
        PostMessageW(self->owner_, WM_DRAW_TEXT_CANCEL, 0, 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && w == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000) &&
        !self->composing_) {
        PostMessageW(self->owner_, WM_DRAW_TEXT_APPLY, 0, 0);
        return 0;
    }
    if (msg == WM_IME_STARTCOMPOSITION) {
        DWORD a = 0, b = 0;
        SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b));
        self->composition_start_ = a;
        self->composition_end_ = b;
        self->composition_.clear();
        self->composing_ = true;
        self->position_ime();
    }
    if (msg == WM_IME_COMPOSITION) {
        HIMC context = ImmGetContext(hwnd);
        if (context) {
            if (l & GCS_COMPSTR) {
                LONG bytes = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
                if (bytes >= 0) {
                    self->composition_.resize(static_cast<size_t>(bytes) / sizeof(wchar_t));
                    if (bytes)
                        ImmGetCompositionStringW(context, GCS_COMPSTR, self->composition_.data(),
                                                 static_cast<DWORD>(bytes));
                }
            }
            if (l & GCS_RESULTSTR)
                self->composition_.clear();
            ImmReleaseContext(hwnd, context);
        }
    }
    LRESULT result = DefSubclassProc(hwnd, msg, w, l);
    if (msg == WM_IME_ENDCOMPOSITION) {
        self->composing_ = false;
        self->composition_.clear();
    }
    if (msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR || msg == WM_PASTE ||
        msg == WM_CUT || msg == WM_CLEAR || msg == WM_UNDO || msg == WM_IME_COMPOSITION ||
        msg == WM_IME_ENDCOMPOSITION || msg == WM_SETFOCUS)
        PostMessageW(self->owner_, WM_DRAW_TEXT_CHANGED, 0, 0);
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, edit_proc, id);
    return result;
}
LRESULT CALLBACK TextEditor::proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto* self = reinterpret_cast<TextEditor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<TextEditor*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->message(msg, w, l) : DefWindowProcW(hwnd, msg, w, l);
}
LRESULT TextEditor::message(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(hwnd_, &ps);
        RECT r{};
        GetClientRect(hwnd_, &r);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, r.right, r.bottom);
        HGDIOBJ old = SelectObject(dc, bitmap);
        paint(dc);
        BitBlt(target, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_COMMAND:
        if (HIWORD(w) == EN_CHANGE)
            PostMessageW(owner_, WM_DRAW_TEXT_CHANGED, 0, 0);
        return 0;
    case WM_LBUTTONUP:
        click({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        return 0;
    case WM_CLOSE:
        PostMessageW(owner_, WM_DRAW_TEXT_CANCEL, 0, 0);
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        edit_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, w, l);
}
}
