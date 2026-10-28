#pragma once
#include "record_limits.h"
#include <windows.h>
#include <cstdint>
#include <string>

namespace drawscr {
struct RecordOptions {
    int fps = 8;
    int countdown = 3;
    GifCompression compression = GifCompression::Normal;
};
bool choose_recording(HWND owner, RecordOptions& options, SIZE selection);
enum class PreviewResult { Saved, Discarded, Failed };
PreviewResult preview_recording(HWND owner, const std::wstring& path, uint64_t size,
                                bool detail_guard, bool storage_limited, std::wstring& error);
}
