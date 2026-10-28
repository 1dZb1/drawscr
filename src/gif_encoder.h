#pragma once
#include <windows.h>
#include <memory>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace drawscr {
class GifEncoder {
  public:
    GifEncoder();
    ~GifEncoder();
    GifEncoder(const GifEncoder&) = delete;
    GifEncoder& operator=(const GifEncoder&) = delete;
    bool open(const std::wstring& path, int width, int height,
              const std::vector<unsigned int>& palette, std::wstring& error,
              std::atomic<bool>* cancel = nullptr);
    bool add(const std::vector<unsigned char>& indexed, uint64_t duration_ms, std::wstring& error);
    bool finish(std::wstring& error);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace drawscr
