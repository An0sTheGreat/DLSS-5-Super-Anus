// Included inside the addon namespace; only POD/constant initialization.
std::array<std::atomic_int, 5> g_control_keys = {0x75, 0x76, 0x74, 0xBB, 0xBD};
std::atomic_int g_rebinding_action = -1;
std::atomic_bool g_key_release_required = true;
std::atomic_bool g_capture_hdr = false;
std::atomic_bool g_binding_conflict = false;
nr::KeyEdges g_key_edges;
constexpr const char *kBindingNames[] = {
    "NRToggleKey", "PresetCycleKey", "NRScreenshotKey", "PassCountIncreaseKey", "PassCountDecreaseKey"};
constexpr const char *kBindingLabels[] = {
    "Toggle Neural Rendering", "Cycle Preset 1 -> 2 -> 3 -> 1", "Capture NR ON/OFF pair",
    "Increase Pass Count", "Decrease Pass Count"};
void request_screenshot();

std::array<int, 5> control_keys()
{
    return {g_control_keys[0].load(), g_control_keys[1].load(), g_control_keys[2].load(),
        g_control_keys[3].load(), g_control_keys[4].load()};
}
void key_name(int key, char (&name)[64])
{
    UINT scan = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
    if (key == VK_DIVIDE || key == VK_NUMLOCK || (key >= VK_PRIOR && key <= VK_DELETE) ||
        key == VK_RCONTROL || key == VK_RMENU) scan |= 0x100;
    if (GetKeyNameTextA(static_cast<LONG>(scan << 16), name, sizeof(name)) == 0)
        std::snprintf(name, sizeof(name), "VK 0x%02X", key);
}
void load_control_keys()
{
    std::array<int, 5> saved_keys = {};
    for (std::size_t i = 0; i < saved_keys.size(); ++i) {
        get_config_int("RenoDXNeuralResolution", kBindingNames[i], saved_keys[i]);
    }
    const auto keys = nr::resolve_bindings(saved_keys);
    for (std::size_t i = 0; i < keys.size(); ++i) g_control_keys[i].store(keys[i]);
    int hdr = 0;
    if (get_config_int("RenoDXNeuralResolution", "ScreenshotHDR", hdr)) g_capture_hdr.store(hdr != 0);
}
void draw_input_controls()
{
    ImGui::SeparatorText("Controls");
    DWORD foreground_pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
    if (foreground_pid != GetCurrentProcessId()) g_rebinding_action.store(-1);
    if (g_rebinding_action.load() >= 0 && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)))
        g_rebinding_action.store(-1);
    if (ImGui::Button("Capture Screenshot")) request_screenshot();
    ImGui::SameLine();
    bool hdr = g_capture_hdr.load();
    if (ImGui::Checkbox("HDR mode", &hdr)) {
        g_capture_hdr.store(hdr);
        set_config_int("RenoDXNeuralResolution", "ScreenshotHDR", hdr ? 1 : 0);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("PNG only. DX12 HDR mode captures final game frames with a brief NR bypass (500 ms limit); includes UI and possible movement. DX11 retains the native same-frame pair. HDR PNG is an SDR rendition, not a monitor-exact HDR master.");
    for (int i : {2, 0, 1, 3, 4}) {
        ImGui::PushID(i);
        char name[64]; key_name(g_control_keys[i].load(), name);
        const bool capturing = g_rebinding_action.load() == i;
        if (ImGui::Button(capturing ? "Press a key..." : name, ImVec2(150, 0))) {
            g_rebinding_action.store(capturing ? -1 : i);
            g_key_release_required.store(true); g_binding_conflict.store(false);
        }
        ImGui::SameLine(); ImGui::TextUnformatted(kBindingLabels[i]);
        ImGui::PopID();
    }
    if (g_rebinding_action.load() >= 0) ImGui::TextUnformatted("Esc or click to cancel.");
    if (g_binding_conflict.load()) ImGui::TextWrapped("That key is already assigned to another action.");
}
template <class Runtime>
std::array<bool, 5> poll_control_keys(Runtime *runtime)
{
    DWORD foreground_pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
    const bool focused = foreground_pid == GetCurrentProcessId();
    if (!runtime || !focused || runtime->is_key_pressed(VK_ESCAPE))
        g_rebinding_action.store(-1);
    const int action = g_rebinding_action.load();
    // ReShade suppresses Windows GetAsyncKeyState while the overlay captures
    // input. Its runtime API still supplies the original virtual-key events.
    if (runtime && focused && action >= 0 && action < 5) {
        for (int key = 8; key < 255; ++key) {
            if (!nr::valid_binding(key) || !runtime->is_key_pressed(key)) continue;
            if (nr::binding_conflict(control_keys(), action, key)) {
                g_binding_conflict.store(true); break;
            }
            g_control_keys[action].store(key);
            set_config_int("RenoDXNeuralResolution", kBindingNames[action], key);
            g_rebinding_action.store(-1); g_binding_conflict.store(false);
            g_key_release_required.store(true);
            break;
        }
    }
    const auto keys = control_keys();
    std::array<bool, 5> down;
    for (std::size_t i = 0; i < down.size(); ++i) down[i] = runtime && runtime->is_key_down(keys[i]);
    if (g_key_release_required.exchange(false)) g_key_edges.release_required = true;
    return g_key_edges.update(down, runtime && focused && action < 0 && !ImGui::GetIO().WantTextInput);
}
