#pragma once
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace nr::screenshots {
struct RGB { float r, g, b; };
enum class Encoding { srgb, linear709, pq2020, scrgb };
inline float half_to_float(std::uint16_t h)
{
    const unsigned sign = static_cast<unsigned>(h & 0x8000) << 16;
    unsigned exponent = (h >> 10) & 31, mantissa = h & 1023;
    if (exponent == 31) return std::bit_cast<float>(sign | 0x7f800000u | (mantissa << 13));
    if (!exponent) {
        if (!mantissa) return std::bit_cast<float>(sign);
        int e = -14;
        while (!(mantissa & 1024)) { mantissa <<= 1; --e; }
        return std::bit_cast<float>(sign | (static_cast<unsigned>(e + 127) << 23) | ((mantissa & 1023) << 13));
    }
    return std::bit_cast<float>(sign | ((exponent + 112) << 23) | (mantissa << 13));
}
inline float unsigned_float(unsigned bits, unsigned mantissa_bits)
{
    const unsigned exponent = bits >> mantissa_bits, mantissa = bits & ((1u << mantissa_bits) - 1);
    if (exponent == 31) return 0.f;
    return std::ldexp(exponent ? 1.f + static_cast<float>(mantissa) / (1u << mantissa_bits) :
        static_cast<float>(mantissa) / (1u << mantissa_bits), exponent ? static_cast<int>(exponent) - 15 : -14);
}
// DXGI format numbers; independent of graphics headers for color-only tests.
inline unsigned pixel_bytes(unsigned format)
{
    switch (format) { case 2: return 16; case 10: return 8;
    case 24: case 26: case 28: case 29: case 87: case 91: return 4; default: return 0; }
}
inline RGB decode_pixel(const unsigned char *data, unsigned format)
{
    RGB c = {};
    if (format == 2) { std::memcpy(&c, data, sizeof(c)); }
    else if (format == 10) {
        std::uint16_t h[3]; std::memcpy(h, data, sizeof(h));
        c = {half_to_float(h[0]), half_to_float(h[1]), half_to_float(h[2])};
    } else if (format == 24 || format == 26) {
        std::uint32_t p; std::memcpy(&p, data, sizeof(p));
        c = format == 24 ? RGB{(p & 1023)/1023.f, ((p >> 10) & 1023)/1023.f, ((p >> 20) & 1023)/1023.f} :
            RGB{unsigned_float(p & 2047, 6), unsigned_float((p >> 11) & 2047, 6), unsigned_float(p >> 22, 5)};
    } else if (format == 28 || format == 29) c = {data[0]/255.f, data[1]/255.f, data[2]/255.f};
    else if (format == 87 || format == 91) c = {data[2]/255.f, data[1]/255.f, data[0]/255.f};
    if (!std::isfinite(c.r)) c.r = 0;
    if (!std::isfinite(c.g)) c.g = 0;
    if (!std::isfinite(c.b)) c.b = 0;
    return c;
}
inline float srgb_decode(float x) { return x <= .04045f ? x / 12.92f : std::pow((x + .055f) / 1.055f, 2.4f); }
inline float srgb_encode(float x) { return x <= .0031308f ? 12.92f*x : 1.055f*std::pow(x, 1.f/2.4f)-.055f; }
inline float pq_nits(float x)
{
    const float p = std::pow(std::clamp(x, 0.f, 1.f), 32.f/2523.f);
    return 10000.f * std::pow(std::max(p-3424.f/4096.f, 0.f) / std::max(2413.f/128.f-2392.f/128.f*p, 1e-6f), 16384.f/2610.f);
}
inline RGB linear709(RGB c, Encoding encoding)
{
    if (encoding == Encoding::srgb) return {srgb_decode(c.r), srgb_decode(c.g), srgb_decode(c.b)};
    if (encoding == Encoding::pq2020) {
        // scRGB convention: 1.0 = 80 nits, BT.709 primaries. Keep extended range.
        const float r = pq_nits(c.r)/80.f, g = pq_nits(c.g)/80.f, b = pq_nits(c.b)/80.f;
        return {1.660491f*r-.587641f*g-.072850f*b, -.124550f*r+1.132900f*g-.008349f*b,
            -.018151f*r-.100579f*g+1.118730f*b};
    }
    return c;
}
inline RGB png_rgb(RGB c, Encoding encoding, bool hdr, float sdr_white_nits = 80.f)
{
    if (encoding == Encoding::srgb && !hdr) return c;
    c = linear709(c, encoding);
    // Absolute display encodings only. Native SR scene-linear data has no
    // proven absolute luminance calibration and must not be normalized here.
    if (hdr && (encoding == Encoding::scrgb || encoding == Encoding::pq2020) &&
        std::isfinite(sdr_white_nits) && sdr_white_nits >= 80.f) {
        const float scale = 80.f / sdr_white_nits;
        c.r *= scale; c.g *= scale; c.b *= scale;
    }
    c.r = std::max(c.r, 0.f); c.g = std::max(c.g, 0.f); c.b = std::max(c.b, 0.f);
    // Luminance-preserving shoulder matching the reference's 0.95 / 0.05 split.
    // PNG-only SDR rendition; no claim of matching a display's peak tone map.
    if (hdr) {
        const float l = .2126f*c.r + .7152f*c.g + .0722f*c.b;
        if (l > .95f) {
            const float s = (.95f + .05f * (1.f-std::exp(-20.f*(l-.95f)))) / l;
            c.r *= s; c.g *= s; c.b *= s;
        }
    }
    return {srgb_encode(c.r), srgb_encode(c.g), srgb_encode(c.b)};
}
inline unsigned char to_byte(float x) { return static_cast<unsigned char>(std::lround(std::clamp(x, 0.f, 1.f)*255.f)); }
}
