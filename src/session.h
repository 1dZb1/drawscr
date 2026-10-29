#pragma once
#include "canvas.h"
#include "capture_timing.h"
#include "toolbar.h"
#include "recorder.h"
#include "record_ui.h"
#include "text_editor.h"
#include "text_transaction.h"
#include "preferences.h"
#include <memory>
#include <vector>

namespace drawscr {
constexpr UINT WM_DRAW_END = WM_APP + 20;
class Session {
  public:
    explicit Session(HWND owner, std::wstring ini = {}, ULONGLONG capture_started = 0);
    ~Session();
    bool begin(bool whole_monitor = false);
    void command(Command command);
    bool is_live() const {
        return canvas_.live();
    }
    bool is_interacting() const {
        return canvas_.interacting();
    }
    bool is_recording() const {
        return record_state_ != RecordState::Idle;
    }
    bool is_encoding() const {
        return record_state_ == RecordState::Encoding;
    }
    std::wstring record_label() const;
    bool is_tool(Command c) const;
    void refresh_theme() {
        repaint();
    }
    COLORREF color() const {
        return color_;
    }
    void set_color(COLORREF c) {
        color_ = c;
    }
    void set_width(float w) {
        width_ = w;
    }
    void restore_draw();
    bool text_edit_active() const {
        return text_transaction_.active();
    }
    void hold_text_edit();
    RECT selected_area() const {
        return canvas_.selection();
    }
    HWND window() const {
        return hwnd_;
    }
    HWND toolbar_window() const {
        return toolbar_ ? toolbar_->window() : nullptr;
    }

  private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK keyboard(int, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    void finish_selection(RECT r);
    void paint(HDC dc);
    void update_live();
    void repaint();
    void mouse_down(POINT p);
    void mouse_move(POINT p);
    void mouse_up(POINT p);
    void finish_pointer_gesture();
    void set_live(bool live);
    void set_interacting(bool value);
    Image current_base();
    Image output_image();
    void record_command();
    void start_recording();
    void record_frame();
    void stop_recording();
    void recording_done();
    void save();
    void copy();
    void cancel();
    void begin_text(POINT anchor, int existing_index);
    void finish_text(bool apply);
    std::vector<Mark> display_marks() const;
    enum class RecordState { Idle, Countdown, Recording, Encoding };
    RecordState record_state_ = RecordState::Idle;
    RecordOptions record_options_;
    std::unique_ptr<Recorder> recorder_;
    uint64_t record_start_ = 0;
    int countdown_ = 0, record_width_ = 0, record_height_ = 0;
    bool capture_excluded_ = false;
    HWND owner_ = nullptr, hwnd_ = nullptr;
    HHOOK hook_ = nullptr;
    std::unique_ptr<Toolbar> toolbar_;
    std::unique_ptr<TextEditor> text_editor_;
    TextTransaction text_transaction_;
    std::wstring ini_;
    CaptureTiming capture_timing_;
    bool allow_unpersisted_size_ = false;
    CanvasState canvas_;
    RECT record_rect_{};
    Image desktop_, surface_;
    History history_;
    std::vector<Mark> working_;
    Tool tool_ = Tool::Pen;
    COLORREF color_ = RGB(244, 83, 81);
    float width_ = 4;
    int selected_ = -1;
    bool dragging_ = false, changed_ = false, modal_ = false, escape_held_ = false;
    bool text_selecting_ = false, caret_visible_ = true;
    POINT anchor_{}, last_{};
    static Session* active_;
};
}
