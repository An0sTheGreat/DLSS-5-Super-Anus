#pragma once
#include <cstdint>
namespace nr {
enum class ActivityChange { none, stalled, resumed };
struct ActivityMonitor {
    std::uint32_t count = 0;
    std::uint64_t changed_at = 0;
    bool initialized = false, stalled = false;
    ActivityChange observe(std::uint64_t now, bool enabled, std::uint32_t current)
    {
        if (!initialized || !enabled || now < changed_at) {
            initialized = true; count = current; changed_at = now; stalled = false;
            return ActivityChange::none;
        }
        if (current != count) {
            const bool was_stalled = stalled;
            count = current; changed_at = now; stalled = false;
            return was_stalled ? ActivityChange::resumed : ActivityChange::none;
        }
        if (!stalled && now - changed_at >= 3000) {
            stalled = true;
            return ActivityChange::stalled;
        }
        return ActivityChange::none;
    }
};
}
