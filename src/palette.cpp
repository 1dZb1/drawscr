#include "palette.h"
#include <algorithm>
#include <array>
#include <limits>

namespace drawscr {
static uint32_t rgb(const BYTE* p) {
    return (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[0];
}
static int bin_index(const BYTE* p) {
    return ((p[2] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[0] >> 3);
}
static bool stopped(std::atomic<bool>* cancel) {
    return cancel && cancel->load();
}
int PalettePlan::index(const BYTE* p) const {
    if (exact_colors) {
        auto it = exact.find(rgb(p));
        return it == exact.end() ? -1 : it->second;
    }
    return lookup.empty() ? -1 : lookup[bin_index(p)];
}
ColorHistogram::ColorHistogram() : bins_(32768) {
    unique_.reserve(257);
    exact_.reserve(257);
}
void ColorHistogram::add(const BYTE* p) {
    auto& b = bins_[bin_index(p)];
    ++b.count;
    b.r += p[2];
    b.g += p[1];
    b.b += p[0];
    if (exact_possible_) {
        uint32_t value = rgb(p);
        if (exact_.find(value) == exact_.end()) {
            if (unique_.size() == 256) {
                exact_possible_ = false;
                unique_.clear();
                exact_.clear();
            } else {
                exact_.emplace(value, static_cast<BYTE>(unique_.size()));
                unique_.push_back(value);
            }
        }
    }
}
struct ColorBin {
    int r, g, b;
    uint64_t weight;
    uint64_t rs, gs, bs;
};
struct Box {
    size_t begin, end;
    int low[3]{}, high[3]{};
    uint64_t weight = 0;
};
static void measure(Box& box, const std::vector<ColorBin>& bins) {
    box.low[0] = box.low[1] = box.low[2] = 255;
    box.high[0] = box.high[1] = box.high[2] = 0;
    box.weight = 0;
    for (size_t i = box.begin; i < box.end; ++i) {
        const auto& b = bins[i];
        int c[] = {b.r, b.g, b.b};
        for (int k = 0; k < 3; ++k) {
            box.low[k] = std::min(box.low[k], c[k]);
            box.high[k] = std::max(box.high[k], c[k]);
        }
        box.weight += b.weight;
    }
}
static std::vector<unsigned int> median_palette(const std::vector<HistogramBin>& histogram,
                                                unsigned max_colors, std::atomic<bool>* cancel) {
    std::vector<ColorBin> bins;
    bins.reserve(32768);
    for (const auto& h : histogram)
        if (h.count)
            bins.push_back({static_cast<int>((h.r + h.count / 2) / h.count),
                            static_cast<int>((h.g + h.count / 2) / h.count),
                            static_cast<int>((h.b + h.count / 2) / h.count), h.count, h.r, h.g,
                            h.b});
    if (bins.empty())
        return {};
    std::vector<Box> boxes{{0, bins.size()}};
    measure(boxes[0], bins);
    while (boxes.size() < max_colors) {
        if (stopped(cancel))
            return {};
        size_t chosen = boxes.size();
        uint64_t score = 0;
        for (size_t i = 0; i < boxes.size(); ++i) {
            const auto& b = boxes[i];
            int span = std::max({b.high[0] - b.low[0], b.high[1] - b.low[1], b.high[2] - b.low[2]});
            uint64_t value = static_cast<uint64_t>(span) * b.weight;
            if (b.end - b.begin > 1 && value > score) {
                score = value;
                chosen = i;
            }
        }
        if (chosen == boxes.size())
            break;
        Box box = boxes[chosen];
        int axis = 0;
        for (int k = 1; k < 3; ++k)
            if (box.high[k] - box.low[k] > box.high[axis] - box.low[axis])
                axis = k;
        std::sort(bins.begin() + static_cast<ptrdiff_t>(box.begin),
                  bins.begin() + static_cast<ptrdiff_t>(box.end),
                  [axis](const ColorBin& a, const ColorBin& b) {
                      int x = axis == 0   ? a.r
                              : axis == 1 ? a.g
                                          : a.b,
                          y = axis == 0   ? b.r
                              : axis == 1 ? b.g
                                          : b.b;
                      if (x != y)
                          return x < y;
                      if (a.r != b.r)
                          return a.r < b.r;
                      if (a.g != b.g)
                          return a.g < b.g;
                      return a.b < b.b;
                  });
        uint64_t sum = 0;
        size_t mid = box.end - 1;
        for (size_t i = box.begin; i < box.end - 1; ++i) {
            sum += bins[i].weight;
            if (sum * 2 >= box.weight) {
                mid = i + 1;
                break;
            }
        }
        Box left{box.begin, mid}, right{mid, box.end};
        measure(left, bins);
        measure(right, bins);
        boxes[chosen] = left;
        boxes.push_back(right);
    }
    std::vector<unsigned int> palette;
    palette.reserve(boxes.size());
    for (const auto& box : boxes) {
        uint64_t r = 0, g = 0, b = 0;
        for (size_t i = box.begin; i < box.end; ++i) {
            r += bins[i].rs;
            g += bins[i].gs;
            b += bins[i].bs;
        }
        uint64_t n = box.weight;
        palette.push_back(0xff000000U | (static_cast<unsigned>((r + n / 2) / n) << 16) |
                          (static_cast<unsigned>((g + n / 2) / n) << 8) |
                          static_cast<unsigned>((b + n / 2) / n));
    }
    return palette;
}
static std::vector<BYTE> make_lookup(const std::vector<HistogramBin>& histogram,
                                     const std::vector<unsigned int>& palette,
                                     std::atomic<bool>* cancel) {
    std::vector<BYTE> lookup(32768);
    for (int i = 0; i < 32768; ++i) {
        if ((i & 255) == 0 && stopped(cancel))
            return {};
        const auto& h = histogram[i];
        int rr = h.count ? static_cast<int>((h.r + h.count / 2) / h.count)
                         : (((i >> 10) & 31) * 255 + 15) / 31;
        int gg = h.count ? static_cast<int>((h.g + h.count / 2) / h.count)
                         : (((i >> 5) & 31) * 255 + 15) / 31;
        int bb =
            h.count ? static_cast<int>((h.b + h.count / 2) / h.count) : ((i & 31) * 255 + 15) / 31;
        int best = 0, best_distance = std::numeric_limits<int>::max();
        for (size_t j = 0; j < palette.size(); ++j) {
            unsigned p = palette[j];
            int dr = rr - static_cast<int>((p >> 16) & 255),
                dg = gg - static_cast<int>((p >> 8) & 255), db = bb - static_cast<int>(p & 255);
            int d = dr * dr + dg * dg + db * db;
            if (d < best_distance) {
                best_distance = d;
                best = static_cast<int>(j);
                if (!d)
                    break;
            }
        }
        lookup[i] = static_cast<BYTE>(best);
    }
    return lookup;
}
PalettePlan ColorHistogram::make_plan(unsigned max_colors, bool refine,
                                      std::atomic<bool>* cancel) const {
    PalettePlan plan;
    if (!max_colors || max_colors > 256 || stopped(cancel))
        return plan;
    if (exact_possible_ && !unique_.empty() && unique_.size() <= max_colors) {
        plan.exact_colors = true;
        plan.exact = exact_;
        for (uint32_t c : unique_)
            plan.colors.push_back(0xff000000U | c);
        return plan;
    }
    plan.colors = median_palette(bins_, max_colors, cancel);
    if (plan.colors.empty())
        return {};
    for (int pass = 0; pass < (refine ? 2 : 0); ++pass) {
        auto lookup = make_lookup(bins_, plan.colors, cancel);
        if (lookup.empty())
            return {};
        std::vector<std::array<uint64_t, 4>> sums(plan.colors.size());
        for (size_t i = 0; i < bins_.size(); ++i) {
            const auto& h = bins_[i];
            auto& s = sums[lookup[i]];
            s[0] += h.r;
            s[1] += h.g;
            s[2] += h.b;
            s[3] += h.count;
        }
        for (size_t i = 0; i < sums.size(); ++i) {
            auto& s = sums[i];
            if (s[3])
                plan.colors[i] = 0xff000000U |
                                 (static_cast<unsigned>((s[0] + s[3] / 2) / s[3]) << 16) |
                                 (static_cast<unsigned>((s[1] + s[3] / 2) / s[3]) << 8) |
                                 static_cast<unsigned>((s[2] + s[3] / 2) / s[3]);
        }
    }
    plan.lookup = make_lookup(bins_, plan.colors, cancel);
    if (plan.lookup.empty())
        return {};
    return plan;
}
} // namespace drawscr
