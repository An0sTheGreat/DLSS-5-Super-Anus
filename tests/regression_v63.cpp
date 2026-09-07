#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include "preset_section.hpp"
#include "native_gate_policy.hpp"
#include "neural_ui_layout.hpp"

static void test_presets()
{
    for (const std::string name : {"renodx", "RENODX-DLSS", "A-longer-runtime-addon-name"})
    {
        PresetStringView global;
        global.size = name.size();
        if (name.size() <= 15) std::memcpy(global.storage, name.c_str(), name.size() + 1);
        else
        {
            const char *pointer = name.c_str();
            std::memcpy(global.storage, &pointer, sizeof(pointer));
            global.capacity = name.size();
        }
        for (int cycle = 0; cycle < 12; ++cycle)
        {
            const int preset = cycle % 3 + 1;
            char buffer[128] = {};
            PresetStringView section;
            assert(make_preset_section(global, preset, buffer, section));
            assert(section.data() == buffer || section.data() == section.storage);
            assert(std::string(section.data()) == name + "-preset" + std::to_string(preset));
            assert(section.size == name.size() + 8);
        }
    }
    char buffer[128] = {};
    PresetStringView bad, section;
    assert(!make_preset_section(bad, 1, buffer, section));
    bad.size = 120;
    bad.capacity = 127;
    assert(!make_preset_section(bad, 1, buffer, section));
    bad = {};
    std::memcpy(bad.storage, "renodx", 7);
    bad.size = 6;
    assert(!make_preset_section(bad, 0, buffer, section));
    assert(!make_preset_section(bad, 4, buffer, section));
    std::puts("Preset namespace tests: repeated cycles, inline and external storage, bounds passed.");
}

static void test_gate()
{
    for (int original = 0; original < 2; ++original)
    for (int enabled = 0; enabled < 2; ++enabled)
    for (int loaded = 0; loaded < 2; ++loaded)
    for (int source = 0; source < 6; ++source)
    for (int retry = 0; retry < 2; ++retry)
    {
        const bool result = permit_native_evaluation(original != 0, enabled != 0,
            loaded != 0, static_cast<std::uint8_t>(source), retry != 0);
        const bool expected = original != 0;
        assert(result == expected);
    }
    std::puts("Gate policy: 192 combinations passed; legacy toggle never broadens upstream acceptance.");
}

static void assert_aligned(float x)
{
    assert(std::fabs(ImGui::GetItemRectMin().x - x) < 0.1f);
}

static void test_layout(float width, float font_scale, bool debug_open)
{
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(3440, 1440);
    io.DeltaTime = 1.0f / 60.0f;
    io.FontGlobalScale = font_scale;
    unsigned char *pixels;
    int font_width, font_height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &font_width, &font_height);
    for (int frame = 0; frame < 3; ++frame)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(57, 32));
        ImGui::SetNextWindowSize(ImVec2(width, 1326));
        ImGui::Begin("RenoDX DLSS", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted("Reference setting");
        const float root_x = ImGui::GetItemRectMin().x;
        ImGui::SetNextItemOpen(debug_open, ImGuiCond_Always);
        if (ImGui::TreeNode("Debug"))
        {
            ImGui::TextUnformatted("Debug counters");
            ImGui::TreePop();
            // Reproduce the observed leftover one-level negative line indent.
            ImGui::Unindent();
            ImGui::TextUnformatted("Old clipped line");
            assert(ImGui::GetItemRectMin().x < root_x);
        }
        begin_neural_controls_layout();
        ImGui::SeparatorText("Neural Rendering Performance");
        int scale = 75;
        ImGui::SliderInt("Neural Rendering Resolution", &scale, 50, 100);
        assert_aligned(root_x);
        ImGui::Button("Apply");
        assert_aligned(root_x);
        ImGui::TextWrapped("Applied: 75%%. Detail text must share the normal line origin.");
        assert_aligned(root_x);
        ImGui::SeparatorText("Controls");
        ImGui::TextUnformatted("Numpad * Cycle presets");
        assert_aligned(root_x);
        ImGui::EndGroup();
        // The following native sections also need the corrected line origin.
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        const bool links = ImGui::TreeNode("Links");
        assert(links);
        ImGui::Unindent();
        ImGui::Button("Discord");
        assert_aligned(root_x);
        ImGui::Indent();
        ImGui::TreePop();
        ImGui::TextUnformatted("About");
        assert_aligned(root_x);
        ImGui::TextUnformatted("Build: V6.3");
        assert_aligned(root_x);
        ImGui::End();
        ImGui::Render();
    }
    ImGui::DestroyContext();
}

int main()
{
    test_presets();
    test_gate();
    for (float width : {500.f, 763.f, 1100.f})
    for (float font_scale : {1.f, 1.5f, 2.f})
    for (bool debug_open : {false, true}) test_layout(width, font_scale, debug_open);
    std::puts("Actual ImGui layout: 18 width/font/Debug cases (54 frames) passed.");
}
