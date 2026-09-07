#pragma once
#include <cstdint>
#include <cstddef>

// Verified POD snapshot layout in official image SHA256 1d855cf2...d27e9.
// The host owns both vectors. Never free/reallocate their backing storage.
struct NativeFeatureSlot
{
    void *parameters = nullptr;
    void *handle = nullptr;
    std::uint32_t width = 0, height = 0;
    std::uint32_t performance = 3, preset = 1;
    std::uint64_t revision = 0;
    std::uint8_t dirty = 1, ui = 0, valid = 1;
    std::uint8_t padding[5] = {};
};
static_assert(sizeof(NativeFeatureSlot) == 0x30);
static_assert(offsetof(NativeFeatureSlot, valid) == 0x2a);

inline std::size_t feature_slot_count(std::uintptr_t begin, std::uintptr_t end,
                                     std::uintptr_t capacity)
{
    if (begin == 0) return end == 0 && capacity == 0 ? 0 : SIZE_MAX;
    if (end < begin || capacity < end || (end - begin) % sizeof(NativeFeatureSlot) != 0 ||
        (capacity - begin) % sizeof(NativeFeatureSlot) != 0 ||
        (end - begin) / sizeof(NativeFeatureSlot) > 64) return SIZE_MAX;
    return (end - begin) / sizeof(NativeFeatureSlot);
}
