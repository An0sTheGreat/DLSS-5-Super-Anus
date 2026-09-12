#pragma once
// TEST ONLY: mirrors the verified RenoDX direct-snippet caller compatibility
// mechanism, scoped to this test process and this exact NR DLL. No driver table,
// installed DLL, machine layer registration, or executable code is patched.
#include <bcrypt.h>
#include <array>

namespace vulkan_direct_probe
{
inline float requested_scaling_ratio = 1.0f;
inline NVSDK_NGX_Result scaling_ratio(NVSDK_NGX_Parameter *parameters)
{
    if (!parameters) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    parameters->Set("DLSSNR.ScalingRatio", requested_scaling_ratio);
    return NVSDK_NGX_Result_Success;
}

inline void creation_parameters(NVSDK_NGX_Parameter *parameters, unsigned width, unsigned height)
{
    // Exact key/type contract from the working RenoDX NR feature setup. NR is
    // not SR: at 100% its input/output extents match and it needs its own keys.
    parameters->Set("CreationNodeMask", 1u); parameters->Set("VisibilityNodeMask", 1u);
    for (const char *key : { "Width", "OutWidth", "DLSSNR.Width", "DLSSNR.InputWidth", "DLSSNR.OutputWidth", "DLSSNR.Output.Width" })
        parameters->Set(key, width);
    for (const char *key : { "Height", "OutHeight", "DLSSNR.Height", "DLSSNR.InputHeight", "DLSSNR.OutputHeight", "DLSSNR.Output.Height" })
        parameters->Set(key, height);
    parameters->Set("PerfQualityValue", 2);
    parameters->Set("DLSSNR.Hint.Render.Preset", 1u);
    parameters->Set("DLSSNRComputeScalingRatioCallback", reinterpret_cast<void *>(&scaling_ratio));
    parameters->Set("DLSSNR.ScalingRatio", requested_scaling_ratio); parameters->Set("DLSSNR.Scale", requested_scaling_ratio);
    parameters->Set("DLSSNR.Upscaling", 0);
}
inline HMODULE caller_module = nullptr;
inline DWORD WINAPI caller_filename(HMODULE module, LPWSTR output, DWORD capacity)
{
    if (module != caller_module) return GetModuleFileNameW(module, output, capacity);
    constexpr wchar_t runtime_name[] = L"_nvngx.dll";
    if (!capacity) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    const DWORD length = static_cast<DWORD>(std::size(runtime_name) - 1);
    const DWORD copied = length < capacity ? length : capacity - 1;
    std::memcpy(output, runtime_name, copied * sizeof(wchar_t)); output[copied] = 0;
    if (length >= capacity) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return capacity; }
    return length;
}

inline bool exact_snippet(const wchar_t *path)
{
    constexpr unsigned char expected[32] = {
        0xe1,0x6b,0xcf,0x15,0xe1,0x6e,0x13,0xf5,0x27,0x49,0x1c,0xdf,0x78,0x45,0xb2,0xfe,
        0x65,0x21,0xa7,0x38,0xd8,0xf7,0xc9,0xc7,0x21,0x86,0x6a,0x84,0x96,0xe1,0xfc,0x8e };
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool valid = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (valid) valid = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 65536> bytes;
    while (valid)
    {
        DWORD count = 0;
        if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)) { valid = false; break; }
        if (!count) break;
        valid = BCryptHashData(hash, bytes.data(), count, 0) >= 0;
    }
    unsigned char result[32] = {};
    if (valid) valid = BCryptFinishHash(hash, result, sizeof(result), 0) >= 0 && !std::memcmp(result, expected, sizeof(result));
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return valid;
}

