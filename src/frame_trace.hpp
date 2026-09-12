#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>

namespace nr
{
enum class TraceKind : unsigned { gate, evaluation, framegen_entry, framegen_exit };
struct FrameTraceEvent
{
    TraceKind kind = TraceKind::gate;
    std::uint64_t tick = 0;
    unsigned thread = 0;
    std::uint64_t frame = 0; // Gate only; never infer this from another thread.
    unsigned source = 0;
    bool retry = false;
    std::uint64_t result = 0; // Gate allow / NR wrapper return, NOT GPU completion.
    std::uint64_t command = 0, color = 0, output = 0;
    unsigned width = 0, height = 0, pass = 0;
    unsigned mfg_index = ~0u;
    std::uint64_t callback = 0, gap = 0;
    unsigned hook = 0, evaluations = 0, successes = 0;
    bool original_called = false;
};

// Explicit ten-second capture only. No allocations, blocking locks, formatting,
// disk I/O, or GPU commands on the observed rendering thread. Overflow is counted.
class FrameTrace
{
public:
    static constexpr unsigned capacity = 4096;
    bool start(std::uint64_t now)
    {
        if (!TryAcquireSRWLockExclusive(&lock_)) return false;
        const bool idle = end_.load(std::memory_order_relaxed) == 0 && read_ == count_;
        if (idle)
        {
            read_ = count_ = 0;
            dropped_.store(0, std::memory_order_relaxed);
            end_.store(now + 10000, std::memory_order_release);
        }
        ReleaseSRWLockExclusive(&lock_);
        return idle;
    }
    bool recording(std::uint64_t now) const
    {
        return now < end_.load(std::memory_order_acquire);
    }
    bool enabled() const { return end_.load(std::memory_order_relaxed) != 0; }
    void push(const FrameTraceEvent &event)
    {
        if (!recording(event.tick)) return;
        if (!TryAcquireSRWLockExclusive(&lock_))
        {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (recording(event.tick))
        {
            if (count_ < capacity) events_[count_++] = event;
            else dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        ReleaseSRWLockExclusive(&lock_);
    }
    // Called only by the serialized overlay. Drain after the capture ends, in
    // bounded batches; never hold the lock while emitting a log line.
    bool pop(std::uint64_t now, FrameTraceEvent *event)
    {
        if (recording(now) || !TryAcquireSRWLockExclusive(&lock_)) return false;
        end_.store(0, std::memory_order_release);
        const bool present = read_ < count_;
        if (present) *event = events_[read_++];
        ReleaseSRWLockExclusive(&lock_);
        return present;
    }
    unsigned dropped() const { return dropped_.load(std::memory_order_relaxed); }
private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    std::atomic_ullong end_ = 0;
    std::atomic_uint dropped_ = 0;
    unsigned count_ = 0, read_ = 0;
    std::array<FrameTraceEvent, capacity> events_ = {};
};
}
