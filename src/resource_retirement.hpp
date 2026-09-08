#pragma once
#include <cstdint>
#include "neural_scale_policy.hpp"

// Reset events are PRE-Reset. Do not discard a potentially executable recording
// until a subsequent recording command confirms the application began a new one.
struct RecordingReferences
{
    std::uint64_t sets = 0;
    bool reset_pending = false;
    void resetting() { reset_pending = true; }
    void recording()
    {
        if (reset_pending) { sets = 0; reset_pending = false; }
    }
};

inline bool retirement_candidate(bool valid, bool nr_enabled, int scale,
                                 unsigned generation, unsigned current_generation,
                                 std::uint64_t last_use, std::uint64_t now)
{
    return !valid || !nr_enabled || !nr::uses_scaled_path(scale) || generation != current_generation ||
        (now >= last_use && now - last_use >= 2000);
}

inline bool allocation_fits(std::uint64_t used, std::uint64_t requested, std::uint64_t budget)
{
    return requested != 0 && used <= budget && requested <= budget - used;
}

inline bool fence_completed(std::uint64_t completed, std::uint64_t required)
{
    // D3D12 reports UINT64_MAX on device removal, not successful completion.
    return completed != UINT64_MAX && completed >= required;
}
