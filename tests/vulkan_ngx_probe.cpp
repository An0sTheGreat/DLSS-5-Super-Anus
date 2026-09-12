// Headless Vulkan/NGX feasibility probe. Default uses the SDK frontend; the
// explicit direct mode tests RenoDX-style snippet binding in this process only.
// Capability/init results are NOT proof of NR evaluation or rendered pixels.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "nvsdk_ngx_vk.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <stdexcept>
#include "vulkan_direct_binding.hpp"

static void check(VkResult value, const char *stage)
{
    if (value != VK_SUCCESS) { std::printf("FAIL %s: Vulkan %d\n", stage, value); throw std::runtime_error(stage); }
}
static bool ngx(NVSDK_NGX_Result value, const char *stage)
{
    std::printf("%s: NGX 0x%08X\n", stage, static_cast<unsigned>(value));
    return value == NVSDK_NGX_Result_Success;
}
static void NVSDK_CONV ngx_log(const char *text, NVSDK_NGX_Logging_Level level, NVSDK_NGX_Feature component)
{
    // CRT serializes each fprintf on this FILE; never retain the borrowed text.
    std::fprintf(stdout, "NGX log [%u/%u]: %s\n", static_cast<unsigned>(component), static_cast<unsigned>(level), text ? text : "");
}
template<class T> static T required(PFN_vkVoidFunction function, const char *name)
{
    if (!function) throw std::runtime_error(name);
    return reinterpret_cast<T>(function);
}

#include "vulkan_image_fixture.hpp"

