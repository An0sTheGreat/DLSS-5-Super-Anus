#pragma once
#include <Windows.h>
#include <objidl.h>
#include <wincodec.h>
#include <cstdio>
#include "screenshot_pixels.hpp"
namespace nr::screenshots {
struct Image {
    const unsigned char *pixels;
    unsigned width, height, pitch, format;
    Encoding encoding;
    float sdr_white_nits = 80.f;
};
inline bool write_all(HANDLE file, const void *data, unsigned bytes)
{
    DWORD written = 0;
    return WriteFile(file, data, bytes, &written, nullptr) && written == bytes;
}
// Writes only a caller-owned NEW path. Never replaces an existing screenshot.
inline bool write_png(const wchar_t *path, const Image &image, bool hdr)
{
    if (!image.pixels || !image.width || !image.height || image.width > 16384 || image.height > 16384 ||
        !pixel_bytes(image.format) || image.pitch < image.width * pixel_bytes(image.format)) return false;
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) return false;
    IWICImagingFactory *factory = nullptr; IWICBitmapEncoder *encoder = nullptr;
    IWICBitmapFrameEncode *frame = nullptr; IStream *stream = nullptr;
    IPropertyBag2 *properties = nullptr; HGLOBAL memory = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;
    auto *row = static_cast<unsigned char *>(HeapAlloc(GetProcessHeap(), 0, image.width * 3));
    bool ok = row && SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))) && SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, &properties)) && SUCCEEDED(frame->Initialize(properties)) &&
        SUCCEEDED(frame->SetSize(image.width, image.height));
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    ok = ok && SUCCEEDED(frame->SetPixelFormat(&format)) && IsEqualGUID(format, GUID_WICPixelFormat24bppBGR);
    for (unsigned y = 0; ok && y < image.height; ++y) {
        for (unsigned x = 0; x < image.width; ++x) {
            const auto c = png_rgb(decode_pixel(image.pixels + static_cast<std::size_t>(y)*image.pitch +
                x*pixel_bytes(image.format), image.format), image.encoding, hdr, image.sdr_white_nits);
            row[x*3] = to_byte(c.b); row[x*3+1] = to_byte(c.g); row[x*3+2] = to_byte(c.r);
        }
        ok = SUCCEEDED(frame->WritePixels(1, image.width*3, image.width*3, row));
    }
    ok = ok && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()) &&
        SUCCEEDED(GetHGlobalFromStream(stream, &memory));
    STATSTG stats = {};
    ok = ok && SUCCEEDED(stream->Stat(&stats, STATFLAG_NONAME)) && !stats.cbSize.HighPart;
    if (ok) {
        file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        void *bytes = GlobalLock(memory);
        ok = file != INVALID_HANDLE_VALUE && bytes && write_all(file, bytes, stats.cbSize.LowPart);
        if (bytes) GlobalUnlock(memory);
    }
    if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); if (!ok) DeleteFileW(path); }
    if (row) HeapFree(GetProcessHeap(), 0, row);
    if (properties) properties->Release(); if (frame) frame->Release();
    if (encoder) encoder->Release(); if (stream) stream->Release(); if (factory) factory->Release();
    if (SUCCEEDED(apartment)) CoUninitialize();
    return ok;
}
// OpenEXR v2 uncompressed scanlines, float RGB, linear BT.709. Negative and
// >1.0 values survive; no SDR tone mapping is applied to the master.
inline bool write_exr(const wchar_t *path, const Image &image)
{
    if (!image.pixels || !image.width || !image.height || image.width > 16384 || image.height > 16384 ||
        !pixel_bytes(image.format) || image.pitch < image.width*pixel_bytes(image.format)) return false;
    unsigned char header[1024] = {}; unsigned used = 0;
    auto append = [&](const void *p, unsigned n) { std::memcpy(header+used, p, n); used += n; };
    auto string = [&](const char *s) { append(s, static_cast<unsigned>(std::strlen(s)+1)); };
    auto attribute = [&](const char *name, const char *type, const void *p, unsigned n) {
        string(name); string(type); append(&n, 4); append(p,n);
    };
    const std::uint32_t magic = 20000630, version = 2; append(&magic,4); append(&version,4);
    unsigned char channels[55] = {}; unsigned offset = 0;
    for (char name : {'B','G','R'}) {
        channels[offset] = static_cast<unsigned char>(name);
        const unsigned pixel_type = 2, sampling = 1;
        std::memcpy(channels+offset+2,&pixel_type,4);
        std::memcpy(channels+offset+10,&sampling,4); std::memcpy(channels+offset+14,&sampling,4);
        offset += 18;
    }
    attribute("channels","chlist",channels,55);
    const unsigned char zero = 0; attribute("compression","compression",&zero,1);
    const int bounds[] = {0,0,static_cast<int>(image.width)-1,static_cast<int>(image.height)-1};
    attribute("dataWindow","box2i",bounds,16); attribute("displayWindow","box2i",bounds,16);
    attribute("lineOrder","lineOrder",&zero,1);
    const float one = 1.f, center[] = {0.f,0.f};
    attribute("pixelAspectRatio","float",&one,4); attribute("screenWindowCenter","v2f",center,8);
    attribute("screenWindowWidth","float",&one,4);
    const float chromaticities[] = {.64f,.33f,.30f,.60f,.15f,.06f,.3127f,.3290f};
    attribute("chromaticities","chromaticities",chromaticities,32);
    // Scene-linear native SR has no established absolute luminance scale. Only
    // display-referred scRGB (including converted PQ) has the 80-nit convention.
    if (image.encoding == Encoding::pq2020 || image.encoding == Encoding::scrgb) {
        const float white = 80.f; attribute("whiteLuminance","float",&white,4);
    }
    append(&zero,1);
    HANDLE file = CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    auto *row = static_cast<float *>(HeapAlloc(GetProcessHeap(),0,image.width*12));
    bool ok = row && write_all(file,header,used);
    const unsigned bytes = image.width*12;
    for (unsigned y = 0; ok && y < image.height; ++y) {
        const std::uint64_t position = used + static_cast<std::uint64_t>(image.height)*8 +
            static_cast<std::uint64_t>(y)*(bytes+8);
        ok = write_all(file,&position,8);
    }
    for (unsigned y = 0; ok && y < image.height; ++y) {
        for (unsigned x = 0; x < image.width; ++x) {
            const auto c = linear709(decode_pixel(image.pixels + static_cast<std::size_t>(y)*image.pitch +
                x*pixel_bytes(image.format), image.format), image.encoding);
            row[x] = c.b; row[image.width+x] = c.g; row[image.width*2+x] = c.r;
        }
        ok = write_all(file,&y,4) && write_all(file,&bytes,4) && write_all(file,row,bytes);
    }
    if (row) HeapFree(GetProcessHeap(),0,row);
    CloseHandle(file); if (!ok) DeleteFileW(path);
    return ok;
}
}
