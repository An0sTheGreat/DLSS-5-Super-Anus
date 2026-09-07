#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cwchar>

#include <imgui.h>
#include <reshade.hpp>
#include <intrin.h>

#include "neural_resample_shader.hpp"
#include "preset_section.hpp"
#include "neural_ui_layout.hpp"
#include "native_gate_policy.hpp"
#include "resource_retirement.hpp"
#include "native_feature_slots.hpp"
#include "diagnostic_format.hpp"
#include "frame_trace.hpp"
#include "backends/dx11_build_config.hpp"
#include "backends/backend_support.hpp"
#include "runtime_api_ui.hpp"
#include "debug_section_default.hpp"
#include "neural_performance_ui.hpp"
#include "key_binding_policy.hpp"
#include "capture_chain.hpp"
#include "screenshot_files.hpp"
#include "screenshot_copy_shader.hpp"
#include "nr_activity_monitor.hpp"
#include "final_capture_policy.hpp"
#include "auto_source_policy.hpp"
#include "display_white.hpp"
#ifdef NR_EXPERIMENTAL_DX11
#include <intrin.h>
#include "scale_history.hpp"
#include <Psapi.h>
#include <nvsdk_ngx_params.h>
#include <MinHook.h>
#include "backends/dx11_transport.hpp"
extern "C" __declspec(dllexport) NVSDK_NGX_Result dx11_create_dispatch(ID3D11DeviceContext *, int, NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
extern "C" __declspec(dllexport) NVSDK_NGX_Result dx11_release_dispatch(NVSDK_NGX_Handle *);
extern "C" __declspec(dllexport) NVSDK_NGX_Result dx11_evaluate_dispatch(ID3D11DeviceContext *, const NVSDK_NGX_Handle *, const NVSDK_NGX_Parameter *, void *);
extern "C" __declspec(dllexport) NVSDK_NGX_Result dx11_evaluate_c_dispatch(ID3D11DeviceContext *, const NVSDK_NGX_Handle *, const NVSDK_NGX_Parameter *, void *);
extern "C" __declspec(dllexport) NVSDK_NGX_Result dx11_shutdown_dispatch(ID3D11Device *, unsigned *);
extern "C" __declspec(dllexport) NVSDK_NGX_Result dx11_sdk_shutdown_dispatch(ID3D11Device *);
#endif

#ifndef NR_LIFETIME_TEST
#pragma function(memcpy, memmove, memset, memcmp)
extern "C" int memcmp(const void *left, const void *right, std::size_t count)
{
    const auto *a = static_cast<const unsigned char *>(left);
    const auto *b = static_cast<const unsigned char *>(right);
    for (std::size_t i = 0; i < count; ++i)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
extern "C" int _fltused = 0;
extern "C" void *memcpy(void *destination, const void *source, std::size_t count)
{
    auto *out = static_cast<unsigned char *>(destination);
    const auto *in = static_cast<const unsigned char *>(source);
    while (count-- != 0) *out++ = *in++;
    return destination;
}
extern "C" void *memmove(void *destination, const void *source, std::size_t count)
{
    auto *out = static_cast<unsigned char *>(destination);
    const auto *in = static_cast<const unsigned char *>(source);
    if (out < in)
        while (count-- != 0) *out++ = *in++;
    else
        while (count-- != 0) out[count] = in[count];
    return destination;
}
extern "C" void *memset(void *destination, int value, std::size_t count)
{
    auto *out = static_cast<unsigned char *>(destination);
    while (count-- != 0) *out++ = static_cast<unsigned char>(value);
    return destination;
}
#endif

namespace
{
constexpr std::uintptr_t kEvaluateWrapperRva = 0x5D880;
constexpr std::uintptr_t kFirstCallRva = 0x5C281;
constexpr std::uintptr_t kSecondCallRva = 0x5C3D2;
constexpr std::uintptr_t kPresetIndexRva = 0x2677F8;
constexpr std::uintptr_t kGlobalNameRva = 0x265D40;
constexpr std::uintptr_t kSettingsBeginRva = 0x270DC0;
constexpr std::uintptr_t kSettingsEndRva = 0x270DC8;
constexpr std::uintptr_t kPresetOffCallbacksBeginRva = 0x26E000;
constexpr std::uintptr_t kPresetOffCallbacksEndRva = 0x26E008;
constexpr std::uintptr_t kPresetChangedCallbacksBeginRva = 0x26E018;
constexpr std::uintptr_t kPresetChangedCallbacksEndRva = 0x26E020;
constexpr std::uintptr_t kSettingsMutexRva = 0x2718E0;
constexpr std::uintptr_t kLoadSettingRva = 0x0A71C0;
constexpr std::uintptr_t kWriteSettingRva = 0x022740;
constexpr std::uintptr_t kImguiTableRva = 0x271000;
constexpr std::uintptr_t kGetHostModuleRva = 0x025D90;
constexpr std::size_t kInputSize = 0xA5;
constexpr std::size_t kMaximumResourceSets = 64;
constexpr std::size_t kMaximumTrackedCommandLists = 256;
constexpr std::size_t kMaximumQueues = 16;
constexpr std::uint64_t kWorkingTextureBudget = 512ull * 1024 * 1024;
constexpr std::uint8_t kPendingRecordingGuid[16] = {
    0x83, 0x39, 0x7e, 0x61, 0x56, 0x2c, 0x4b, 0xf7,
    0xa0, 0x51, 0x9c, 0x42, 0x64, 0x31, 0xe8, 0xd2 };

constexpr std::array<std::uint8_t, 5> kFirstOriginalCall = { 0xE8, 0xFA, 0x15, 0x00, 0x00 };
constexpr std::array<std::uint8_t, 5> kSecondOriginalCall = { 0xE8, 0xA9, 0x14, 0x00, 0x00 };

std::atomic_int g_scale_percent = 100;
std::atomic_int g_pending_scale_percent = 100;
std::atomic_int g_sharpness_percent = 25;
std::atomic_int g_resolve_mode = 1;
std::atomic_int g_transfer_percent = 100;
std::atomic_int g_color_percent = 100;
std::atomic_uint g_scale_generation = 1;
std::atomic_uint g_logged_create_site_generation = 0;
std::atomic_uint g_logged_existing_site_generation = 0;
std::atomic_uint g_logged_success_generation = 0;
std::atomic_uint g_logged_failure_generation = 0;
std::atomic_bool g_command_registry_warning_logged = false;
std::atomic_bool g_hook_installed = true;
HMODULE g_target_module = nullptr;

using GetHostModuleFunction = HMODULE (*)();
using GetConfigFunction = bool (*)(void *, reshade::api::effect_runtime *, const char *, const char *, char *, std::size_t *);
using SetConfigFunction = void (*)(void *, reshade::api::effect_runtime *, const char *, const char *, const char *);
using LogFunction = void (*)(void *, int, const char *);
using RegisterEventForAddonFunction = void (*)(void *, reshade::addon_event, void *);
using UnregisterEventForAddonFunction = void (*)(void *, reshade::addon_event, void *);
GetConfigFunction g_get_config = nullptr;
SetConfigFunction g_set_config = nullptr;
LogFunction g_log = nullptr;
RegisterEventForAddonFunction g_register_event_for_addon = nullptr;
UnregisterEventForAddonFunction g_unregister_event_for_addon = nullptr;
bool g_overlay_event_registered = false;

enum class OverlayMessage : std::uint32_t
{
    none,
    nr_on,
    nr_off,
    preset_1,
    preset_2,
    preset_3,
};

OverlayMessage g_overlay_message = OverlayMessage::none;
ULONGLONG g_overlay_started = 0;
int g_last_preset = 1;
bool g_preset_persistence_initialized = false;
std::atomic_int g_queued_preset = -1;
OverlayMessage g_queued_message = OverlayMessage::none;
std::atomic_bool g_xefg_probe_succeeded = false;
std::atomic_bool g_native_compatibility_enabled = false;
std::atomic_uint g_native_gate_calls = 0;
struct DispatchCounters {
    std::atomic_ullong calls = 0, rejected = 0, frame = 0, tick = 0;
    std::atomic_ullong parents = 0, failed = 0;
};
std::array<DispatchCounters, 8> g_dispatch;
std::atomic_uint g_native_gate_rejected = 0;
std::atomic_uint g_native_zero_frame = 0;
std::atomic_ullong g_native_last_frame = 0;
std::atomic_uint g_native_repeated_frame = 0;
std::atomic_uint g_native_backward_frame = 0;
std::atomic_uint g_evaluation_calls = 0;
std::atomic_uint g_successful_evaluations = 0;
std::atomic_uint g_scaled_calls = 0;
std::atomic_uint g_off_evaluation_calls = 0;
std::atomic_uint g_budget_fallbacks = 0;
std::atomic_uint g_retired_sets = 0;
std::atomic_uint g_cached_mib = 0;
std::atomic_uint g_cached_sets = 0;
std::atomic_uint g_pinned_sets = 0;
std::atomic_uint g_retiring_sets = 0;
std::atomic_uint g_unsafe_sets = 0;
std::atomic_uint g_native_features = 0;
std::atomic_uint g_native_features_retired = 0;
std::atomic_int g_effective_scale = 100;
bool g_lifetime_events_registered = false;
std::atomic_flag g_preset_transaction_active = ATOMIC_FLAG_INIT;
std::atomic_flag g_overlay_active = ATOMIC_FLAG_INIT;
std::atomic_uint g_runtime_api = 0;
std::atomic<ULONGLONG> g_capture_off_until = 0;
std::atomic_uint g_capture_skipped = 0;
nr::backends::EvaluationDevice g_evaluation_device;
nr::FrameTrace g_frame_trace;
std::atomic_bool g_trace_requested = false;
std::atomic_uint g_trace_status = 0; // idle / recording / draining
void pump_frame_trace();

bool nr_enabled()
{
    return g_target_module != nullptr && *reinterpret_cast<volatile int *>(
        reinterpret_cast<std::uintptr_t>(g_target_module) + kPresetIndexRva) != 0;
}

using EvaluateFunction = std::uint64_t(__fastcall *)(void *);

template <typename T>
T &field(void *base, std::size_t offset);

template <typename... Args>
void log_message(reshade::log::level level, const char *format, Args... args)
{
    char buffer[512] = {};
    diagnostic_format(buffer, format, args...);
    if (g_log != nullptr)
        g_log(g_target_module, static_cast<int>(level), buffer);
}

void log_text(reshade::log::level level, const char *message)
{
    if (g_log != nullptr)
        g_log(g_target_module, static_cast<int>(level), message);
}

bool get_config_int(const char *section, const char *key, int &value)
{
    if (g_get_config == nullptr)
        return false;
    char text[32] = {};
    std::size_t size = sizeof(text);
    if (!g_get_config(g_target_module, nullptr, section, key, text, &size))
        return false;
    bool negative = false;
    std::size_t index = 0;
    if (text[0] == '-') { negative = true; ++index; }
    int parsed = 0;
    bool any = false;
    for (; index < sizeof(text) && text[index] >= '0' && text[index] <= '9'; ++index)
    {
        parsed = parsed * 10 + text[index] - '0';
        any = true;
    }
    if (!any)
        return false;
    value = negative ? -parsed : parsed;
    return true;
}

void set_config_int(const char *section, const char *key, int value)
{
    if (g_set_config == nullptr)
        return;
    char text[16] = {};
    diagnostic_format(text, "%d", value);
    g_set_config(g_target_module, nullptr, section, key, text);
}

class ScopedLock
{
public:
    explicit ScopedLock(CRITICAL_SECTION &section) : section_(section) { EnterCriticalSection(&section_); }
    ~ScopedLock() { LeaveCriticalSection(&section_); }
    ScopedLock(const ScopedLock &) = delete;
    ScopedLock &operator=(const ScopedLock &) = delete;
private:
    CRITICAL_SECTION &section_;
};

__declspec(noinline) void set_scale(int scale)
{
    scale = std::clamp(scale, 25, 100);
    if (g_scale_percent.exchange(scale, std::memory_order_relaxed) != scale)
    {
        g_scale_generation.fetch_add(1, std::memory_order_relaxed);
        set_config_int("RenoDXNeuralResolution", "ScalePercent", scale);
        set_config_int("RenoDXNeuralResolution", "AppliedScalePercentV6", scale);
        log_message(reshade::log::level::info,
            "RenoDX Neural Resolution: applied %d%% scale.", scale);
    }
}

void pump_frame_trace()
{
    static std::uint64_t deadline = 0, next_drain = 0;
    const auto now = GetTickCount64();
    if (g_trace_requested.exchange(false) && g_frame_trace.start(now))
    {
        deadline = now + 10000;
        g_trace_status.store(1);
        log_text(reshade::log::level::info,
            "NR V6.6 trace START: 10 seconds, at most 4096 records; native-observer gate and NR wrapper only, not every route, GPU timings or proven generated-frame identity.");
    }
    if (g_trace_status.load() == 0 || now < deadline || now < next_drain) return;
    g_trace_status.store(2);
    next_drain = now + 250;
    nr::FrameTraceEvent event;
    unsigned emitted = 0;
    for (; emitted < 64 && g_frame_trace.pop(now, &event); ++emitted)
    {
        if (event.kind == nr::TraceKind::gate)
            log_message(reshade::log::level::info,
                "NR V6.6 trace gate: ms=%llu thread=%u frame=%llu source=%u retry=%u allowed=%llu.",
                event.tick, event.thread, event.frame, event.source, event.retry ? 1u : 0u, event.result);
        else
            log_message(reshade::log::level::info,
                "NR V6.6 trace eval: ms=%llu thread=%u cmd=0x%llx color=0x%llx output=0x%llx extent=%ux%u pass=%u result=0x%llx (CPU wrapper return).",
                event.tick, event.thread, event.command, event.color, event.output,
                event.width, event.height, event.pass, event.result);
    }
    if (emitted < 64)
    {
        log_message(reshade::log::level::info, "NR V6.6 trace END: dropped=%u (capacity/lock contention).", g_frame_trace.dropped());
        g_trace_status.store(0);
    }
}

template <typename T>
T &field(void *base, std::size_t offset)
{
    return *reinterpret_cast<T *>(static_cast<std::uint8_t *>(base) + offset);
}

#include "input_controls.inl"
#include "backends/dx12_backend.inl"
#include "framegen_probe.inl"
void request_screenshot() { dx12::capture::request(); }
// Retained entry for the isolated, hash-checked GPU acceptance host. The final
// addon keeps the official export table; games do not auto-trigger this entry.
extern "C" __declspec(dllexport) void embedded_request_screenshot() { request_screenshot(); }
extern "C" __declspec(dllexport) std::uint64_t embedded_capture_parent(void *descriptor)
{
    const unsigned source = descriptor ? field<unsigned char>(descriptor,0x40) : 0;
    const bool other_auto = source != 1 && auto_source_enabled();
    if (other_auto && !g_auto_source.enter_other(native_application_frame())) return 0;
    const auto before = g_successful_evaluations.load();
    auto &route = g_dispatch[source < g_dispatch.size() ? source : 0];
    route.parents.fetch_add(1, std::memory_order_relaxed);
    if (descriptor) dx12::on_init_command_list(field<reshade::api::command_list *>(descriptor,0));
    const auto result = dx12::capture::parent(descriptor);
    if (other_auto) g_auto_source.leave_other(GetTickCount64(),g_successful_evaluations.load()!=before);
    if ((result & 255) != 1) route.failed.fetch_add(1, std::memory_order_relaxed);
    return result;
}
extern "C" __declspec(dllexport) void embedded_capture_codec(void *cmd, void *codec, unsigned encoding,
    unsigned balance, const unsigned *rect, unsigned char decode)
{
    dx12::capture::codec(cmd,codec,encoding,balance,rect,decode);
}
extern "C" __declspec(dllexport) std::uint64_t embedded_capture_api_codec(reshade::api::command_list *cmd,
    void *pipeline, std::uint64_t source, std::uint64_t original, std::uint64_t destination,
    const unsigned *constants, unsigned width, unsigned height)
{
    return dx12::capture::api_codec(cmd,pipeline,source,original,destination,constants,width,height);
}
#ifdef NR_EXPERIMENTAL_DX11
#include "backends/dx11_native_bridge.inl"
#endif

void __fastcall draw_inline_settings(void *setting)
{
    if (!setting) return;
    // Verified section std::string at +A0. This hook runs after the previous
    // section's TreePop and immediately before the next section's TreeNodeEx.
    const auto &section = field<PresetStringView>(setting, 0xA0);
    if (section.size != 5 || section.capacity < section.size || !section.data()) return;
    const bool before_debug = std::memcmp(section.data(), "Debug", 5) == 0;
    const bool before_links = std::memcmp(section.data(), "Links", 5) == 0;
    if (!before_debug && !before_links) {
        apply_debug_section_default(section.data(), section.size);
        return;
    }

    begin_neural_controls_layout();
    ImGui::Spacing();
    const auto status = nr::backends::runtime_status(
        static_cast<reshade::api::device_api>(g_runtime_api.load()),
        g_evaluation_device.observed(), g_lifetime_events_registered);
    if (before_debug)
    {
        int pending = g_pending_scale_percent.load(std::memory_order_relaxed);
        const int applied = g_scale_percent.load(std::memory_order_relaxed);
        int sharpness = g_sharpness_percent.load(std::memory_order_relaxed);
        NeuralResolveControls resolve{g_resolve_mode.load(), g_transfer_percent.load(), g_color_percent.load()};
        const auto edits = draw_neural_performance_section(status.controls_available, pending, applied, sharpness, &resolve);
        if (edits.resolve_changed)
        {
            g_resolve_mode.store(std::clamp(resolve.mode, 0, 1));
            g_transfer_percent.store(std::clamp(resolve.transfer, 0, 200));
            g_color_percent.store(std::clamp(resolve.color, 0, 100));
            set_config_int("RenoDXNeuralResolution", "CostResolveMode", g_resolve_mode.load());
            set_config_int("RenoDXNeuralResolution", "CostTransferPercent", g_transfer_percent.load());
            set_config_int("RenoDXNeuralResolution", "CostColorPercent", g_color_percent.load());
        }
        if (edits.pending_changed) g_pending_scale_percent.store(pending, std::memory_order_relaxed);
        if (edits.apply) set_scale(pending);
        if (edits.sharpness_changed)
        {
            sharpness = std::clamp(sharpness, 0, 100);
            g_sharpness_percent.store(sharpness, std::memory_order_relaxed);
            set_config_int("RenoDXNeuralResolution", "SharpnessPercent", sharpness);
        }
    }
    else
    {
        draw_runtime_api_section(status);
        draw_input_controls();
        const unsigned capture_status = dx12::capture::status.load();
        constexpr const char *messages[] = {"", "Screenshot armed...", "Screenshot awaiting GPU completion...",
            "Writing screenshots...", "Screenshot pair saved.", "Screenshot unavailable/failed; see ReShade.log."};
        if (capture_status > 0 && capture_status < std::size(messages)) ImGui::TextWrapped("%s", messages[capture_status]);
        if (capture_status == 5) ImGui::TextWrapped("%s", dx12::capture::failure.load());
    }
    ImGui::EndGroup();
    // Set the default only AFTER our own widgets, so the upcoming native section
    // node consumes it, not the Performance separator or a slider.
    apply_debug_section_default(section.data(), section.size);
}

extern "C" __declspec(dllexport) std::uint64_t __fastcall scaled_evaluate_create(void *input)
{
    return dx12::scaled_evaluate_impl(input, 1);
}

extern "C" __declspec(dllexport) std::uint64_t __fastcall scaled_evaluate_existing(void *input)
{
    return dx12::scaled_evaluate_impl(input, 2);
}

bool install_hook()
{
    return g_target_module != nullptr && g_hook_installed.load(std::memory_order_acquire);
}

void uninstall_hook()
{
    g_hook_installed.store(false, std::memory_order_release);
}

void on_init_device(reshade::api::device *device)
{
    if (device == nullptr) return;
#ifdef NR_EXPERIMENTAL_DX11
    if (device->get_api() == reshade::api::device_api::d3d11) dx11_native::start_discovery();
#endif
    const auto backend = nr::backends::support(device->get_api());
    log_message(reshade::log::level::info, "NR V6.6 backend: API=%s; %s", backend.name, backend.reason);
    if (backend.evaluator_available) install_hook();
}

void show_message(OverlayMessage message, ULONGLONG now)
{
    g_overlay_message = message;
    g_overlay_started = now;
}

const char *message_text(OverlayMessage message)
{
    switch (message)
    {
    case OverlayMessage::nr_on: return "NR ON";
    case OverlayMessage::nr_off: return "NR OFF";
    case OverlayMessage::preset_1: return "PRESET 1";
    case OverlayMessage::preset_2: return "PRESET 2";
    case OverlayMessage::preset_3: return "PRESET 3";
    default: return "";
    }
}

void remember_preset(int preset)
{
    if (preset < 1 || preset > 3)
        return;
    const bool changed = preset != g_last_preset;
    g_last_preset = preset;
    if (changed || !g_preset_persistence_initialized)
    {
        set_config_int("RenoDXNeuralResolution", "LastEnabledPreset", preset);
        g_preset_persistence_initialized = true;
    }
}

void queue_preset(int preset, OverlayMessage message)
{
    int expected = -1;
    if (g_queued_preset.compare_exchange_strong(expected, preset, std::memory_order_release))
        g_queued_message = message;
}

void probe_optional_xefg_path()
{
    static ULONGLONG next_probe = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < next_probe) return;
    next_probe = now + 1000;

    // Positive runtime detection only: the local DXGI proxy must identify as
    // OptiScaler and XeSS-FG must actually be loaded. No file or INI dependency
    // is introduced for native DLSS-G or frame-generation-off configurations.
    const HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    const HMODULE xefg = GetModuleHandleW(L"libxess_fg.dll");
    const bool detected = dxgi != nullptr && xefg != nullptr &&
        GetProcAddress(dxgi, "ffxOpticalflowContextCreate") != nullptr;
    if (g_xefg_probe_succeeded.exchange(detected, std::memory_order_relaxed) != detected)
        log_message(reshade::log::level::info,
            "RenoDX Neural Resolution V6.6: optional OptiScaler XeFG modules %s; Hook Method unchanged.",
            detected ? "loaded" : "unloaded");

}

bool valid_vector(std::uintptr_t begin, std::uintptr_t end, std::size_t stride, std::size_t maximum)
{
    return begin != 0 && end >= begin && ((end - begin) % stride) == 0 &&
        (end - begin) / stride <= maximum;
}

bool callback_vector_is_valid(std::uintptr_t begin_rva, std::uintptr_t end_rva)
{
    const auto base = reinterpret_cast<std::uintptr_t>(g_target_module);
    const std::uintptr_t begin = *reinterpret_cast<const std::uintptr_t *>(base + begin_rva);
    const std::uintptr_t end = *reinterpret_cast<const std::uintptr_t *>(base + end_rva);
    if (begin == end)
        return true;
    if (!valid_vector(begin, end, 0x40, 64))
        return false;
    for (std::uintptr_t entry = begin; entry != end; entry += 0x40)
    {
        void *object = *reinterpret_cast<void **>(entry + 0x38);
        if (object == nullptr)
            return false;
        auto **vtable = *reinterpret_cast<void ***>(object);
        if (vtable == nullptr || vtable[2] == nullptr)
            return false;
    }
    return true;
}

bool invoke_callback_vector(std::uintptr_t begin_rva, std::uintptr_t end_rva)
{
    if (!callback_vector_is_valid(begin_rva, end_rva))
        return false;
    const auto base = reinterpret_cast<std::uintptr_t>(g_target_module);
    const std::uintptr_t begin = *reinterpret_cast<const std::uintptr_t *>(base + begin_rva);
    const std::uintptr_t end = *reinterpret_cast<const std::uintptr_t *>(base + end_rva);
    using InvokeFunction = void (__fastcall *)(void *);
    for (std::uintptr_t entry = begin; entry != end; entry += 0x40)
    {
        void *object = *reinterpret_cast<void **>(entry + 0x38);
        auto **vtable = *reinterpret_cast<void ***>(object);
        reinterpret_cast<InvokeFunction>(vtable[2])(object);
    }
    return true;
}

bool apply_preset_transaction(int desired)
{
    if (desired < 0 || desired > 3 || g_target_module == nullptr ||
        g_preset_transaction_active.test_and_set(std::memory_order_acquire))
        return false;

    struct ClearTransactionFlag
    {
        ~ClearTransactionFlag() { g_preset_transaction_active.clear(std::memory_order_release); }
    } clear_flag;

    const auto base = reinterpret_cast<std::uintptr_t>(g_target_module);
    const std::uintptr_t settings_begin =
        *reinterpret_cast<const std::uintptr_t *>(base + kSettingsBeginRva);
    const std::uintptr_t settings_end =
        *reinterpret_cast<const std::uintptr_t *>(base + kSettingsEndRva);
    if ((desired != 0 && !valid_vector(settings_begin, settings_end, sizeof(void *), 512)) ||
        !callback_vector_is_valid(
            kPresetChangedCallbacksBeginRva, kPresetChangedCallbacksEndRva) ||
        (desired == 0 && !callback_vector_is_valid(
            kPresetOffCallbacksBeginRva, kPresetOffCallbacksEndRva)))
        return false;

    char section_buffer[128] = {};
    PresetStringView section;
    if (desired != 0 && !make_preset_section(
            *reinterpret_cast<const PresetStringView *>(base + kGlobalNameRva),
            desired, section_buffer, section))
        return false;

    auto *preset = reinterpret_cast<volatile int *>(base + kPresetIndexRva);
    *preset = desired;

    if (desired == 0)
    {
        if (!invoke_callback_vector(kPresetOffCallbacksBeginRva, kPresetOffCallbacksEndRva))
            return false;
    }
    else
    {
        using LoadSettingFunction = void (__fastcall *)(const PresetStringView *, void *);
        using WriteSettingFunction = void (__fastcall *)(void *);
        const auto load_setting = reinterpret_cast<LoadSettingFunction>(base + kLoadSettingRva);
        const auto write_setting = reinterpret_cast<WriteSettingFunction>(base + kWriteSettingRva);
        auto *settings_mutex = reinterpret_cast<PSRWLOCK>(base + kSettingsMutexRva);

        for (std::uintptr_t cursor = settings_begin; cursor != settings_end; cursor += sizeof(void *))
        {
            void *setting = *reinterpret_cast<void **>(cursor);
            if (setting == nullptr)
                return false;
            if (field<std::uint8_t>(setting, 0x338) != 0)
                continue;
            load_setting(&section, setting);
            AcquireSRWLockExclusive(settings_mutex);
            write_setting(setting);
            ReleaseSRWLockExclusive(settings_mutex);
        }
        log_message(reshade::log::level::info,
            "RenoDX Neural Resolution V6.6: loaded settings from [%s].", section_buffer);
    }

    return invoke_callback_vector(
        kPresetChangedCallbacksBeginRva, kPresetChangedCallbacksEndRva);
}

void commit_queued_preset(ULONGLONG now)
{
    const int desired = g_queued_preset.exchange(-1, std::memory_order_acq_rel);
    if (desired < 0 || desired > 3)
        return;

    if (!apply_preset_transaction(desired))
    {
        log_text(reshade::log::level::warning,
            "RenoDX Neural Resolution V6.6: rejected a hotkey preset transaction before callbacks completed.");
        g_queued_message = OverlayMessage::none;
        return;
    }

    const int committed = *reinterpret_cast<volatile int *>(
        reinterpret_cast<std::uintptr_t>(g_target_module) + kPresetIndexRva);
    if (committed >= 1 && committed <= 3)
        remember_preset(committed);
    if (g_queued_message != OverlayMessage::none)
        show_message(g_queued_message, now);
    log_message(reshade::log::level::info,
        "RenoDX Neural Resolution V6.6: committed preset transaction %d on overlay thread %lu.",
        committed, GetCurrentThreadId());
    g_queued_message = OverlayMessage::none;
}

void draw_hotkey_overlay(reshade::api::effect_runtime *runtime)
{
    // Multiple ReShade runtimes may invoke this callback on different threads.
    // Keep edge detection, queued messages, and persistence a single transaction.
    if (g_overlay_active.test_and_set(std::memory_order_acquire)) return;
    struct ClearOverlayFlag
    {
        ~ClearOverlayFlag() { g_overlay_active.clear(std::memory_order_release); }
    } clear_overlay_flag;
    const ULONGLONG now = GetTickCount64();
    if (runtime != nullptr && runtime->get_device() != nullptr)
        g_runtime_api.store(static_cast<unsigned>(runtime->get_device()->get_api()));
#ifdef NR_EXPERIMENTAL_DX11
    if (runtime && runtime->get_device()->get_api() == reshade::api::device_api::d3d11)
        dx11_native::install_hooks();
#endif
    probe_optional_xefg_path();
    pump_frame_trace();
    dx12::maintain_resources();
    dx12::capture::tick();
    static nr::ActivityMonitor activity;
    const auto change = activity.observe(now, nr_enabled(), g_successful_evaluations.load());
    if (change != nr::ActivityChange::none)
        log_message(reshade::log::level::info,
            "NR activity %s: entries=%u successes=%u gate=%u rejected=%u hook=%u scale=%d features=%u held=%u. Observation only; no feature reset/replay.",
            change == nr::ActivityChange::stalled ? "stalled for 3s" : "resumed",
            g_evaluation_calls.load(), g_successful_evaluations.load(), g_native_gate_calls.load(),
            g_native_gate_rejected.load(), static_cast<unsigned>(field<float>(g_target_module,0x270FB0)), g_scale_percent.load(),
            g_native_features.load(), g_unsafe_sets.load());
    // Event-driven source diagnostics. These counters never change the original
    // frame arbitration, hook selection, preset or NGX feature state.
    static unsigned last_passes = UINT_MAX;
    const unsigned passes = field<unsigned>(g_target_module,0x266FA4);
    if (last_passes != passes || change != nr::ActivityChange::none)
    {
        log_message(reshade::log::level::info,
            "NR PASS TEST 1: passes=%u previous=%u entries=%u successes=%u scale=%d route-flags=%u/%u/%u/%u registry-recycled=%u fully-pinned=%u capture-skips=%u.",
            passes, last_passes, g_evaluation_calls.load(), g_successful_evaluations.load(), g_scale_percent.load(),
            field<unsigned char>(g_target_module,0x27100C), field<unsigned char>(g_target_module,0x27100D),
            field<unsigned char>(g_target_module,0x27100E), field<unsigned char>(g_target_module,0x27100F),
            dx12::g_command_slots_recycled.load(), dx12::g_command_slots_exhausted.load(), g_capture_skipped.load());
        for (unsigned i = 0; i < g_dispatch.size(); ++i) {
            const auto &route = g_dispatch[i];
            if (!route.calls.load() && !route.parents.load()) continue;
            log_message(reshade::log::level::info,
                "NR dispatch source=%u calls=%llu rejected=%llu last-frame=%llu last-tick=%llu parents=%llu failed=%llu.",
                i, route.calls.load(), route.rejected.load(), route.frame.load(), route.tick.load(),
                route.parents.load(), route.failed.load());
        }
        last_passes = passes;
    }
    auto *preset = reinterpret_cast<volatile int *>(
        reinterpret_cast<std::uintptr_t>(g_target_module) + kPresetIndexRva);

    // Capture preset changes made through RenoDX's normal UI. Preset 0 means
    // disabled and never replaces the last enabled choice.
    const int observed = *preset;
    if (g_queued_preset.load(std::memory_order_acquire) == -1 &&
        observed >= 1 && observed <= 3)
        remember_preset(observed);

    const auto pressed = poll_control_keys(runtime);

    if (pressed[0] &&
        g_queued_preset.load(std::memory_order_acquire) == -1)
    {
        int current = *preset;
        if (current != 0)
        {
            if (current >= 1 && current <= 3)
                remember_preset(current);
            queue_preset(0, OverlayMessage::nr_off);
        }
        else
        {
            g_last_preset = std::clamp(g_last_preset, 1, 3);
            queue_preset(g_last_preset, OverlayMessage::nr_on);
        }
    }
    if (pressed[1] &&
        g_queued_preset.load(std::memory_order_acquire) == -1)
    {
        int current = *preset;
        if (current == 0)
        {
            const int next = g_last_preset >= 1 && g_last_preset < 3 ? g_last_preset + 1 : 1;
            remember_preset(next);
            show_message(static_cast<OverlayMessage>(
                static_cast<std::uint32_t>(OverlayMessage::preset_1) + next - 1), now);
        }
        else
        {
            const int next = current >= 1 && current < 3 ? current + 1 : 1;
            queue_preset(next, static_cast<OverlayMessage>(
                static_cast<std::uint32_t>(OverlayMessage::preset_1) + next - 1));
        }
    }
    commit_queued_preset(now);
    if (pressed[2]) request_screenshot();

    if (g_overlay_message == OverlayMessage::none)
        return;
    const ULONGLONG age = now - g_overlay_started;
    if (age >= 3000)
    {
        g_overlay_message = OverlayMessage::none;
        return;
    }

    const float alpha = age <= 1000 ? 1.0f : static_cast<float>(3000 - age) / 2000.0f;
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 24.0f, 24.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.72f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##RenoDXNeuralResolutionOSD", nullptr, flags))
        ImGui::TextUnformatted(message_text(g_overlay_message));
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace

#ifdef NR_EXPERIMENTAL_DX11
extern "C" __declspec(dllexport) NVSDK_NGX_Result dx11_create_dispatch(
    ID3D11DeviceContext *context, int kind, NVSDK_NGX_Parameter *parameters, NVSDK_NGX_Handle **handle)
{
    dx11_native::Shape shape;
    const bool valid = kind == NVSDK_NGX_Feature_SuperSampling && dx11_native::shape_of(parameters, shape);
    AcquireSRWLockExclusive(&dx11_native::mutex);
    const auto epoch = dx11_native::capture_epoch;
    const bool can_capture = dx11_native::shutdowns_in_flight == 0;
    ReleaseSRWLockExclusive(&dx11_native::mutex);
    const auto original = dx11_native::original_create;
    const auto result = original(context, kind, parameters, handle);
    if (result == 1 && handle && *handle)
    {
        AcquireSRWLockExclusive(&dx11_native::mutex);
        const bool captured = can_capture && dx11_native::capture_feature(context, *handle, shape, valid, epoch);
        ReleaseSRWLockExclusive(&dx11_native::mutex);
        dx11_native::message(captured ? "native SR feature captured" : "native feature not captured; preserving native output", kind);
    }
    return result;
}
extern "C" __declspec(dllexport) NVSDK_NGX_Result dx11_release_dispatch(NVSDK_NGX_Handle *handle)
{
    AcquireSRWLockExclusive(&dx11_native::mutex);
    const auto ticket = dx11_native::feature_ticket(handle);
    ReleaseSRWLockExclusive(&dx11_native::mutex);
    const auto result = dx11_native::original_release(handle);
#if defined(NR_DX11_RECYCLE_PROBE) || defined(NR_DX11_REPLACEMENT_PROBE)
    ID3D11Device *probe_owner = nullptr;
#endif
    if (result == 1)
    {
        AcquireSRWLockExclusive(&dx11_native::mutex);
        const bool removed = dx11_native::forget_released_feature(handle, ticket);
        if (removed && dx11_native::active_native_feature == handle && dx11_native::active_native_generation == ticket)
        {
#if defined(NR_DX11_RECYCLE_PROBE) || defined(NR_DX11_REPLACEMENT_PROBE)
            static unsigned probes = 0;
            if (probes < 2 && dx11_native::native_context)
            { ++probes; dx11_native::native_context->GetDevice(&probe_owner); }
#endif
            dx11_native::active_native_feature = nullptr;
            dx11_native::active_native_generation = 0;
            dx11_native::reset_history = true;
        }
        ReleaseSRWLockExclusive(&dx11_native::mutex);
    }
#if defined(NR_DX11_RECYCLE_PROBE) || defined(NR_DX11_REPLACEMENT_PROBE)
    if (probe_owner)
    {
        dx11_native::close_for_device(probe_owner);
#ifdef NR_DX11_RECYCLE_PROBE
        dx11_native::probe_restart_after_retirement(dx11_native::native_context);
#endif
        probe_owner->Release();
    }
#endif
    return result;
}
extern "C" __declspec(dllexport) NVSDK_NGX_Result dx11_evaluate_dispatch(
    ID3D11DeviceContext *context, const NVSDK_NGX_Handle *handle, const NVSDK_NGX_Parameter *parameters, void *callback)
{
    dx11_native::EvaluationScope scope;
    if (!scope.outer) return dx11_native::original_evaluate(context, handle, parameters, callback);
    const auto ticket = dx11_native::evaluation_ticket(handle);
    const auto result = dx11_native::original_evaluate(context, handle, parameters, callback);
    dx11_native::after_evaluate(context, handle, parameters, result, ticket);
    return result;
}
extern "C" __declspec(dllexport) NVSDK_NGX_Result dx11_evaluate_c_dispatch(
    ID3D11DeviceContext *context, const NVSDK_NGX_Handle *handle, const NVSDK_NGX_Parameter *parameters, void *callback)
{
    dx11_native::EvaluationScope scope;
    if (!scope.outer) return dx11_native::original_evaluate_c(context, handle, parameters, callback);
    const auto ticket = dx11_native::evaluation_ticket(handle);
    const auto result = dx11_native::original_evaluate_c(context, handle, parameters, callback);
    dx11_native::after_evaluate(context, handle, parameters, result, ticket);
    return result;
}
extern "C" __declspec(dllexport) NVSDK_NGX_Result dx11_shutdown_dispatch(ID3D11Device *device, unsigned *driver_output)
{
    // Invalidate before forwarding: native Shutdown1 can fail or never return.
    // Capture is suspended during ANY native shutdown, without blocking native
    // calls or allowing a pending create to repopulate a retired device's table.
    AcquireSRWLockExclusive(&dx11_native::mutex);
    ++dx11_native::shutdowns_in_flight;
    dx11_native::invalidate_device_features(device);
    ReleaseSRWLockExclusive(&dx11_native::mutex);
    dx11_native::close_for_device(device);
    const auto result = dx11_native::sdk_frontend ? dx11_native::original_sdk_shutdown(device) :
        dx11_native::original_shutdown(device, driver_output);
    AcquireSRWLockExclusive(&dx11_native::mutex);
    dx11_native::invalidate_device_features(device);
    --dx11_native::shutdowns_in_flight;
    ReleaseSRWLockExclusive(&dx11_native::mutex);
    return result;
}
extern "C" __declspec(dllexport) NVSDK_NGX_Result dx11_sdk_shutdown_dispatch(ID3D11Device *device)
{
    // SDK frontend has ONE argument. Never forward the driver's output pointer
    // ABI to this entry point, or infer an output pointer from an unused register.
    return dx11_shutdown_dispatch(device, nullptr);
}
#endif

extern "C" __declspec(dllexport) void embedded_draw_inline_settings(void *setting)
{
    draw_inline_settings(setting);
}

extern "C" __declspec(dllexport) void embedded_on_init_device(reshade::api::device *device)
{
    on_init_device(device);
}

extern "C" __declspec(dllexport) void embedded_on_destroy_device(reshade::api::device *device)
{
    if (nr::backends::handles(device)) dx12::on_destroy_device(device);
}

extern "C" __declspec(dllexport) void embedded_on_init_command_list(reshade::api::command_list *command_list)
{
    if (command_list != nullptr && nr::backends::handles(command_list->get_device()))
        dx12::on_init_command_list(command_list);
}

extern "C" __declspec(dllexport) void embedded_on_destroy_command_list(reshade::api::command_list *command_list)
{
    if (command_list != nullptr && nr::backends::handles(command_list->get_device()))
        dx12::on_destroy_command_list(command_list);
}

extern "C" __declspec(dllexport) void embedded_on_destroy_resource(
    reshade::api::device *device, reshade::api::resource resource)
{
    if (nr::backends::handles(device)) dx12::on_destroy_resource(device, resource);
}

extern "C" __declspec(dllexport) void embedded_draw_hotkey_overlay(reshade::api::effect_runtime *runtime)
{
    draw_hotkey_overlay(runtime);
}

extern "C" __declspec(dllexport) bool observed_evaluation_gate(
    std::uint64_t frame, std::uint8_t source, bool owned_retry)
{
    using GateFunction = bool (__fastcall *)(std::uint64_t, std::uint8_t, bool);
    const auto original = reinterpret_cast<GateFunction>(
        reinterpret_cast<std::uintptr_t>(g_target_module) + 0x0B1F40);
    const bool allowed = !(source != 1 && auto_source_enabled() && g_auto_source.fallback())
        && original(frame, source, owned_retry);
    auto &route = g_dispatch[source < g_dispatch.size() ? source : 0];
    route.calls.fetch_add(1, std::memory_order_relaxed);
    if (!allowed) route.rejected.fetch_add(1, std::memory_order_relaxed);
    route.frame.store(frame, std::memory_order_relaxed);
    route.tick.store(GetTickCount64(), std::memory_order_relaxed);
    if (g_frame_trace.enabled())
    {
        nr::FrameTraceEvent event;
        event.tick = GetTickCount64();
        event.thread = GetCurrentThreadId();
        event.frame = frame;
        event.source = source;
        event.retry = owned_retry;
        event.result = allowed ? 1 : 0;
        g_frame_trace.push(event);
    }
    return allowed;
}

extern "C" __declspec(dllexport) bool native_evaluation_gate(
    std::uint64_t frame, std::uint8_t source, bool owned_retry)
{
    const bool allowed = observed_evaluation_gate(frame, source, owned_retry);
    g_native_gate_calls.fetch_add(1, std::memory_order_relaxed);
    const auto last_frame = g_native_last_frame.exchange(frame, std::memory_order_relaxed);
    if (frame == last_frame) g_native_repeated_frame.fetch_add(1, std::memory_order_relaxed);
    if (frame < last_frame) g_native_backward_frame.fetch_add(1, std::memory_order_relaxed);
    const bool result = permit_native_evaluation(allowed,
        g_native_compatibility_enabled.load(std::memory_order_relaxed),
        g_xefg_probe_succeeded.load(std::memory_order_relaxed), source, owned_retry);
    if (!allowed) g_native_gate_rejected.fetch_add(1, std::memory_order_relaxed);
    if (frame == 0) g_native_zero_frame.fetch_add(1, std::memory_order_relaxed);
    return result;
}

extern "C" __declspec(dllexport) const char *NAME = "RenoDX Neural Resolution";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "V6.6: budgeted fence-retired NR textures, native frame checks without replay, persistent presets, aligned controls and 50-100% neural resolution.";

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        InitializeCriticalSection(&dx12::g_render_mutex);
        dx12::g_render_mutex_initialized = true;
        g_target_module = module;
        const HMODULE host = reinterpret_cast<GetHostModuleFunction>(
            reinterpret_cast<std::uintptr_t>(module) + kGetHostModuleRva)();
        if (host != nullptr)
        {
            g_get_config = reinterpret_cast<GetConfigFunction>(GetProcAddress(host, "ReShadeGetConfigValue"));
            g_set_config = reinterpret_cast<SetConfigFunction>(GetProcAddress(host, "ReShadeSetConfigValue"));
            g_log = reinterpret_cast<LogFunction>(GetProcAddress(host, "ReShadeLogMessage"));
            g_register_event_for_addon = reinterpret_cast<RegisterEventForAddonFunction>(
                GetProcAddress(host, "ReShadeRegisterEventForAddon"));
            g_unregister_event_for_addon = reinterpret_cast<UnregisterEventForAddonFunction>(
                GetProcAddress(host, "ReShadeUnregisterEventForAddon"));
        }
        imgui_function_table_instance() =
            *reinterpret_cast<const imgui_function_table **>(
                reinterpret_cast<std::uintptr_t>(module) + kImguiTableRva);
        int configured_scale = 100;
        int configured_resolve = 1, configured_transfer = 100, configured_color = 100;
        get_config_int("RenoDXNeuralResolution", "CostResolveMode", configured_resolve);
        get_config_int("RenoDXNeuralResolution", "CostTransferPercent", configured_transfer);
        get_config_int("RenoDXNeuralResolution", "CostColorPercent", configured_color);
        g_resolve_mode.store(std::clamp(configured_resolve, 0, 1));
        g_transfer_percent.store(std::clamp(configured_transfer, 0, 200));
        g_color_percent.store(std::clamp(configured_color, 0, 100));
        log_text(reshade::log::level::info,
            "NR COST SCALER 1: replaces legacy scaler; 25-100%, matched residual/direct, fence-retired native anchors. No additional hotkeys.");
        load_control_keys();
        int configured_sharpness = 35;
        // Migrate even saved V6.3=true configurations away from unsafe replay.
        set_config_int("RenoDXNeuralResolution", "XeFGNativeInputCompatibility", 0);
        if (get_config_int("RenoDXNeuralResolution", "SharpnessPercent", configured_sharpness) ||
            get_config_int("RenoDX DLSS Unified", "ReconstructionSharpness", configured_sharpness))
            g_sharpness_percent.store(std::clamp(configured_sharpness, 0, 100), std::memory_order_relaxed);
        if (get_config_int("RenoDXNeuralResolution", "AppliedScalePercentV6", configured_scale))
        {
            configured_scale = std::clamp(configured_scale, 25, 100);
            g_scale_percent.store(configured_scale, std::memory_order_relaxed);
            g_pending_scale_percent.store(configured_scale, std::memory_order_relaxed);
        }
        else
        {
            // Values from older layouts stay staged. V6 remains at 100% until Apply is pressed.
            int legacy_scale = 100;
            if (!get_config_int("RenoDXNeuralResolution", "AppliedScalePercentV3", legacy_scale) &&
                !get_config_int("RenoDXNeuralResolution", "AppliedScalePercentV2", legacy_scale) &&
                !get_config_int("RenoDXNeuralResolution", "ScalePercent", legacy_scale))
                get_config_int("RenoDX DLSS Unified", "NeuralResolutionScale", legacy_scale);
            if (legacy_scale != 100)
                g_pending_scale_percent.store(std::clamp(legacy_scale, 25, 100), std::memory_order_relaxed);
        }
        g_hook_installed.store(true, std::memory_order_release);
        const int current = *reinterpret_cast<volatile int *>(
            reinterpret_cast<std::uintptr_t>(module) + kPresetIndexRva);
        int saved_preset = current >= 1 && current <= 3 ? current : 1;
        if (!get_config_int("RenoDXNeuralResolution", "LastEnabledPreset", saved_preset))
            get_config_int("RenoDXNeuralResolution", "LastEnabledPresetV61", saved_preset);
        saved_preset = std::clamp(saved_preset, 1, 3);
        g_last_preset = saved_preset;
        if (current != saved_preset)
            queue_preset(saved_preset, OverlayMessage::none);
        if (g_register_event_for_addon != nullptr)
        {
            g_register_event_for_addon(module, reshade::addon_event::reshade_overlay,
                reinterpret_cast<void *>(&embedded_draw_hotkey_overlay));
            g_overlay_event_registered = true;
            g_register_event_for_addon(module, reshade::addon_event::present,
                reinterpret_cast<void *>(&dx12::capture::observe_present));
            g_register_event_for_addon(module, reshade::addon_event::reshade_present,
                reinterpret_cast<void *>(&dx12::capture::final_screen));
            g_register_event_for_addon(module, reshade::addon_event::reset_command_list,
                reinterpret_cast<void *>(&dx12::on_reset_recording));
            g_register_event_for_addon(module, reshade::addon_event::bind_pipeline,
                reinterpret_cast<void *>(&dx12::on_recording_pipeline));
            g_register_event_for_addon(module, reshade::addon_event::execute_command_list,
                reinterpret_cast<void *>(&dx12::on_execute_recording));
            g_register_event_for_addon(module, reshade::addon_event::execute_secondary_command_list,
                reinterpret_cast<void *>(&dx12::on_secondary_recording));
            g_lifetime_events_registered = true;
        }
        log_text(reshade::log::level::info,
            "RenoDX Neural Resolution V6.6: initialized; 512 MiB texture budget, nonblocking GPU retirement, legacy XeFG replay disabled.");
        log_text(reshade::log::level::info,
            "NR UI REVISION 3: Performance before Debug; Runtime API collapsed by default; obsolete diagnostic controls removed. Rendering backend unchanged.");
        log_text(reshade::log::level::info,
            "NR UI REVISION 4: Reconstruction Sharpness disabled at applied resolution 100%; saved sharpness retained.");
        log_text(reshade::log::level::info,
            "NR CAPTURE TEST 3: scoped no-codec capture; rebindable F6 toggle, F7 presets, F5 NR pair; PNG only. Capture behavior retained; Auto recovery reported separately.");
        log_text(reshade::log::level::info,
            "NR PASS TEST 1: bounded unpinned command-list recycling and live NR re-registration; all five frame-gate sites observed without replay. Waiting root cause is not yet confirmed.");
        log_message(reshade::log::level::info,
            "NR FG PROBE 2: native FrameGen callback diagnostics enabled; see NR AUTO RECOVERY 3 for the guarded Auto fallback policy.");
        log_message(reshade::log::level::info,
            "NR AUTO RECOVERY 3: guarded native SR fallback after 750 ms without successful FrameGen NR; sticky in Auto for this process; original native gate and manual hooks preserved.");
#ifdef NR_DX11_GAME_TEST
        log_text(reshade::log::level::info,
            "NR INTEGRATED GAME TEST 2: DX12 preserved; DX11 SDK-executable interception, packed-depth conversion and guarded lifecycle enabled; Debug collapsed by default; Vulkan/DX9/OpenGL native backends unavailable. OptiScaler optional.");
#endif
        if (!g_overlay_event_registered)
            log_text(reshade::log::level::error,
            "RenoDX Neural Resolution V6.6: ReShade overlay-event registration is unavailable; hotkeys and OSD are disabled safely.");
        break;
    }
    case DLL_PROCESS_DETACH:
        if (g_lifetime_events_registered && g_unregister_event_for_addon != nullptr)
        {
            g_unregister_event_for_addon(module, reshade::addon_event::reset_command_list,
                reinterpret_cast<void *>(&dx12::on_reset_recording));
            g_unregister_event_for_addon(module, reshade::addon_event::bind_pipeline,
                reinterpret_cast<void *>(&dx12::on_recording_pipeline));
            g_unregister_event_for_addon(module, reshade::addon_event::execute_command_list,
                reinterpret_cast<void *>(&dx12::on_execute_recording));
            g_unregister_event_for_addon(module, reshade::addon_event::execute_secondary_command_list,
                reinterpret_cast<void *>(&dx12::on_secondary_recording));
            g_lifetime_events_registered = false;
        }
        if (g_overlay_event_registered && g_unregister_event_for_addon != nullptr)
        {
            g_capture_off_until.store(0);
            g_unregister_event_for_addon(module, reshade::addon_event::present,
                reinterpret_cast<void *>(&dx12::capture::observe_present));
            g_unregister_event_for_addon(module, reshade::addon_event::reshade_present,
                reinterpret_cast<void *>(&dx12::capture::final_screen));
            g_unregister_event_for_addon(module, reshade::addon_event::reshade_overlay,
                reinterpret_cast<void *>(&embedded_draw_hotkey_overlay));
            g_overlay_event_registered = false;
        }
        uninstall_hook();
        g_target_module = nullptr;
        if (dx12::g_render_mutex_initialized)
        {
            DeleteCriticalSection(&dx12::g_render_mutex);
            dx12::g_render_mutex_initialized = false;
        }
        break;
    }
    return TRUE;
}
