#include "launcher_view.h"
#include "theme.h"
#include "ui_type.h"
#include <objidl.h>
#include <gdiplus.h>
using namespace Gdiplus;
namespace drawscr {
namespace {
void round_rect(Graphics& g, RectF r, Color fill, Color border = Color(0, 0, 0, 0)) {
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
        Pen edge(border, 1);
        g.DrawPath(&edge, &p);
    }
}
}
int launcher_hit(POINT p, UINT dpi) {
    if (!dpi)
        dpi = 96;
    p.x = MulDiv(p.x, 96, dpi);
    p.y = MulDiv(p.y, 96, dpi);
    if (p.y >= 12 && p.y < 48) {
        if (p.x >= 368 && p.x < 402)
            return 4;
        if (p.x >= 406 && p.x < 438)
            return 5;
    }
    if (p.y >= 122 && p.y < 162)
        return p.x >= 24 && p.x < 220 ? 0 : p.x >= 230 && p.x < 426 ? 1 : -1;
    if (p.y >= 174 && p.y < 210)
        return p.x >= 24 && p.x < 220 ? 2 : p.x >= 230 && p.x < 426 ? 3 : -1;
    return -1;
}
void paint_launcher(HDC dc, UINT dpi, int hover, int focus, bool focused,
                    const std::wstring& shortcut) {
    if (!dpi)
        dpi = 96;
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.ScaleTransform(dpi / 96.0f, dpi / 96.0f);
    g.Clear(Color(255, 27, 32, 34));
    round_rect(g, {.5f, .5f, 449, 249}, Color(255, 27, 32, 34), Color(255, 62, 73, 73));
    UiTypeface f;
    Font title(f.regular(), 26, FontStyleBold, UnitPixel),
        body(f.medium(), 14, f.medium_style(), UnitPixel),
        button(f.medium(), 14, f.medium_style(), UnitPixel);
    SolidBrush white(Color(255, 238, 242, 236)), muted(Color(255, 165, 180, 176)),
        accent(ui_color(current_theme().frame));
    g.DrawString(L"Drawscr", -1, &title, PointF(24, 18), &white);
    g.DrawString(L"Capture, mark, share.", -1, &body, PointF(25, 56), &muted);
    StringFormat clip;
    clip.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(shortcut.c_str(), -1, &body, RectF(25, 87, 400, 24), &clip, &accent);
    const RectF rects[] = {
        {24, 122, 196, 40}, {230, 122, 196, 40}, {24, 174, 196, 36}, {230, 174, 196, 36}};
    const wchar_t* labels[] = {L"Capture area", L"Capture monitor", L"Settings", L"Hide to tray"};
    for (int i = 0; i < 4; ++i) {
        Color bg = i == 0       ? ui_color(current_theme().accent)
                   : hover == i ? Color(255, 55, 65, 65)
                                : Color(255, 38, 46, 47);
        Color fg = i == 0 ? ui_color(current_theme().ink) : Color(255, 230, 235, 229);
        round_rect(g, rects[i], bg);
        SolidBrush br(fg);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(labels[i], -1, &button, rects[i], &fmt, &br);
    }
    RectF mini{368, 12, 34, 36}, close{406, 12, 32, 36};
    round_rect(g, mini, hover == 4 ? Color(255, 52, 62, 63) : Color(255, 27, 32, 34));
    round_rect(g, close, hover == 5 ? Color(255, 52, 62, 63) : Color(255, 27, 32, 34));
    Pen icon(Color(255, 220, 232, 225), 1.7f);
    icon.SetStartCap(LineCapRound);
    icon.SetEndCap(LineCapRound);
    g.DrawLine(&icon, 379, 31, 390, 31);
    g.DrawLine(&icon, 415, 23, 428, 36);
    g.DrawLine(&icon, 428, 23, 415, 36);
    if (focused && focus >= 0 && focus < 6) {
        RectF r = focus < 4 ? rects[focus] : focus == 4 ? mini : close;
        Pen p(ui_color(current_theme().focus), 1);
        g.DrawRectangle(&p, r.X + 2, r.Y + 2, r.Width - 4, r.Height - 4);
    }
    g.DrawString(L"Esc closes a capture. Live mode can pass clicks through.", -1, &body,
                 PointF(25, 223), &muted);
    g.Flush();
}
}
