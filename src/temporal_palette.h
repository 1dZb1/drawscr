#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <atomic>

namespace drawscr {
// Temporal stability is always measured against the CURRENT frame, not the
// previous source frame. Slow changes therefore cannot accumulate stale pixels.
inline int palette_color_distance(unsigned a, unsigned b) {
    int r = int((a >> 16) & 255) - int((b >> 16) & 255);
    int g = int((a >> 8) & 255) - int((b >> 8) & 255);
    int blue = int(a & 255) - int(b & 255);
    return r * r + g * g + blue * blue;
}
inline unsigned bgra_color(const unsigned char* p) {
    return (unsigned(p[2]) << 16) | (unsigned(p[1]) << 8) | p[0];
}
inline bool temporal_edge(const unsigned char* pixels, size_t i, size_t width, size_t height) {
    const unsigned c = bgra_color(pixels + i * 4);
    auto edge = [&](size_t j) { return palette_color_distance(c, bgra_color(pixels + j * 4)) >= 32 * 32; };
    const size_t x = i % width, y = i / width;
    return (x && edge(i - 1)) || (x + 1 < width && edge(i + 1)) ||
           (y && edge(i - width)) || (y + 1 < height && edge(i + width));
}
// Preserve coherent thin strokes, rather than every local extremum in a photo.
// Protect both current and previous source masks so disappearing strokes clear.
template<class ColorAt>
inline std::vector<unsigned char> temporal_feature_mask(size_t width, size_t height,
                                                       const ColorAt& color_at,
                                                       std::atomic<bool>* cancel = nullptr) {
    const size_t count = width * height;
    std::vector<unsigned char> ridges(count), protect(count);
    auto stopped = [&] { return cancel && cancel->load(); };
    for (size_t y = 0; y < height; ++y) {
        if ((y & 31) == 0 && stopped()) return {};
        for (size_t x = 0; x < width; ++x) {
            size_t i = y * width + x;
            const unsigned center = color_at(i);
            auto ridge = [&](size_t a, size_t b) {
                unsigned ca = color_at(a), cb = color_at(b);
                return palette_color_distance(ca, cb) <= 9 &&
                       palette_color_distance(center, ca) >= 4 &&
                       palette_color_distance(center, cb) >= 4;
            };
            for (size_t d = 1; d <= 2; ++d) {
                if (x >= d && x + d < width && ridge(i - d, i + d))
                    ridges[i] |= static_cast<unsigned char>(1u << (d - 1));
                if (y >= d && y + d < height && ridge(i - d * width, i + d * width))
                    ridges[i] |= static_cast<unsigned char>(1u << (d + 1));
            }
        }
    }
    for (size_t y = 0; y < height; ++y) {
        if ((y & 31) == 0 && stopped()) return {};
        for (size_t x = 0; x < width; ++x) {
            size_t i = y * width + x;
            if (x < 2 || y < 2 || x + 2 >= width || y + 2 >= height) protect[i] = 1;
            if (!ridges[i]) continue;
            const unsigned center = color_at(i);
            bool vertical = y && y + 1 < height &&
                (ridges[i] & ridges[i - width] & ridges[i + width] & 3) &&
                palette_color_distance(center, color_at(i - width)) <= 9 &&
                palette_color_distance(center, color_at(i + width)) <= 9;
            bool horizontal = x && x + 1 < width &&
                (ridges[i] & ridges[i - 1] & ridges[i + 1] & 12) &&
                palette_color_distance(center, color_at(i - 1)) <= 9 &&
                palette_color_distance(center, color_at(i + 1)) <= 9;
            if (vertical || horizontal) {
                size_t y0 = y ? y - 1 : 0, x0 = x ? x - 1 : 0;
                for (size_t yy = y0; yy < height && yy <= y + 1; ++yy)
                    for (size_t xx = x0; xx < width && xx <= x + 1; ++xx)
                        protect[yy * width + xx] = 1;
            }
        }
    }
    return protect;
}
inline bool retain_palette_color(unsigned source, unsigned current, unsigned previous,
                                 unsigned tolerance) {
    const int limit = static_cast<int>(tolerance * tolerance);
    // Bound added error against this frame's source, including its existing
    // quantization error. Reusing a closer color is always safe by this metric.
    return palette_color_distance(source, previous) <= palette_color_distance(source, current) + limit;
}
} // namespace drawscr
