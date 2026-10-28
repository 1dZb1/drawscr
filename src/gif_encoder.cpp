#include "gif_encoder.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
using Microsoft::WRL::ComPtr;

namespace drawscr {
static HRESULT ui2(IWICMetadataQueryWriter* writer, const wchar_t* name, USHORT x) {
    PROPVARIANT p{};
    p.vt = VT_UI2;
    p.uiVal = x;
    return writer->SetMetadataByName(name, &p);
}
static HRESULT ui1(IWICMetadataQueryWriter* writer, const wchar_t* name, BYTE x) {
    PROPVARIANT p{};
    p.vt = VT_UI1;
    p.bVal = x;
    return writer->SetMetadataByName(name, &p);
}
static HRESULT boolean(IWICMetadataQueryWriter* writer, const wchar_t* name, bool x) {
    PROPVARIANT p{};
    p.vt = VT_BOOL;
    p.boolVal = x ? VARIANT_TRUE : VARIANT_FALSE;
    return writer->SetMetadataByName(name, &p);
}
static bool result(HRESULT hr, std::wstring& error, const wchar_t* stage) {
    if (SUCCEEDED(hr))
        return true;
    wchar_t text[180]{};
    swprintf_s(text, L"%s (0x%08lX)", stage, static_cast<unsigned long>(hr));
    error = text;
    return false;
}
struct GifEncoder::Impl {
    int width = 0, height = 0;
    size_t palette_size = 0;
    std::atomic<bool>* cancel = nullptr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICPalette> palette;
    std::vector<BYTE> pending, previous;
    unsigned pending_delay = 0;
    int rounding_error_ms = 0;
    bool closed = false;
    bool emit(std::wstring& error) {
        if (pending.empty())
            return true;
        if (cancel && cancel->load()) {
            error = L"Recording cancelled.";
            return false;
        }
        int l = 0, t = 0, r = width, b = height;
        if (!previous.empty()) {
            l = width;
            t = height;
            r = b = 0;
            for (int y = 0; y < height; ++y) {
                if ((y & 31) == 0 && cancel && cancel->load()) {
                    error = L"Recording cancelled.";
                    return false;
                }
                for (int x = 0; x < width; ++x) {
                    size_t i = static_cast<size_t>(y) * width + x;
                    if (pending[i] != previous[i]) {
                        l = std::min(l, x);
                        t = std::min(t, y);
                        r = std::max(r, x + 1);
                        b = std::max(b, y + 1);
                    }
                }
            }
            if (r <= l || b <= t) {
                l = t = 0;
                r = b = 1;
            }
        }
        UINT w = static_cast<UINT>(r - l), h = static_cast<UINT>(b - t);
        std::vector<BYTE> delta(static_cast<size_t>(w) * h);
        int transparent = -1;
        if (!previous.empty()) {
            std::array<bool, 256> used{};
            bool unchanged = false;
            for (UINT y = 0; y < h; ++y) {
                if ((y & 31) == 0 && cancel && cancel->load()) {
                    error = L"Recording cancelled.";
                    return false;
                }
                for (UINT x = 0; x < w; ++x) {
                    size_t i = static_cast<size_t>(t + y) * width + l + x;
                    if (pending[i] == previous[i])
                        unchanged = true;
                    else
                        used[pending[i]] = true;
                }
            }
            if (unchanged)
                for (size_t i = 0; i < palette_size; ++i)
                    if (!used[i]) {
                        transparent = static_cast<int>(i);
                        break;
                    }
        }
        for (UINT y = 0; y < h; ++y) {
            if ((y & 31) == 0 && cancel && cancel->load()) {
                error = L"Recording cancelled.";
                return false;
            }
            for (UINT x = 0; x < w; ++x) {
                size_t i = static_cast<size_t>(t + y) * width + l + x;
                delta[static_cast<size_t>(y) * w + x] =
                    transparent >= 0 && pending[i] == previous[i] ? static_cast<BYTE>(transparent)
                                                                  : pending[i];
            }
        }
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> options;
        ComPtr<IWICMetadataQueryWriter> metadata;
        HRESULT hr = encoder->CreateNewFrame(&frame, &options);
        if (SUCCEEDED(hr))
            hr = frame->Initialize(options.Get());
        if (SUCCEEDED(hr))
            hr = frame->SetSize(w, h);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat8bppIndexed;
        if (SUCCEEDED(hr))
            hr = frame->SetPixelFormat(&fmt);
        if (SUCCEEDED(hr) && !IsEqualGUID(fmt, GUID_WICPixelFormat8bppIndexed))
            hr = E_FAIL;
        if (SUCCEEDED(hr))
            hr = frame->GetMetadataQueryWriter(&metadata);
        if (SUCCEEDED(hr))
            hr = ui2(metadata.Get(), L"/imgdesc/Left", static_cast<USHORT>(l));
        if (SUCCEEDED(hr))
            hr = ui2(metadata.Get(), L"/imgdesc/Top", static_cast<USHORT>(t));
        if (SUCCEEDED(hr))
            hr = ui2(metadata.Get(), L"/grctlext/Delay", static_cast<USHORT>(pending_delay));
        if (SUCCEEDED(hr))
            hr = ui1(metadata.Get(), L"/grctlext/Disposal", 1);
        if (SUCCEEDED(hr) && transparent >= 0)
            hr = boolean(metadata.Get(), L"/grctlext/TransparencyFlag", true);
        if (SUCCEEDED(hr) && transparent >= 0)
            hr = ui1(metadata.Get(), L"/grctlext/TransparentColorIndex",
                     static_cast<BYTE>(transparent));
        if (SUCCEEDED(hr))
            hr = frame->WritePixels(h, w, static_cast<UINT>(delta.size()), delta.data());
        if (SUCCEEDED(hr))
            hr = frame->Commit();
        if (!result(hr, error, L"Could not encode a GIF frame"))
            return false;
        previous = pending;
        return true;
    }
};
GifEncoder::GifEncoder() : impl_(std::make_unique<Impl>()) {}
GifEncoder::~GifEncoder() = default;
bool GifEncoder::open(const std::wstring& path, int width, int height,
                      const std::vector<unsigned int>& colors, std::wstring& error,
                      std::atomic<bool>* cancel) {
    if (width < 1 || height < 1 || width > 65535 || height > 65535 || colors.empty() ||
        colors.size() > 256) {
        error = L"Invalid GIF dimensions or palette.";
        return false;
    }
    auto& s = *impl_;
    s.width = width;
    s.height = height;
    s.palette_size = colors.size();
    s.cancel = cancel;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&s.factory));
    if (SUCCEEDED(hr))
        hr = s.factory->CreateStream(&s.stream);
    if (SUCCEEDED(hr))
        hr = s.stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr))
        hr = s.factory->CreateEncoder(GUID_ContainerFormatGif, nullptr, &s.encoder);
    if (SUCCEEDED(hr))
        hr = s.encoder->Initialize(s.stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr))
        hr = s.factory->CreatePalette(&s.palette);
    if (SUCCEEDED(hr))
        hr = s.palette->InitializeCustom(const_cast<WICColor*>(colors.data()),
                                         static_cast<UINT>(colors.size()));
    if (SUCCEEDED(hr))
        hr = s.encoder->SetPalette(s.palette.Get());
    ComPtr<IWICMetadataQueryWriter> metadata;
    if (SUCCEEDED(hr))
        hr = s.encoder->GetMetadataQueryWriter(&metadata);
    if (SUCCEEDED(hr))
        hr = ui2(metadata.Get(), L"/logscrdesc/Width", static_cast<USHORT>(width));
    if (SUCCEEDED(hr))
        hr = ui2(metadata.Get(), L"/logscrdesc/Height", static_cast<USHORT>(height));
    BYTE app[] = {'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0'}, data[] = {3, 1, 0, 0, 0};
    PROPVARIANT v{};
    v.vt = VT_UI1 | VT_VECTOR;
    v.caub.cElems = 11;
    v.caub.pElems = app;
    if (SUCCEEDED(hr))
        hr = metadata->SetMetadataByName(L"/appext/Application", &v);
    v.caub.cElems = 5;
    v.caub.pElems = data;
    if (SUCCEEDED(hr))
        hr = metadata->SetMetadataByName(L"/appext/Data", &v);
    return result(hr, error, L"Could not start the GIF encoder");
}
bool GifEncoder::add(const std::vector<unsigned char>& pixels, uint64_t duration_ms,
                     std::wstring& error) {
    auto& s = *impl_;
    if (!s.encoder || s.closed || pixels.size() != static_cast<size_t>(s.width) * s.height ||
        duration_ms > static_cast<uint64_t>(INT64_MAX - 100)) {
        error = L"Invalid GIF frame or duration.";
        return false;
    }
    int64_t adjusted = static_cast<int64_t>(duration_ms) + s.rounding_error_ms;
    uint64_t delay = static_cast<uint64_t>(std::max<int64_t>(1, (adjusted + 5) / 10));
    s.rounding_error_ms = static_cast<int>(adjusted - static_cast<int64_t>(delay * 10));
    if (s.pending.empty()) {
        s.pending = pixels;
        s.pending_delay = 0;
    } else if (s.pending != pixels) {
        if (s.pending_delay && !s.emit(error))
            return false;
        s.pending = pixels;
        s.pending_delay = 0;
    }
    while (delay) {
        if (s.cancel && s.cancel->load()) {
            error = L"Recording cancelled.";
            return false;
        }
        if (s.pending_delay == 65535) {
            if (!s.emit(error))
                return false;
            s.pending_delay = 0;
        }
        unsigned take = static_cast<unsigned>(std::min<uint64_t>(delay, 65535 - s.pending_delay));
        s.pending_delay += take;
        delay -= take;
    }
    return true;
}
bool GifEncoder::finish(std::wstring& error) {
    auto& s = *impl_;
    if (!s.encoder || s.closed || s.pending.empty()) {
        error = L"There are no GIF frames.";
        return false;
    }
    if (!s.emit(error))
        return false;
    HRESULT hr = s.encoder->Commit();
    s.closed = true;
    s.encoder.Reset();
    s.palette.Reset();
    s.stream.Reset();
    s.factory.Reset();
    return result(hr, error, L"Could not finish the GIF");
}
} // namespace drawscr