int main(int argc, char **argv)
{
    const bool sr_control = argc == 2 && !std::strcmp(argv[1], "--sr-control");
    const bool zero_intensity = argc == 2 && !std::strcmp(argv[1], "--direct-nr-zero");
    const bool half_scale = argc == 2 && !std::strcmp(argv[1], "--direct-nr-inplace-half");
    const bool in_place = half_scale || (argc == 2 && !std::strcmp(argv[1], "--direct-nr-inplace"));
    const bool direct_nr = zero_intensity || in_place || (argc == 2 && !std::strcmp(argv[1], "--direct-nr"));
    if (argc > 1 && !sr_control && !direct_nr) { std::puts("Usage: vulkan-ngx-probe [--sr-control|--direct-nr]"); return 1; }
    vulkan_direct_probe::requested_scaling_ratio = half_scale ? 0.5f : 1.0f;
    std::printf("Requested feature: %s\n", sr_control ? "SR control (1)" : "prerelease NR (18)");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    HMODULE loader = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!loader) { std::puts("FAIL: no system Vulkan loader"); return 1; }
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkDestroyInstance destroy_instance = nullptr;
    PFN_vkDestroyDevice destroy_device = nullptr;
    PFN_vkDestroyCommandPool destroy_pool = nullptr;
    PFN_vkDestroyFence destroy_fence = nullptr;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    NVSDK_NGX_Handle *feature = nullptr;
    bool initialized = false;
    bool init_attempted = false;
    NVSDK_NGX_Parameter *parameters = nullptr;
    vulkan_direct_probe::Binding direct;
    VulkanImageFixture images;
    int outcome = 1;
    try
    {
        auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr"));
        if (!gipa) throw std::runtime_error("vkGetInstanceProcAddr");
#define INSTANCE_FUNCTION(name) const auto name = required<PFN_##name>(gipa(instance, #name), #name)
        INSTANCE_FUNCTION(vkCreateInstance);
        INSTANCE_FUNCTION(vkEnumerateInstanceLayerProperties);
        const char *project = "b463b3f3-7014-4b4b-b9d5-d55366670011";
        wchar_t directory[MAX_PATH] = {};
        const auto length = GetModuleFileNameW(nullptr, directory, MAX_PATH);
        if (!length || length >= MAX_PATH) throw std::runtime_error("executable directory");
        auto slash = wcsrchr(directory, L'\\'); if (!slash) throw std::runtime_error("executable directory");
        slash[1] = 0;
        const wchar_t *paths[] = { directory };
        NVSDK_NGX_FeatureCommonInfo common = {};
        common.PathListInfo = { paths, 1 };
        common.LoggingInfo = { &ngx_log, NVSDK_NGX_LOGGING_LEVEL_VERBOSE, true };
        NVSDK_NGX_FeatureDiscoveryInfo discovery = {};
        discovery.SDKVersion = NVSDK_NGX_Version_API;
        // This exact prerelease NR binary identifies its feature as 0x12 in
        // GetFeatureRequirements. SDK calls it Reserved18; not a public NR API.
        discovery.FeatureID = sr_control ? NVSDK_NGX_Feature_SuperSampling : NVSDK_NGX_Feature_Reserved18;
        discovery.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
        discovery.Identifier.v.ProjectDesc = { project, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0" };
        discovery.ApplicationDataPath = directory;
        discovery.FeatureInfo = &common;
        uint32_t extension_count = 0;
        VkExtensionProperties *extensions = nullptr;
        if (!ngx(NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&discovery, &extension_count, &extensions),
                 "instance requirements")) throw std::runtime_error("instance requirements unavailable");
        if (extension_count > 128 || (extension_count && !extensions)) throw std::runtime_error("invalid extension list");
        std::vector<std::string> instance_names;
        for (uint32_t i = 0; i < extension_count; ++i) instance_names.emplace_back(extensions[i].extensionName);
        std::vector<const char *> instance_extensions;
        for (const auto &name : instance_names) { std::printf("instance extension: %s\n", name.c_str()); instance_extensions.push_back(name.c_str()); }
        uint32_t layer_count = 0;
        check(vkEnumerateInstanceLayerProperties(&layer_count, nullptr), "enumerate layers");
        std::vector<VkLayerProperties> layers(layer_count);
        check(vkEnumerateInstanceLayerProperties(&layer_count, layers.data()), "enumerate layers");
        const char *validation = "VK_LAYER_KHRONOS_validation";
        bool validation_available = false;
        for (const auto &layer : layers) if (!std::strcmp(layer.layerName, validation)) validation_available = true;
        std::printf("Khronos validation: %s\n", validation_available ? "enabled" : "UNAVAILABLE (not a validation pass)");
        VkApplicationInfo app = {}; app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "NR Vulkan isolated probe"; app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ici = {}; ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ici.pApplicationInfo = &app;
        ici.enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()); ici.ppEnabledExtensionNames = instance_extensions.data();
        if (validation_available) { ici.enabledLayerCount = 1; ici.ppEnabledLayerNames = &validation; }
        check(vkCreateInstance(&ici, nullptr, &instance), "create instance");
        destroy_instance = required<PFN_vkDestroyInstance>(gipa(instance, "vkDestroyInstance"), "vkDestroyInstance");
        INSTANCE_FUNCTION(vkEnumeratePhysicalDevices);
        INSTANCE_FUNCTION(vkGetPhysicalDeviceProperties);
        INSTANCE_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties);
        INSTANCE_FUNCTION(vkGetPhysicalDeviceFeatures2);
        INSTANCE_FUNCTION(vkCreateDevice);
        INSTANCE_FUNCTION(vkGetDeviceProcAddr);
        uint32_t device_count = 0;
        check(vkEnumeratePhysicalDevices(instance, &device_count, nullptr), "enumerate devices");
        std::vector<VkPhysicalDevice> devices(device_count);
        check(vkEnumeratePhysicalDevices(instance, &device_count, devices.data()), "enumerate devices");
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        uint32_t family = 0;
        for (auto candidate : devices)
        {
            VkPhysicalDeviceProperties properties = {}; vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.vendorID != 0x10DE) continue;
            uint32_t count = 0; vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
            for (uint32_t i = 0; i < count; ++i)
                if (families[i].queueCount && (families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) { physical = candidate; family = i; break; }
            if (physical) { std::printf("device: %s; API %u.%u.%u\n", properties.deviceName,
                VK_API_VERSION_MAJOR(properties.apiVersion), VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion)); break; }
        }
        if (!physical) throw std::runtime_error("no NVIDIA graphics+compute queue");
        NVSDK_NGX_FeatureRequirement requirement = {};
        const auto support_result = NVSDK_NGX_VULKAN_GetFeatureRequirements(instance, physical, &discovery, &requirement);
        const bool supported = ngx(support_result, "feature requirements");
        if (supported)
        {
            std::printf("support flags: 0x%X; minimum architecture 0x%X\n", requirement.FeatureSupported, requirement.MinHWArchitecture);
            if (requirement.FeatureSupported) throw std::runtime_error("feature requirements report unsupported");
        }
        else if (support_result == NVSDK_NGX_Result_FAIL_NotImplemented)
            std::puts("Requirements query NOT IMPLEMENTED: support UNKNOWN. Continue discovery/init diagnostics only.");
        else throw std::runtime_error("feature requirements failed");
        extension_count = 0; extensions = nullptr;
        if (!ngx(NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, physical, &discovery, &extension_count, &extensions),
                 "device requirements")) throw std::runtime_error("device requirements unavailable");
        if (extension_count > 128 || (extension_count && !extensions)) throw std::runtime_error("invalid device extension list");
        std::vector<const char *> device_extensions;
        for (uint32_t i = 0; i < extension_count; ++i)
        { std::printf("device extension: %s\n", extensions[i].extensionName); device_extensions.push_back(extensions[i].extensionName); }
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo qci = {}; qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
        VkDeviceCreateInfo dci = {}; dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        VkPhysicalDeviceBufferDeviceAddressFeatures address = {};
        address.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        VkPhysicalDeviceFeatures2 supported_features = {};
        supported_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; supported_features.pNext = &address;
        vkGetPhysicalDeviceFeatures2(physical, &supported_features);
        if (!address.bufferDeviceAddress) throw std::runtime_error("buffer device address unavailable");
        address.bufferDeviceAddressCaptureReplay = address.bufferDeviceAddressMultiDevice = VK_FALSE;
        dci.pNext = &address;
        dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()); dci.ppEnabledExtensionNames = device_extensions.data();
        check(vkCreateDevice(physical, &dci, nullptr, &device), "create device");
        destroy_device = required<PFN_vkDestroyDevice>(vkGetDeviceProcAddr(device, "vkDestroyDevice"), "vkDestroyDevice");
        init_attempted = true;
        if (!ngx(NVSDK_NGX_VULKAN_Init_with_ProjectID(project, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", directory,
                 instance, physical, device, gipa, vkGetDeviceProcAddr, &common), "Vulkan NGX init"))
            throw std::runtime_error("Vulkan NGX initialization failed");
        initialized = true;
        if (!ngx(NVSDK_NGX_VULKAN_GetCapabilityParameters(&parameters), "Vulkan capabilities") || !parameters)
            throw std::runtime_error("capability parameters");
        for (const char *key : { "SuperSampling.Available", "DLSSNR.Available", "DLSSNR.FeatureInitResult" })
        { int value = 0; auto result = parameters->Get(key, &value); std::printf("%s: query 0x%X value 0x%X\n", key, result, value); }
        if (direct_nr)
        {
            if (!direct.open(directory)) throw std::runtime_error("direct NR binding failed");
            direct.attempted = true;
            const auto result = direct.init(0x876232c, directory, instance, physical, device,
                gipa, vkGetDeviceProcAddr, NVSDK_NGX_Version_API, nullptr);
            if (!ngx(result, "direct Vulkan NR init")) throw std::runtime_error("direct NR initialization failed");
            direct.initialized = true;
        }
