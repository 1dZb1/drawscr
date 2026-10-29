#include "session.h"
#include "ui_type.h"
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
using namespace Gdiplus;

namespace drawscr {
static constexpr wchar_t overlay_class[] = L"Drawscr.Overlay";
Session* Session::active_ = nullptr;
Session::Session(HWND owner, std::wstring ini, ULONGLONG capture_started)
    : owner_(owner), ini_(std::move(ini)), capture_timing_(capture_started) {}
Session::~Session() {
    if (hwnd_) {
        KillTimer(hwnd_, 42);
        KillTimer(hwnd_, 43);
    }
    recorder_.reset();
    if (hook_)
        UnhookWindowsHookEx(hook_);
    hook_ = nullptr;
    if (active_ == this)
        active_ = nullptr;
    text_editor_.reset();
    toolbar_.reset();
    if (hwnd_ && IsWindow(hwnd_))
        DestroyWindow(hwnd_);
}

bool Session::begin(bool whole_monitor) {
    capture_timing_.mark("begin");
    RECT bounds{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
    bounds.right = bounds.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    bounds.bottom = bounds.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    canvas_.reset(bounds);
    capture_timing_.mark("before_compositor_flush");
    DwmFlush();
    capture_timing_.mark("compositor_flushed");
    desktop_ = capture_screen(bounds);
    capture_timing_.mark("desktop_captured");
    surface_ = Image(bounds.right - bounds.left, bounds.bottom - bounds.top);
    capture_timing_.mark("surface_allocated");
    if (!desktop_.valid() || !surface_.valid()) {
        MessageBoxW(owner_, L"Could not prepare the desktop capture.", L"Drawscr", MB_ICONERROR);
        return false;
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = overlay_class;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, overlay_class, L"Drawscr selection",
                            WS_POPUP, bounds.left, bounds.top, bounds.right - bounds.left,
                            bounds.bottom - bounds.top, nullptr, nullptr, wc.hInstance, this);
    if (!hwnd_)
        return false;
    capture_timing_.mark("overlay_created");
    active_ = this;
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard, GetModuleHandleW(nullptr), 0);
    capture_timing_.mark("before_show");
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    if (whole_monitor) {
        POINT p{};
        GetCursorPos(&p);
        MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi))
            finish_selection(mi.rcMonitor);
    }
    return true;
}
void Session::finish_selection(RECT r) {
    if (!canvas_.select(r)) {
        dragging_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    dragging_ = false;
    RECT selection = canvas_.selection(), bounds = canvas_.bounds();
    UINT dpi = GetDpiForWindow(hwnd_);
    if (!dpi)
        dpi = 96;
    DWORD affinity = 0;
    capture_excluded_ = SetWindowDisplayAffinity(hwnd_, 0x00000011) != FALSE &&
                        GetWindowDisplayAffinity(hwnd_, &affinity) != FALSE &&
                        affinity == 0x00000011;
    RECT avoid = frame_chrome_bounds(selection, bounds, dpi);
    if (!toolbar_) {
        toolbar_ = std::make_unique<Toolbar>(*this);
        if (!toolbar_->create(hwnd_, selection, avoid)) {
            MessageBoxW(owner_, L"Could not open the toolbar.", L"Drawscr", MB_ICONERROR);
            cancel();
            return;
        }
    } else
        toolbar_->set_selection(selection, avoid);
    repaint();
    SetFocus(hwnd_);
}
bool Session::is_tool(Command c) const {
    switch (c) {
    case Command::Select:
        return tool_ == Tool::Select;
    case Command::Pen:
        return tool_ == Tool::Pen;
    case Command::Arrow:
        return tool_ == Tool::Arrow;
    case Command::Line:
        return tool_ == Tool::Line;
    case Command::Rectangle:
        return tool_ == Tool::Rectangle;
    case Command::Marker:
        return tool_ == Tool::Marker;
    case Command::Text:
        return tool_ == Tool::Text;
    case Command::Redact:
        return tool_ == Tool::Redact;
    default:
        return false;
    }
}
void Session::paint(HDC dc) {
    if (canvas_.live())
        return;
    RECT frame = canvas_.selecting() ? normalized(anchor_, last_) : canvas_.selection();
    frame = intersection(frame, canvas_.bounds());
    bool has_frame = !canvas_.selecting() || (dragging_ && !empty(frame));
    static const std::vector<Mark> none;
    auto visible = display_marks();
    const auto& marks = canvas_.selecting() ? none : (dragging_ ? working_ : visible);
    UINT dpi = GetDpiForWindow(hwnd_);
    if (!dpi)
        dpi = 96;
    if (!render_frozen_canvas(surface_, desktop_, canvas_.bounds(), frame, has_frame, marks,
                              selected_, canvas_.locked(), dpi))
        return;
    capture_timing_.mark("canvas_rendered");
    if (canvas_.selecting()) {
        HDC buffered = surface_.begin();
        if (buffered) {
            Graphics g(buffered);
            g.ScaleTransform(dpi / 96.0f, dpi / 96.0f);
            SolidBrush plate(Color(235, 29, 36, 37)), ink(Color(255, 235, 240, 236));
            g.FillRectangle(&plate, 14, 14, 530, 40);
            capture_timing_.mark("before_font_family");
            UiTypeface family;
            capture_timing_.mark("font_family_ready");
            Font font(family.medium(), 15, family.medium_style(), UnitPixel);
            g.DrawString(L"Drag to select  |  M monitor  |  Ctrl+A all  |  Esc cancel", -1, &font,
                         PointF(25, 24), &ink);
            g.Flush();
            capture_timing_.mark("hint_text_drawn");
            GdiFlush();
            surface_.end(buffered);
        }
    }
    if (text_editor_)
        render_inline_overlay(surface_, canvas_.bounds(), canvas_.selection(), text_editor_->view(),
                              false, caret_visible_);
    surface_.paint(dc, 0, 0);
}
void Session::update_live() {
    if (!canvas_.live() || !hwnd_ || canvas_.selecting())
        return;
    UINT dpi = GetDpiForWindow(hwnd_);
    if (!dpi)
        dpi = 96;
    auto visible = display_marks();
    if (!render_live_canvas(surface_, canvas_.bounds(), canvas_.selection(),
                            dragging_ ? working_ : visible, selected_, canvas_.locked(), dpi))
        return;
    if (text_editor_)
        render_inline_overlay(surface_, canvas_.bounds(), canvas_.selection(), text_editor_->view(),
                              true, caret_visible_);
    RECT bounds = canvas_.bounds();
    HDC screen = GetDC(nullptr), source = surface_.begin();
    POINT dst{bounds.left, bounds.top}, origin{0, 0};
    SIZE size{surface_.width(), surface_.height()};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (screen && source)
        UpdateLayeredWindow(hwnd_, screen, &dst, &size, source, &origin, 0, &blend, ULW_ALPHA);
    surface_.end(source);
    if (screen)
        ReleaseDC(nullptr, screen);
}
void Session::repaint() {
    if (canvas_.live())
        update_live();
    else if (hwnd_)
        InvalidateRect(hwnd_, nullptr, FALSE);
    if (toolbar_ && !dragging_ && !canvas_.framing())
        toolbar_->refresh();
}
static POINT bounded_point(POINT p, RECT r) {
    p.x = std::clamp<LONG>(p.x, r.left, r.right - 1);
    p.y = std::clamp<LONG>(p.y, r.top, r.bottom - 1);
    return p;
}
// Keep the last point under the cursor and the preceding one as the last
// accepted sample. Compact old samples in place when the bounded path fills.
static void update_stroke(Mark& mark, POINT p) {
    auto& points = mark.points;
    if (points.empty()) {
        points.push_back(p);
        points.push_back(p);
        return;
    }
    if (points.size() == 1) {
        points.push_back(p);
        return;
    }
    POINT accepted = points[points.size() - 2];
    points.back() = p;
    long long distance = std::abs(static_cast<long long>(p.x) - accepted.x) +
                         std::abs(static_cast<long long>(p.y) - accepted.y);
    if (distance < 2)
        return;
    if (points.size() >= 2048) {
        const size_t count = points.size();
        size_t write = 1;
        for (size_t i = 2; i + 2 < count; i += 2)
            points[write++] = points[i];
        points[write++] = points[count - 2];
        points[write++] = points[count - 1];
        points.resize(write);
    }
    points.push_back(p);
}
void Session::finish_pointer_gesture() {
    bool had_text_selection = text_selecting_, framed = canvas_.framing(), had_drag = dragging_;
    if (!had_text_selection && !framed && !had_drag)
        return;
    text_selecting_ = false;
    if (framed)
        canvas_.end_frame();
    if (had_drag) {
        if (!canvas_.selecting() && !working_.empty() && (tool_ != Tool::Select || changed_))
            history_.commit(std::move(working_));
        dragging_ = false;
        changed_ = false;
        working_.clear();
    }
    if (framed && toolbar_) {
        UINT dpi = GetDpiForWindow(hwnd_);
        if (!dpi)
            dpi = 96;
        toolbar_->set_selection(canvas_.selection(),
                                frame_chrome_bounds(canvas_.selection(), canvas_.bounds(), dpi));
    }
    repaint();
}
void Session::mouse_down(POINT client) {
    if (dragging_ || canvas_.framing() || text_selecting_) {
        if (GetCapture() == hwnd_)
            ReleaseCapture();
        finish_pointer_gesture();
    }
    POINT p = canvas_to_screen(client, canvas_.bounds());
    if (text_editor_) {
        RECT field = text_editor_->view().placement.field;
        if (PtInRect(&field, p)) {
            text_editor_->select_at(p, false);
            text_selecting_ = true;
            caret_visible_ = true;
            SetCapture(hwnd_);
            repaint();
        } else
            hold_text_edit();
        return;
    }
    if (canvas_.interacting())
        return;
    if (canvas_.selecting()) {
        anchor_ = last_ = p;
        dragging_ = true;
        SetCapture(hwnd_);
        repaint();
        return;
    }
    RECT selection = canvas_.selection();
    UINT dpi = GetDpiForWindow(hwnd_);
    if (!dpi)
        dpi = 96;
    FramePart part = frame_hit(selection, canvas_.bounds(), p, dpi);
    if (part != FramePart::None) {
        if (canvas_.begin_frame(part, p))
            SetCapture(hwnd_);
        repaint();
        return;
    }
    if (!PtInRect(&selection, p))
        return;
    if (tool_ == Tool::Text) {
        int hit = hit_test(history_.current(), p);
        if (hit >= 0 && history_.current()[static_cast<size_t>(hit)].tool != Tool::Text)
            hit = -1;
        begin_text(p, hit);
        return;
    }
    working_ = history_.current();
    anchor_ = last_ = p;
    changed_ = false;
    if (tool_ == Tool::Select) {
        selected_ = hit_test(working_, p);
        if (selected_ < 0) {
            repaint();
            return;
        }
    } else {
        if (working_.size() >= 256) {
            MessageBoxW(hwnd_, L"This capture has reached the annotation limit.", L"Drawscr",
                        MB_ICONINFORMATION);
            return;
        }
        Mark m;
        m.tool = tool_;
        m.points = {p, p};
        m.color = color_;
        m.width = width_ * dpi / 96.0f;
        working_.push_back(std::move(m));
        selected_ = -1;
    }
    dragging_ = true;
    SetCapture(hwnd_);
    repaint();
}
void Session::mouse_move(POINT client) {
    POINT p = canvas_to_screen(client, canvas_.bounds());
    if (text_selecting_ && text_editor_) {
        text_editor_->select_at(p, true);
        caret_visible_ = true;
        repaint();
        return;
    }
    if (canvas_.framing()) {
        if (canvas_.move_frame(p))
            repaint();
        return;
    }
    if (!dragging_)
        return;
    if (canvas_.selecting()) {
        last_ = p;
        repaint();
        return;
    }
    p = bounded_point(p, canvas_.selection());
    if (tool_ == Tool::Select) {
        int dx = p.x - last_.x, dy = p.y - last_.y;
        if (selected_ >= 0 && (dx || dy)) {
            translate(working_[static_cast<size_t>(selected_)], dx, dy);
            changed_ = true;
        }
    } else if (!working_.empty()) {
        auto& m = working_.back();
        if (m.tool == Tool::Pen || m.tool == Tool::Marker)
            update_stroke(m, p);
        else
            m.points.back() = p;
        changed_ = true;
    }
    last_ = p;
    repaint();
}
void Session::mouse_up(POINT client) {
    POINT p = canvas_to_screen(client, canvas_.bounds());
    if (text_selecting_ && text_editor_) {
        text_selecting_ = false;
        text_editor_->select_at(p, true);
        if (GetCapture() == hwnd_)
            ReleaseCapture();
        repaint();
        return;
    }
    if (canvas_.framing()) {
        canvas_.move_frame(p);
        canvas_.end_frame();
        if (GetCapture() == hwnd_)
            ReleaseCapture();
        if (toolbar_) {
            UINT dpi = GetDpiForWindow(hwnd_);
            if (!dpi)
                dpi = 96;
            toolbar_->set_selection(
                canvas_.selection(),
                frame_chrome_bounds(canvas_.selection(), canvas_.bounds(), dpi));
        }
        repaint();
        return;
    }
    if (!dragging_)
        return;
    if (canvas_.selecting()) {
        last_ = p;
        dragging_ = false;
        if (GetCapture() == hwnd_)
            ReleaseCapture();
        finish_selection(normalized(anchor_, last_));
        return;
    }
    mouse_move(client);
    dragging_ = false;
    if (GetCapture() == hwnd_)
        ReleaseCapture();
    bool edit_selected = tool_ == Tool::Select && !changed_ && selected_ >= 0 &&
                         static_cast<size_t>(selected_) < history_.current().size() &&
                         history_.current()[static_cast<size_t>(selected_)].tool == Tool::Text;
    int edit_index = selected_;
    if (tool_ != Tool::Select || changed_)
        history_.commit(std::move(working_));
    working_.clear();
    repaint();
    if (edit_selected)
        begin_text(p, edit_index);
}
void Session::set_live(bool value) {
    if (canvas_.selecting() || value == canvas_.live() || !canvas_.set_live(value))
        return;
    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    style = value ? (style | WS_EX_LAYERED) : (style & ~(WS_EX_LAYERED | WS_EX_TRANSPARENT));
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (toolbar_) {
        UINT dpi = GetDpiForWindow(hwnd_);
        if (!dpi)
            dpi = 96;
        toolbar_->set_selection(canvas_.selection(),
                                frame_chrome_bounds(canvas_.selection(), canvas_.bounds(), dpi));
    }
    repaint();
}
void Session::set_interacting(bool value) {
    if (!canvas_.live() || value == canvas_.interacting())
        return;
    if (value && !hook_) {
        MessageBoxW(
            hwnd_,
            L"Drawscr could not install the escape key handler. Interact mode is unavailable.",
            L"Drawscr", MB_ICONWARNING);
        return;
    }
    if (!canvas_.set_interacting(value))
        return;
    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    style = value ? (style | WS_EX_TRANSPARENT) : (style & ~WS_EX_TRANSPARENT);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (toolbar_) {
        UINT dpi = GetDpiForWindow(hwnd_);
        if (!dpi)
            dpi = 96;
        toolbar_->set_selection(canvas_.selection(),
                                frame_chrome_bounds(canvas_.selection(), canvas_.bounds(), dpi));
    }
    if (!value) {
        SetForegroundWindow(hwnd_);
        SetFocus(hwnd_);
    }
    repaint();
}
void Session::restore_draw() {
    if (canvas_.live())
        set_interacting(false);
}
Image Session::current_base() {
    RECT selection = canvas_.selection();
    if (!canvas_.live())
        return crop_image(desktop_, screen_to_canvas(selection, canvas_.bounds()));
    if (!capture_excluded_ || !toolbar_ || !toolbar_->capture_excluded())
        return {};
    toolbar_->suspend_tooltips_for_capture();
    DwmFlush();
    Image image = capture_screen(selection);
    toolbar_->resume_tooltips_after_capture();
    return image;
}
Image Session::output_image() {
    if (canvas_.selecting())
        return {};
    if (!canvas_.live())
        return compose_selection(desktop_, canvas_.bounds(), canvas_.selection(),
                                 history_.current());
    Image base = current_base();
    RECT r = canvas_.selection();
    return compose(base, history_.current(), {r.left, r.top});
}
void Session::copy() {
    Image out = output_image();
    std::wstring error;
    if (!out.valid() && canvas_.live())
        error = L"Windows could not exclude the Drawscr overlay from this capture.";
    else if (copy_clipboard(hwnd_, out, error)) {
        cancel();
        return;
    }
    MessageBoxW(hwnd_, error.c_str(), L"Drawscr", MB_ICONERROR);
}
void Session::save() {
    Image out = output_image();
    if (!out.valid()) {
        MessageBoxW(hwnd_,
                    canvas_.live()
                        ? L"Windows could not exclude the Drawscr overlay from this capture."
                        : L"Could not compose the image.",
                    L"Drawscr", MB_ICONERROR);
        return;
    }
    wchar_t name[32768] = L"Drawscr";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter =
        L"PNG image (*.png)\0*.png\0JPEG image, quality 100 (*.jpg)\0*.jpg;*.jpeg\0\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = name;
    ofn.nMaxFile = 32768;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrTitle = L"Save image";
    modal_ = true;
    bool chosen = GetSaveFileNameW(&ofn) != FALSE;
    modal_ = false;
    if (!chosen)
        return;
    std::wstring path = name;
    size_t sep = path.find_last_of(L"\\/"), dot = path.find_last_of(L'.');
    bool jpeg = ofn.nFilterIndex == 2;
    if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep)) {
        path += jpeg ? L".jpg" : L".png";
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES &&
            MessageBoxW(hwnd_, L"A file with that name already exists. Replace it?", L"Drawscr",
                        MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
    } else {
        std::wstring ext = path.substr(dot + 1);
        if (_wcsicmp(ext.c_str(), L"jpg") == 0 || _wcsicmp(ext.c_str(), L"jpeg") == 0)
            jpeg = true;
        else if (_wcsicmp(ext.c_str(), L"png") == 0)
            jpeg = false;
    }
    std::wstring error;
    if (!save_image(out, path, jpeg, error))
        MessageBoxW(hwnd_, error.c_str(), L"Drawscr", MB_ICONERROR);
    else
        cancel();
}
std::vector<Mark> Session::display_marks() const {
    auto marks = history_.current();
    if (text_transaction_.existing() && text_transaction_.index() < marks.size())
        marks.erase(marks.begin() + static_cast<std::ptrdiff_t>(text_transaction_.index()));
    return marks;
}
void Session::begin_text(POINT anchor, int existing_index) {
    if (text_editor_ || is_recording() ||
        (existing_index < 0 && history_.current().size() >= 256)) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    int preferred = ini_.empty() ? default_text_size : load_text_size(ini_);
    bool started =
        existing_index >= 0
            ? text_transaction_.begin_existing(history_, static_cast<size_t>(existing_index))
            : text_transaction_.begin_new(anchor, color_, static_cast<float>(preferred));
    if (!started)
        return;
    const Mark& original = text_transaction_.original();
    if (existing_index >= 0 && !original.points.empty())
        anchor = original.points.front();
    float physical =
        existing_index >= 0
            ? (original.text_size > 0 ? original.text_size : std::max(16.0f, original.width * 6))
            : 0;
    auto editor = std::make_unique<TextEditor>();
    if (!editor->create(hwnd_, anchor, canvas_.selection(), original.text, original.color,
                        preferred, physical, is_recording())) {
        editor.reset();
        text_transaction_.cancel();
        MessageBeep(MB_ICONWARNING);
        SetForegroundWindow(hwnd_);
        SetFocus(hwnd_);
        return;
    }
    text_editor_ = std::move(editor);
    allow_unpersisted_size_ = false;
    selected_ = -1;
    caret_visible_ = true;
    SetTimer(hwnd_, 43, 500, nullptr);
    repaint();
}
void Session::hold_text_edit() {
    if (text_editor_)
        text_editor_->focus(L"Apply or cancel text first.");
}
void Session::finish_text(bool apply) {
    if (!text_editor_)
        return;
    if (apply) {
        text_editor_->prepare_apply();
        std::wstring value = text_editor_->text();
        auto result = commit_text_edit(text_transaction_, history_, value, text_editor_->color(),
                                       text_editor_->size_px(), text_editor_->size_dip(), ini_,
                                       allow_unpersisted_size_);
        if (result == TextApplyResult::PreferencesUnavailable) {
            allow_unpersisted_size_ = true;
            text_editor_->focus(L"Could not remember size. Apply again to continue.");
            return;
        }
        if (result != TextApplyResult::Applied) {
            hold_text_edit();
            return;
        }
    } else
        text_transaction_.cancel();
    KillTimer(hwnd_, 43);
    text_selecting_ = false;
    if (GetCapture() == hwnd_)
        ReleaseCapture();
    text_editor_.reset();
    allow_unpersisted_size_ = false;
    repaint();
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
}

