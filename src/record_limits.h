#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>

namespace drawscr {
enum class GifCompression { Strong, Normal, Quality };
constexpr uint64_t RECORD_DISK_RESERVE = 256ULL * 1024 * 1024;
constexpr uint64_t RECORD_FRAME_BYTE_LIMIT = 64ULL * 1024 * 1024;
constexpr int RECORD_MAX_DIMENSION = 8192;

inline bool valid_record_frame_size(uint64_t width, uint64_t height) {
    return width > 0 && height > 0 && width <= RECORD_MAX_DIMENSION &&
           height <= RECORD_MAX_DIMENSION && width * height <= RECORD_FRAME_BYTE_LIMIT / 4;
}
// Bounds follow from uint64_t sums of worst-case RGB squared error, not a duration limit.
constexpr uint64_t MAX_RECORD_PIXELS = UINT64_MAX / (3ULL * 255 * 255);
inline bool safe_record_count(uint64_t pixels_per_frame, uint64_t frames) {
    return pixels_per_frame > 0 && frames <= UINT32_MAX &&
           frames <= MAX_RECORD_PIXELS / pixels_per_frame;
}
inline bool sufficient_spool_space(uint64_t free_bytes, uint64_t written, uint64_t next_frame,
                                   uint64_t frames) {
    constexpr uint64_t overhead = 1024ULL * 1024;
    if (written > UINT64_MAX - next_frame || frames > (UINT64_MAX - overhead) / 4096)
        return false;
    uint64_t projected = written + next_frame,
             gif_budget = projected / 2 + frames * 4096 + overhead;
    if (gif_budget < projected / 2 || gif_budget > UINT64_MAX - RECORD_DISK_RESERVE ||
        next_frame > UINT64_MAX - (gif_budget + RECORD_DISK_RESERVE))
        return false;
    return free_bytes >= RECORD_DISK_RESERVE + gif_budget + next_frame;
}
} // namespace drawscr
