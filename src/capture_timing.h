#pragma once
#include <windows.h>
#include <array>
#include <cstdio>
#include <string>

namespace drawscr {
// Bounded, metadata-only diagnostics for unusually slow capture initialization.
// No screen pixels, document names or input events are recorded.
class CaptureTiming {
  public:
    explicit CaptureTiming(ULONGLONG start = 0) : started_(start ? start : GetTickCount64()) {}
    void mark(const char* name) {
        if (!finished_ && count_ < events_.size())
            events_[count_++] = {name, GetTickCount64() - started_};
    }
    bool finish() {
        if (finished_) return false;
        mark("first_paint_handler_complete");
        finished_ = true;
        return GetTickCount64() - started_ >= 1000;
    }
    void write(const std::wstring& ini) const {
        if (!finished_ || ini.empty()) return;
        FILE* file = nullptr;
        const std::wstring path = ini + L".capture-timing.log";
        if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file) return;
        std::fputs("Drawscr slow capture; elapsed milliseconds from activation\n", file);
        for (size_t i = 0; i < count_; ++i)
            std::fprintf(file, "%s=%llu\n", events_[i].name,
                         static_cast<unsigned long long>(events_[i].elapsed));
        std::fclose(file);
    }
  private:
    struct Event { const char* name = nullptr; ULONGLONG elapsed = 0; };
    ULONGLONG started_ = 0;
    std::array<Event, 20> events_{};
    size_t count_ = 0;
    bool finished_ = false;
};
} // namespace drawscr