std::wstring Session::record_label() const {
    if (record_state_ == RecordState::Countdown)
        return L"Cancel " + std::to_wstring(countdown_);
    if (record_state_ == RecordState::Encoding)
        return L"Encoding";
    if (record_state_ == RecordState::Recording) {
        uint64_t secs = (GetTickCount64() - record_start_) / 1000;
        wchar_t label[48]{};
        swprintf_s(label, L"Stop %llu:%02llu", static_cast<unsigned long long>(secs / 60),
                   static_cast<unsigned long long>(secs % 60));
        return label;
    }
    return L"Record";
}
void Session::record_command() {
    if (record_state_ == RecordState::Countdown) {
        KillTimer(hwnd_, 42);
        record_state_ = RecordState::Idle;
        canvas_.lock_frame(false);
        repaint();
        return;
    }
    if (record_state_ == RecordState::Recording) {
        stop_recording();
        return;
    }
    if (record_state_ == RecordState::Encoding) {
        if (recorder_)
            recorder_->cancel();
        recorder_.reset();
        record_state_ = RecordState::Idle;
        canvas_.lock_frame(false);
        repaint();
        return;
    }
    if (!capture_excluded_ || !toolbar_ || !toolbar_->capture_excluded()) {
        MessageBoxW(
            hwnd_,
            L"Windows could not exclude Drawscr controls from capture on this display. Recording is unavailable.",
            L"Drawscr", MB_ICONWARNING);
        return;
    }
    RECT selected = canvas_.selection();
    SIZE source{selected.right - selected.left, selected.bottom - selected.top};
    modal_ = true;
    bool chosen = choose_recording(hwnd_, record_options_, source);
    modal_ = false;
    if (!chosen)
        return;
    if (!valid_record_frame_size(source.cx, source.cy)) {
        MessageBoxW(hwnd_,
                    L"The selected area exceeds the recording limit. Resize the frame first.",
                    L"Drawscr", MB_ICONWARNING);
        return;
    }
    set_live(true);
    if (!canvas_.live())
        return;
    record_rect_ = selected;
    canvas_.lock_frame(true);
    countdown_ = record_options_.countdown;
    if (countdown_ > 0) {
        record_state_ = RecordState::Countdown;
        SetTimer(hwnd_, 42, 1000, nullptr);
        repaint();
    } else
        start_recording();
}
void Session::start_recording() {
    KillTimer(hwnd_, 42);
    int w = record_rect_.right - record_rect_.left, h = record_rect_.bottom - record_rect_.top;
    record_width_ = w;
    record_height_ = h;
    recorder_ = std::make_unique<Recorder>();
    std::wstring error;
    if (!recorder_->start(record_width_, record_height_, hwnd_, error, {}, nullptr,
                          record_options_.compression)) {
        recorder_.reset();
        record_state_ = RecordState::Idle;
        canvas_.lock_frame(false);
        MessageBoxW(hwnd_, error.c_str(), L"Drawscr", MB_ICONERROR);
        repaint();
        return;
    }
    record_start_ = GetTickCount64();
    record_state_ = RecordState::Recording;
    record_frame();
    if (record_state_ == RecordState::Recording)
        SetTimer(hwnd_, 42, 1000 / record_options_.fps, nullptr);
    repaint();
}
void Session::record_frame() {
    if (record_state_ != RecordState::Recording || !recorder_)
        return;
    uint64_t elapsed = GetTickCount64() - record_start_;
    Image image = capture_screen(record_rect_);
    if (!image.valid()) {
        stop_recording();
        MessageBoxW(hwnd_, L"Capture stopped because the screen could not be read.", L"Drawscr",
                    MB_ICONWARNING);
        return;
    }
    HDC dc = image.begin();
    if (!dc) {
        stop_recording();
        return;
    }
    const auto& marks = dragging_ ? working_ : history_.current();
    draw_marks(dc, marks, -1, {record_rect_.left, record_rect_.top});
    GdiFlush();
    image.end(dc);
    auto result = recorder_->submit(std::move(image), elapsed);
    if (result == SubmitResult::Closed)
        stop_recording();
    if (toolbar_)
        toolbar_->refresh();
}

