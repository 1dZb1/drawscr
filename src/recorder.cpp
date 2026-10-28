#include "recorder.h"
#include "gif_encoder.h"
#include "palette.h"
#include <objbase.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>
#include <vector>

namespace drawscr {
#pragma pack(push, 1)
struct RawHeader {
    char magic[8];
    uint32_t width, height;
    uint64_t stop_ms, frames;
};
#pragma pack(pop)
static constexpr char raw_magic[8] = {'D', 'S', 'R', 'A', 'W', '0', '1', 0};
using File = std::unique_ptr<FILE, decltype(&fclose)>;
static File open_file(const std::wstring& path, const wchar_t* mode) {
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), mode);
    return File(f, &fclose);
}
static bool cancelled(std::atomic<bool>* flag) {
    return flag && flag->load();
}
static bool system_storage_probe(const std::wstring& directory, uint64_t& free_bytes, void*) {
    ULARGE_INTEGER available{};
    if (!GetDiskFreeSpaceExW(directory.c_str(), &available, nullptr, nullptr))
        return false;
    free_bytes = available.QuadPart;
    return true;
}
static bool temp_name(const std::wstring& directory, std::wstring& name) {
    wchar_t root[MAX_PATH + 1]{};
    if (directory.empty()) {
        DWORD n = GetTempPathW(MAX_PATH, root);
        if (!n || n > MAX_PATH)
            return false;
    } else {
        if (directory.size() >= MAX_PATH)
            return false;
        wcscpy_s(root, directory.c_str());
    }
    wchar_t path[MAX_PATH + 1]{};
    if (!GetTempFileNameW(root, L"dsr", 0, path))
        return false;
    name = path;
    return true;
}
static bool read_frame(FILE* f, std::vector<BYTE>& pixels, uint64_t& timestamp) {
    return fread(&timestamp, sizeof(timestamp), 1, f) == 1 &&
           fread(pixels.data(), 1, pixels.size(), f) == pixels.size();
}
enum class Comparison { Accept, Reject, Cancelled, ReadError };
static int color_error(const BYTE* p, unsigned color) {
    int r = int(p[2]) - int((color >> 16) & 255), g = int(p[1]) - int((color >> 8) & 255),
        b = int(p[0]) - int(color & 255);
    return r * r + g * g + b * b;
}
static bool is_edge(const BYTE* pixels, size_t i, size_t width) {
    auto different = [&](size_t j) {
        int r = int(pixels[i * 4 + 2]) - int(pixels[j * 4 + 2]),
            g = int(pixels[i * 4 + 1]) - int(pixels[j * 4 + 1]),
            b = int(pixels[i * 4]) - int(pixels[j * 4]);
        return r * r + g * g + b * b >= 32 * 32;
    };
    return (i % width && different(i - 1)) || (i >= width && different(i - width));
}
static Comparison compare_plans(FILE* file, uint64_t frames, size_t width,
                                std::vector<BYTE>& pixels, const PalettePlan& base,
                                const PalettePlan& candidate, GifCompression mode,
                                std::atomic<bool>* cancel) {
    if (_fseeki64(file, sizeof(RawHeader), SEEK_SET) != 0)
        return Comparison::ReadError;
    uint64_t normal_error = 0, candidate_error = 0, normal_edge = 0, candidate_edge = 0,
             edge_count = 0, total = 0;
    const size_t count = pixels.size() / 4;
    for (uint64_t j = 0; j < frames; ++j) {
        if (cancelled(cancel))
            return Comparison::Cancelled;
        uint64_t timestamp = 0;
        if (!read_frame(file, pixels, timestamp))
            return Comparison::ReadError;
        for (size_t i = 0; i < count; ++i) {
            if ((i & 65535) == 0 && cancelled(cancel))
                return Comparison::Cancelled;
            const BYTE* p = pixels.data() + i * 4;
            int bi = base.index(p), ci = candidate.index(p);
            if (bi < 0 || ci < 0)
                return Comparison::ReadError;
            int be = color_error(p, base.colors[static_cast<size_t>(bi)]),
                ce = color_error(p, candidate.colors[static_cast<size_t>(ci)]);
            bool edge = is_edge(pixels.data(), i, width);
            if (mode == GifCompression::Strong && ce > be + (edge ? 108 : 768))
                return Comparison::Reject;
            if (mode == GifCompression::Quality && edge && ce > be + 1728)
                return Comparison::Reject;
            normal_error += be;
            candidate_error += ce;
            ++total;
            if (edge) {
                normal_edge += be;
                candidate_edge += ce;
                ++edge_count;
            }
        }
    }
    if (mode == GifCompression::Strong)
        return candidate_error <= normal_error || candidate_error - normal_error <= total * 48
                   ? Comparison::Accept
                   : Comparison::Reject;
    return candidate_error < normal_error &&
                   (candidate_edge <= normal_edge || candidate_edge - normal_edge <= edge_count * 6)
               ? Comparison::Accept
               : Comparison::Reject;
}
bool recover_raw(const std::wstring& raw_path, const std::wstring& gif_path, std::wstring& error,
                 std::atomic<bool>* stop, GifCompression compression,
                 QuantizationOutcome* outcome) {
    if (outcome)
        *outcome = {};
    File raw = open_file(raw_path, L"rb");
    if (!raw) {
        error = L"Could not open the recording data.";
        return false;
    }
    RawHeader h{};
    if (fread(&h, sizeof(h), 1, raw.get()) != 1 || std::memcmp(h.magic, raw_magic, 8) ||
        !valid_record_frame_size(h.width, h.height)) {
        error = L"Invalid recording data.";
        return false;
    }
    uint64_t pixels64 = static_cast<uint64_t>(h.width) * h.height, bytes64 = pixels64 * 4;
    _fseeki64(raw.get(), 0, SEEK_END);
    int64_t file_size = _ftelli64(raw.get());
    if (file_size < static_cast<int64_t>(sizeof(h))) {
        error = L"Recording data is incomplete.";
        return false;
    }
    uint64_t frame_bytes = bytes64 + sizeof(uint64_t),
             available = (static_cast<uint64_t>(file_size) - sizeof(h)) / frame_bytes;
    uint64_t frames = h.frames ? std::min(h.frames, available) : available;
    if (!frames || !safe_record_count(pixels64, frames)) {
        error = L"Recording data contains no complete frames or exceeds safe processing limits.";
        return false;
    }
    size_t count = static_cast<size_t>(pixels64);
    std::vector<BYTE> frame(static_cast<size_t>(bytes64));
    ColorHistogram histogram;
    _fseeki64(raw.get(), sizeof(h), SEEK_SET);
    for (uint64_t j = 0; j < frames; ++j) {
        if (cancelled(stop)) {
            error = L"Recording cancelled.";
            return false;
        }
        uint64_t stamp = 0;
        if (!read_frame(raw.get(), frame, stamp)) {
            error = L"Recording data is incomplete.";
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            if ((i & 65535) == 0 && cancelled(stop)) {
                error = L"Recording cancelled.";
                return false;
            }
            histogram.add(frame.data() + i * 4);
        }
    }
    PalettePlan plan = histogram.make_plan(256, false, stop);
    if (plan.colors.empty()) {
        error = cancelled(stop) ? L"Recording cancelled." : L"Could not create the GIF palette.";
        return false;
    }
    if (compression == GifCompression::Strong || compression == GifCompression::Quality) {
        bool useful = !(compression == GifCompression::Strong && plan.colors.size() <= 128) &&
                      !(compression == GifCompression::Quality && plan.exact_colors);
        if (useful) {
            PalettePlan candidate =
                histogram.make_plan(compression == GifCompression::Strong ? 128 : 256,
                                    compression == GifCompression::Quality, stop);
            if (candidate.colors.empty()) {
                error = cancelled(stop) ? L"Recording cancelled."
                                        : L"Could not create the GIF palette.";
                return false;
            }
            Comparison comparison = compare_plans(raw.get(), frames, h.width, frame, plan,
                                                  candidate, compression, stop);
            if (comparison == Comparison::Cancelled) {
                error = L"Recording cancelled.";
                return false;
            }
            if (comparison == Comparison::ReadError) {
                error = L"Recording data is incomplete.";
                return false;
            }
            if (comparison == Comparison::Accept)
                plan = std::move(candidate);
            else if (outcome)
                outcome->fallback = true;
        }
    }
    if (outcome)
        outcome->colors = static_cast<unsigned>(plan.colors.size());
    GifEncoder gif;
    if (!gif.open(gif_path, static_cast<int>(h.width), static_cast<int>(h.height), plan.colors,
                  error, stop))
        return false;
    _fseeki64(raw.get(), sizeof(h), SEEK_SET);
    std::vector<BYTE> previous(count);
    uint64_t previous_time = 0;
    for (uint64_t j = 0; j < frames; ++j) {
        if (cancelled(stop)) {
            error = L"Recording cancelled.";
            return false;
        }
        uint64_t timestamp = 0;
        if (!read_frame(raw.get(), frame, timestamp)) {
            error = L"Recording data is incomplete.";
            return false;
        }
        if (j) {
            if (timestamp < previous_time) {
                error = L"Recording timestamps are invalid.";
                return false;
            }
            uint64_t duration = std::max<uint64_t>(timestamp - previous_time, 10);
            if (!gif.add(previous, duration, error))
                return false;
        }
        for (size_t i = 0; i < count; ++i) {
            if ((i & 65535) == 0 && cancelled(stop)) {
                error = L"Recording cancelled.";
                return false;
            }
            int index = plan.index(frame.data() + i * 4);
            if (index < 0) {
                error = L"Recording data changed during encoding.";
                return false;
            }
            previous[i] = static_cast<BYTE>(index);
        }
        previous_time = timestamp;
    }
    uint64_t final_duration =
        std::max<uint64_t>(h.stop_ms > previous_time ? h.stop_ms - previous_time : 100, 10);
    return gif.add(previous, final_duration, error) && gif.finish(error);
}
Recorder::~Recorder() {
    cancel();
}
bool Recorder::start(int w, int h, HWND notify, std::wstring& error, const std::wstring& dir,
                     void (*before_encode)(), GifCompression compression,
                     StorageProbe storage_probe, void* storage_context) {
    if (worker_.joinable() || w < 1 || h < 1 ||
        !valid_record_frame_size(static_cast<uint64_t>(w), static_cast<uint64_t>(h))) {
        error = L"Invalid recording size.";
        return false;
    }
    width_ = w;
    height_ = h;
    notify_ = notify;
    compression_ = compression;
    storage_probe_ = storage_probe ? storage_probe : system_storage_probe;
    storage_context_ = storage_context;
    if (!temp_name(dir, raw_path_) || !temp_name(dir, gif_path_)) {
        if (!raw_path_.empty())
            DeleteFileW(raw_path_.c_str());
        error = L"Could not create recording files.";
        return false;
    }
    size_t slash = raw_path_.find_last_of(L"\\/");
    storage_directory_ = slash == std::wstring::npos ? L"." : raw_path_.substr(0, slash + 1);
    try {
        result_.raw_path = raw_path_;
        result_.gif_path = gif_path_;
        before_encode_ = before_encode;
        worker_ = std::thread(&Recorder::run, this);
    } catch (...) {
        DeleteFileW(raw_path_.c_str());
        DeleteFileW(gif_path_.c_str());
        error = L"Could not start the recording worker.";
        return false;
    }
    return true;
}
SubmitResult Recorder::submit(Image image, uint64_t elapsed) {
    std::lock_guard lock(mutex_);
    if (stopping_ || cancelled_.load() || finished_.load())
        return SubmitResult::Closed;
    if (!image.valid() || image.width() != width_ || image.height() != height_ ||
        elapsed > static_cast<uint64_t>(INT64_MAX - 100))
        return SubmitResult::Closed;
    if (queue_.size() >= 3) {
        if (result_.dropped != UINT64_MAX)
            ++result_.dropped;
        return SubmitResult::Dropped;
    }
    queue_.push_back({std::move(image), elapsed});
    result_.queue_peak =
        std::max<unsigned>(result_.queue_peak, static_cast<unsigned>(queue_.size()));
    cv_.notify_one();
    return SubmitResult::Accepted;
}
void Recorder::stop(uint64_t elapsed) {
    std::lock_guard lock(mutex_);
    if (stopping_ || finished_.load())
        return;
    stopping_ = true;
    stop_ms_ = std::min<uint64_t>(elapsed, static_cast<uint64_t>(INT64_MAX - 100));
    cv_.notify_all();
}
void Recorder::cancel() {
    cancelled_ = true;
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        queue_.clear();
        cv_.notify_all();
    }
    if (worker_.joinable())
        worker_.join();
    if (!claimed_) {
        if (!raw_path_.empty())
            DeleteFileW(raw_path_.c_str());
        if (!gif_path_.empty())
            DeleteFileW(gif_path_.c_str());
    }
}
RecordingResult Recorder::take_result() {
    if (worker_.joinable())
        worker_.join();
    std::lock_guard lock(mutex_);
    claimed_ = true;
    return std::move(result_);
}
void Recorder::worker_failure(RecordingFailure failure) noexcept {
    try {
        std::lock_guard lock(mutex_);
        result_.ok = false;
        result_.failure = failure;
    } catch (...) {
    }
    DeleteFileW(gif_path_.c_str());
    WIN32_FILE_ATTRIBUTE_DATA info{};
    uint64_t min_bytes =
        sizeof(RawHeader) + sizeof(uint64_t) + static_cast<uint64_t>(width_) * height_ * 4;
    bool complete =
        GetFileAttributesExW(raw_path_.c_str(), GetFileExInfoStandard, &info) &&
        (((static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow) >= min_bytes);
    if (cancelled_.load() || !complete)
        DeleteFileW(raw_path_.c_str());
    finished_ = true;
    if (!cancelled_.load() && notify_ && IsWindow(notify_))
        PostMessageW(notify_, WM_DRAW_RECORD_DONE, 0, 0);
}
void Recorder::run() noexcept {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        run_impl();
    } catch (const std::bad_alloc&) {
        worker_failure(RecordingFailure::OutOfMemory);
    } catch (...) {
        worker_failure(RecordingFailure::Unexpected);
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
}
void Recorder::run_impl() {

    RawHeader header{};
    std::memcpy(header.magic, raw_magic, 8);
    header.width = static_cast<uint32_t>(width_);
    header.height = static_cast<uint32_t>(height_);
    std::wstring error;
    File raw = open_file(raw_path_, L"w+b");
    if (!raw || fwrite(&header, sizeof(header), 1, raw.get()) != 1)
        error = L"Could not write recording data.";
    uint64_t written = sizeof(header);
    Image previous;
    while (error.empty()) {
        Frame frame;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return cancelled_.load() || stopping_ || !queue_.empty(); });
            if (cancelled_.load())
                break;
            if (queue_.empty() && stopping_)
                break;
            if (queue_.empty())
                continue;
            frame = std::move(queue_.front());
            queue_.pop_front();
        }
        size_t n = static_cast<size_t>(width_) * height_ * 4;
        bool duplicate =
            previous.valid() && std::memcmp(previous.pixels(), frame.image.pixels(), n) == 0;
        uint64_t free_bytes = 0, next = duplicate ? 0 : static_cast<uint64_t>(n) + sizeof(uint64_t);
        bool known = storage_probe_(storage_directory_, free_bytes, storage_context_);
        bool safe_count = duplicate || safe_record_count(static_cast<uint64_t>(width_) * height_,
                                                         header.frames + 1);
        if (!known) {
            error = L"Could not check free storage. Recording data was retained when possible.";
            std::lock_guard lock(mutex_);
            stopping_ = true;
            queue_.clear();
            break;
        }
        if (!safe_count) {
            error =
                L"Recording reached the format processing limit. Data was retained for recovery.";
            std::lock_guard lock(mutex_);
            stopping_ = true;
            queue_.clear();
            stop_ms_ = frame.timestamp;
            break;
        }
        if (!sufficient_spool_space(free_bytes, written, next,
                                    header.frames + (duplicate ? 0 : 1))) {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            queue_.clear();
            stop_ms_ = frame.timestamp;
            result_.storage_limited = true;
            if (!header.frames)
                error = L"Not enough free storage to start recording.";
            break;
        }
        if (duplicate)
            continue;
        if (fwrite(&frame.timestamp, sizeof(frame.timestamp), 1, raw.get()) != 1 ||
            fwrite(frame.image.pixels(), 1, n, raw.get()) != n) {
            error = L"Recording storage is full or unavailable.";
            std::lock_guard lock(mutex_);
            stopping_ = true;
            queue_.clear();
            break;
        }
        written += next;
        ++header.frames;
        previous = std::move(frame.image);
    }
    if (raw) {
        {
            std::lock_guard lock(mutex_);
            header.stop_ms = stop_ms_;
        }
        if (_fseeki64(raw.get(), 0, SEEK_SET) != 0 ||
            fwrite(&header, sizeof(header), 1, raw.get()) != 1 || fflush(raw.get()) != 0) {
            if (error.empty())
                error = L"Could not finish writing recording data.";
        }
        raw.reset();
    }
    bool ok = false;
    QuantizationOutcome quantization;
    if (!cancelled_.load() && error.empty()) {
        if (!header.frames)
            error = L"No frames were captured.";
        else {
            if (before_encode_)
                before_encode_();
            ok = recover_raw(raw_path_, gif_path_, error, &cancelled_, compression_, &quantization);
        }
    }
    {
        std::lock_guard lock(mutex_);
        result_.ok = ok;
        result_.error = error;
        result_.quantization = quantization;
        if (ok) {
            WIN32_FILE_ATTRIBUTE_DATA info{};
            if (GetFileAttributesExW(gif_path_.c_str(), GetFileExInfoStandard, &info))
                result_.bytes =
                    (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        } else
            DeleteFileW(gif_path_.c_str());
    }
    if (ok || cancelled_.load() || !header.frames)
        DeleteFileW(raw_path_.c_str());
    finished_ = true;
    if (!cancelled_.load() && notify_ && IsWindow(notify_))
        PostMessageW(notify_, WM_DRAW_RECORD_DONE, 0, 0);
}
} // namespace drawscr
