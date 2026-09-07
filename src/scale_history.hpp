#pragma once
#include <array>
#include <cstdint>

namespace nr
{
// History belongs to an evaluation stream (device/pass), not a rotating work
// texture. Caller serializes access and commits only after successful recording.
class ScaleHistory
{
public:
    struct Entry { std::uintptr_t device = 0; unsigned pass = 0, generation = 0; };
    static constexpr unsigned capacity = 64;
    constexpr Entry *find(std::uintptr_t device, unsigned pass)
    {
        if (!device) return nullptr;
        for (auto &entry : entries_)
            if (entry.device == device && entry.pass == pass) return &entry;
        for (auto &entry : entries_)
            if (!entry.device) { entry = {device, pass, 0}; return &entry; }
        return nullptr;
    }
    constexpr void forget(std::uintptr_t device)
    {
        for (auto &entry : entries_) if (entry.device == device) entry = {};
    }
private:
    std::array<Entry, capacity> entries_ = {};
};
}
