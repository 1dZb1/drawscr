#include "canvas.h"
#include "ui_type.h"
#include "theme.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <limits>
using namespace Gdiplus;

namespace drawscr {
static int scaled(int value, UINT dpi) {
    return std::max(1, MulDiv(value, static_cast<int>(dpi ? dpi : 96), 96));
}
POINT screen_to_canvas(POINT p, RECT b) {
    return {p.x - b.left, p.y - b.top};
}
POINT canvas_to_screen(POINT p, RECT b) {
    return {p.x + b.left, p.y + b.top};
}
RECT screen_to_canvas(RECT r, RECT b) {
    return {r.left - b.left, r.top - b.top, r.right - b.left, r.bottom - b.top};
}
std::array<POINT, 8> frame_handles(RECT r) {
    LONG right = r.right - 1, bottom = r.bottom - 1, mx = r.left + (right - r.left) / 2,
         my = r.top + (bottom - r.top) / 2;
    return {{{r.left, r.top},
             {mx, r.top},
             {right, r.top},
             {right, my},
             {right, bottom},
             {mx, bottom},
             {r.left, bottom},
             {r.left, my}}};
}
RECT frame_grip(RECT r, RECT bounds, UINT dpi) {
    LONG width = std::min<LONG>(scaled(38, dpi), bounds.right - bounds.left),
         height = std::min<LONG>(scaled(18, dpi), bounds.bottom - bounds.top);
    if (width <= 0 || height <= 0)
        return {};
    LONG x = std::clamp<LONG>(r.left + (r.right - r.left - width) / 2, bounds.left,
                              bounds.right - width);
    LONG y = r.top - height - scaled(5, dpi);
    if (y < bounds.top)
        y = r.top + scaled(5, dpi);
    y = std::clamp<LONG>(y, bounds.top, bounds.bottom - height);
    return {x, y, x + width, y + height};
}
RECT frame_chrome_bounds(RECT r, RECT b, UINT dpi) {
    RECT grip = frame_grip(r, b, dpi);
    RECT avoid{std::min(r.left, grip.left), std::min(r.top, grip.top),
               std::max(r.right, grip.right), std::max(r.bottom, grip.bottom)};
    LONG tag = scaled(25, dpi);
    if (r.bottom + tag <= b.bottom)
        avoid.bottom = std::max(avoid.bottom, r.bottom + tag);
    else
        avoid.top = std::min(avoid.top, std::max(b.top, r.top - tag));
    return intersection(avoid, b);
}
FramePart frame_hit(RECT r, RECT bounds, POINT p, UINT dpi) {
    if (empty(r) || !PtInRect(&bounds, p))
        return FramePart::None;
    const auto points = frame_handles(r);
    const FramePart parts[] = {FramePart::NorthWest, FramePart::North,     FramePart::NorthEast,
                               FramePart::East,      FramePart::SouthEast, FramePart::South,
                               FramePart::SouthWest, FramePart::West};
    const int radius = scaled(8, dpi);
    long long best = std::numeric_limits<long long>::max();
    FramePart hit = FramePart::None;
    for (size_t i = 0; i < points.size(); ++i) {
        long long dx = static_cast<long long>(p.x) - points[i].x,
                  dy = static_cast<long long>(p.y) - points[i].y;
        if (std::abs(dx) <= radius && std::abs(dy) <= radius && dx * dx + dy * dy < best) {
            best = dx * dx + dy * dy;
            hit = parts[i];
        }
    }
    if (hit != FramePart::None)
        return hit;
    RECT grip = frame_grip(r, bounds, dpi);
    if (PtInRect(&grip, p))
        return FramePart::Move;
    if (p.x >= r.left && p.x < r.right && std::abs(p.y - r.top) <= radius)
        return FramePart::North;
    if (p.x >= r.left && p.x < r.right && std::abs(p.y - (r.bottom - 1)) <= radius)
        return FramePart::South;
    if (p.y >= r.top && p.y < r.bottom && std::abs(p.x - r.left) <= radius)
        return FramePart::West;
    if (p.y >= r.top && p.y < r.bottom && std::abs(p.x - (r.right - 1)) <= radius)
        return FramePart::East;
    return FramePart::None;
}
RECT adjust_frame(RECT r, RECT b, FramePart part, POINT d) {
    if (empty(r) || empty(b))
        return r;
    LONG minw = std::min<LONG>(3, b.right - b.left), minh = std::min<LONG>(3, b.bottom - b.top);
    if (part == FramePart::Move) {
        LONG dx = std::clamp<LONG>(d.x, b.left - r.left, b.right - r.right);
        LONG dy = std::clamp<LONG>(d.y, b.top - r.top, b.bottom - r.bottom);
        return {r.left + dx, r.top + dy, r.right + dx, r.bottom + dy};
    }
    bool west =
        part == FramePart::West || part == FramePart::NorthWest || part == FramePart::SouthWest;
    bool east =
        part == FramePart::East || part == FramePart::NorthEast || part == FramePart::SouthEast;
    bool north =
        part == FramePart::North || part == FramePart::NorthWest || part == FramePart::NorthEast;
    bool south =
        part == FramePart::South || part == FramePart::SouthWest || part == FramePart::SouthEast;
    if (west)
        r.left = std::clamp<LONG>(r.left + d.x, b.left, r.right - minw);
    if (east)
        r.right = std::clamp<LONG>(r.right + d.x, r.left + minw, b.right);
    if (north)
        r.top = std::clamp<LONG>(r.top + d.y, b.top, r.bottom - minh);
    if (south)
        r.bottom = std::clamp<LONG>(r.bottom + d.y, r.top + minh, b.bottom);
    return r;
}
void CanvasState::reset(RECT bounds) {
    bounds_ = bounds;
    clear();
}
void CanvasState::clear() {
    selection_ = {};
    frame_start_ = {};
    frame_anchor_ = {};
    mode_ = CanvasMode::Selecting;
    frame_part_ = FramePart::None;
    locked_ = false;
}
bool CanvasState::select(RECT r) {
    if (locked_)
        return false;
    r = intersection(r, bounds_);
    if (r.right - r.left < 3 || r.bottom - r.top < 3)
        return false;
    selection_ = r;
    if (selecting())
        mode_ = CanvasMode::Screenshot;
    frame_part_ = FramePart::None;
    return true;
}
bool CanvasState::begin_frame(FramePart part, POINT screen) {
    if (selecting() || locked_ || part == FramePart::None)
        return false;
    frame_part_ = part;
    frame_start_ = selection_;
    frame_anchor_ = screen;
    return true;
}
bool CanvasState::move_frame(POINT screen) {
    if (frame_part_ == FramePart::None || locked_)
        return false;
    RECT updated = adjust_frame(frame_start_, bounds_, frame_part_,
                                {screen.x - frame_anchor_.x, screen.y - frame_anchor_.y});
    bool changed = updated.left != selection_.left || updated.top != selection_.top ||
                   updated.right != selection_.right || updated.bottom != selection_.bottom;
    selection_ = updated;
    return changed;
}
void CanvasState::end_frame() {
    frame_part_ = FramePart::None;
}
bool CanvasState::set_live(bool live) {
    if (selecting() || locked_)
        return false;
    if (live) {
        if (!this->live())
            mode_ = CanvasMode::LiveDraw;
    } else
        mode_ = CanvasMode::Screenshot;
    return true;
}
bool CanvasState::set_interacting(bool interact) {
    if (!live())
        return false;
    mode_ = interact ? CanvasMode::LiveInteract : CanvasMode::LiveDraw;
    return true;
}
CanvasOutput CanvasState::output(CanvasOutput requested) const {
    if (selecting() || requested == CanvasOutput::None)
        return CanvasOutput::None;
    if (locked_ && requested != CanvasOutput::Record)
        return CanvasOutput::None;
    return requested;
}

static void rounded_frame(Graphics& g, const RectF& r, float radius, Color fill) {
    GraphicsPath path;
    float d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    SolidBrush brush(fill);
    g.FillPath(&brush, &path);
}
static void frame_chrome(Graphics& g, RECT bounds, RECT selection, bool locked, UINT dpi) {
    RECT r = screen_to_canvas(selection, bounds),
         grip = screen_to_canvas(frame_grip(selection, bounds, dpi), bounds);
    float scale = static_cast<float>(dpi ? dpi : 96) / 96.0f;
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    Pen shadow(Color(205, 15, 24, 24), 3.0f * scale);
    Pen border(ui_color(current_theme().frame), 1.2f * scale);
    border.SetDashStyle(DashStyleDash);
    REAL w = static_cast<REAL>(std::max<LONG>(0, r.right - r.left - 1)),
         h = static_cast<REAL>(std::max<LONG>(0, r.bottom - r.top - 1));
    g.DrawRectangle(&shadow, static_cast<REAL>(r.left), static_cast<REAL>(r.top), w, h);
    g.DrawRectangle(&border, static_cast<REAL>(r.left), static_cast<REAL>(r.top), w, h);
    auto handles = frame_handles(selection);
    float half = 3.5f * scale;
    SolidBrush fill(locked ? Color(255, 136, 153, 146) : Color(255, 236, 245, 237));
    Pen outline(locked ? Color(255, 93, 109, 105) : ui_color(current_theme().soft), 1.1f * scale);
    for (auto p : handles) {
        PointF pt(static_cast<REAL>(p.x - bounds.left), static_cast<REAL>(p.y - bounds.top));
        g.FillRectangle(&fill, pt.X - half, pt.Y - half, half * 2, half * 2);
        g.DrawRectangle(&outline, pt.X - half, pt.Y - half, half * 2, half * 2);
    }
    RectF pill(static_cast<REAL>(grip.left), static_cast<REAL>(grip.top),
               static_cast<REAL>(grip.right - grip.left),
               static_cast<REAL>(grip.bottom - grip.top));
    rounded_frame(g, pill, 4.0f * scale, locked ? Color(242, 45, 52, 51) : Color(245, 27, 36, 36));
    Pen move(locked ? Color(255, 121, 141, 133) : ui_color(current_theme().frame), 1.35f * scale);
    move.SetStartCap(LineCapRound);
    move.SetEndCap(LineCapRound);
    REAL cx = pill.X + pill.Width / 2, cy = pill.Y + pill.Height / 2, q = 4.5f * scale;
    g.DrawLine(&move, cx - q, cy, cx + q, cy);
    g.DrawLine(&move, cx, cy - q, cx, cy + q);
    g.DrawLine(&move, cx - q, cy, cx - q + 2 * scale, cy - 2 * scale);
    g.DrawLine(&move, cx - q, cy, cx - q + 2 * scale, cy + 2 * scale);
    g.DrawLine(&move, cx + q, cy, cx + q - 2 * scale, cy - 2 * scale);
    g.DrawLine(&move, cx + q, cy, cx + q - 2 * scale, cy + 2 * scale);
    g.DrawLine(&move, cx, cy - q, cx - 2 * scale, cy - q + 2 * scale);
    g.DrawLine(&move, cx, cy + q, cx + 2 * scale, cy + q - 2 * scale);
    wchar_t text_value[96]{};
    if (locked)
        swprintf_s(text_value, L"%ld x %ld  |  Frame fixed until ready",
                   selection.right - selection.left, selection.bottom - selection.top);
    else
        swprintf_s(text_value, L"%ld x %ld", selection.right - selection.left,
                   selection.bottom - selection.top);
    UiTypeface family;
    Font font(family.medium(), 12.0f * scale, family.medium_style(), UnitPixel);
    RectF measured;
    g.MeasureString(text_value, -1, &font, PointF(0, 0), &measured);
    REAL tag_w = std::ceil(measured.Width) + 12 * scale, tag_h = 20 * scale;
    REAL tag_x =
        std::clamp<REAL>(static_cast<REAL>(r.left), 0.0f,
                         std::max<REAL>(0, static_cast<REAL>(bounds.right - bounds.left) - tag_w));
    REAL tag_y = static_cast<REAL>(r.bottom) + 5 * scale;
    if (tag_y + tag_h > bounds.bottom - bounds.top)
        tag_y = std::max<REAL>(0, static_cast<REAL>(r.top) - tag_h - 5 * scale);
    rounded_frame(g, {tag_x, tag_y, tag_w, tag_h}, 3.0f * scale, Color(235, 27, 36, 36));
    SolidBrush label(Color(255, 233, 242, 235));
    g.DrawString(text_value, -1, &font, PointF(tag_x + 5 * scale, tag_y + 1 * scale), &label);
}
static bool valid_canvas(const Image& target, RECT bounds) {
    return target.valid() && bounds.right - bounds.left == target.width() &&
           bounds.bottom - bounds.top == target.height();
}
bool render_frozen_canvas(Image& target, const Image& desktop, RECT bounds, RECT selection,
                          bool has_selection, const std::vector<Mark>& marks, int selected,
                          bool locked, UINT dpi) {
    if (!valid_canvas(target, bounds) || !valid_canvas(desktop, bounds))
        return false;
    HDC dc = target.begin();
    if (!dc)
        return false;
    desktop.paint(dc, 0, 0);
    RECT r = screen_to_canvas(intersection(selection, bounds), bounds);
    {
        Graphics g(dc);
        SolidBrush shade(Color(110, 0, 0, 0));
        if (!has_selection || empty(r))
            g.FillRectangle(&shade, 0, 0, target.width(), target.height());
        else {
            g.FillRectangle(&shade, 0, 0, target.width(), r.top);
            g.FillRectangle(&shade, 0, r.bottom, target.width(), target.height() - r.bottom);
            g.FillRectangle(&shade, 0, r.top, r.left, r.bottom - r.top);
            g.FillRectangle(&shade, static_cast<INT>(r.right), static_cast<INT>(r.top),
                            static_cast<INT>(target.width() - r.right),
                            static_cast<INT>(r.bottom - r.top));
        }
        g.Flush();
    }
    if (has_selection && !empty(r)) {
        int saved = SaveDC(dc);
        IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
        draw_marks(dc, marks, selected, {bounds.left, bounds.top});
        RestoreDC(dc, saved);
        {
            Graphics g(dc);
            frame_chrome(g, bounds, selection, locked, dpi);
            g.Flush();
        }
    }
    GdiFlush();
    target.end(dc);
    return true;
}
bool render_live_canvas(Image& target, RECT bounds, RECT selection, const std::vector<Mark>& marks,
                        int selected, bool locked, UINT dpi) {
    if (!valid_canvas(target, bounds))
        return false;
    RECT clip = screen_to_canvas(intersection(selection, bounds), bounds);
    draw_marks_layer(target.pixels(), target.width(), target.height(), marks, selected,
                     {bounds.left, bounds.top}, &clip);
    Bitmap bitmap(target.width(), target.height(), target.width() * 4, PixelFormat32bppPARGB,
                  target.pixels());
    Graphics g(&bitmap);
    frame_chrome(g, bounds, selection, locked, dpi);
    g.Flush();
    return true;
}
Image compose_selection(const Image& desktop, RECT bounds, RECT selection,
                        const std::vector<Mark>& marks) {
    if (!valid_canvas(desktop, bounds))
        return {};
    RECT clipped = intersection(selection, bounds);
    if (clipped.left != selection.left || clipped.top != selection.top ||
        clipped.right != selection.right || clipped.bottom != selection.bottom || empty(clipped))
        return {};
    Image result = crop_image(desktop, screen_to_canvas(selection, bounds));
    if (!result.valid())
        return {};
    HDC dc = result.begin();
    if (!dc)
        return {};
    draw_marks(dc, marks, -1, {selection.left, selection.top});
    GdiFlush();
    result.end(dc);
    return result;
}
}
