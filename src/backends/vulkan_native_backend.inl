// Experimental Vulkan path: append the verified NR snippet directly after a
// successful native DLSS SR evaluation in the same recording command buffer.
namespace vulkan_native
{
using Result = NVSDK_NGX_Result;
using Params = NVSDK_NGX_Parameter;
using Handle = NVSDK_NGX_Handle;
using InitProject = Result (*)(const char *, NVSDK_NGX_EngineType, const char *, const wchar_t *,
    VkInstance, VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr,
    NVSDK_NGX_Version, const NVSDK_NGX_FeatureCommonInfo *);
using Create = Result (*)(VkDevice, VkCommandBuffer, NVSDK_NGX_Feature, Params *, Handle **);
using Evaluate = Result (*)(VkCommandBuffer, const Handle *, const Params *, void *);
using Release = Result (*)(Handle *);

struct Feature { const Handle *native = nullptr; Handle *nr = nullptr; };
Feature features[32];
SRWLOCK mutex = SRWLOCK_INIT;
InitProject original_init = nullptr;
Create original_create = nullptr;
Evaluate original_evaluate = nullptr, original_evaluate_c = nullptr;
Release original_release = nullptr;
VkDevice device = VK_NULL_HANDLE;
HMODULE snippet = nullptr;
HMODULE caller_module = nullptr;
using SnippetInit = Result (*)(unsigned long long, const wchar_t *, VkInstance, VkPhysicalDevice, VkDevice,
    PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr, NVSDK_NGX_Version, const Params *);
SnippetInit snippet_init = nullptr;
Create snippet_create = nullptr;
Evaluate snippet_evaluate = nullptr;
Release snippet_release = nullptr;
ULONG_PTR *caller_slot = nullptr, caller_original = 0;
std::atomic_bool startup_started = false, hook_finished = false;
DWORD evaluation_tls = TLS_OUT_OF_INDEXES;
bool initialized = false, failed = false;
using GetProcAddressFn = FARPROC (WINAPI *)(HMODULE, LPCSTR);
GetProcAddressFn original_get_proc_address = nullptr;

void message(const char *stage, unsigned result)
{
    char text[256] = {};
    diagnostic_format(text, "NR Vulkan experimental: %s (0x%08X).", stage, result);
    log_text(reshade::log::level::info, text);
}

Result scaling_ratio(Params *parameters)
{
    if (!parameters) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    parameters->Set("DLSSNR.ScalingRatio", 1.0f);
    return NVSDK_NGX_Result_Success;
}

DWORD WINAPI caller_filename(HMODULE module, LPWSTR output, DWORD capacity)
{
    if (module != caller_module) return GetModuleFileNameW(module, output, capacity);
    constexpr wchar_t name[] = L"_nvngx.dll";
    if (!capacity) return 0;
    const DWORD length = static_cast<DWORD>(std::size(name) - 1);
    const DWORD copied = length < capacity ? length : capacity - 1;
    memcpy(output, name, copied * sizeof(wchar_t)); output[copied] = 0;
    if (copied != length) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return capacity; }
    return length;
}

bool exact_snippet(const wchar_t *path)
{
    constexpr unsigned char expected[32] = {0xe1,0x6b,0xcf,0x15,0xe1,0x6e,0x13,0xf5,0x27,0x49,0x1c,0xdf,0x78,0x45,0xb2,0xfe,
        0x65,0x21,0xa7,0x38,0xd8,0xf7,0xc9,0xc7,0x21,0x86,0x6a,0x84,0x96,0xe1,0xfc,0x8e};
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    unsigned char bytes[1024];
    while (ok) { DWORD count = 0; if (!ReadFile(file, bytes, sizeof(bytes), &count, nullptr)) { ok = false; break; }
        if (!count) break; ok = BCryptHashData(hash, bytes, count, 0) >= 0; }
    unsigned char digest[32] = {};
    ok = ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0 && memcmp(digest, expected, sizeof(digest)) == 0;
    if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); CloseHandle(file);
    return ok;
}

