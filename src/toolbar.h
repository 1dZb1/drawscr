#pragma once
#include "model.h"
#include "tooltips.h"
#include <windows.h>
#include <vector>
#include <array>

namespace drawscr {
class Session;
enum class Command {
    Mode,
    Screenshot,
    Live,
    Interact,
    Record,
    Copy,
    Save,
    Close,
    SelectAll,
    Select,
    Pen,
    Arrow,
    Line,
    Rectangle,
    Marker,
    Text,
    Redact,
    Color,
    Width,
    Undo,
    Redo
};
struct ToolbarButton {
    Command command;
    RECT rect;
    const wchar_t* label;
    const wchar_t* tip;
};
struct ToolbarGeometry {
    RECT mode, interact, record, copy, save, close;
    std::array<RECT, 12> tools;
};
inline ToolbarGeometry wide_toolbar_geometry() {
    ToolbarGeometry g{{10, 8, 128, 40},
                      {132, 8, 230, 40},
                      {234, 8, 344, 40},
                      {348, 8, 407, 40},
                      {411, 8, 470, 40},
                      {484, 8, 516, 40},
                      {}};
    for (int i = 0; i < 12; ++i) {
        LONG x = 10 + i * 38 + (i >= 8 ? 8 : 0);
        g.tools[static_cast<size_t>(i)] = {x, 50, x + 34, 84};
    }
    return g;
}
class Toolbar {
  public:
    explicit Toolbar(Session& session);
    ~Toolbar();
    bool create(HWND owner, RECT selection, RECT avoid);
    void set_selection(RECT selection, RECT avoid);
    void refresh();
    void show(bool visible);
    void suspend_tooltips_for_capture();
    void resume_tooltips_after_capture();
    HWND window() const {
        return hwnd_;
    }
    bool capture_excluded() const {
        return capture_excluded_;
    }

  private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    void paint(HDC);
    void render(HDC, int width, int height);
    void layout();
    void reposition();
    void sync_flyout_tooltips();
    void choose(POINT);
    Session& session_;
    HWND hwnd_ = nullptr;
    Tooltips tips_;
    RECT selection_{}, avoid_{};
    UINT dpi_ = 96;
    int hover_ = -1, focus_ = 0, flyout_ = 0, bar_width_ = 526, bar_height_ = 94;
    bool tracking_ = false, capture_excluded_ = false, tooltip_recording_ = false,
         tooltip_allowed_ = true;
    bool tooltip_live_ = false;
    std::vector<ToolbarButton> buttons_;
};
}
