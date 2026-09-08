#pragma once
#include <array>
#include <cstddef>

namespace nr {
inline constexpr std::array<int, 5> default_keys = {
    0x75, 0x76, 0x74, 0xBB, 0xBD}; // F6, F7, F5, =/+, -/_
inline bool valid_binding(int key) { return key >= 8 && key <= 254 && key != 27; }
template <std::size_t Count>
inline bool binding_conflict(const std::array<int, Count> &keys, std::size_t action, int key)
{
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (i != action && keys[i] == key) return true;
    return false;
}
inline std::array<int, 5> resolve_bindings(const std::array<int, 5> &saved)
{
    std::array<int, 5> keys = {};
    // Explicit saved keys take priority over new defaults (including an old
    // customized F7 toggle). Missing keys must not erase those preferences.
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (valid_binding(saved[i]) && !binding_conflict(keys, i, saved[i])) keys[i] = saved[i];
    for (std::size_t i = 0; i < keys.size(); ++i) if (!keys[i]) {
        if (!binding_conflict(keys, i, default_keys[i])) keys[i] = default_keys[i];
        else {
            for (int fallback : {0x6a, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C})
                if (!binding_conflict(keys, i, fallback)) { keys[i] = fallback; break; }
        }
    }
    return keys;
}
struct KeyEdges {
    std::array<bool, 5> previous = {};
    bool release_required = true;
    std::array<bool, 5> update(const std::array<bool, 5> &down, bool allowed)
    {
        std::array<bool, 5> pressed = {};
        if (!allowed) release_required = true;
        if (allowed && !release_required)
            for (std::size_t i = 0; i < down.size(); ++i) pressed[i] = down[i] && !previous[i];
        previous = down;
        bool any_down = false;
        for (bool state : down) any_down |= state;
        if (allowed && !any_down) release_required = false;
        return pressed;
    }
};
}
