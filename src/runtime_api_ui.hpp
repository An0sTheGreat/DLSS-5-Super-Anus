#pragma once
#include <imgui.h>
#include "backends/backend_support.hpp"

// Called after the shared root-alignment correction, outside Debug's tree.
// Starts collapsed; normal user expansion persists. No style/indent or
// disabled-state changes escape this section.
inline void draw_runtime_api_section(const nr::backends::RuntimeStatus &status)
{
    if (ImGui::TreeNodeEx("Runtime API", ImGuiTreeNodeFlags_SpanFullWidth))
    {
        ImGui::TextWrapped("Presentation API (last observed): %s", status.presentation_name);
        ImGui::TextWrapped("NR backend: %s", status.evaluation_state);
        if (status.detail[0] != '\0') ImGui::TextWrapped("%s", status.detail);
        ImGui::TreePop();
    }
    ImGui::Spacing();
}
