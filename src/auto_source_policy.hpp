#pragma once
#include <atomic>
#include <cstdint>

namespace nr {
// Once Auto loses its FG producer, keep the native source for this process.
// An epoch prevents an enter/leave ABA race while native ownership is claimed.
// No waits, resource retention, frame replay or timer-based GPU retirement.
class AutoSourcePolicy {
    static constexpr std::uint64_t fallback_bit = 1ull << 63;
    static constexpr std::uint64_t active_mask = 0xffff;
    static constexpr std::uint64_t epoch_step = 1ull << 16;
    std::atomic_uint64_t state_{0}, last_frame_{0}, last_work_{0}, first_native_{0};
    static void raise(std::atomic_uint64_t &value, std::uint64_t next) {
        auto old = value.load();
        while (old < next && !value.compare_exchange_weak(old,next)) {}
    }
public:
    bool fallback() const { return (state_.load() & fallback_bit) != 0; }
    bool enter_other(std::uint64_t frame) {
        auto old = state_.load();
        do {
            if ((old & fallback_bit) || (old & active_mask) == active_mask) return false;
        } while (!state_.compare_exchange_weak(old,old+1));
        raise(last_frame_,frame);
        return true;
    }
    void leave_other(std::uint64_t now, bool worked) {
        if (worked) raise(last_work_,now);
        // Saturating epoch: do not let its carry manufacture a fallback state.
        auto old = state_.load();
        std::uint64_t next;
        do {
            const auto epoch = old & ~(fallback_bit | active_mask);
            const auto advanced = epoch < fallback_bit-epoch_step ? epoch+epoch_step : epoch;
            next = advanced | ((old & active_mask)-1);
        } while (!state_.compare_exchange_weak(old,next));
    }
    bool select_native(std::uint64_t frame, std::uint64_t now) {
        if (!frame || !now) return false;
        auto old = state_.load();
        if (old & fallback_bit) return true;
        std::uint64_t zero = 0;
        first_native_.compare_exchange_strong(zero,now);
        const auto first = first_native_.load();
        const auto work = last_work_.load();
        const auto since = work > first ? work : first;
        if ((old & active_mask) || old >= fallback_bit-epoch_step || now < since || now-since < 750 || frame <= last_frame_.load()) return false;
        return state_.compare_exchange_strong(old,old | fallback_bit);
    }
};
}
