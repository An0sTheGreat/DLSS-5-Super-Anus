#pragma once
#include <reshade_api_device.hpp>
#include <atomic>
#include <cstdint>

namespace nr::backends
{
struct Support
{
    const char *name;
    bool evaluator_available;
    const char *reason;
};

// This table is a truthful capability boundary, not an implementation of the
// missing transports. Never pass a foreign API handle to the DX12 binary ABI.
constexpr Support support(reshade::api::device_api api)
{
    using reshade::api::device_api;
    switch (api)
    {
    case device_api::d3d12:
        return {"DX12", true, "Native binary adapter; GPU/game validation required."};
    case device_api::d3d11:
#ifdef NR_DX11_GAME_TEST
        // This false value routes *resource callbacks*, not the native bridge.
        // Never send DX11 resources through the DX12 binary adapter.
        return {"DX11", false, "DX11-to-DX12 transport: experimental native SR bridge enabled; awaiting captured inputs."};
#else
        return {"DX11", false, "NR unavailable: DX11-to-DX12 transport and input capture are not integrated."};
#endif
    case device_api::vulkan:
#ifdef NR_EXPERIMENTAL_VULKAN
        return {"Vulkan", false, "Experimental native post-DLSS NR: 100% resolution and one pass only."};
#else
        return {"Vulkan", false, "NR unavailable: Vulkan-to-DX12 transport and input capture are not integrated."};
#endif
    case device_api::d3d9:
        return {"DX9", false, "NR unavailable: transport and a depth/motion input provider are required."};
    case device_api::opengl:
        return {"OpenGL", false, "NR unavailable: transport and a depth/motion input provider are required."};
    default:
        return {"Unknown/unsupported API", false, "No neural-rendering backend is available for this API."};
    }
}

inline bool handles(reshade::api::device *device)
{
    return device != nullptr && support(device->get_api()).evaluator_available;
}

// A device announcement alone does not prove the neural consumer received its
// inputs. Retain only the identity of a tracked device with a successful CPU NR
// wrapper return; never dereference this pointer from the UI. This is not proof
// of completed GPU work or correct Feeder output.
class EvaluationDevice
{
public:
    void observe(std::uintptr_t device, std::uint64_t result)
    {
        if (device != 0 && (result & 0xFFu) != 0)
            device_.store(device, std::memory_order_release);
    }
    void forget(std::uintptr_t device)
    {
        device_.compare_exchange_strong(device, 0, std::memory_order_acq_rel);
    }
    bool observed() const { return device_.load(std::memory_order_acquire) != 0; }
private:
    std::atomic_uintptr_t device_ = 0;
};

struct RuntimeStatus
{
    const char *presentation_name;
    const char *evaluation_state;
    const char *detail;
    bool controls_available;
};

constexpr RuntimeStatus runtime_status(reshade::api::device_api presentation_api,
                                      bool tracked_dx12_evaluation, bool lifetime_events)
{
    const auto presentation = support(presentation_api);
    if (!lifetime_events)
        return {presentation.name, "Tracking unavailable",
            "Resolution controls require ReShade command-list lifetime events.", false};
#ifdef NR_EXPERIMENTAL_VULKAN
    if (presentation_api == reshade::api::device_api::vulkan)
        return {presentation.name, "Vulkan native hook",
            "Experimental post-DLSS NR supports 100% resolution and one pass. Unsupported settings preserve native output.", true};
#endif
    if (tracked_dx12_evaluation)
        return {presentation.name, "DX12 evaluation observed",
            presentation.evaluator_available ? "" :
            "NR uses a separate DX12 device. The presentation API does not disable its resolution controls.", true};
    if (presentation.evaluator_available)
        return {presentation.name, "DX12 (awaiting tracked evaluation)", "", true};
#ifdef NR_DX11_GAME_TEST
    if (presentation_api == reshade::api::device_api::d3d11)
        return {presentation.name, "DX11 native SR bridge (awaiting NR evaluation)",
            "Experimental private DX12 consumer. Enable native DLSS SR; uncaptured/unsupported inputs keep native output. Controls unlock after tracked NR evaluation.", false};
#endif
    return {presentation.name, "Awaiting compatible DX12 inputs",
        "For games without native DLSS, use external DLSS 5 Feeder and your chosen compatible motion estimator. No motion estimator is bundled.", false};
}
}
