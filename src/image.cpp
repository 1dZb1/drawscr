#include "image.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <vector>
using Microsoft::WRL::ComPtr;

namespace drawscr {
Image::Image(int width, int height) : width_(width), height_(height) {
    if (width <= 0 || height <= 0 || static_cast<uint64_t>(width) * height > 268435456ULL) {
        width_ = height_ = 0;
        return;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* data = nullptr;
    bitmap_ = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    pixels_ = static_cast<uint8_t*>(data);
    if (!valid()) {
        width_ = height_ = 0;
        return;
    }
    std::memset(pixels_, 0, static_cast<size_t>(width) * height * 4);
}
Image::~Image() {
    if (bitmap_)
        DeleteObject(bitmap_);
}
Image::Image(Image&& o) noexcept
    : bitmap_(o.bitmap_), pixels_(o.pixels_), width_(o.width_), height_(o.height_) {
    o.bitmap_ = nullptr;
    o.pixels_ = nullptr;
    o.width_ = o.height_ = 0;
}
Image& Image::operator=(Image&& o) noexcept {
    if (this != &o) {
        if (bitmap_)
            DeleteObject(bitmap_);
        bitmap_ = o.bitmap_;
        pixels_ = o.pixels_;
        width_ = o.width_;
        height_ = o.height_;
        o.bitmap_ = nullptr;
        o.pixels_ = nullptr;
        o.width_ = o.height_ = 0;
    }
    return *this;
}
HDC Image::begin() const {
    HDC dc = CreateCompatibleDC(nullptr);
    if (dc)
        SelectObject(dc, bitmap_);
    return dc;
}
void Image::end(HDC dc) const {
    if (dc)
        DeleteDC(dc);
}
void Image::paint(HDC dst, int x, int y) const {
    if (!valid())
        return;
    HDC src = begin();
    if (src) {
        BitBlt(dst, x, y, width_, height_, src, 0, 0, SRCCOPY);
        end(src);
    }
}
Image capture_screen(RECT r) {
    Image out(r.right - r.left, r.bottom - r.top);
    if (!out.valid())
        return {};
    HDC screen = GetDC(nullptr), dest = out.begin();
    bool ok =
        screen && dest &&
        BitBlt(dest, 0, 0, out.width(), out.height(), screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    out.end(dest);
    if (screen)
        ReleaseDC(nullptr, screen);
    if (!ok)
        return {};
    return out;
}
Image crop_image(const Image& src, RECT r) {
    RECT bounds{0, 0, src.width(), src.height()};
    r = intersection(r, bounds);
    Image out(r.right - r.left, r.bottom - r.top);
    if (!out.valid() || !src.valid())
        return {};
    HDC s = src.begin(), d = out.begin();
    bool ok = s && d && BitBlt(d, 0, 0, out.width(), out.height(), s, r.left, r.top, SRCCOPY);
    src.end(s);
    out.end(d);
    if (!ok)
        return {};
    return out;
}
bool blit_gif_delta(Image& target, const uint8_t* bgra, unsigned width, unsigned height,
                    unsigned left, unsigned top) {
    if (!target.valid() || !bgra || !width || !height ||
        static_cast<uint64_t>(left) + width > static_cast<uint64_t>(target.width()) ||
        static_cast<uint64_t>(top) + height > static_cast<uint64_t>(target.height()))
        return false;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const uint8_t* src = bgra + (static_cast<size_t>(y) * width + x) * 4;
            if (!src[3])
                continue;
            uint8_t* dest =
                target.pixels() + (static_cast<size_t>(top + y) * target.width() + left + x) * 4;
            dest[0] = src[0];
            dest[1] = src[1];
            dest[2] = src[2];
            dest[3] = 255;
        }
    return true;
}
Image compose(const Image& base, const std::vector<Mark>& marks, POINT screen_origin) {
    auto result = crop_image(base, {0, 0, base.width(), base.height()});
    if (!result.valid())
        return {};
    HDC dc = result.begin();
    if (!dc)
        return {};
    draw_marks(dc, marks, -1, screen_origin);
    GdiFlush();
    result.end(dc);
    return result;
}
bool copy_clipboard(HWND owner, const Image& im, std::wstring& error) {
    if (!im.valid()) {
        error = L"There is no image to copy.";
        return false;
    }
    size_t n = static_cast<size_t>(im.width()) * im.height() * 4,
           total = sizeof(BITMAPINFOHEADER) + n;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, total);
    if (!mem) {
        error = L"Not enough memory for the clipboard.";
        return false;
    }
    void* data = GlobalLock(mem);
    if (!data) {
        GlobalFree(mem);
        error = L"Could not prepare the clipboard.";
        return false;
    }
    BITMAPINFOHEADER hdr{};
    hdr.biSize = sizeof(hdr);
    hdr.biWidth = im.width();
    hdr.biHeight = -im.height();
    hdr.biPlanes = 1;
    hdr.biBitCount = 32;
    hdr.biCompression = BI_RGB;
    hdr.biSizeImage = static_cast<DWORD>(n);
    std::memcpy(data, &hdr, sizeof(hdr));
    std::memcpy(static_cast<BYTE*>(data) + sizeof(hdr), im.pixels(), n);
    auto* p = static_cast<BYTE*>(data) + sizeof(hdr);
    for (size_t i = 3; i < n; i += 4)
        p[i] = 255;
    GlobalUnlock(mem);
    bool opened = false;
    for (int attempt = 0; attempt < 25 && !opened; ++attempt) {
        opened = OpenClipboard(owner) != FALSE;
        if (!opened)
            Sleep(12);
    }
    if (!opened) {
        GlobalFree(mem);
        error = L"The clipboard is busy. Please try again.";
        return false;
    }
    bool ok = EmptyClipboard() && SetClipboardData(CF_DIB, mem);
    CloseClipboard();
    if (!ok) {
        GlobalFree(mem);
        error = L"Could not copy the image.";
    }
    return ok;
}
bool save_image(const Image& im, const std::wstring& path, bool jpeg, std::wstring& error) {
    if (!im.valid()) {
        error = L"There is no image to save.";
        return false;
    }
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        error = L"Could not prepare the output file.";
        return false;
    }
    wchar_t id[40]{};
    StringFromGUID2(guid, id, 40);
    std::wstring temp = path + L".drawscr-" + id + L".tmp";
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromFilename(temp.c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr))
        hr = factory->CreateEncoder(jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng,
                                    nullptr, &encoder);
    if (SUCCEEDED(hr))
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr))
        hr = encoder->CreateNewFrame(&frame, &options);
    if (SUCCEEDED(hr) && jpeg) {
        PROPBAG2 property{};
        property.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT v{};
        v.vt = VT_R4;
        v.fltVal = jpeg_quality;
        hr = options->Write(1, &property, &v);
    }
    if (SUCCEEDED(hr))
        hr = frame->Initialize(options.Get());
    if (SUCCEEDED(hr))
        hr = frame->SetSize(static_cast<UINT>(im.width()), static_cast<UINT>(im.height()));
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    if (SUCCEEDED(hr))
        hr = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(hr) && !IsEqualGUID(fmt, GUID_WICPixelFormat24bppBGR))
        hr = E_FAIL;
    size_t stride = static_cast<size_t>(im.width()) * 3;
    std::vector<BYTE> data;
    if (SUCCEEDED(hr)) {
        try {
            data.resize(stride * im.height());
        } catch (...) {
            hr = E_OUTOFMEMORY;
        }
        if (SUCCEEDED(hr))
            for (int y = 0; y < im.height(); ++y)
                for (int x = 0; x < im.width(); ++x) {
                    size_t si = (static_cast<size_t>(y) * im.width() + x) * 4,
                           di = static_cast<size_t>(y) * stride + x * 3;
                    data[di] = im.pixels()[si];
                    data[di + 1] = im.pixels()[si + 1];
                    data[di + 2] = im.pixels()[si + 2];
                }
    }
    if (SUCCEEDED(hr))
        hr = frame->WritePixels(static_cast<UINT>(im.height()), static_cast<UINT>(stride),
                                static_cast<UINT>(data.size()), data.data());
    if (SUCCEEDED(hr))
        hr = frame->Commit();
    if (SUCCEEDED(hr))
        hr = encoder->Commit();
    frame.Reset();
    options.Reset();
    encoder.Reset();
    stream.Reset();
    factory.Reset();
    if (SUCCEEDED(hr) && !MoveFileExW(temp.c_str(), path.c_str(),
                                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        hr = HRESULT_FROM_WIN32(GetLastError());
    if (FAILED(hr)) {
        DeleteFileW(temp.c_str());
        wchar_t msg[128]{};
        swprintf_s(msg, L"Could not save the image (0x%08lX).", static_cast<unsigned long>(hr));
        error = msg;
        return false;
    }
    return true;
}
} // namespace drawscr
