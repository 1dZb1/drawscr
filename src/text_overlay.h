#pragma once
#include "image.h"
#include "text_layout.h"
#include "text_transaction.h"
namespace drawscr {
struct InlineTextView {
    Mark mark;
    TextLayout text;
    InlineTextLayout placement;
    POINT scroll{};
    bool valid = false;
};
InlineTextView make_inline_view(POINT anchor, RECT monitor, RECT selection, UINT dpi, int size_dip,
                                float size_px, COLORREF color, const std::wstring& text,
                                size_t first, size_t last, size_t caret);
bool render_inline_overlay(Image& target, RECT bounds, RECT selection, const InlineTextView& view,
                           bool layered, bool caret_visible);
}
