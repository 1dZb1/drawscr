#pragma once
#include "model.h"
#include <string>
#include <functional>
#include <vector>
namespace drawscr {
struct TextLine {
    size_t start = 0, end = 0;
    int width = 0;
};
struct TextLayout {
    SIZE content{};
    int line_height = 0;
    POINT caret{};
    size_t caret_index = 0;
    std::vector<RECT> selection;
    std::vector<TextLine> lines;
};
TextLayout measure_inline_text(const std::wstring& text, float size_px, size_t first, size_t last,
                               size_t caret);
size_t text_index_at(const std::wstring& text, float size_px, POINT local);
struct InlineLinePreview {
    std::wstring visible;
    size_t first = 0, last = 0, caret = 0, caret_offset = 0;
};
// measure must use the same font and format as the preview renderer.
InlineLinePreview fit_inline_line_preview(const std::wstring& text, size_t caret, float max_width,
                                          const std::function<float(const std::wstring&)>& measure);
}