struct Binding
{
    using Init = NVSDK_NGX_Result (*)(unsigned long long, const wchar_t *, VkInstance, VkPhysicalDevice, VkDevice,
        PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr, NVSDK_NGX_Version, const NVSDK_NGX_Parameter *);
    using Create = NVSDK_NGX_Result (*)(VkDevice, VkCommandBuffer, NVSDK_NGX_Feature, const NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
    using Release = NVSDK_NGX_Result (*)(NVSDK_NGX_Handle *);
    using Evaluate = NVSDK_NGX_Result (*)(VkCommandBuffer, const NVSDK_NGX_Handle *, const NVSDK_NGX_Parameter *, void *);
    using Shutdown = NVSDK_NGX_Result (*)(VkDevice);
    HMODULE module = nullptr;
    Init init = nullptr;
    Create create = nullptr;
    Release release = nullptr;
    Evaluate evaluate = nullptr;
    Shutdown shutdown = nullptr;
    ULONG_PTR *slot = nullptr;
    ULONG_PTR original = 0;
    bool attempted = false, initialized = false;
    bool patched = false;

    bool open(const std::wstring &directory)
    {
        if (module || caller_module) return false; // one scoped owner in this probe
        const auto path = directory + L"nvngx_dlssnr.dll";
        if (!exact_snippet(path.c_str())) { std::puts("FAIL: direct NR snippet SHA-256 mismatch"); return false; }
        module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) return false;
        init = reinterpret_cast<Init>(GetProcAddress(module, "NVSDK_NGX_VULKAN_Init_Ext2"));
        create = reinterpret_cast<Create>(GetProcAddress(module, "NVSDK_NGX_VULKAN_CreateFeature1"));
        release = reinterpret_cast<Release>(GetProcAddress(module, "NVSDK_NGX_VULKAN_ReleaseFeature"));
        evaluate = reinterpret_cast<Evaluate>(GetProcAddress(module, "NVSDK_NGX_VULKAN_EvaluateFeature"));
        shutdown = reinterpret_cast<Shutdown>(GetProcAddress(module, "NVSDK_NGX_VULKAN_Shutdown1"));
        if (!init || !create || !release || !shutdown || !evaluate) return false;
        auto *base = reinterpret_cast<unsigned char *>(module);
        auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
        auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
        const size_t size = nt->OptionalHeader.SizeOfImage;
        const auto imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!imports.VirtualAddress || imports.VirtualAddress >= size || imports.Size > size - imports.VirtualAddress) return false;
        for (size_t offset = 0; offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imports.Size; offset += sizeof(IMAGE_IMPORT_DESCRIPTOR))
        {
            const auto *desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + imports.VirtualAddress + offset);
            if (!desc->Name) break;
            if (!desc->OriginalFirstThunk || !desc->FirstThunk) return false;
            for (size_t index = 0; ; ++index)
            {
                const auto lookup = static_cast<size_t>(desc->OriginalFirstThunk) + index * sizeof(ULONG_PTR);
                const auto address = static_cast<size_t>(desc->FirstThunk) + index * sizeof(ULONG_PTR);
                if (lookup + sizeof(ULONG_PTR) > size || address + sizeof(ULONG_PTR) > size) return false;
                const auto name = *reinterpret_cast<const ULONG_PTR *>(base + lookup);
                if (!name) break;
                if (IMAGE_SNAP_BY_ORDINAL64(name)) continue;
                constexpr char wanted[] = "GetModuleFileNameW";
                if (name >= size || sizeof(WORD) + sizeof(wanted) > size - name) return false;
                if (std::memcmp(base + name + sizeof(WORD), wanted, sizeof(wanted))) continue;
                if (slot) return false; // unexpected duplicate import: fail before writing
                slot = reinterpret_cast<ULONG_PTR *>(base + address);
            }
        }
        if (!slot) return false;
        original = *slot;
        if (original != reinterpret_cast<ULONG_PTR>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetModuleFileNameW"))) return false;
        DWORD protection = 0;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return false;
        caller_module = GetModuleHandleW(nullptr);
        *slot = reinterpret_cast<ULONG_PTR>(&caller_filename);
        patched = true;
        DWORD ignored = 0;
        if (!VirtualProtect(slot, sizeof(*slot), protection, &ignored)) return false;
        std::puts("TEST ONLY: exact NR snippet loaded; scoped caller compatibility installed.");
        return true;
    }

    // Only call after successful snippet shutdown, or before any init attempt.
    // Failed initialization/shutdown retains module + callback until process exit.
    bool close()
    {
        if (attempted) return false;
        if (!module) return !patched;
        if (patched)
        {
            if (!slot || *slot != reinterpret_cast<ULONG_PTR>(&caller_filename)) return false;
            DWORD protection = 0, ignored = 0;
            if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return false;
            *slot = original;
            if (!VirtualProtect(slot, sizeof(*slot), protection, &ignored)) return false;
            patched = false; caller_module = nullptr;
        }
        slot = nullptr;
        if (module) FreeLibrary(module);
        module = nullptr;
        init = nullptr; create = nullptr; release = nullptr; evaluate = nullptr; shutdown = nullptr;
        return true;
    }
};
}
