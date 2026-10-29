#include "gif_encoder.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>

namespace drawscr {
namespace {
using Bytes = std::vector<unsigned char>;
unsigned bits_for(unsigned n) {
    unsigned bits = 1;
    while ((1u << bits) < n) ++bits;
    return bits;
}
bool stopped(std::atomic<bool>* cancel) { return cancel && cancel->load(); }
// GIF LZW uses least-significant-bit first codes. The encoder dictionary leads
// the decoder by one entry, including the final prefix before the end code.
Bytes compress(const Bytes& pixels, unsigned minimum, bool clear_full,
               std::atomic<bool>* cancel, bool* filled_dictionary = nullptr) {
    if (filled_dictionary) *filled_dictionary = false;
    Bytes out;
    if (pixels.empty()) return out;
    out.reserve(pixels.size() / 2 + 64);
    constexpr unsigned slots = 8192;
    std::array<unsigned, slots> keys;
    std::array<unsigned short, slots> values{};
    keys.fill(~0u);
    const unsigned clear = 1u << minimum, end = clear + 1;
    unsigned next = end + 1, width = minimum + 1, accumulator = 0, filled = 0;
    auto emit = [&](unsigned code) {
        accumulator |= code << filled;
        filled += width;
        while (filled >= 8) {
            out.push_back(static_cast<unsigned char>(accumulator));
            accumulator >>= 8;
            filled -= 8;
        }
    };
    emit(clear);
    unsigned prefix = pixels[0];
    for (size_t i = 1; i < pixels.size(); ++i) {
        if ((i & 65535) == 0 && stopped(cancel)) return {};
        unsigned symbol = pixels[i], key = (prefix << 8) | symbol;
        unsigned slot = (key * 2654435761u) >> 19;
        while (keys[slot] != ~0u && keys[slot] != key) slot = (slot + 1) & (slots - 1);
        if (keys[slot] == key) { prefix = values[slot]; continue; }
        emit(prefix);
        if (next < 4096) {
            keys[slot] = key;
            values[slot] = static_cast<unsigned short>(next++);
            if (next > (1u << width) && width < 12) ++width;
        } else if (clear_full) {
            if (filled_dictionary) *filled_dictionary = true;
            emit(clear);
            keys.fill(~0u);
            next = end + 1;
            width = minimum + 1;
        }
        prefix = symbol;
    }
    emit(prefix);
    if (next == (1u << width) && width < 12) ++width;
    emit(end);
    if (filled) out.push_back(static_cast<unsigned char>(accumulator));
    return out;
}
struct Candidate {
    Bytes compressed;
    std::vector<unsigned int> palette;
    unsigned minimum = 8;
    int transparent = -1;
    size_t cost() const {
        return compressed.size() + (compressed.size() + 254) / 255 +
               (palette.empty() ? 0 : 3 * (size_t{1} << bits_for(static_cast<unsigned>(palette.size()))));
    }
};
}
struct GifEncoder::Impl {
    int width = 0, height = 0;
    std::atomic<bool>* cancel = nullptr;
    std::ofstream stream;
    std::vector<unsigned int> colors;
    Bytes pending, previous;
    unsigned pending_delay = 0;
    int rounding_error_ms = 0;
    bool closed = false;
    void byte(unsigned x) { stream.put(static_cast<char>(x)); }
    void word(unsigned x) { byte(x); byte(x >> 8); }
    void table(const std::vector<unsigned int>& palette) {
        unsigned count = 1u << bits_for(static_cast<unsigned>(palette.size()));
        for (unsigned i = 0; i < count; ++i) {
            unsigned c = i < palette.size() ? palette[i] : 0;
            byte(c >> 16); byte(c >> 8); byte(c);
        }
    }
    Candidate encode(const Bytes& pixels, int transparent, bool local) {
        Candidate candidate;
        candidate.transparent = transparent;
        Bytes remapped;
        const Bytes* input = &pixels;
        if (local) {
            std::array<int, 256> mapping;
            mapping.fill(-1);
            remapped.resize(pixels.size());
            for (size_t i = 0; i < pixels.size(); ++i) {
                if ((i & 65535) == 0 && stopped(cancel)) return {};
                unsigned index = pixels[i];
                if (mapping[index] < 0) {
                    mapping[index] = static_cast<int>(candidate.palette.size());
                    candidate.palette.push_back(colors[index]);
                }
                remapped[i] = static_cast<unsigned char>(mapping[index]);
            }
            unsigned source_maximum = *std::max_element(pixels.begin(), pixels.end());
            if (std::max(2u, bits_for(static_cast<unsigned>(candidate.palette.size()))) >=
                std::max(2u, bits_for(source_maximum + 1))) return {};
            if (transparent >= 0) candidate.transparent = mapping[static_cast<size_t>(transparent)];
            input = &remapped;
        }
        unsigned maximum = *std::max_element(input->begin(), input->end());
        candidate.minimum = std::max(2u, bits_for(maximum + 1));
        bool filled_dictionary = false;
        candidate.compressed = compress(*input, candidate.minimum, true, cancel, &filled_dictionary);
        if (stopped(cancel)) return candidate;
        // Both policies are lossless. Never assume freezing a full dictionary
        // helps: changing scenes benefit substantially from a fresh dictionary.
        auto frozen = filled_dictionary ? compress(*input, candidate.minimum, false, cancel) : Bytes{};
        if (!frozen.empty() && frozen.size() < candidate.compressed.size())
            candidate.compressed = std::move(frozen);
        return candidate;
    }
    bool emit(std::wstring& error) {
        if (pending.empty()) return true;
        if (stopped(cancel)) { error = L"Recording cancelled."; return false; }
        int left = 0, top = 0, right = width, bottom = height;
        if (!previous.empty()) {
            left = width; top = height; right = bottom = 0;
            for (int y = 0; y < height; ++y) {
                if ((y & 31) == 0 && stopped(cancel)) { error = L"Recording cancelled."; return false; }
                for (int x = 0; x < width; ++x) {
                    size_t i = static_cast<size_t>(y) * width + x;
                    if (pending[i] != previous[i]) {
                        left = std::min(left, x); top = std::min(top, y);
                        right = std::max(right, x + 1); bottom = std::max(bottom, y + 1);
                    }
                }
            }
            if (right <= left || bottom <= top) { left = top = 0; right = bottom = 1; }
        }
        const int w = right - left, h = bottom - top;
        Bytes opaque(static_cast<size_t>(w) * h);
        std::array<bool, 256> changed{};
        bool has_unchanged = false;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            if ((x == 0) && (y & 31) == 0 && stopped(cancel)) { error = L"Recording cancelled."; return false; }
            size_t i = static_cast<size_t>(top + y) * width + left + x;
            opaque[static_cast<size_t>(y) * w + x] = pending[i];
            if (!previous.empty() && pending[i] == previous[i]) has_unchanged = true;
            else changed[pending[i]] = true;
        }
        Candidate best = encode(opaque, -1, false);
        auto consider = [&](const Bytes& pixels, int transparent) {
            if (stopped(cancel)) return;
            auto plain = encode(pixels, transparent, false);
            if (!plain.compressed.empty() && plain.cost() < best.cost()) best = std::move(plain);
            // A local table is useful only when its byte saving pays for the table.
            if (stopped(cancel)) return;
            auto compact = encode(pixels, transparent, true);
            if (!compact.compressed.empty() && compact.cost() < best.cost()) best = std::move(compact);
        };
        auto compact = encode(opaque, -1, true);
        if (!compact.compressed.empty() && compact.cost() < best.cost()) best = std::move(compact);
        if (has_unchanged) {
            int transparent = -1;
            for (size_t i = 0; i < colors.size(); ++i)
                if (!changed[i]) { transparent = static_cast<int>(i); break; }
            if (transparent >= 0) {
                Bytes masked = opaque;
                for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                    if ((x == 0) && (y & 31) == 0 && stopped(cancel)) { error = L"Recording cancelled."; return false; }
                    size_t i = static_cast<size_t>(top + y) * width + left + x;
                    if (pending[i] == previous[i]) masked[static_cast<size_t>(y) * w + x] = static_cast<unsigned char>(transparent);
                }
                consider(masked, transparent);
            }
        }
        if (stopped(cancel)) { error = L"Recording cancelled."; return false; }
        if (best.compressed.empty()) { error = L"Could not compress GIF frame."; return false; }
        byte(0x21); byte(0xf9); byte(4);
        byte(4 | (best.transparent >= 0 ? 1 : 0)); // retain previous composited canvas
        word(pending_delay); byte(best.transparent >= 0 ? best.transparent : 0); byte(0);
        byte(0x2c); word(left); word(top); word(w); word(h);
        byte(best.palette.empty() ? 0 : 0x80 | (bits_for(static_cast<unsigned>(best.palette.size())) - 1));
        if (!best.palette.empty()) table(best.palette);
        byte(best.minimum);
        for (size_t i = 0; i < best.compressed.size(); i += 255) {
            size_t count = std::min(size_t{255}, best.compressed.size() - i);
            byte(static_cast<unsigned>(count));
            stream.write(reinterpret_cast<const char*>(best.compressed.data() + i), static_cast<std::streamsize>(count));
        }
        byte(0);
        if (!stream) { error = L"Could not write GIF frame."; return false; }
        previous = pending;
        return true;
    }
};
GifEncoder::GifEncoder() : impl_(std::make_unique<Impl>()) {}
GifEncoder::~GifEncoder() = default;
bool GifEncoder::open(const std::wstring& path, int width, int height,
                      const std::vector<unsigned int>& colors, std::wstring& error,
                      std::atomic<bool>* cancel) {
    if (width < 1 || height < 1 || width > 65535 || height > 65535 || colors.empty() || colors.size() > 256) {
        error = L"Invalid GIF dimensions or palette."; return false;
    }
    impl_ = std::make_unique<Impl>();
    auto& s = *impl_;
    s.width = width; s.height = height; s.colors = colors; s.cancel = cancel;
    s.stream.open(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!s.stream) { error = L"Could not open GIF output."; return false; }
    s.stream.write("GIF89a", 6); s.word(width); s.word(height);
    s.byte(0xf0 | (bits_for(static_cast<unsigned>(colors.size())) - 1)); s.byte(0); s.byte(0);
    s.table(colors);
    const unsigned char loop[] = {0x21,0xff,11,'N','E','T','S','C','A','P','E','2','.','0',3,1,0,0,0};
    s.stream.write(reinterpret_cast<const char*>(loop), sizeof(loop));
    if (!s.stream) { error = L"Could not write GIF header."; return false; }
    return true;
}
bool GifEncoder::add(const std::vector<unsigned char>& pixels, uint64_t duration_ms, std::wstring& error) {
    auto& s = *impl_;
    if (!s.stream || s.closed || pixels.size() != static_cast<size_t>(s.width) * s.height ||
        duration_ms > static_cast<uint64_t>(INT64_MAX - 100) ||
        std::any_of(pixels.begin(), pixels.end(), [&](unsigned char c) { return c >= s.colors.size(); })) {
        error = L"Invalid GIF frame or duration."; return false;
    }
    int64_t adjusted = static_cast<int64_t>(duration_ms) + s.rounding_error_ms;
    uint64_t delay = static_cast<uint64_t>(std::max<int64_t>(1, (adjusted + 5) / 10));
    s.rounding_error_ms = static_cast<int>(adjusted - static_cast<int64_t>(delay * 10));
    if (s.pending.empty()) { s.pending = pixels; s.pending_delay = 0; }
    else if (s.pending != pixels) {
        if (s.pending_delay && !s.emit(error)) return false;
        s.pending = pixels; s.pending_delay = 0;
    }
    while (delay) {
        if (stopped(s.cancel)) { error = L"Recording cancelled."; return false; }
        if (s.pending_delay == 65535) { if (!s.emit(error)) return false; s.pending_delay = 0; }
        unsigned take = static_cast<unsigned>(std::min<uint64_t>(delay, 65535 - s.pending_delay));
        s.pending_delay += take; delay -= take;
    }
    return true;
}
bool GifEncoder::finish(std::wstring& error) {
    auto& s = *impl_;
    if (!s.stream || s.closed || s.pending.empty()) { error = L"There are no GIF frames."; return false; }
    if (!s.emit(error)) return false;
    s.byte(0x3b); s.stream.flush();
    bool good = s.stream.good(); s.stream.close(); s.closed = true;
    if (!good) error = L"Could not finish GIF output.";
    return good;
}
} // namespace drawscr