bool patch_caller_import()
{
    auto *base = reinterpret_cast<unsigned char *>(snippet);
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const size_t size = nt->OptionalHeader.SizeOfImage;
    const auto imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress || imports.VirtualAddress >= size) return false;
    for (size_t offset = 0; offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imports.Size; offset += sizeof(IMAGE_IMPORT_DESCRIPTOR))
    {
        const auto *desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + imports.VirtualAddress + offset);
        if (!desc->Name) break;
        for (size_t i = 0; desc->OriginalFirstThunk && desc->FirstThunk; ++i)
        {
            const size_t lookup = desc->OriginalFirstThunk + i * sizeof(ULONG_PTR), address = desc->FirstThunk + i * sizeof(ULONG_PTR);
            if (lookup + sizeof(ULONG_PTR) > size || address + sizeof(ULONG_PTR) > size) return false;
            const auto name = *reinterpret_cast<const ULONG_PTR *>(base + lookup); if (!name) break;
            if (IMAGE_SNAP_BY_ORDINAL64(name) || name >= size) continue;
            if (memcmp(base + name + sizeof(WORD), "GetModuleFileNameW", 19) != 0) continue;
            if (caller_slot) return false; caller_slot = reinterpret_cast<ULONG_PTR *>(base + address);
        }
    }
    if (!caller_slot) return false;
    caller_original = *caller_slot;
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(caller_slot, sizeof(*caller_slot), PAGE_READWRITE, &protection)) return false;
    *caller_slot = reinterpret_cast<ULONG_PTR>(&caller_filename);
    return VirtualProtect(caller_slot, sizeof(*caller_slot), protection, &ignored) != FALSE;
}

bool open_snippet(const wchar_t *directory)
{
    (void)directory;
    wchar_t path[MAX_PATH] = {}; size_t length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    while (length && path[length - 1] != L'\\' && path[length - 1] != L'/') --length;
    constexpr wchar_t filename[] = L"nvngx_dlssnr.dll";
    for (size_t i = 0; filename[i] && length + 1 < std::size(path); ++i) path[length++] = filename[i];
    path[length] = 0;
    if (!exact_snippet(path)) { message("exact nvngx_dlssnr.dll not found", 0); return false; }
    snippet = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    caller_module = GetModuleHandleW(nullptr);
    if (!snippet || !patch_caller_import()) return false;
    snippet_init = reinterpret_cast<SnippetInit>(original_get_proc_address(snippet, "NVSDK_NGX_VULKAN_Init_Ext2"));
    snippet_create = reinterpret_cast<Create>(original_get_proc_address(snippet, "NVSDK_NGX_VULKAN_CreateFeature1"));
    snippet_evaluate = reinterpret_cast<Evaluate>(original_get_proc_address(snippet, "NVSDK_NGX_VULKAN_EvaluateFeature"));
    snippet_release = reinterpret_cast<Release>(original_get_proc_address(snippet, "NVSDK_NGX_VULKAN_ReleaseFeature"));
    return snippet_init && snippet_create && snippet_evaluate && snippet_release;
}

void set_creation(Params *p, unsigned width, unsigned height)
{
    p->Set("CreationNodeMask", 1u); p->Set("VisibilityNodeMask", 1u);
    for (const char *key : {"Width","OutWidth","DLSSNR.Width","DLSSNR.InputWidth","DLSSNR.OutputWidth","DLSSNR.Output.Width"}) p->Set(key, width);
    for (const char *key : {"Height","OutHeight","DLSSNR.Height","DLSSNR.InputHeight","DLSSNR.OutputHeight","DLSSNR.Output.Height"}) p->Set(key, height);
    p->Set("PerfQualityValue", 2); p->Set("DLSSNR.Hint.Render.Preset", 1u);
    p->Set("DLSSNRComputeScalingRatioCallback", reinterpret_cast<void *>(&scaling_ratio));
    p->Set("DLSSNR.ScalingRatio", 1.0f); p->Set("DLSSNR.Scale", 1.0f); p->Set("DLSSNR.Upscaling", 0);
}

