#pragma once
#include "model.h"
#include <windows.h>
#include <cstdint>
#include <string>

namespace drawscr {
class Image {
  public:
    Image() = default;
    Image(int width, int height);
    ~Image();
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;
    bool valid() const {
        return bitmap_ && pixels_;
    }
    int width() const {
        return width_;
    }
    int height() const {
        return height_;
    }
    uint8_t* pixels() {
        return pixels_;
    }
    const uint8_t* pixels() const {
        return pixels_;
    }
    HBITMAP bitmap() const {
        return bitmap_;
    }
    HDC begin() const;
    void end(HDC dc) const;
    void paint(HDC dst, int x, int y) const;

  private:
    HBITMAP bitmap_ = nullptr;
    uint8_t* pixels_ = nullptr;
    int width_ = 0, height_ = 0;
};
Image capture_screen(RECT r);
Image crop_image(const Image& source, RECT r);
bool blit_gif_delta(Image& target, const uint8_t* bgra, unsigned width, unsigned height,
                    unsigned left, unsigned top);
Image compose(const Image& base, const std::vector<Mark>& marks, POINT screen_origin = {});
bool copy_clipboard(HWND owner, const Image& image, std::wstring& error);
constexpr float jpeg_quality = 1.0f;
bool save_image(const Image& image, const std::wstring& path, bool jpeg, std::wstring& error);
} // namespace drawscr
