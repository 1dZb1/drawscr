#pragma once
#include <objidl.h>
#include <gdiplus.h>
namespace drawscr {
// UI sizes are logical pixels. The paint surface applies DPI scaling once.
inline float ui_size(float size) {
    return size < 11.0f ? 12.0f : size < 20.0f ? size + 1.0f : size;
}
class UiTypeface {
  public:
    UiTypeface() : regular_(L"Segoe UI"), semibold_(L"Segoe UI Semibold") {
        has_semibold_ =
            semibold_.IsAvailable() && semibold_.IsStyleAvailable(Gdiplus::FontStyleRegular);
    }
    const Gdiplus::FontFamily* medium() const {
        return has_semibold_ ? &semibold_ : &regular_;
    }
    int medium_style() const {
        return has_semibold_ ? Gdiplus::FontStyleRegular : Gdiplus::FontStyleBold;
    }
    const Gdiplus::FontFamily* regular() const {
        return &regular_;
    }

  private:
    Gdiplus::FontFamily regular_, semibold_;
    bool has_semibold_ = false;
};
}
