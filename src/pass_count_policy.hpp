#pragma once
#include <algorithm>

namespace nr
{
inline constexpr unsigned minimum_pass_count = 1;
inline constexpr unsigned maximum_pass_count = 10;

inline constexpr unsigned clamp_pass_count(unsigned passes)
{
    return std::clamp(passes, minimum_pass_count, maximum_pass_count);
}

inline constexpr unsigned adjust_pass_count(unsigned passes, int direction)
{
    passes = clamp_pass_count(passes);
    if (direction > 0 && passes < maximum_pass_count) return passes + 1;
    if (direction < 0 && passes > minimum_pass_count) return passes - 1;
    return passes;
}
}
