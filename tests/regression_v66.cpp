#define NOMINMAX
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <imgui_internal.h>
#include "neural_ui_layout.hpp"
#include "runtime_api_ui.hpp"
#include "debug_section_default.hpp"
#include "neural_performance_ui.hpp"
#include "neural_scale_policy.hpp"

static void test_backend_status()
{
    using namespace nr::backends;
    using reshade::api::device_api;
#ifdef NR_DX11_GAME_TEST
    assert(!support(device_api::d3d11).evaluator_available); // Never DX12 resource dispatch.
    assert(!support(device_api::vulkan).evaluator_available);
    assert(std::strstr(runtime_status(device_api::d3d11, false, true).evaluation_state,
                       "DX11 native SR bridge"));
#endif
    EvaluationDevice device;
    assert(!device.observed());
    device.observe(0, 1);
    device.observe(1, 0);
    device.observe(1, 0x100); // Wrapper success is the low byte, not the full word.
    assert(!device.observed());
    device.observe(1, 1);
    assert(device.observed());
    device.forget(2); // Teardown of an unrelated device must not erase the state.
    assert(device.observed());
    device.observe(2, 1);
    device.forget(1);
    assert(device.observed());
    device.forget(2);
    assert(!device.observed());

    for (auto api : {device_api::d3d12, device_api::d3d11, device_api::d3d9,
                    device_api::vulkan, device_api::opengl, static_cast<device_api>(0)})
    {
        for (bool observed : {false, true})
        {
            const auto status = runtime_status(api, observed, true);
            assert(status.controls_available == (api == device_api::d3d12 || observed));
            assert(!runtime_status(api, observed, false).controls_available);
        }
    }
}

static void test_layout(float width, float font_scale, bool advanced_open,
                        bool debug_open, bool runtime_open, bool observed)
{
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(3440, 1440); io.DeltaTime = 1.0f / 60.0f;
    io.FontGlobalScale = font_scale;
    unsigned char *pixels; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    for (int frame = 0; frame < 3; ++frame)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(57, 32));
        ImGui::SetNextWindowSize(ImVec2(width, 1326));
        ImGui::Begin("RenoDX DLSS_A", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::LogToBuffer(0);
        ImGui::TextUnformatted("Reference setting");
        const float root_x = ImGui::GetItemRectMin().x;
        ImGui::SetNextItemOpen(advanced_open, ImGuiCond_Always);
        if (ImGui::TreeNode("Advanced")) { ImGui::TextUnformatted("Pass Count"); ImGui::TreePop(); }
        const float advanced_end = ImGui::GetCursorPosY();
        begin_neural_controls_layout();
        const auto status = nr::backends::runtime_status(reshade::api::device_api::d3d11, observed, true);
        const float alpha = ImGui::GetStyle().Alpha;
        const int depth = ImGui::GetCurrentWindow()->DC.TreeDepth;
        int pending = 75, sharpness = 25;
        NeuralResolveControls resolve;
        const auto edits = draw_neural_performance_section(status.controls_available, pending, 100, sharpness, &resolve);
        assert(!edits.pending_changed && !edits.apply && !edits.sharpness_changed);
        assert(pending == 75 && sharpness == 25);
        assert(std::fabs(ImGui::GetItemRectMin().x - root_x) < 0.1f);
        assert(ImGui::GetStyle().Alpha == alpha && ImGui::GetCurrentWindow()->DC.TreeDepth == depth);
        assert(ImGui::GetCursorPosY() > advanced_end);
        ImGui::EndGroup();
        const float performance_end = ImGui::GetCursorPosY();
        apply_debug_section_default("Debug", 5);
        ImGui::SetNextItemOpen(debug_open, ImGuiCond_Always);
        if (ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextUnformatted("Debug counters");
            ImGui::TreePop(); ImGui::Unindent(); // reproduce the stock bad indent
        }
        assert(ImGui::GetCursorPosY() > performance_end);
        const float debug_end = ImGui::GetCursorPosY();
        begin_neural_controls_layout();
        ImGui::SetNextItemOpen(runtime_open, ImGuiCond_Always);
        draw_runtime_api_section(status);
        assert(ImGui::GetCursorPosY() > debug_end);
        assert(ImGui::GetStyle().Alpha == alpha && ImGui::GetCurrentWindow()->DC.TreeDepth == depth);
        ImGui::SeparatorText("Controls");
        assert(std::fabs(ImGui::GetItemRectMin().x - root_x) < 0.1f);
        ImGui::TextUnformatted("Numpad / and *");
        assert((ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) == 0);
        ImGui::EndGroup();
        ImGui::Button("Discord");
        assert(std::fabs(ImGui::GetItemRectMin().x - root_x) < 0.1f);
        ImGui::TextUnformatted("About / Build");
        assert(std::fabs(ImGui::GetItemRectMin().x - root_x) < 0.1f);
        const char *log = ImGui::GetCurrentContext()->LogBuffer.c_str();
        const char *advanced = std::strstr(log, "Advanced");
        const char *performance = std::strstr(log, "Neural Rendering Performance");
        const char *debug = std::strstr(log, "Debug");
        const char *runtime = std::strstr(log, "Runtime API");
        const char *controls = std::strstr(log, "Numpad / and *");
        assert(advanced && performance && debug && runtime && controls);
        assert(advanced < performance && performance < debug && debug < runtime && runtime < controls);
        assert((std::strstr(log, "Presentation API (last observed): DX11") != nullptr) == runtime_open);
        assert(std::strstr(log, "Applied: 100%"));
        for (const char *removed : {"Moving the slider", "XeFG native-input compatibility",
             "Working textures:", "Capture 10-second", "Integrated DX11 game test:"})
            assert(!std::strstr(log, removed));
        ImGui::LogFinish(); ImGui::End(); ImGui::Render();
    }
    ImGui::DestroyContext();
}