void set_evaluation(Params *p, unsigned width, unsigned height)
{
    void *output = nullptr, *depth = nullptr, *motion = nullptr;
    p->Get("Output", &output); p->Get("Depth", &depth); p->Get("MotionVectors", &motion);
    p->Set("DLSSNR.Color", output); p->Set("DLSSNR.Output", output); p->Set("DLSSNR.Depth", depth); p->Set("DLSSNR.MVec", motion);
    for (const char *key : {"DLSSNR.Color","DLSSNR.Output","DLSSNR.Depth","DLSSNR.MVec"})
    { char x[64] = {}, y[64] = {}, w[64] = {}, h[64] = {}; diagnostic_format(x, "%s.Subrect.BaseX", key); diagnostic_format(y, "%s.Subrect.BaseY", key);
      diagnostic_format(w, "%s.Subrect.Width", key); diagnostic_format(h, "%s.Subrect.Height", key); p->Set(x, 0u); p->Set(y, 0u); p->Set(w, width); p->Set(h, height); }
    int reset = 0, inverted = 0; p->Get("Reset", &reset); p->Get("DepthInverted", &inverted);
    p->Set("DLSSNR.UI", static_cast<void *>(nullptr)); p->Set("DLSSNR.UIAlpha", static_cast<void *>(nullptr));
    p->Set("DLSSNR.Enabled", 1); p->Set("DLSSNR.Reset", reset); p->Set("DLSSNR.DepthInverted", inverted); p->Set("DLSSNR.UICorrection", 0);
    p->Set("DLSSNR.Intensity", g_transfer_percent.load() / 100.0f); p->Set("DLSSNR.Style", 2u);
    const float local = g_color_percent.load() / 100.0f;
    for (const char *key : {"DLSSNR.LocalToneStrength","DLSSNR.LocalStructureStrength","DLSSNR.GlobalToneStrength","DLSSNR.SkinStructureStrength"}) p->Set(key, local);
    p->Set("DLSSNR.UseAutoMask", 0);
    float value = 0; if (p->Get("Jitter.Offset.X", &value) == 1) p->Set("DLSSNR.JitterOffsetX", value);
    if (p->Get("Jitter.Offset.Y", &value) == 1) p->Set("DLSSNR.JitterOffsetY", value);
    if (p->Get("MV.Scale.X", &value) == 1) p->Set("DLSSNR.MVecScaleX", value);
    if (p->Get("MV.Scale.Y", &value) == 1) p->Set("DLSSNR.MVecScaleY", value);
}

extern "C" Result vulkan_init_dispatch(const char *project, NVSDK_NGX_EngineType engine, const char *version,
    const wchar_t *directory, VkInstance instance, VkPhysicalDevice physical, VkDevice next_device,
    PFN_vkGetInstanceProcAddr gipa, PFN_vkGetDeviceProcAddr gdpa, NVSDK_NGX_Version sdk,
    const NVSDK_NGX_FeatureCommonInfo *info)
{
    const auto result = original_init(project, engine, version, directory, instance, physical, next_device, gipa, gdpa, sdk, info);
    if (result != 1) return result;
    AcquireSRWLockExclusive(&mutex);
    if (!initialized && !failed)
    {
        device = next_device;
        if (!open_snippet(directory) || snippet_init(0x876232c, directory, instance, physical, next_device, gipa, gdpa, sdk, nullptr) != 1)
            failed = true;
        else initialized = true;
        message(initialized ? "private NR initialized" : "private NR initialization failed; native output preserved", initialized ? 1 : 0);
    }
    ReleaseSRWLockExclusive(&mutex);
    return result;
}

extern "C" Result vulkan_create_dispatch(VkDevice d, VkCommandBuffer commands, NVSDK_NGX_Feature kind, Params *p, Handle **out)
{
    const auto result = original_create(d, commands, kind, p, out);
    if (result != 1 || kind != NVSDK_NGX_Feature_SuperSampling || !out || !*out || !p || !TryAcquireSRWLockExclusive(&mutex)) return result;
    Feature *slot = nullptr; for (auto &item : features) if (!item.native) { slot = &item; break; }
    unsigned width = 0, height = 0; p->Get("OutWidth", &width); p->Get("OutHeight", &height);
    if (slot && initialized && width && height) { set_creation(p, width, height); Handle *nr = nullptr;
        if (snippet_create(d, commands, NVSDK_NGX_Feature_Reserved18, p, &nr) == 1 && nr) *slot = {*out, nr};
        else message("private feature creation failed; native output preserved", 0); }
    ReleaseSRWLockExclusive(&mutex); return result;
}

