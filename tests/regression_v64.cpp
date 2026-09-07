#include <cassert>
#include <cstdio>
#include <array>
#include "resource_retirement.hpp"
#include "native_feature_slots.hpp"
#include "native_gate_policy.hpp"

int main()
{
    for (int allowed = 0; allowed < 2; ++allowed)
    for (int enabled = 0; enabled < 2; ++enabled)
    for (int loaded = 0; loaded < 2; ++loaded)
    for (int source = 0; source < 6; ++source)
    for (int retry = 0; retry < 2; ++retry)
        assert(permit_native_evaluation(allowed != 0, enabled != 0, loaded != 0,
            static_cast<std::uint8_t>(source), retry != 0) == (allowed != 0));

    RecordingReferences recording;
    recording.sets = 3;
    recording.resetting();
    assert(recording.sets == 3); // Failed Reset/no subsequent recording: still pinned.
    recording.resetting();
    assert(recording.sets == 3);
    recording.recording();
    assert(recording.sets == 0 && !recording.reset_pending);
    recording.sets = 4;
    recording.recording();
    assert(recording.sets == 4); // Additional binds in same recording preserve pins.
    RecordingReferences primary, secondary;
    secondary.sets = 8;
    primary.sets |= secondary.sets;
    secondary.resetting(); secondary.recording();
    assert(primary.sets == 8); // Primary can still execute an older bundle.

    assert(!retirement_candidate(true, true, 75, 1, 1, 100, 2099));
    assert(retirement_candidate(true, true, 75, 1, 1, 100, 2100));
    assert(retirement_candidate(false, true, 75, 1, 1, 100, 101));
    assert(retirement_candidate(true, false, 75, 1, 1, 100, 101));
    assert(retirement_candidate(true, true, 100, 1, 1, 100, 101));
    assert(retirement_candidate(true, true, 75, 1, 2, 100, 101));
    // Native feature lifetimes use 99 for the scale predicate: native 100% NR
    // remains active and must not be repeatedly destroyed/re-created.
    assert(!retirement_candidate(true, true, 99, 1, 1, 100, 101));

    constexpr std::uint64_t budget = 512ull << 20;
    assert(allocation_fits(0, budget, budget));
    assert(!allocation_fits(1, budget, budget));
    assert(!allocation_fits(UINT64_MAX, 1, budget));
    assert(!allocation_fits(0, 0, budget));
    assert(!fence_completed(7, 8));
    assert(fence_completed(8, 8));
    assert(!fence_completed(UINT64_MAX, 8));
    std::array<std::uint64_t, 2> completed{5, 9}, required{6, 9};
    assert(!(fence_completed(completed[0], required[0]) && fence_completed(completed[1], required[1])));
    completed[0] = 6;
    assert(fence_completed(completed[0], required[0]) && fence_completed(completed[1], required[1]));

    assert(feature_slot_count(0, 0, 0) == 0);
    assert(feature_slot_count(0, 48, 48) == SIZE_MAX);
    assert(feature_slot_count(100, 148, 196) == 1);
    assert(feature_slot_count(100, 149, 196) == SIZE_MAX);
    assert(feature_slot_count(100, 196, 148) == SIZE_MAX);
    assert(feature_slot_count(100, 100 + 65 * 48, 100 + 65 * 48) == SIZE_MAX);
    NativeFeatureSlot slot;
    assert(slot.handle == nullptr && slot.parameters == nullptr);
    assert(slot.performance == 3 && slot.preset == 1 && slot.dirty == 1 && slot.valid == 1);

    // Exercise 10,000 scale/pass-generation changes with recording pins,
    // asynchronous completion, and the same byte-budget admission predicate.
    std::uint64_t used = 0;
    for (unsigned generation = 1; generation <= 10000; ++generation)
    {
        const auto allocation = static_cast<std::uint64_t>(generation % 100 + 1) << 20;
        assert(allocation_fits(used, allocation, budget));
        used += allocation;
        recording.sets = 1;
        recording.resetting();
        assert(recording.sets != 0); // Cannot retire merely because settings changed.
        recording.recording();
        assert(retirement_candidate(true, true, 75, generation, generation + 1, 0, 1));
        assert(!fence_completed(generation - 1, generation));
        assert(fence_completed(generation, generation));
        used -= allocation;
        assert(used == 0);
    }
    std::puts("V6.4: gate, recording pins, budget, independent queue fences, native-slot bounds and 10,000 policy cycles passed (simulation, not game GPU testing).");
}
