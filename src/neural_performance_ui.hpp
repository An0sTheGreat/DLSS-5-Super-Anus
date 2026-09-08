#pragma once
#include <imgui.h>
#include "neural_scale_policy.hpp"

struct NeuralResolveControls { int mode = 1, transfer = 100, color = 100; };
struct NeuralPerformanceEdits { bool pending_changed, apply, sharpness_changed, resolve_changed; };

inline NeuralPerformanceEdits draw_neural_performance_section(
    bool available, int &pending, int applied, int &sharpness, NeuralResolveControls *resolve = nullptr)
{
    ImGui::SeparatorText("Neural Rendering Performance");
    ImGui::BeginDisabled(!available);
    NeuralPerformanceEdits edits = {};
    edits.pending_changed = ImGui::SliderInt("Neural Rendering Resolution", &pending,
        nr::minimum_scale_percent, nr::maximum_scale_percent, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    edits.apply = ImGui::Button("Apply##NeuralRenderingResolution") && pending != applied;
    ImGui::SameLine();
    ImGui::Text("Applied: %d%%", applied);
    ImGui::BeginDisabled(applied == nr::native_scale_percent);
    if (resolve)
    {
        edits.resolve_changed = ImGui::Combo("Reconstruction Mode", &resolve->mode,
            "Direct Reconstruction\0Matched Residual\0");
        edits.resolve_changed |= ImGui::SliderInt("Neural Transfer Strength", &resolve->transfer,
            0, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
        edits.resolve_changed |= ImGui::SliderInt("Neural Color Strength", &resolve->color,
            0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    }
    edits.sharpness_changed = ImGui::SliderInt("Reconstruction Sharpness", &sharpness,
        0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::TextWrapped("Reconstruction controls apply below or above 100%%. At 100%% the native RenoDX output is intentionally unchanged.");
    return edits;
}
