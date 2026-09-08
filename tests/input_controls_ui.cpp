#define NOMINMAX
#include <Windows.h>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <imgui_internal.h>
#include "../src/key_binding_policy.hpp"
static bool focus = true;
static std::array<bool,256> test_keys;
static std::map<std::string,int> saved;
static std::array<ImRect,6> buttons;
static ImRect hdr_rect;
static unsigned button_index = 0, requests = 0;
static SHORT TestGetAsyncKeyState(int) { return 0; } // ReShade overlay blocks Windows input.
struct TestRuntime {
    std::array<bool,256> previous = {}, pressed = {};
    void advance() {
        for (unsigned i = 0; i < 256; ++i) pressed[i] = test_keys[i] && !previous[i];
        previous = test_keys;
    }
    bool is_key_down(unsigned key) const { return test_keys[key]; }
    bool is_key_pressed(unsigned key) const { return pressed[key]; }
} runtime;
static HWND TestGetForegroundWindow() { return reinterpret_cast<HWND>(1); }
static DWORD TestGetWindowThreadProcessId(HWND, DWORD *pid) { *pid = focus ? GetCurrentProcessId() : 0; return 1; }
static bool get_config_int(const char *, const char *key, int &value) {
    auto it = saved.find(key); if (it == saved.end()) return false; value = it->second; return true;
}
static void set_config_int(const char *, const char *key, int value) { saved[key] = value; }
namespace ImGui {
static bool TrackedButton(const char *label, const ImVec2 &size = ImVec2()) {
    const bool result = Button(label,size);
    if (button_index < buttons.size()) buttons[button_index++] = ImRect(GetItemRectMin(),GetItemRectMax());
    return result;
}
static bool TrackedCheckbox(const char *label, bool *value) {
    const bool result = Checkbox(label,value); hdr_rect = ImRect(GetItemRectMin(),GetItemRectMax()); return result;
}
}
#define GetAsyncKeyState TestGetAsyncKeyState
#define GetForegroundWindow TestGetForegroundWindow
#define GetWindowThreadProcessId TestGetWindowThreadProcessId
#define Button TrackedButton
#define Checkbox TrackedCheckbox
#include "../src/input_controls.inl"
#undef Button
#undef Checkbox
#undef GetAsyncKeyState
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
void request_screenshot() { ++requests; }
static std::array<bool,5> poll() { runtime.advance(); return poll_control_keys(&runtime); }
static void frame() {
    button_index = 0; ImGui::NewFrame();
    poll();
    ImGui::SetNextWindowPos(ImVec2(20,20),ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(780,500),ImGuiCond_Always);
    ImGui::Begin("controls",nullptr,ImGuiWindowFlags_NoSavedSettings);
    draw_input_controls();
    assert(button_index == 6);
    for (const auto &button : buttons) assert(button.Min.x >= 28 && button.Max.x <= 800);
    ImGui::End(); ImGui::Render();
}
static void click(ImRect rect) {
    auto &io = ImGui::GetIO();
    io.AddMousePosEvent(rect.GetCenter().x,rect.GetCenter().y); frame();
    io.AddMouseButtonEvent(0,true); frame();
    io.AddMouseButtonEvent(0,false); frame(); frame();
}
int main() {
    ImGui::CreateContext(); auto &io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = ImVec2(1000,800); io.DeltaTime = 1.f/60;
    unsigned char *pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    load_control_keys(); assert(control_keys() == nr::default_keys);
    assert(TestGetAsyncKeyState(VK_F8) == 0);
    frame(); frame();
    for (std::size_t i = 1; i < buttons.size(); ++i) assert(buttons[i-1].Min.y < buttons[i].Min.y);
    click(buttons[2]); assert(g_rebinding_action == 0);
    test_keys[VK_F8] = true; frame(); assert(g_control_keys[0] == VK_F8 && saved["NRToggleKey"] == VK_F8);
    assert(!poll()[0]); test_keys[VK_F8] = false; assert(!poll()[0]);
    test_keys[VK_F8] = true; assert(poll()[0]); assert(!poll()[0]);
    test_keys[VK_F8] = false; poll(); frame();
    click(buttons[2]); test_keys[VK_F7] = true; frame();
    assert(g_rebinding_action == 0 && g_control_keys[0] == VK_F8 && g_binding_conflict); // duplicate refused
    test_keys[VK_F7] = false; test_keys[VK_ESCAPE] = true; frame(); test_keys[VK_ESCAPE] = false;
    assert(g_rebinding_action == -1);
    click(hdr_rect); assert(g_capture_hdr && saved["ScreenshotHDR"] == 1);
    click(buttons[0]); assert(requests == 1);
    click(buttons[3]); focus = false; frame(); assert(g_rebinding_action == -1);
    test_keys[VK_F8] = true; assert(!poll()[0]); focus = true; assert(!poll()[0]);
    test_keys[VK_F8] = false; poll(); test_keys[VK_F8] = true; assert(poll()[0]);
    io.WantTextInput = true; test_keys[VK_F7] = true; assert(!poll()[1]);
    io.WantTextInput = false; test_keys = {}; poll();
    io.WantCaptureKeyboard = true; test_keys[VK_F5] = true; assert(poll()[2]);
    test_keys = {}; poll();
    click(buttons[4]); test_keys[VK_F9] = true; frame();
    assert(g_control_keys[3] == VK_F9 && saved["PassCountIncreaseKey"] == VK_F9);
    test_keys[VK_F9] = false; poll(); test_keys[VK_F9] = true; assert(poll()[3]);
    test_keys = {}; poll();
    g_control_keys[0] = VK_F6; g_capture_hdr = false; load_control_keys();
    assert(g_control_keys[0] == VK_F8 && g_capture_hdr);
    saved["NRToggleKey"] = VK_F7; load_control_keys();
    assert(g_control_keys[0] == VK_F7 && g_control_keys[1] == VK_MULTIPLY &&
        g_control_keys[3] == VK_F9 && g_control_keys[4] == VK_OEM_MINUS); // keep explicit keys
    ImGui::DestroyContext();
    std::puts("Controls UI: screenshot-first order; five runtime-rebindable actions, pass hotkeys, duplicate rejection, cancel/focus, HDR, persistence and text-input suppression passed.");
}
