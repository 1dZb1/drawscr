#pragma once
#include "image.h"
#include <array>

namespace drawscr {
enum class CanvasMode { Selecting, Screenshot, LiveDraw, LiveInteract };
enum class CanvasOutput { None, Copy, Save, Record };
enum class FramePart {
    None,
    Move,
    NorthWest,
    North,
    NorthEast,
    East,
    SouthEast,
    South,
    SouthWest,
    West
};

POINT screen_to_canvas(POINT screen, RECT virtual_bounds);
POINT canvas_to_screen(POINT client, RECT virtual_bounds);
RECT screen_to_canvas(RECT screen, RECT virtual_bounds);
std::array<POINT, 8> frame_handles(RECT frame);
RECT frame_grip(RECT frame, RECT bounds, UINT dpi);
RECT frame_chrome_bounds(RECT frame, RECT bounds, UINT dpi);
FramePart frame_hit(RECT frame, RECT bounds, POINT screen, UINT dpi);
RECT adjust_frame(RECT frame, RECT bounds, FramePart part, POINT delta);

class CanvasState {
  public:
    void reset(RECT bounds);
    bool select(RECT screen_rect);
    void clear();
    bool begin_frame(FramePart part, POINT screen);
    bool move_frame(POINT screen);
    void end_frame();
    bool set_live(bool live);
    bool set_interacting(bool interact);
    CanvasOutput output(CanvasOutput requested) const;
    void lock_frame(bool locked) {
        locked_ = locked;
    }
    RECT bounds() const {
        return bounds_;
    }
    RECT selection() const {
        return selection_;
    }
    CanvasMode mode() const {
        return mode_;
    }
    bool selecting() const {
        return mode_ == CanvasMode::Selecting;
    }
    bool live() const {
        return mode_ == CanvasMode::LiveDraw || mode_ == CanvasMode::LiveInteract;
    }
    bool interacting() const {
        return mode_ == CanvasMode::LiveInteract;
    }
    bool locked() const {
        return locked_;
    }
    bool framing() const {
        return frame_part_ != FramePart::None;
    }

  private:
    RECT bounds_{}, selection_{}, frame_start_{};
    POINT frame_anchor_{};
    CanvasMode mode_ = CanvasMode::Selecting;
    FramePart frame_part_ = FramePart::None;
    bool locked_ = false;
};

bool render_frozen_canvas(Image& target, const Image& desktop, RECT bounds, RECT selection,
                          bool has_selection, const std::vector<Mark>& marks, int selected,
                          bool locked, UINT dpi);
bool render_live_canvas(Image& target, RECT bounds, RECT selection, const std::vector<Mark>& marks,
                        int selected, bool locked, UINT dpi);
Image compose_selection(const Image& desktop, RECT bounds, RECT selection,
                        const std::vector<Mark>& marks);
} // namespace drawscr
