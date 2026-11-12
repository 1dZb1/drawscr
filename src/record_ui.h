#pragma once
#include "record_limits.h"
#include <windows.h>
#include <cstdint>
#include <string>
#include <algorithm>
#include <cwchar>
#include <cerrno>
#include <limits>

namespace drawscr {
struct RecordOptions {
    int fps = 8;
    int countdown = 5;
    GifCompression compression = GifCompression::Normal;
};
inline RecordOptions normalize_record_options(RecordOptions options) {
    options.fps = std::clamp(options.fps, 1, 20);
    if (options.countdown != 0 && options.countdown != 3 && options.countdown != 5)
        options.countdown = 5;
    if (options.compression != GifCompression::Strong &&
        options.compression != GifCompression::Normal &&
        options.compression != GifCompression::Quality)
        options.compression = GifCompression::Normal;
    return options;
}
inline RecordOptions load_record_options(const std::wstring& path) {
    RecordOptions options;
    auto read = [&](const wchar_t* key, int fallback) {
        wchar_t text[64]{};
        DWORD n = GetPrivateProfileStringW(L"Recording", key, L"", text, 64, path.c_str());
        if (!n || n >= 63)
            return fallback;
        wchar_t* end = nullptr;
        errno = 0;
        long value = wcstol(text, &end, 10);
        if (errno || end == text || *end || value < std::numeric_limits<int>::min() ||
            value > std::numeric_limits<int>::max())
            return fallback;
        return static_cast<int>(value);
    };
    options.fps = read(L"FPS", options.fps);
    options.countdown = read(L"Countdown", options.countdown);
    options.compression =
        static_cast<GifCompression>(read(L"Compression", static_cast<int>(options.compression)));
    return normalize_record_options(options);
}
inline bool save_record_options(const std::wstring& path, RecordOptions options) {
    options = normalize_record_options(options);
    std::wstring values = L"FPS=" + std::to_wstring(options.fps);
    values.push_back(L'\0');
    values += L"Countdown=" + std::to_wstring(options.countdown);
    values.push_back(L'\0');
    values += L"Compression=" + std::to_wstring(static_cast<int>(options.compression));
    values.push_back(L'\0');
    values.push_back(L'\0');
    return WritePrivateProfileSectionW(L"Recording", values.c_str(), path.c_str()) != FALSE;
}
// Updates out only when the user saves; never starts a recording.
bool choose_recording(HWND owner, RecordOptions& options, SIZE selection);
enum class PreviewResult { Saved, Discarded, Failed };
PreviewResult preview_recording(HWND owner, const std::wstring& path, uint64_t size,
                                bool detail_guard, bool storage_limited, std::wstring& error);
}
