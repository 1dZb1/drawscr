#pragma once
#include "image.h"
#include "record_limits.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace drawscr {
constexpr UINT WM_DRAW_RECORD_DONE = WM_APP + 21;
enum class RecordingFailure { None, OutOfMemory, Unexpected };
struct QuantizationOutcome {
    bool fallback = false;
    unsigned colors = 0;
};
struct RecordingResult {
    bool ok = false;
    RecordingFailure failure = RecordingFailure::None;
    std::wstring gif_path, raw_path, error;
    uint64_t bytes = 0;
    uint64_t dropped = 0;
    unsigned queue_peak = 0;
    bool storage_limited = false;
    QuantizationOutcome quantization;
};
enum class SubmitResult { Accepted, Dropped, Closed };
using StorageProbe = bool (*)(const std::wstring& directory, uint64_t& free_bytes, void* context);
class Recorder {
  public:
    Recorder() = default;
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    // before_encode is an optional fault-injection seam for lifecycle tests.
    bool start(int width, int height, HWND notify, std::wstring& error,
               const std::wstring& temp_dir = {}, void (*before_encode)() = nullptr,
               GifCompression compression = GifCompression::Normal,
               StorageProbe storage_probe = nullptr, void* storage_context = nullptr);
    SubmitResult submit(Image frame, uint64_t elapsed_ms);
    void stop(uint64_t elapsed_ms);
    void cancel();
    bool finished() const {
        return finished_.load();
    }
    RecordingResult take_result();
    std::wstring raw_path() const {
        return raw_path_;
    }
    std::wstring gif_path() const {
        return gif_path_;
    }

  private:
    struct Frame {
        Image image;
        uint64_t timestamp = 0;
    };
    void run() noexcept;
    void run_impl();
    void worker_failure(RecordingFailure failure) noexcept;
    int width_ = 0, height_ = 0;
    HWND notify_ = nullptr;
    void (*before_encode_)() = nullptr;
    GifCompression compression_ = GifCompression::Normal;
    StorageProbe storage_probe_ = nullptr;
    void* storage_context_ = nullptr;
    std::wstring storage_directory_;
    std::wstring raw_path_, gif_path_;
    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Frame> queue_;
    std::atomic<bool> cancelled_{false}, finished_{false};
    bool stopping_ = false, claimed_ = false;
    uint64_t stop_ms_ = 0;
    RecordingResult result_;
};
bool recover_raw(const std::wstring& raw_path, const std::wstring& gif_path, std::wstring& error,
                 std::atomic<bool>* cancel = nullptr,
                 GifCompression compression = GifCompression::Normal,
                 QuantizationOutcome* outcome = nullptr);
} // namespace drawscr