static void test_sharpness_interaction(bool available, int applied, int staged)
{
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1000, 700); io.DeltaTime = 1.f / 60.f;
    unsigned char *pixels; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    int pending = staged, sharpness = 25;
    NeuralResolveControls resolve;
    ImVec2 target;
    bool changed = false;
    for (int frame = 0; frame < 3; ++frame)
    {
        if (frame > 0)
        {
            io.AddMousePosEvent(target.x, target.y);
            io.AddMouseButtonEvent(0, frame == 1);
        }
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(20, 20));
        ImGui::SetNextWindowSize(ImVec2(750, 500));
        ImGui::Begin("Sharpness interaction", nullptr, ImGuiWindowFlags_NoSavedSettings);
        const float root_x = ImGui::GetCursorScreenPos().x;
        const float alpha = ImGui::GetStyle().Alpha;
        const auto edits = draw_neural_performance_section(available, pending, applied, sharpness, &resolve);
        // The final explanation follows the sharpness slider by ItemSpacing.y.
        target = ImVec2(root_x + 300.f, ImGui::GetItemRectMin().y -
            ImGui::GetStyle().ItemSpacing.y - ImGui::GetFrameHeight() * .5f);
        changed |= edits.sharpness_changed;
        assert(!edits.pending_changed && !edits.apply && pending == staged);
        assert(ImGui::GetStyle().Alpha == alpha);
        assert((ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) == 0);
        ImGui::End(); ImGui::Render();
    }
    const bool enabled = available && applied != 100;
    assert(changed == enabled);
    assert((sharpness != 25) == enabled);
    ImGui::DestroyContext();
}

int main()
{
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; ImGui::GetIO().LogFilename = nullptr;
    ImGui::GetIO().DisplaySize = ImVec2(800, 600);
    unsigned char *pixels; int atlas_width, atlas_height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);
    for (int frame = 0; frame < 4; ++frame)
    {
        ImGui::NewFrame(); ImGui::Begin("Debug default test");
        apply_debug_section_default("Debug", 5);
        // Emulate a user opening the node after its first appearance.
        if (frame == 1) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        const bool opened = ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_DefaultOpen);
        assert(opened == (frame > 0));
        if (opened) ImGui::TreePop();
        for (const char *section : {"Links", "About"}) {
            apply_debug_section_default(section, 5);
            if (frame == 1) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            const bool section_open = ImGui::TreeNodeEx(section, ImGuiTreeNodeFlags_DefaultOpen);
            assert(section_open == (frame > 0));
            if (section_open) ImGui::TreePop();
        }
        apply_debug_section_default("Advanced", 8);
        assert(ImGui::TreeNodeEx("Advanced", ImGuiTreeNodeFlags_DefaultOpen)); ImGui::TreePop();
        if (frame == 1) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        draw_runtime_api_section(nr::backends::runtime_status(reshade::api::device_api::d3d11, true, true));
        assert(ImGui::GetStateStorage()->GetInt(ImGui::GetID("Runtime API"), 0) == (frame > 0));
        ImGui::End(); ImGui::Render();
    }
    ImGui::DestroyContext();
    std::puts("Debug/Runtime API/Links/About: initially collapsed, user expansion retained; Advanced unchanged.");
    test_backend_status();
    for (bool available : {false, true})
    for (int applied = 25; applied <= 150; ++applied)
    for (int staged : {25, 99, 100, 101, 150}) test_sharpness_interaction(available, applied, staged);
    assert(nr::clamp_scale_percent(0) == 25 && nr::clamp_scale_percent(999) == 150);
    assert(!nr::uses_scaled_path(100) && nr::uses_scaled_path(99) && nr::uses_scaled_path(101));
    assert(!nr::uses_evaluation_working_path(100, 0));
    for (unsigned pass = 1; pass < 10; ++pass) assert(nr::uses_evaluation_working_path(100, pass));
    assert(nr::scaled_extent(3840,150) == 5760 && nr::scaled_extent(2160,25) == 540);
    assert(nr::motion_resample_filter(0) == 3);
    for (unsigned pass = 1; pass < 10; ++pass) assert(nr::motion_resample_filter(pass) == 5);
    std::puts("Sharpness: 1260 click cases passed; only applied 100 disabled, 25-99 and 101-150 enabled when available; staged scale ignored; scale policy passed.");
    for (float width : {500.f, 763.f, 1100.f})
    for (float scale : {1.f, 1.5f, 2.f})
    for (bool advanced : {false, true})
    for (bool debug : {false, true})
    for (bool runtime : {false, true})
    for (bool observed : {false, true}) test_layout(width, scale, advanced, debug, runtime, observed);
    std::puts("V6.6 compact UI: 144 cases, 432 ImGui frames; section order/alignment/default collapse/disabled-state isolation passed.");
}