Result evaluate_dispatch(Evaluate original, VkCommandBuffer commands, const Handle *handle, const Params *parameters, void *callback)
{
    const bool outer = evaluation_tls != TLS_OUT_OF_INDEXES && TlsGetValue(evaluation_tls) == nullptr &&
        TlsSetValue(evaluation_tls, reinterpret_cast<void *>(1)) != FALSE;
    const auto result = original(commands, handle, parameters, callback);
    if (!outer) return result;
    struct ClearTls { ~ClearTls() { TlsSetValue(evaluation_tls, nullptr); } } clear_tls;
    if (result != 1 || !commands || !handle || !parameters || g_scale_percent.load() != 100 || g_observed_pass_count.load() != 1 || !nr_enabled() ||
        !TryAcquireSRWLockExclusive(&mutex)) return result;
    Handle *nr = nullptr; for (const auto &item : features) if (item.native == handle) { nr = item.nr; break; }
    unsigned width = 0, height = 0; auto *p = const_cast<Params *>(parameters); p->Get("OutWidth", &width); p->Get("OutHeight", &height);
    if (nr && width && height) { set_evaluation(p, width, height); const auto nr_result = snippet_evaluate(commands, nr, p, nullptr);
        if (nr_result == 1) g_successful_evaluations.fetch_add(1); else message("private evaluation failed; native result retained", nr_result); }
    ReleaseSRWLockExclusive(&mutex); return result;
}
extern "C" Result vulkan_evaluate_dispatch(VkCommandBuffer c, const Handle *h, const Params *p, void *cb) { return evaluate_dispatch(original_evaluate, c, h, p, cb); }
extern "C" Result vulkan_evaluate_c_dispatch(VkCommandBuffer c, const Handle *h, const Params *p, void *cb) { return evaluate_dispatch(original_evaluate_c, c, h, p, cb); }
extern "C" Result vulkan_release_dispatch(Handle *handle)
{
    if (TryAcquireSRWLockExclusive(&mutex)) { for (auto &item : features) if (item.native == handle) { if (item.nr) snippet_release(item.nr); item = {}; break; }
        ReleaseSRWLockExclusive(&mutex); }
    return original_release(handle);
}

FARPROC WINAPI get_proc_address_dispatch(HMODULE module, LPCSTR name)
{
    FARPROC target = original_get_proc_address(module, name);
    if (!name || !target) return target;
    const auto is = [name](const char *wanted) { size_t i = 0; while (name[i] && wanted[i] && name[i] == wanted[i]) ++i; return name[i] == wanted[i]; };
    if (is("NVSDK_NGX_VULKAN_Init_ProjectID")) { original_init = reinterpret_cast<InitProject>(target); return reinterpret_cast<FARPROC>(&vulkan_init_dispatch); }
    if (is("NVSDK_NGX_VULKAN_CreateFeature1")) { original_create = reinterpret_cast<Create>(target); return reinterpret_cast<FARPROC>(&vulkan_create_dispatch); }
    if (is("NVSDK_NGX_VULKAN_EvaluateFeature")) { original_evaluate = reinterpret_cast<Evaluate>(target); return reinterpret_cast<FARPROC>(&vulkan_evaluate_dispatch); }
    if (is("NVSDK_NGX_VULKAN_EvaluateFeature_C")) { original_evaluate_c = reinterpret_cast<Evaluate>(target); return reinterpret_cast<FARPROC>(&vulkan_evaluate_c_dispatch); }
    if (is("NVSDK_NGX_VULKAN_ReleaseFeature")) { original_release = reinterpret_cast<Release>(target); return reinterpret_cast<FARPROC>(&vulkan_release_dispatch); }
    return target;
}

void install_get_proc_address_hook()
{
    if (hook_finished.load()) return;
    if (evaluation_tls == TLS_OUT_OF_INDEXES) evaluation_tls = TlsAlloc();
    if (evaluation_tls == TLS_OUT_OF_INDEXES) { message("evaluation nesting guard unavailable", 0); hook_finished.store(true); return; }
    const auto initialized_mh = MH_Initialize();
    if (initialized_mh != MH_OK && initialized_mh != MH_ERROR_ALREADY_INITIALIZED) { hook_finished.store(true); return; }
    auto target = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetProcAddress"));
    const auto created = MH_CreateHook(target, &get_proc_address_dispatch, reinterpret_cast<void **>(&original_get_proc_address));
    if ((created == MH_OK || created == MH_ERROR_ALREADY_CREATED) && MH_EnableHook(target) == MH_OK) message("GetProcAddress interception enabled", 1);
    else message("GetProcAddress interception unavailable", created);
    hook_finished.store(true);
}
DWORD WINAPI startup_worker(void *) { install_get_proc_address_hook(); return 0; }
void start_discovery()
{
    if (startup_started.exchange(true)) return;
    HMODULE retained = nullptr; if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&startup_worker), &retained)) return;
    HANDLE worker = CreateThread(nullptr, 0, &startup_worker, nullptr, 0, nullptr); if (worker) CloseHandle(worker);
}
}