#define DEVICE_FUNCTION(name) const auto name = required<PFN_##name>(vkGetDeviceProcAddr(device, #name), #name)
        DEVICE_FUNCTION(vkCreateCommandPool); DEVICE_FUNCTION(vkAllocateCommandBuffers);
        DEVICE_FUNCTION(vkBeginCommandBuffer); DEVICE_FUNCTION(vkEndCommandBuffer);
        DEVICE_FUNCTION(vkCreateFence); DEVICE_FUNCTION(vkGetDeviceQueue);
        DEVICE_FUNCTION(vkQueueSubmit); DEVICE_FUNCTION(vkWaitForFences);
        destroy_pool = required<PFN_vkDestroyCommandPool>(vkGetDeviceProcAddr(device, "vkDestroyCommandPool"), "vkDestroyCommandPool");
        destroy_fence = required<PFN_vkDestroyFence>(vkGetDeviceProcAddr(device, "vkDestroyFence"), "vkDestroyFence");
        VkCommandPoolCreateInfo pci = {}; pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = family;
        check(vkCreateCommandPool(device, &pci, nullptr, &pool), "create command pool");
        VkCommandBufferAllocateInfo cai = {}; cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
        VkCommandBuffer commands = VK_NULL_HANDLE;
        check(vkAllocateCommandBuffers(device, &cai, &commands), "allocate command buffer");
        VkCommandBufferBeginInfo cbi = {}; cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(commands, &cbi), "begin command buffer");
        parameters->Reset();
        parameters->Set("Width", 1280u); parameters->Set("Height", 720u);
        parameters->Set("OutWidth", 1920u); parameters->Set("OutHeight", 1080u);
        parameters->Set("PerfQualityValue", 2u); parameters->Set("DLSS.Feature.Create.Flags", 0u);
        if (direct_nr) vulkan_direct_probe::creation_parameters(parameters, 1920, 1080);
        const auto created = direct_nr ? direct.create(device, commands, discovery.FeatureID, parameters, &feature) :
            NVSDK_NGX_VULKAN_CreateFeature1(device, commands, discovery.FeatureID, parameters, &feature);
        std::printf("NR snippet module loaded: %s\n", GetModuleHandleW(L"nvngx_dlssnr.dll") ? "yes" : "no");
        if (!ngx(created, "Vulkan feature create") || !feature) throw std::runtime_error("feature creation failed (no evaluation attempted)");
        if (direct_nr)
        {
            images.create(instance, physical, device, gipa, vkGetDeviceProcAddr);
            images.record_inputs(commands, parameters, zero_intensity ? 0.0f : 1.0f, in_place);
            if (!ngx(direct.evaluate(commands, feature, parameters, nullptr), "direct Vulkan NR evaluate"))
                throw std::runtime_error("direct NR evaluation failed; recording discarded");
            images.record_readback(commands, in_place ? 0u : 3u);
        }
        check(vkEndCommandBuffer(commands), "end command buffer");
        VkFenceCreateInfo fci = {}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check(vkCreateFence(device, &fci, nullptr, &fence), "create fence");
        VkQueue queue = VK_NULL_HANDLE; vkGetDeviceQueue(device, family, 0, &queue);
        VkSubmitInfo submit = {}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &commands;
        // On submission/completion failure retain graphics and NGX ownership:
        // never destroy possibly in-flight objects or synthesize completion.
        if (vkQueueSubmit(queue, 1, &submit, fence) != VK_SUCCESS ||
            vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull) != VK_SUCCESS)
        { std::puts("FAIL: submission/completion; objects retained until process exit."); return 1; }
        if (direct_nr)
        {
            if (!images.capture()) throw std::runtime_error("invalid Vulkan GPU readback");
            std::printf("Direct NR image checkpoint: intensity=%.0f; compare pixels separately. NOT game compatibility.\n", zero_intensity ? 0.0 : 1.0);
        }
        else std::puts("Feature creation/submission completed. NO evaluation, NO NR pixels, NO game compatibility claim.");
        outcome = 0;
    }
    catch (const std::exception &error) { std::printf("STOP: %s\n", error.what()); }
    if (direct.attempted && !direct.initialized)
    { std::puts("Retaining objects after ambiguous direct NR init failure; no retry."); return 1; }
    if (init_attempted && !initialized)
    { std::puts("Retaining Vulkan objects after ambiguous NGX init failure; process exits without retry."); return 1; }
    if (pool && destroy_pool) destroy_pool(device, pool, nullptr);
    if (fence && destroy_fence) destroy_fence(device, fence, nullptr);
    if (feature && !ngx(direct_nr ? direct.release(feature) : NVSDK_NGX_VULKAN_ReleaseFeature(feature), "release feature"))
    { std::puts("Retaining session after ambiguous feature release failure."); return 1; }
    if (direct.initialized)
    {
        if (!ngx(direct.shutdown(device), "direct Vulkan NR shutdown"))
        { std::puts("Retaining objects after ambiguous direct NR shutdown failure."); return 1; }
        direct.initialized = direct.attempted = false;
    }
    if (!direct.close()) { std::puts("FAIL: direct NR caller restoration"); return 1; }
    images.destroy();
    if (parameters && !ngx(NVSDK_NGX_VULKAN_DestroyParameters(parameters), "destroy parameters"))
    { std::puts("Retaining session after ambiguous parameter destroy failure."); return 1; }
    if (initialized && !ngx(NVSDK_NGX_VULKAN_Shutdown1(device), "Vulkan NGX shutdown"))
    { std::puts("Retaining Vulkan objects after ambiguous NGX shutdown failure."); return 1; }
    if (device && destroy_device) destroy_device(device, nullptr);
    if (instance && destroy_instance) destroy_instance(instance, nullptr);
    FreeLibrary(loader);
    return outcome;
}
