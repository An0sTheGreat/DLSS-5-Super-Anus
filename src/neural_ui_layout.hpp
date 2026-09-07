#pragma once
#include <imgui.h>

inline void begin_neural_controls_layout()
{
    ImGui::NewLine();
    // Normalize the line origin, not just one widget's cursor. Debug's custom
    // tree can leave a negative indent; SetCursorPosX alone cannot fix later
    // ItemSize/NewLine calls or the stock Links/About sections.
    const float root_x = ImGui::GetCursorStartPos().x + ImGui::GetScrollX();
    const float correction = root_x - ImGui::GetCursorPosX();
    if (correction != 0.0f) ImGui::Indent(correction);
    ImGui::BeginGroup();
}
