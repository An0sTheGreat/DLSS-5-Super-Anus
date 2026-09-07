#pragma once
#include <cstdint>

inline bool permit_native_evaluation(bool original_allowed, bool,
                                     bool, std::uint8_t, bool)
{
    // V6.3's replay bypass could repeat NR and alter the output. A saved legacy
    // toggle must never override upstream duplicate/stale-frame rejection.
    return original_allowed;
}
