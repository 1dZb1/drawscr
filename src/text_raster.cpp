#include "text_raster.h"
#include <algorithm>
#include <cmath>
#include <cstring>
using namespace Gdiplus;
namespace drawscr {
namespace {
HFONT text_font(const Mark& mark) {
    int size = static_cast<int>(
        std::lround(mark.text_size > 0 ? mark.text_size : std::max(16.0f, mark.width * 6)));
    return CreateFontW(-std::max(size, 1), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                       L"Segoe UI");
}
}
SIZE measure_text(const Mark& mark) {
    if (mark.text.empty())
        return {};
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = text_font(mark);
    if (!dc || !font) {
        if (dc)
            DeleteDC(dc);
        if (font)
            DeleteObject(font);
        return {};
    }
    HGDIOBJ old = SelectObject(dc, font);
    RECT rect{0, 0, 0, 0};
    DrawTextW(dc, mark.text.c_str(), static_cast<int>(mark.text.size()), &rect,
              DT_CALCRECT | DT_NOPREFIX | DT_NOCLIP);
    SelectObject(dc, old);
    DeleteObject(font);
    DeleteDC(dc);
    return {rect.right - rect.left, rect.bottom - rect.top};
}
void draw_text_grayscale(Graphics& target, const Mark& mark, POINT origin) {
    if (mark.points.empty() || mark.text.empty())
        return;
    SIZE measured = measure_text(mark);
    if (measured.cx <= 0 || measured.cy <= 0)
        return;
    RectF visible;
    if (target.GetVisibleClipBounds(&visible) != Ok)
        return;
    int ax = mark.points[0].x - origin.x, ay = mark.points[0].y - origin.y;
    int left = static_cast<int>(std::floor(std::max<REAL>(static_cast<REAL>(ax - 2), visible.X)));
    int top = static_cast<int>(std::floor(std::max<REAL>(static_cast<REAL>(ay - 2), visible.Y)));
    int right = static_cast<int>(
        std::ceil(std::min<REAL>(static_cast<REAL>(ax + measured.cx + 3), visible.GetRight())));
    int bottom = static_cast<int>(
        std::ceil(std::min<REAL>(static_cast<REAL>(ay + measured.cy + 3), visible.GetBottom())));
    if (right <= left || bottom <= top)
        return;
    HFONT font = text_font(mark);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!font || !dc) {
        if (font)
            DeleteObject(font);
        if (dc)
            DeleteDC(dc);
        return;
    }
    HGDIOBJ original_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    GraphicsState state = target.Save();
    target.SetInterpolationMode(InterpolationModeNearestNeighbor);
    target.SetPixelOffsetMode(PixelOffsetModeHalf);
    constexpr int tile_limit = 1024;
    for (int y = top; y < bottom; y += tile_limit)
        for (int x = left; x < right; x += tile_limit) {
            int w = std::min(tile_limit, right - x), h = std::min(tile_limit, bottom - y);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = w;
            info.bmiHeader.biHeight = -h;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* data = nullptr;
            HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
            if (!bitmap || !data) {
                if (bitmap)
                    DeleteObject(bitmap);
                continue;
            }
            HGDIOBJ old = SelectObject(dc, bitmap);
            std::memset(data, 255, static_cast<size_t>(w) * h * 4);
            RECT r{ax - x, ay - y, ax - x + measured.cx + 3, ay - y + measured.cy + 3};
            DrawTextW(dc, mark.text.c_str(), static_cast<int>(mark.text.size()), &r,
                      DT_NOPREFIX | DT_NOCLIP);
            GdiFlush();
            auto* pixels = static_cast<BYTE*>(data);
            for (size_t i = 0, n = static_cast<size_t>(w) * h * 4; i < n; i += 4) {
                unsigned a = 255u - pixels[i];
                pixels[i] = static_cast<BYTE>((GetBValue(mark.color) * a + 127) / 255);
                pixels[i + 1] = static_cast<BYTE>((GetGValue(mark.color) * a + 127) / 255);
                pixels[i + 2] = static_cast<BYTE>((GetRValue(mark.color) * a + 127) / 255);
                pixels[i + 3] = static_cast<BYTE>(a);
            }
            Bitmap layer(w, h, w * 4, PixelFormat32bppPARGB, pixels);
            target.DrawImage(&layer, Rect(x, y, w, h), 0, 0, w, h, UnitPixel);
            target.Flush(FlushIntentionSync);
            SelectObject(dc, old);
            DeleteObject(bitmap);
        }
    target.Restore(state);
    SelectObject(dc, original_font);
    DeleteObject(font);
    DeleteDC(dc);
}
}