void Session::stop_recording() {
    if (record_state_ != RecordState::Recording || !recorder_)
        return;
    KillTimer(hwnd_, 42);
    recorder_->stop(GetTickCount64() - record_start_);
    record_state_ = RecordState::Encoding;
    repaint();
}
void Session::recording_done() {
    if (!recorder_ || !recorder_->finished())
        return;
    KillTimer(hwnd_, 42);
    auto result = recorder_->take_result();
    recorder_.reset();
    if (!result.ok) {
        record_state_ = RecordState::Idle;
        canvas_.lock_frame(false);
        repaint();
        std::wstring message = result.error.empty()
                                   ? (result.failure == RecordingFailure::OutOfMemory
                                          ? L"Not enough memory to finish the recording."
                                          : L"Could not finish the recording.")
                                   : result.error;
        if (GetFileAttributesW(result.raw_path.c_str()) != INVALID_FILE_ATTRIBUTES)
            message += L"\n\nRecording data was kept for recovery:\n" + result.raw_path;
        modal_ = true;
        MessageBoxW(hwnd_, message.c_str(), L"Drawscr", MB_ICONERROR);
        modal_ = false;
        return;
    }
    std::wstring error;
    modal_ = true;
    PreviewResult preview = preview_recording(
        hwnd_, result.gif_path, result.bytes,
        record_options_.compression == GifCompression::Strong && result.quantization.fallback,
        result.storage_limited, error);
    modal_ = false;
    if (preview == PreviewResult::Failed) {
        error += L"\n\nThe GIF was kept here:\n" + result.gif_path;
        MessageBoxW(hwnd_, error.c_str(), L"Drawscr", MB_ICONERROR);
    } else
        DeleteFileW(result.gif_path.c_str());
    record_state_ = RecordState::Idle;
    canvas_.lock_frame(false);
    if (preview == PreviewResult::Saved) {
        cancel();
        return;
    }
    repaint();
}

