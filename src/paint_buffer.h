#pragma once
#include <windows.h>

namespace drawscr {
class PaintBuffer {
  public:
    PaintBuffer() = default;
    PaintBuffer(const PaintBuffer&) = delete;
    PaintBuffer& operator=(const PaintBuffer&) = delete;
    ~PaintBuffer() {
        if (dc_) {
            if (original_)
                SelectObject(dc_, original_);
            if (bitmap_)
                DeleteObject(bitmap_);
            DeleteDC(dc_);
        }
    }
    HDC prepare(HDC target, int width, int height) {
        if (width <= 0 || height <= 0)
            return nullptr;
        if (!dc_) {
            dc_ = CreateCompatibleDC(target);
            if (!dc_)
                return nullptr;
        }
        if (bitmap_ && width == width_ && height == height_)
            return dc_;
        HBITMAP next = CreateCompatibleBitmap(target, width, height);
        if (!next)
            return nullptr;
        HGDIOBJ previous = SelectObject(dc_, next);
        if (!previous || previous == HGDI_ERROR) {
            DeleteObject(next);
            return nullptr;
        }
        if (!original_)
            original_ = previous;
        else if (bitmap_)
            DeleteObject(bitmap_);
        bitmap_ = next;
        width_ = width;
        height_ = height;
        return dc_;
    }
    void present(HDC target, RECT area) const {
        if (dc_ && bitmap_ && area.right > area.left && area.bottom > area.top)
            BitBlt(target, area.left, area.top, area.right - area.left, area.bottom - area.top, dc_,
                   area.left, area.top, SRCCOPY);
    }

  private:
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ original_ = nullptr;
    int width_ = 0, height_ = 0;
};
} // namespace drawscr
