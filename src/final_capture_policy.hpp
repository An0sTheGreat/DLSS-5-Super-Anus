#pragma once
#include <cstdint>
namespace nr {
struct FinalCaptureTiming {
    static constexpr std::uint64_t limit_ms = 500;
    std::uint64_t on_at = 0, deadline = 0;
    unsigned skipped_at = 0, success_at = 0;
    void start(std::uint64_t now, unsigned skipped, unsigned success) {
        on_at = now; deadline = now + limit_ms; skipped_at = skipped; success_at = success;
    }
    bool expired(std::uint64_t now) const { return deadline && now >= deadline; }
    bool ready(std::uint64_t now, unsigned skipped, unsigned success) const {
        return deadline && now > on_at && !expired(now) && skipped != skipped_at && success == success_at;
    }
};
}