void Session::command(Command c) {
    if (text_editor_) {
        hold_text_edit();
        return;
    }
    if (is_recording() && (c == Command::Mode || c == Command::Screenshot || c == Command::Live ||
                           c == Command::Copy || c == Command::Save || c == Command::SelectAll))
        return;
    switch (c) {
    case Command::Mode:
        set_live(!canvas_.live());
        break;
    case Command::Screenshot:
        set_live(false);
        break;
    case Command::Live:
        set_live(true);
        break;
    case Command::Interact:
        set_interacting(!canvas_.interacting());
        break;
    case Command::Record:
        if (canvas_.output(CanvasOutput::Record) == CanvasOutput::Record)
            record_command();
        break;
    case Command::Copy:
        if (canvas_.output(CanvasOutput::Copy) == CanvasOutput::Copy)
            copy();
        break;
    case Command::Save:
        if (canvas_.output(CanvasOutput::Save) == CanvasOutput::Save)
            save();
        break;
    case Command::Close:
        cancel();
        break;
    case Command::SelectAll:
        finish_selection(canvas_.bounds());
        break;
    case Command::Select:
        tool_ = Tool::Select;
        break;
    case Command::Pen:
        tool_ = Tool::Pen;
        selected_ = -1;
        break;
    case Command::Arrow:
        tool_ = Tool::Arrow;
        selected_ = -1;
        break;
    case Command::Line:
        tool_ = Tool::Line;
        selected_ = -1;
        break;
    case Command::Rectangle:
        tool_ = Tool::Rectangle;
        selected_ = -1;
        break;
    case Command::Marker:
        tool_ = Tool::Marker;
        selected_ = -1;
        break;
    case Command::Text:
        tool_ = Tool::Text;
        selected_ = -1;
        break;
    case Command::Redact:
        tool_ = Tool::Redact;
        selected_ = -1;
        break;
    case Command::Undo:
        history_.undo();
        selected_ = -1;
        break;
    case Command::Redo:
        history_.redo();
        selected_ = -1;
        break;
    default:
        break;
    }
    repaint();
}
void Session::cancel() {
    PostMessageW(owner_, WM_DRAW_END, 0, 0);
}
LRESULT CALLBACK Session::keyboard(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION && active_ &&
        reinterpret_cast<KBDLLHOOKSTRUCT*>(l)->vkCode == VK_ESCAPE) {
        if (w == WM_KEYUP || w == WM_SYSKEYUP) {
            bool consumed = active_->escape_held_;
            active_->escape_held_ = false;
            if (consumed)
                return 1;
        }
        EscapeTarget target = escape_target(active_->modal_, active_->text_editor_ != nullptr);
        if (target != EscapeTarget::PassThrough && (w == WM_KEYDOWN || w == WM_SYSKEYDOWN)) {
            if (!active_->escape_held_) {
                active_->escape_held_ = true;
                if (target == EscapeTarget::CancelText)
                    PostMessageW(active_->hwnd_, WM_DRAW_TEXT_CANCEL, 0, 0);
                else
                    active_->cancel();
            }
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}
LRESULT CALLBACK Session::proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    Session* self = reinterpret_cast<Session*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<Session*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->message(msg, w, l) : DefWindowProcW(hwnd, msg, w, l);
}
LRESULT Session::message(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        capture_timing_.mark("first_paint_begin");
        HDC dc = BeginPaint(hwnd_, &ps);
        paint(dc);
        EndPaint(hwnd_, &ps);
        if (capture_timing_.finish())
            PostMessageW(hwnd_, WM_APP + 61, 0, 0);
        return 0;
    }
    case WM_APP + 61:
        capture_timing_.write(ini_);
        return 0;
    case WM_PRINTCLIENT:
        if (canvas_.live()) {
            if (render_live_canvas(surface_, canvas_.bounds(), canvas_.selection(),
                                   dragging_ ? working_ : display_marks(), selected_,
                                   canvas_.locked(), GetDpiForWindow(hwnd_))) {
                if (text_editor_)
                    render_inline_overlay(surface_, canvas_.bounds(), canvas_.selection(),
                                          text_editor_->view(), true, caret_visible_);
                surface_.paint(reinterpret_cast<HDC>(w), 0, 0);
            }
        } else
            paint(reinterpret_cast<HDC>(w));
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        mouse_down({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        return 0;
    case WM_MOUSEMOVE:
        if (!(w & MK_LBUTTON) && (dragging_ || canvas_.framing() || text_selecting_)) {
            if (GetCapture() == hwnd_)
                ReleaseCapture();
            finish_pointer_gesture();
        } else
            mouse_move({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        return 0;
    case WM_LBUTTONUP:
        mouse_up({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        return 0;
    case WM_SETCURSOR: {
        if (LOWORD(l) == HTCLIENT) {
            LPCWSTR cursor = IDC_ARROW;
            if (text_editor_) {
                POINT p{};
                GetCursorPos(&p);
                RECT field = text_editor_->view().placement.field;
                cursor = PtInRect(&field, p) ? IDC_IBEAM : IDC_ARROW;
            } else if (canvas_.selecting())
                cursor = IDC_CROSS;
            else if (!canvas_.interacting()) {
                POINT p{};
                GetCursorPos(&p);
                FramePart part =
                    frame_hit(canvas_.selection(), canvas_.bounds(), p, GetDpiForWindow(hwnd_));
                if (part != FramePart::None && canvas_.locked())
                    cursor = IDC_NO;
                else if (part == FramePart::Move)
                    cursor = IDC_SIZEALL;
                else if (part == FramePart::NorthWest || part == FramePart::SouthEast)
                    cursor = IDC_SIZENWSE;
                else if (part == FramePart::NorthEast || part == FramePart::SouthWest)
                    cursor = IDC_SIZENESW;
                else if (part == FramePart::North || part == FramePart::South)
                    cursor = IDC_SIZENS;
                else if (part == FramePart::East || part == FramePart::West)
                    cursor = IDC_SIZEWE;
                else {
                    RECT selected = canvas_.selection();
                    if (PtInRect(&selected, p))
                        cursor = tool_ == Tool::Select ? IDC_ARROW : IDC_CROSS;
                }
            }
            SetCursor(LoadCursorW(nullptr, cursor));
            return TRUE;
        }
        break;
    }
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(l) != hwnd_)
            finish_pointer_gesture();
        return 0;
    case WM_CANCELMODE:
        if (GetCapture() == hwnd_)
            ReleaseCapture();
        finish_pointer_gesture();
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            if (text_editor_)
                finish_text(false);
            else
                cancel();
            return 0;
        }
        if (text_editor_) {
            hold_text_edit();
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && w == 'A') {
            command(Command::SelectAll);
            return 0;
        }
        if (canvas_.selecting() && w == 'M') {
            POINT p{};
            GetCursorPos(&p);
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi))
                finish_selection(mi.rcMonitor);
            return 0;
        }
        if (!canvas_.selecting() && w == VK_DELETE && selected_ >= 0) {
            auto marks = history_.current();
            marks.erase(marks.begin() + selected_);
            history_.commit(std::move(marks));
            selected_ = -1;
            repaint();
            return 0;
        }
        if (!canvas_.selecting() && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (w == 'C')
                command(Command::Copy);
            else if (w == 'S')
                command(Command::Save);
            else if (w == 'Z')
                command(Command::Undo);
            else if (w == 'Y')
                command(Command::Redo);
            return 0;
        }
        break;
    case WM_TIMER:
        if (w == 43 && text_editor_) {
            caret_visible_ = !caret_visible_;
            repaint();
            return 0;
        }
        if (w == 42) {
            if (record_state_ == RecordState::Countdown) {
                if (--countdown_ <= 0)
                    start_recording();
                else
                    repaint();
            } else if (record_state_ == RecordState::Recording)
                record_frame();
            return 0;
        }
        break;
    case WM_DRAW_TEXT_CHANGED:
        if (text_editor_) {
            text_editor_->refresh_draft();
            caret_visible_ = true;
            repaint();
        }
        return 0;
    case WM_DRAW_TEXT_APPLY:
        finish_text(true);
        return 0;
    case WM_DRAW_TEXT_CANCEL:
        finish_text(false);
        return 0;
    case WM_DRAW_RECORD_DONE:
        recording_done();
        return 0;
    case WM_DISPLAYCHANGE:
        cancel();
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, w, l);
}
}
