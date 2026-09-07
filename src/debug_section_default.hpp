#pragma once
#include <imgui.h>
#include <cstring>

inline void apply_debug_section_default(const char *section, std::size_t length)
{
    // Once per ImGui node/context, not every frame: users can still open each.
    if (section && length == 5 && (std::memcmp(section, "Debug", 5) == 0 ||
        std::memcmp(section, "Links", 5) == 0 || std::memcmp(section, "About", 5) == 0))
        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
}
