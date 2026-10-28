#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace drawscr {
struct HistogramBin {
    uint64_t count = 0;
    uint64_t r = 0, g = 0, b = 0;
};
struct PalettePlan {
    std::vector<unsigned int> colors;
    std::vector<BYTE> lookup;
    std::unordered_map<uint32_t, BYTE> exact;
    bool exact_colors = false;
    int index(const BYTE* bgra) const;
};
class ColorHistogram {
  public:
    ColorHistogram();
    void add(const BYTE* bgra);
    PalettePlan make_plan(unsigned max_colors, bool refine, std::atomic<bool>* cancel) const;

  private:
    std::vector<HistogramBin> bins_;
    std::vector<uint32_t> unique_;
    std::unordered_map<uint32_t, BYTE> exact_;
    bool exact_possible_ = true;
};
} // namespace drawscr
