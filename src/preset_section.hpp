#pragma once
#include <cstddef>
#include <cstring>

// Read-only MSVC std::string ABI view. LoadSetting only calls c_str(); this
// object must never be passed to a host function that mutates or destroys it.
struct PresetStringView
{
    char storage[16] = {};
    std::size_t size = 0;
    std::size_t capacity = 15;

    const char *data() const
    {
        if (capacity <= 15) return storage;
        const char *pointer = nullptr;
        std::memcpy(&pointer, storage, sizeof(pointer));
        return pointer;
    }
};
static_assert(sizeof(PresetStringView) == 32);

inline bool make_preset_section(const PresetStringView &global_name, int preset,
                               char (&buffer)[128], PresetStringView &section)
{
    if (preset < 1 || preset > 3 || global_name.size == 0 ||
        global_name.size > sizeof(buffer) - 9 || global_name.size > global_name.capacity)
        return false;
    const char *name = global_name.data();
    if (name == nullptr) return false;
    std::memcpy(buffer, name, global_name.size);
    std::memcpy(buffer + global_name.size, "-preset1", 9);
    buffer[global_name.size + 7] = static_cast<char>('0' + preset);
    section = {};
    section.size = global_name.size + 8;
    if (section.size <= 15)
        std::memcpy(section.storage, buffer, section.size + 1);
    else
    {
        const char *pointer = buffer;
        std::memcpy(section.storage, &pointer, sizeof(pointer));
        section.capacity = sizeof(buffer) - 1;
    }
    return true;
}
