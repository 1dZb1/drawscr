#include "model.h"
#include "text_raster.h"
#include "theme.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <climits>

namespace drawscr {
RECT normalized(POINT a, POINT b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}
RECT intersection(RECT a, RECT b) {
    RECT r{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
           std::min(a.bottom, b.bottom)};
    if (r.right < r.left)
        r.right = r.left;
    if (r.bottom < r.top)
        r.bottom = r.top;
    return r;
}
bool empty(RECT r) {
    return r.right <= r.left || r.bottom <= r.top;
}

RECT mark_bounds(const Mark& m) {
    if (m.points.empty())
        return {};
    LONG l = m.points[0].x, r = l, t = m.points[0].y, b = t;
    for (auto p : m.points) {
        l = std::min(l, p.x);
        r = std::max(r, p.x);
        t = std::min(t, p.y);
        b = std::max(b, p.y);
    }
    if (m.tool == Tool::Text) {
        SIZE size = measure_text(m);
        r = l + size.cx;
        b = t + size.cy;
    }
    int pad = std::max(
        6, static_cast<int>(std::ceil(m.tool == Tool::Marker ? m.width * 2 : m.width)) + 3);
    return {l - pad, t - pad, r + pad + 1, b + pad + 1};
}
int hit_test(const std::vector<Mark>& marks, POINT p) {
    for (int i = static_cast<int>(marks.size()) - 1; i >= 0; --i) {
        RECT r = mark_bounds(marks[static_cast<size_t>(i)]);
        if (PtInRect(&r, p))
            return i;
    }
    return -1;
}
void translate(Mark& m, int dx, int dy) {
    for (auto& p : m.points) {
        p.x += dx;
        p.y += dy;
    }
}
static long long area(RECT r) {
    return empty(r) ? 0 : static_cast<long long>(r.right - r.left) * (r.bottom - r.top);
}
RECT place_toolbar(RECT s, RECT m, SIZE z, int gap) {
    LONG w = std::min<LONG>(z.cx, m.right - m.left), h = std::min<LONG>(z.cy, m.bottom - m.top);
    auto clamp_x = [&](LONG x) { return std::clamp<LONG>(x, m.left, m.right - w); };
    auto clamp_y = [&](LONG y) { return std::clamp<LONG>(y, m.top, m.bottom - h); };
    RECT candidates[4] = {
        {clamp_x(s.left), s.bottom + gap, clamp_x(s.left) + w, s.bottom + gap + h},
        {clamp_x(s.left), s.top - gap - h, clamp_x(s.left) + w, s.top - gap},
        {s.right + gap, clamp_y(s.top), s.right + gap + w, clamp_y(s.top) + h},
        {s.left - gap - w, clamp_y(s.top), s.left - gap, clamp_y(s.top) + h}};
    for (auto r : candidates)
        if (r.left >= m.left && r.top >= m.top && r.right <= m.right && r.bottom <= m.bottom &&
            area(intersection(r, s)) == 0)
            return r;
    RECT best{};
    long long best_overlap = LLONG_MAX;
    for (auto r : candidates) {
        LONG x = clamp_x(r.left), y = clamp_y(r.top);
        RECT c{x, y, x + w, y + h};
        auto overlap = area(intersection(c, s));
        if (overlap < best_overlap) {
            best = c;
            best_overlap = overlap;
        }
    }
    return best;
}

void History::commit(std::vector<Mark> marks) {
    states_.erase(states_.begin() + static_cast<ptrdiff_t>(index_ + 1), states_.end());
    states_.push_back(std::move(marks));
    if (states_.size() > 65)
        states_.erase(states_.begin());
    index_ = states_.size() - 1;
}
bool History::undo() {
    if (!can_undo())
        return false;
    --index_;
    return true;
}
bool History::redo() {
    if (!can_redo())
        return false;
    ++index_;
    return true;
}

static Gdiplus::Color color(COLORREF c, BYTE a = 255) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}
static void draw_marker(Gdiplus::Graphics& target, const Mark& mark, POINT origin) {
    using namespace Gdiplus;
    if (mark.points.empty())
        return;
    const REAL width = std::max(1.0f, mark.width * 4.0f), radius = width / 2.0f;
    std::vector<PointF> points;
    try {
        points.reserve(mark.points.size());
    } catch (...) {
        return;
    }
    REAL left = static_cast<REAL>(mark.points.front().x - origin.x), right = left;
    REAL top = static_cast<REAL>(mark.points.front().y - origin.y), bottom = top;
    bool has_length = false;
    for (size_t i = 0; i < mark.points.size(); ++i) {
        PointF point(static_cast<REAL>(mark.points[i].x - origin.x),
                     static_cast<REAL>(mark.points[i].y - origin.y));
        points.push_back(point);
        left = std::min(left, point.X);
        right = std::max(right, point.X);
        top = std::min(top, point.Y);
        bottom = std::max(bottom, point.Y);
        if (i &&
            (mark.points[i].x != mark.points[i - 1].x || mark.points[i].y != mark.points[i - 1].y))
            has_length = true;
    }
    GraphicsPath path;
    if (has_length)
        path.AddLines(points.data(), static_cast<INT>(points.size()));
    RectF visible;
    if (target.GetVisibleClipBounds(&visible) != Ok)
        return;
    int x0 = static_cast<int>(std::floor(std::max(left - radius - 2, visible.X)));
    int y0 = static_cast<int>(std::floor(std::max(top - radius - 2, visible.Y)));
    int x1 = static_cast<int>(std::ceil(std::min(right + radius + 2, visible.GetRight())));
    int y1 = static_cast<int>(std::ceil(std::min(bottom + radius + 2, visible.GetBottom())));
    if (x1 <= x0 || y1 <= y0)
        return;
    constexpr int tile_size = 1024;
    constexpr unsigned opacity = 100;
    static thread_local std::vector<BYTE> buffer;
    for (int y = y0; y < y1; y += tile_size)
        for (int x = x0; x < x1; x += tile_size) {
            int w = std::min(tile_size, x1 - x), h = std::min(tile_size, y1 - y);
            try {
                buffer.resize(static_cast<size_t>(w) * h * 4);
            } catch (...) {
                return;
            }
            Bitmap tile(w, h, w * 4, PixelFormat32bppPARGB, buffer.data());
            {
                Graphics mask(&tile);
                mask.Clear(Color(0, 0, 0, 0));
                mask.SetSmoothingMode(SmoothingModeAntiAlias);
                mask.TranslateTransform(static_cast<REAL>(-x), static_cast<REAL>(-y));
                if (has_length) {
                    Pen pen(Color(255, 255, 255, 255), width);
                    pen.SetStartCap(LineCapFlat);
                    pen.SetEndCap(LineCapFlat);
                    pen.SetLineJoin(LineJoinRound);
                    mask.DrawPath(&pen, &path);
                } else {
                    SolidBrush brush(Color(255, 255, 255, 255));
                    mask.FillRectangle(&brush, points[0].X - radius / 2, points[0].Y - radius,
                                       radius, width);
                }
                mask.Flush(FlushIntentionSync);
            }
            for (size_t i = 0, n = static_cast<size_t>(w) * h * 4; i < n; i += 4) {
                unsigned a = (static_cast<unsigned>(buffer[i + 3]) * opacity + 127) / 255;
                buffer[i] = static_cast<BYTE>((GetBValue(mark.color) * a + 127) / 255);
                buffer[i + 1] = static_cast<BYTE>((GetGValue(mark.color) * a + 127) / 255);
                buffer[i + 2] = static_cast<BYTE>((GetRValue(mark.color) * a + 127) / 255);
                buffer[i + 3] = static_cast<BYTE>(a);
            }
            target.DrawImage(&tile, Rect(x, y, w, h), 0, 0, w, h, UnitPixel);
            target.Flush(FlushIntentionSync);
        }
}
static void draw_impl(Gdiplus::Graphics& g, const std::vector<Mark>& marks, int selected,
                      POINT origin) {
    using namespace Gdiplus;
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    for (size_t i = 0; i < marks.size(); ++i) {
        const auto& m = marks[i];
        if (m.points.empty())
            continue;
        PointF a(static_cast<REAL>(m.points.front().x - origin.x),
                 static_cast<REAL>(m.points.front().y - origin.y));
        PointF b(static_cast<REAL>(m.points.back().x - origin.x),
                 static_cast<REAL>(m.points.back().y - origin.y));
        Pen pen(color(m.color), m.width);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        if (m.tool == Tool::Marker) {
            draw_marker(g, m, origin);
        } else if (m.tool == Tool::Pen) {
            if (m.points.size() == 1) {
                SolidBrush brush(color(m.color));
                g.FillEllipse(&brush, a.X - m.width / 2, a.Y - m.width / 2, m.width, m.width);
            } else
                for (size_t j = 1; j < m.points.size(); ++j)
                    g.DrawLine(&pen, static_cast<REAL>(m.points[j - 1].x - origin.x),
                               static_cast<REAL>(m.points[j - 1].y - origin.y),
                               static_cast<REAL>(m.points[j].x - origin.x),
                               static_cast<REAL>(m.points[j].y - origin.y));
        } else if (m.tool == Tool::Line || m.tool == Tool::Arrow) {
            g.DrawLine(&pen, a, b);
            if (m.tool == Tool::Arrow) {
                float dx = b.X - a.X, dy = b.Y - a.Y, len = std::sqrt(dx * dx + dy * dy);
                if (len > 0.01f) {
                    dx /= len;
                    dy /= len;
                    float k = std::max(10.0f, m.width * 4.0f);
                    g.DrawLine(&pen, b.X, b.Y, b.X - dx * k - dy * k * .45f,
                               b.Y - dy * k + dx * k * .45f);
                    g.DrawLine(&pen, b.X, b.Y, b.X - dx * k + dy * k * .45f,
                               b.Y - dy * k - dx * k * .45f);
                }
            }
        } else if (m.tool == Tool::Rectangle || m.tool == Tool::Redact) {
            RectF r(std::min(a.X, b.X), std::min(a.Y, b.Y), std::abs(a.X - b.X),
                    std::abs(a.Y - b.Y));
            if (m.tool == Tool::Redact) {
                SolidBrush fill(color(m.color));
                g.SetSmoothingMode(SmoothingModeNone);
                g.FillRectangle(&fill, r);
                g.SetSmoothingMode(SmoothingModeAntiAlias);
            } else
                g.DrawRectangle(&pen, r);
        } else if (m.tool == Tool::Text) {
            draw_text_grayscale(g, m, origin);
        }
        if (static_cast<int>(i) == selected) {
            RECT r = mark_bounds(m);
            Pen border(ui_color(current_theme().focus, 235), 1.0f);
            border.SetDashStyle(DashStyleDash);
            g.DrawRectangle(
                &border, static_cast<REAL>(r.left - origin.x), static_cast<REAL>(r.top - origin.y),
                static_cast<REAL>(r.right - r.left), static_cast<REAL>(r.bottom - r.top));
        }
    }
}
void draw_marks(HDC dc, const std::vector<Mark>& marks, int selected, POINT origin) {
    Gdiplus::Graphics g(dc);
    draw_impl(g, marks, selected, origin);
    g.Flush();
}
void draw_marks_layer(uint8_t* pixels, int width, int height, const std::vector<Mark>& marks,
                      int selected, POINT origin, const RECT* clip) {
    Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppPARGB, pixels);
    Gdiplus::Graphics g(&bitmap);
    g.Clear(Gdiplus::Color(1, 0, 0, 0));
    if (clip)
        g.SetClip(Gdiplus::Rect(clip->left, clip->top, clip->right - clip->left,
                                clip->bottom - clip->top));
    draw_impl(g, marks, selected, origin);
    g.Flush();
}
}
