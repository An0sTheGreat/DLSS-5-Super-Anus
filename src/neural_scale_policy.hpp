#pragma once
#include <algorithm>
#include <cstdint>

namespace nr
{
inline constexpr int minimum_scale_percent = 25;
inline constexpr int native_scale_percent = 100;
inline constexpr int maximum_scale_percent = 150;
inline constexpr std::uint32_t maximum_texture_extent = 16384;

inline constexpr int clamp_scale_percent(int scale)
{
    return std::clamp(scale, minimum_scale_percent, maximum_scale_percent);
}

inline constexpr bool uses_scaled_path(int scale)
{
    return scale != native_scale_percent;
}

inline constexpr std::uint32_t scaled_extent(std::uint32_t native_extent, int scale)
{
    const auto scaled = (static_cast<std::uint64_t>(native_extent) *
        static_cast<unsigned>(clamp_scale_percent(scale)) + 50u) / 100u;
    return std::max(2u, static_cast<std::uint32_t>(scaled) & ~1u);
}

inline constexpr unsigned input_resample_filter(int scale)
{
    return scale > native_scale_percent ? 3u : 0u; // bilinear upscale / area downscale
}
}
