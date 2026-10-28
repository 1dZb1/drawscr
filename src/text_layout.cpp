#include "text_layout.h"
#include <algorithm>
#include <cmath>
namespace drawscr {
namespace {
struct FontContext {
    HDC dc = nullptr;
    HFONT font = nullptr;
    HGDIOBJ old = nullptr;
    TEXTMETRICW metrics{};
    explicit FontContext(float size) {
        dc = CreateCompatibleDC(nullptr);
        font = CreateFontW(-std::max(1, static_cast<int>(std::lround(size))), 0, 0, 0, FW_NORMAL,
                           FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (dc && font) {
            old = SelectObject(dc, font);
            GetTextMetricsW(dc, &metrics);
        }
    }
    ~FontContext() {
        if (dc && old)
            SelectObject(dc, old);
        if (font)
            DeleteObject(font);
        if (dc)
            DeleteDC(dc);
    }
    int width(const wchar_t* text, size_t length) const {
        SIZE s{};
        if (length && dc)
            GetTextExtentPoint32W(dc, text, static_cast<int>(length), &s);
        return s.cx;
    }
};
std::vector<TextLine> split_lines(const std::wstring& text, const FontContext& font) {
    std::vector<TextLine> result;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] == L'\r' || text[i] == L'\n') {
            result.push_back({start, i, font.width(text.data() + start, i - start)});
            if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')
                ++i;
            start = i + 1;
        }
    result.push_back({start, text.size(), font.width(text.data() + start, text.size() - start)});
    return result;
}
}
TextLayout measure_inline_text(const std::wstring& text, float size, size_t first, size_t last,
                               size_t caret) {
    FontContext f(size);
    TextLayout out;
    out.line_height = std::max<int>(1, f.metrics.tmHeight);
    out.lines = split_lines(text, f);
    first = std::min(first, text.size());
    last = std::min(last, text.size());
    if (first > last)
        std::swap(first, last);
    caret = std::min(caret, text.size());
    out.caret_index = caret;
    for (size_t i = 0; i < out.lines.size(); ++i) {
        const auto& line = out.lines[i];
        out.content.cx = std::max<LONG>(out.content.cx, line.width);
        size_t a = std::max(first, line.start), b = std::min(last, line.end);
        if (a < b) {
            int x0 = f.width(text.data() + line.start, a - line.start),
                x1 = f.width(text.data() + line.start, b - line.start);
            out.selection.push_back({x0, static_cast<LONG>(i * out.line_height), x1,
                                     static_cast<LONG>((i + 1) * out.line_height)});
        }
        bool here = caret >= line.start && (caret <= line.end || i + 1 == out.lines.size());
        if (here) {
            size_t pos = std::min(caret, line.end);
            out.caret = {f.width(text.data() + line.start, pos - line.start),
                         static_cast<LONG>(i * out.line_height)};
        }
    }
    out.content.cy = static_cast<LONG>(out.lines.size() * out.line_height);
    return out;
}
size_t text_index_at(const std::wstring& text, float size, POINT local) {
    FontContext f(size);
    auto lines = split_lines(text, f);
    int h = std::max<int>(1, f.metrics.tmHeight);
    size_t line =
        std::min<size_t>(static_cast<size_t>(std::max<LONG>(0, local.y) / h), lines.size() - 1);
    const auto& item = lines[line];
    int fit = 0;
    SIZE measured{};
    int max_width = std::max<LONG>(0, local.x);
    if (item.end > item.start)
        GetTextExtentExPointW(f.dc, text.data() + item.start,
                              static_cast<int>(item.end - item.start), max_width, &fit, nullptr,
                              &measured);
    return std::min(item.end, item.start + static_cast<size_t>(std::max(0, fit)));
}
InlineLinePreview
fit_inline_line_preview(const std::wstring& text, size_t caret, float max_width,
                        const std::function<float(const std::wstring&)>& measure) {
    InlineLinePreview result;
    if (!measure || max_width <= 0)
        return result;
    auto high = [](wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; };
    auto low = [](wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; };
    std::vector<size_t> boundaries{0};
    for (size_t i = 0; i < text.size();) {
        i += (high(text[i]) && i + 1 < text.size() && low(text[i + 1])) ? 2 : 1;
        boundaries.push_back(i);
    }
    caret = std::min(caret, text.size());
    size_t center = static_cast<size_t>(
        std::upper_bound(boundaries.begin(), boundaries.end(), caret) - boundaries.begin() - 1);
    caret = boundaries[center];
    auto make = [&](size_t first, size_t last) {
        std::wstring out;
        if (first)
            out += L"\u2026";
        out.append(text, first, last - first);
        if (last < text.size())
            out += L"\u2026";
        return out;
    };
    auto fits = [&](size_t a, size_t b, float limit) {
        return measure(make(boundaries[a], boundaries[b])) <= limit;
    };
    size_t last_index = boundaries.size() - 1, first = 0, last = last_index;
    if (!fits(first, last, max_width)) {
        // When there is content after the caret, reserve some room for it.
        auto left = [&](size_t end, float limit) {
            if (fits(0, end, limit))
                return size_t{0};
            size_t lo = 1, hi = center, best = center;
            while (lo <= hi) {
                size_t mid = lo + (hi - lo) / 2;
                if (fits(mid, end, limit)) {
                    best = mid;
                    if (mid == 0)
                        break;
                    hi = mid - 1;
                } else
                    lo = mid + 1;
            }
            return best;
        };
        auto right = [&](size_t begin, float limit) {
            if (fits(begin, last_index, limit))
                return last_index;
            size_t lo = center, hi = last_index ? last_index - 1 : 0, best = center;
            while (lo <= hi) {
                size_t mid = lo + (hi - lo) / 2;
                if (fits(begin, mid, limit)) {
                    best = mid;
                    lo = mid + 1;
                } else {
                    if (mid == 0)
                        break;
                    hi = mid - 1;
                }
            }
            return best;
        };
        if (!fits(center, center, max_width))
            return result;
        first = left(center, center < last_index ? max_width * 0.65f : max_width);
        last = right(first, max_width);
        first = left(last, max_width);
    }
    result.first = boundaries[first];
    result.last = boundaries[last];
    result.caret = caret;
    result.visible = make(result.first, result.last);
    result.caret_offset = (result.first ? 1u : 0u) + (caret - result.first);
    return result;
}
}
