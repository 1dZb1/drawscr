#include "text_overlay.h"
#include "text_raster.h"
#include "theme.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
using namespace Gdiplus;
namespace drawscr {
InlineTextView make_inline_view(POINT anchor, RECT monitor, RECT selection, UINT dpi, int size_dip,
                                float size_px, COLORREF color, const std::wstring& text,
                                size_t first, size_t last, size_t caret) {
    InlineTextView v;
    v.mark.tool = Tool::Text;
    v.mark.points = {anchor};
    v.mark.color = color;
    v.mark.text_size = size_px;
    v.mark.text = text;
    v.text = measure_inline_text(text, size_px, first, last, caret);
    v.placement = place_inline_text(anchor, monitor, selection, dpi, size_dip, v.text.content);
    if (!v.placement.valid)
        return v;
    int w = std::max<LONG>(1, v.placement.field.right - v.placement.field.left - 2),
        h = std::max<LONG>(1, v.placement.field.bottom - v.placement.field.top - 2);
    v.scroll.x = std::max<LONG>(0, v.text.caret.x - w + 2);
    v.scroll.y = std::max<LONG>(0, v.text.caret.y + v.text.line_height - h);
    if (v.scroll.x || v.scroll.y)
        v.placement =
            place_inline_text(anchor, monitor, selection, dpi, size_dip, v.text.content, true);
    v.valid = v.placement.valid;
    return v;
}
namespace {
void paint(Graphics& g, RECT bounds, RECT selection, const InlineTextView& view,
           bool caret_visible) {
    if (!view.valid)
        return;
    RECT field = view.placement.field;
    POINT anchor = view.mark.points[0];
    GraphicsState state = g.Save();
    RECT clip = intersection(field, selection);
    if (empty(clip)) {
        g.Restore(state);
        return;
    }
    g.SetClip(Rect(clip.left - bounds.left, clip.top - bounds.top, clip.right - clip.left,
                   clip.bottom - clip.top));
    SolidBrush selected(ui_color(current_theme().accent, 100));
    for (auto r : view.text.selection) {
        r.left += anchor.x - bounds.left;
        r.right += anchor.x - bounds.left;
        r.top += anchor.y - bounds.top;
        r.bottom += anchor.y - bounds.top;
        g.FillRectangle(&selected, Rect(static_cast<INT>(r.left), static_cast<INT>(r.top),
                                        static_cast<INT>(r.right - r.left),
                                        static_cast<INT>(r.bottom - r.top)));
    }
    draw_text_grayscale(g, view.mark, {bounds.left, bounds.top});
    if (caret_visible) {
        REAL x = static_cast<REAL>(anchor.x - bounds.left + view.text.caret.x),
             y = static_cast<REAL>(anchor.y - bounds.top + view.text.caret.y);
        Pen shadow(Color(210, 18, 24, 24), 3), caret(ui_color(current_theme().focus), 1.4f);
        g.DrawLine(&shadow, x, y, x, y + view.text.line_height);
        g.DrawLine(&caret, x, y, x, y + view.text.line_height);
    }
    g.Restore(state);
    state = g.Save();
    RECT clipped = intersection(field, selection);
    g.SetClip(Rect(clipped.left - bounds.left, clipped.top - bounds.top,
                   clipped.right - clipped.left, clipped.bottom - clipped.top));
    Pen outline(ui_color(current_theme().focus), 1);
    outline.SetDashStyle(DashStyleDash);
    g.DrawRectangle(&outline, static_cast<REAL>(field.left - bounds.left),
                    static_cast<REAL>(field.top - bounds.top),
                    static_cast<REAL>(std::max<LONG>(0, field.right - field.left - 1)),
                    static_cast<REAL>(std::max<LONG>(0, field.bottom - field.top - 1)));
    g.Restore(state);
    g.Flush();
}
}
bool render_inline_overlay(Image& target, RECT bounds, RECT selection, const InlineTextView& view,
                           bool layered, bool caret_visible) {
    if (!target.valid() || target.width() != bounds.right - bounds.left ||
        target.height() != bounds.bottom - bounds.top)
        return false;
    if (layered) {
        Bitmap bitmap(target.width(), target.height(), target.width() * 4, PixelFormat32bppPARGB,
                      target.pixels());
        Graphics g(&bitmap);
        paint(g, bounds, selection, view, caret_visible);
    } else {
        HDC dc = target.begin();
        if (!dc)
            return false;
        {
            Graphics g(dc);
            paint(g, bounds, selection, view, caret_visible);
        }
        GdiFlush();
        target.end(dc);
    }
    return true;
}
}
