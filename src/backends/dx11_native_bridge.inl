// Experimental native-DLSS adapter. Included inside the shared addon namespace.
// SDK declarations are included by the parent translation unit, not here.
namespace dx11_native
{
using Result = NVSDK_NGX_Result;
using Params = NVSDK_NGX_Parameter;
using Handle = NVSDK_NGX_Handle;
using Create11 = Result (*)(ID3D11DeviceContext *, int, Params *, Handle **);
using Evaluate11 = Result (*)(ID3D11DeviceContext *, const Handle *, const Params *, void *);
using Release11 = Result (*)(Handle *);
using Initialize12 = Result (*)(const char *, int, const char *, const wchar_t *, ID3D12Device *, int, const NVSDK_NGX_FeatureCommonInfo *);
using Allocate = Result (*)(Params **);
using Destroy = Result (*)(Params *);
using Create12 = Result (*)(ID3D12GraphicsCommandList *, int, Params *, Handle **);
// Driver exports are NOT the public SDK frontend ABI. Both Shutdown1 exports
// consume a second writable DWORD pointer (verified on driver 616.56); the SDK
// frontend supplies this internally. Preserve it on native hooks too.
using Shutdown12 = Result (*)(ID3D12Device *, unsigned *);
using nr::backends::dx11::release;
Create11 original_create = nullptr;
Evaluate11 original_evaluate = nullptr, original_evaluate_c = nullptr;
Release11 original_release = nullptr;
using Shutdown11 = Result (*)(ID3D11Device *, unsigned *);
Shutdown11 original_shutdown = nullptr;
using ShutdownSdk = Result (*)(ID3D11Device *);
ShutdownSdk original_sdk_shutdown = nullptr;
bool sdk_frontend = false;
DWORD evaluation_tls = TLS_OUT_OF_INDEXES; // process-lifetime hooks own this slot
struct EvaluationScope
{
    bool outer = true, owns = false;
    EvaluationScope()
    {
        if (evaluation_tls == TLS_OUT_OF_INDEXES) return;
        outer = TlsGetValue(evaluation_tls) == nullptr;
        if (outer) { owns = TlsSetValue(evaluation_tls, reinterpret_cast<void *>(1)) != FALSE; outer = owns; }
    }
    ~EvaluationScope() { if (owns) TlsSetValue(evaluation_tls, nullptr); }
};

struct Shape
{
    unsigned width = 0, height = 0, out_width = 0, out_height = 0, quality = 0, flags = 0;
    bool operator==(const Shape &) const = default;
};
struct NativeFeature
{
    const Handle *handle = nullptr;
    Shape shape;
    IUnknown *owner = nullptr; // owned canonical device identity, not a borrowed pointer
    unsigned long long generation = 0;
};
NativeFeature features[32];
SRWLOCK mutex = SRWLOCK_INIT;
unsigned long long capture_epoch = 0, feature_generation = 0;
unsigned shutdowns_in_flight = 0;

// Registry helpers require mutex. Native NGX calls must remain outside it.
void forget_feature(NativeFeature &item)
{
    auto *owned = item.owner;
    item = {};
    release(owned);
}

unsigned long long feature_ticket(const Handle *handle)
{
    for (const auto &item : features) if (handle && item.handle == handle) return item.generation;
    return 0;
}

unsigned long long evaluation_ticket(const Handle *handle)
{
    // Never delay a native evaluation on private rendering/shutdown work.
    if (!TryAcquireSRWLockExclusive(&mutex)) return 0;
    const auto ticket = shutdowns_in_flight ? 0 : feature_ticket(handle);
    ReleaseSRWLockExclusive(&mutex);
    return ticket;
}

bool capture_feature(ID3D11DeviceContext *context, const Handle *handle,
                     const Shape &shape, bool valid, unsigned long long epoch)
{
    if (shutdowns_in_flight || epoch != capture_epoch || !handle ||
        feature_generation == UINT64_MAX || capture_epoch == UINT64_MAX) return false;
    // Search the entire table for an existing handle BEFORE considering holes.
    // A returned handle can reuse an address while an earlier release unwinds.
    NativeFeature *slot = nullptr;
    for (auto &item : features) if (item.handle == handle) { slot = &item; break; }
    if (slot) forget_feature(*slot);
    if (!valid || !context) return false;
    if (!slot) for (auto &item : features) if (!item.handle) { slot = &item; break; }
    if (!slot) return false; // bounded registry; preserve native output when full
    ID3D11Device *source = nullptr;
    IUnknown *identity = nullptr;
    context->GetDevice(&source);
    if (source) source->QueryInterface(IID_PPV_ARGS(&identity));
    release(source);
    if (!identity) return false;
    *slot = {handle, shape, identity, ++feature_generation};
    return true;
}

bool find_feature(ID3D11DeviceContext *context, const Handle *handle, Shape &shape)
{
    if (!context || !handle || shutdowns_in_flight) return false;
    for (const auto &item : features) if (item.handle == handle)
    {
        ID3D11Device *source = nullptr;
        context->GetDevice(&source);
        const bool matches = nr::backends::dx11::same_object(source, item.owner);
        release(source);
        if (matches) shape = item.shape;
        return matches;
    }
    return false;
}

bool forget_released_feature(const Handle *handle, unsigned long long ticket)
{
    for (auto &item : features) if (ticket && item.handle == handle && item.generation == ticket)
    { forget_feature(item); return true; }
    return false;
}

void invalidate_device_features(ID3D11Device *closing)
{
    // Saturation fails capture closed rather than allowing epoch wraparound.
    if (capture_epoch != UINT64_MAX) ++capture_epoch;
    for (auto &item : features)
        if (item.owner && nr::backends::dx11::same_object(item.owner, closing)) forget_feature(item);
}
nr::backends::dx11::Transport transport;
ID3D11DeviceContext *native_context = nullptr;
ID3D12Device *device = nullptr;
ID3D12CommandQueue *queue = nullptr;
reshade::api::device *consumer_device = nullptr; // borrowed while private device is retained
HMODULE ngx_module = nullptr, d3d12_module = nullptr;
Allocate allocate = nullptr;
Destroy destroy = nullptr;
Create12 create = nullptr;
Release11 release_feature = nullptr;
Shutdown12 shutdown = nullptr;
Params *parameters = nullptr;
Handle *feature = nullptr;
Shape active_shape;
const Handle *active_native_feature = nullptr;
unsigned long long active_native_generation = 0;
bool failed = false, ready = false;
bool init_attempted = false, shutdown_seen = false, transport_closed = false;
#if defined(NR_DX11_RECYCLE_PROBE) || defined(NR_DX11_REPLACEMENT_PROBE) || defined(NR_DX11_CORE_SHUTDOWN_ENABLED)
bool consumer_detached = false;
bool probe_restart_after_retirement(ID3D11DeviceContext *replacement);
#endif
unsigned long long delivered = 0;
const Params *current_parameters = nullptr;
Shape current_shape;
bool reset_history = true;
std::atomic_bool hooks_finished = false;
std::atomic_bool startup_started = false;

void message(const char *stage, unsigned result)
{
    char text[256];
    diagnostic_format(text, "NR DX11 experimental: %s (0x%08X).", stage, result);
    log_text(reshade::log::level::info, text);
}

#ifdef NR_DX11_CORE_SHUTDOWN_ENABLED
bool core_shutdown_attempted = false, core_closed = false;
bool graphics_closed = false;
// Caller holds mutex. A successful core close is necessary but not sufficient:
// the private pipeline and queue/fence caches also own graphics references.
bool probe_close_private_graphics()
{
    if (graphics_closed || !core_closed || ready || init_attempted || !transport_closed ||
        !consumer_detached || feature || parameters || !device || !consumer_device) return false;
    if (!dx12::close_quiescent_private_caches(consumer_device)) return false;
    auto *owned_context = native_context; native_context = nullptr;
    auto *owned_queue = queue; queue = nullptr;
    auto *owned_device = device; device = nullptr;
    auto owned_ngx = ngx_module; ngx_module = nullptr;
    auto owned_d3d12 = d3d12_module; d3d12_module = nullptr;
    consumer_device = nullptr; current_parameters = nullptr;
    active_native_feature = nullptr; active_native_generation = 0;
    active_shape = {}; current_shape = {}; reset_history = true;
    allocate = nullptr; destroy = nullptr; create = nullptr;
    release_feature = nullptr; shutdown = nullptr;
    graphics_closed = true; // callbacks see no remaining bridge ownership
    release(owned_context); release(owned_queue); release(owned_device);
    if (owned_ngx) FreeLibrary(owned_ngx);
    if (owned_d3d12) FreeLibrary(owned_d3d12);
    message(NR_DX11_LIFECYCLE_PREFIX "private graphics/cache ownership released", 0);
    return true;
}
// Caller holds mutex. Exactly one attempt, only after ALL private rendering
// ownership has been discharged. A failed/hung call never enables reinit.
bool probe_close_private_core()
{
    if (core_shutdown_attempted || !shutdown_seen || !transport_closed || !consumer_detached ||
        !ready || !init_attempted || feature || parameters || !device || !shutdown) return false;
    core_shutdown_attempted = true;
    message(NR_DX11_LIFECYCLE_PREFIX "private NGX core shutdown begin", 0);
    unsigned driver_output = 0;
    const auto result = shutdown(device, &driver_output);
    message(NR_DX11_LIFECYCLE_PREFIX "private NGX core shutdown result", result);
    message(NR_DX11_LIFECYCLE_PREFIX "private NGX core shutdown output word", driver_output);
    if (result != 1) return false;
    core_closed = true; ready = false; init_attempted = false;
    return true;
}
#ifdef NR_DX11_CORE_RECREATE_ENABLED
bool probe_prepare_fresh_core()
{
    // A one-shot permission consumed BEFORE attempting initialization. Failed
    // initialization cannot recycle this latch or retry unknown core state.
    if (!graphics_closed || !core_closed || !core_shutdown_attempted || !shutdown_seen ||
        !consumer_detached || !transport_closed || ready || init_attempted ||
        native_context || device || queue || consumer_device || ngx_module || d3d12_module ||
        feature || parameters || shutdowns_in_flight) return false;
    graphics_closed = core_closed = core_shutdown_attempted = false;
    shutdown_seen = consumer_detached = transport_closed = failed = false;
    message(NR_DX11_LIFECYCLE_PREFIX "fresh private core initialization permitted", 0);
    return true;
}
#endif
#endif

void install_hooks()
{
    static SRWLOCK install_mutex = SRWLOCK_INIT;
    if (!TryAcquireSRWLockExclusive(&install_mutex)) return;
    struct UnlockInstall { SRWLOCK *lock; ~UnlockInstall() { ReleaseSRWLockExclusive(lock); } } unlock{&install_mutex};
    static bool installed = false;
    static ULONGLONG last_attempt = 0;
    if (installed || GetTickCount64() - last_attempt < 10) return;
    last_attempt = GetTickCount64();
    HMODULE module = GetModuleHandleW(L"_nvngx.dll");
#ifdef NR_DX11_GAME_TEST
    // FFXIV exports the SDK frontend and can bypass the driver's evaluation
    // entry via EvaluateFeature_C. Choose ONE complete boundary, never both.
    HMODULE executable = GetModuleHandleW(nullptr);
    if (GetProcAddress(executable, "NVSDK_NGX_D3D11_CreateFeature") &&
        GetProcAddress(executable, "NVSDK_NGX_D3D11_ReleaseFeature") &&
        GetProcAddress(executable, "NVSDK_NGX_D3D11_Shutdown1") &&
        (GetProcAddress(executable, "NVSDK_NGX_D3D11_EvaluateFeature") ||
         GetProcAddress(executable, "NVSDK_NGX_D3D11_EvaluateFeature_C")))
    { module = executable; sdk_frontend = true; }
#endif
    if (!module) return;
    struct Hook { const char *name; void *callback; void **original; };
    const Hook hooks[] = {
        {"NVSDK_NGX_D3D11_CreateFeature", reinterpret_cast<void *>(&dx11_create_dispatch), reinterpret_cast<void **>(&original_create)},
        {"NVSDK_NGX_D3D11_ReleaseFeature", reinterpret_cast<void *>(&dx11_release_dispatch), reinterpret_cast<void **>(&original_release)},
        {"NVSDK_NGX_D3D11_EvaluateFeature", reinterpret_cast<void *>(&dx11_evaluate_dispatch), reinterpret_cast<void **>(&original_evaluate)},
        {"NVSDK_NGX_D3D11_EvaluateFeature_C", reinterpret_cast<void *>(&dx11_evaluate_c_dispatch), reinterpret_cast<void **>(&original_evaluate_c)},
        {"NVSDK_NGX_D3D11_Shutdown1", sdk_frontend ? reinterpret_cast<void *>(&dx11_sdk_shutdown_dispatch) : reinterpret_cast<void *>(&dx11_shutdown_dispatch),
            sdk_frontend ? reinterpret_cast<void **>(&original_sdk_shutdown) : reinterpret_cast<void **>(&original_shutdown)},
    };
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED)
    { installed = true; hooks_finished.store(true); return; }
    void *targets[5] = {};
    bool created[5] = {};
    for (unsigned i = 0; i < 5; ++i)
        targets[i] = reinterpret_cast<void *>(GetProcAddress(module, hooks[i].name));
    if (!targets[0] || !targets[1] || !targets[4] || (!targets[2] && !targets[3]))
    {
        installed = true; hooks_finished.store(true);
        message("required runtime hook exports missing; adapter not installed", 0); return;
    }
    // The consumer already pins the addon. Keep the hooked driver module alive
    // too; do not remove trampolines while another thread may be inside one.
    HMODULE retained = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(module), &retained)) return;
    unsigned count = 0;
    // _C may call the other exported evaluation wrapper. Count/render only the
    // outer call, with independent state per native thread (no PE TLS graft).
    if (evaluation_tls == TLS_OUT_OF_INDEXES) evaluation_tls = TlsAlloc();
    if (evaluation_tls == TLS_OUT_OF_INDEXES)
    { installed = true; hooks_finished.store(true); message("evaluation nesting guard unavailable; adapter disabled", 0); return; }
    MH_STATUS result = MH_OK;
    for (unsigned i = 0; i < 5 && result == MH_OK; ++i)
    {
        if (!targets[i]) continue;
        result = MH_CreateHook(targets[i], hooks[i].callback, hooks[i].original);
        if (result == MH_OK) { created[i] = true; ++count; result = MH_QueueEnableHook(targets[i]); }
    }
    if (result != MH_OK)
    {
        // None enabled yet. Remove only our own newly created hooks.
        for (unsigned i = 0; i < 5; ++i) if (created[i]) MH_RemoveHook(targets[i]);
        installed = true; hooks_finished.store(true);
        message("runtime hook preparation failed; adapter not installed", result); return;
    }
    result = MH_ApplyQueued();
    installed = true;
    hooks_finished.store(true);
    if (result != MH_OK)
    {
        // Applying a batch can partially succeed. Keep all trampolines/module
        // storage alive; callbacks preserve native calls but no private work.
        AcquireSRWLockExclusive(&mutex); failed = true; ReleaseSRWLockExclusive(&mutex);
        message("runtime hook activation failed; adapter disabled", result); return;
    }
    message("native runtime hooks enabled", count);
    message(sdk_frontend ? "interception boundary: executable SDK (Shutdown1 ABI=1 argument)" :
        "interception boundary: driver (Shutdown1 ABI=2 arguments)", 1);
}

DWORD WINAPI startup_worker(void *)
{
    const auto began = GetTickCount64();
    // A bounded worker outside DllMain/loader callbacks. Do not hold up device
    // creation, load NGX ourselves, or patch code from a loader notification.
    while (!hooks_finished.load() && GetTickCount64() - began < 60000)
    {
        install_hooks();
        if (!hooks_finished.load()) Sleep(GetTickCount64() - began < 5000 ? 10 : 100);
    }
    if (!hooks_finished.load()) message("startup discovery timed out; Present discovery remains available", 0);
    return 0;
}

void start_discovery()
{
    if (!g_target_module || startup_started.exchange(true)) return;
    HMODULE retained = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&startup_worker), &retained))
    { startup_started.store(false); return; }
    const HANDLE worker = CreateThread(nullptr, 0, &startup_worker, nullptr, 0, nullptr);
    if (!worker) { startup_started.store(false); message("startup worker creation failed", GetLastError()); return; }
    CloseHandle(worker);
    message("early DX11 device discovery started", 1);
}

bool shape_of(const Params *p, Shape &s)
{
    return p && p->Get("Width", &s.width) == 1 && p->Get("Height", &s.height) == 1 &&
        p->Get("OutWidth", &s.out_width) == 1 && p->Get("OutHeight", &s.out_height) == 1 &&
        p->Get("PerfQualityValue", &s.quality) == 1 && p->Get("DLSS.Feature.Create.Flags", &s.flags) == 1 &&
        s.width && s.height && s.out_width >= s.width && s.out_height >= s.height;
}

void rollback_before_ngx_init()
{
    // No driver init attempt, consumer call or queue submission has occurred.
    // After an attempted NGX init, success/failure ownership needs a different
    // teardown path; never pretend a failed return proves no internal state.
    if (init_attempted || !transport.try_close()) return;
    release(native_context); release(queue); release(device);
    if (ngx_module) { FreeLibrary(ngx_module); ngx_module = nullptr; }
    if (d3d12_module) { FreeLibrary(d3d12_module); d3d12_module = nullptr; }
    allocate = nullptr; destroy = nullptr; create = nullptr;
    release_feature = nullptr; shutdown = nullptr;
    message("pre-NGX initialization rollback complete", 0);
}

bool initialize(ID3D11DeviceContext *context)
{
    ID3D11Device *device11 = nullptr;
    IDXGIDevice *dxgi = nullptr;
    IDXGIAdapter *adapter = nullptr;
    context->GetDevice(&device11);
    HRESULT hr = device11->QueryInterface(IID_PPV_ARGS(&dxgi));
    if (SUCCEEDED(hr)) hr = dxgi->GetAdapter(&adapter);
    release(dxgi); release(device11);
    d3d12_module = LoadLibraryW(L"d3d12.dll");
    auto create_device = reinterpret_cast<HRESULT (WINAPI *)(IUnknown *, D3D_FEATURE_LEVEL, REFIID, void **)>(
        d3d12_module ? GetProcAddress(d3d12_module, "D3D12CreateDevice") : nullptr);
    if (SUCCEEDED(hr)) hr = create_device ? create_device(adapter, D3D_FEATURE_LEVEL_11_0,
        __uuidof(ID3D12Device), reinterpret_cast<void **>(&device)) : E_NOINTERFACE;
    release(adapter);
    if (FAILED(hr)) { message("private device failed", hr); return false; }
    D3D12_COMMAND_QUEUE_DESC desc = {};
    hr = device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue));
    if (SUCCEEDED(hr)) hr = transport.initialize(context, device, queue);
    if (FAILED(hr)) { message("transport initialization failed", hr); return false; }
    native_context = context; native_context->AddRef();

    // Use the same already-loaded driver module as the native hooks, not the
    // first unrelated module exporting a public frontend with a different ABI.
    const auto runtime = GetModuleHandleW(L"_nvngx.dll");
    const auto init = reinterpret_cast<Initialize12>(runtime ?
        GetProcAddress(runtime, "NVSDK_NGX_D3D12_Init_ProjectID") : nullptr);
    if (!init || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(init), &ngx_module))
    { message("driver NGX exports unavailable", 0); return false; }
    allocate = reinterpret_cast<Allocate>(GetProcAddress(ngx_module, "NVSDK_NGX_D3D12_AllocateParameters"));
    destroy = reinterpret_cast<Destroy>(GetProcAddress(ngx_module, "NVSDK_NGX_D3D12_DestroyParameters"));
    create = reinterpret_cast<Create12>(GetProcAddress(ngx_module, "NVSDK_NGX_D3D12_CreateFeature"));
    release_feature = reinterpret_cast<Release11>(GetProcAddress(ngx_module, "NVSDK_NGX_D3D12_ReleaseFeature"));
    shutdown = reinterpret_cast<Shutdown12>(GetProcAddress(ngx_module, "NVSDK_NGX_D3D12_Shutdown1"));
    if (!allocate || !destroy || !create || !release_feature || !shutdown) return false;
    wchar_t directory[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    for (DWORD i = length; i != 0; --i) if (directory[i - 1] == L'\\') { directory[i] = 0; break; }
    const wchar_t *paths[] = {directory};
    NVSDK_NGX_FeatureCommonInfo common = {};
    common.PathListInfo = {paths, 1};
    // Driver export ABI differs from the public SDK frontend: version precedes
    // FeatureCommonInfo. Use the SDK's declared version, not a guessed retry loop.
    init_attempted = true;
    auto result = init("b463b3f3-7014-4b4b-b9d5-d55366670011", 0, "1.0", directory,
        device, NVSDK_NGX_Version_API, &common);
    message("private NGX init", result);
    if (result != 1) return false;
    ready = true;
    result = allocate(&parameters);
    message("private NGX parameters", result);
    return result == 1 && parameters;
}

void set_shape(Params *p, const Shape &s)
{
    p->Set("Width", s.width); p->Set("Height", s.height);
    p->Set("OutWidth", s.out_width); p->Set("OutHeight", s.out_height);
    p->Set("PerfQualityValue", s.quality); p->Set("DLSS.Feature.Create.Flags", s.flags);
    p->Set("CreationNodeMask", 1u); p->Set("VisibilityNodeMask", 1u);
}

bool record(void *, const nr::backends::dx11::Evaluation &e)
{
    using namespace nr::backends::dx11;
    // Resolve the consumer identity before any private feature/NR work. Do not
    // use a guessed ReShade device or a pointer from another backend session.
    reshade::api::command_list *api_commands = nullptr;
    UINT bytes = sizeof(api_commands);
    auto *guid = reinterpret_cast<const GUID *>(reinterpret_cast<std::uintptr_t>(g_target_module) + 0x22AA30);
    const auto mapped = e.commands->GetPrivateData(*guid, &bytes, &api_commands);
    if (FAILED(mapped) ||
        bytes != sizeof(api_commands) || !api_commands) return false;
    auto *mapped_device = api_commands->get_device();
    // get_native() is ReShade's underlying device, whereas device is its COM
    // proxy: comparing those IUnknown identities rejects a valid mapping. The
    // command list here was created by our own transport on our private device;
    // use its verified ReShade association and reject API/session changes.
    if (!mapped_device || mapped_device->get_api() != reshade::api::device_api::d3d12 ||
        (consumer_device && consumer_device != mapped_device)) return false;
    consumer_device = mapped_device;
    parameters->Reset();
    set_shape(parameters, current_shape);
    if (!feature)
    {
        using HostCreate = Result (*)(Create12, ID3D12GraphicsCommandList *, int, Params *, Handle **);
        const auto host_create = reinterpret_cast<HostCreate>(reinterpret_cast<std::uintptr_t>(g_target_module) + 0x4CB60);
        auto result = host_create(create, e.commands, NVSDK_NGX_Feature_SuperSampling, parameters, &feature);
        message("private SR create", result);
        if (result != 1 || !feature) { failed = true; return false; }
        active_shape = current_shape;
        reset_history = true;
    }
    const char *keys[texture_count] = {"Color", "Depth", "MotionVectors", "Output", "ExposureTexture"};
    D3D12_RESOURCE_BARRIER barriers[texture_count] = {};
    unsigned count = 0;
    for (unsigned i = 0; i < texture_count; ++i)
    {
        parameters->Set(keys[i], e.textures[i]);
        if (!e.textures[i]) continue;
        auto &b = barriers[count++]; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {e.textures[i], D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_COMMON, i == output ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    }
    constexpr const char *float_keys[] = {"Jitter.Offset.X", "Jitter.Offset.Y", "MV.Scale.X", "MV.Scale.Y",
        "DLSS.Pre.Exposure", "DLSS.Exposure.Scale", "FrameTimeDeltaInMsec"};
    for (auto key : float_keys) { float value = 0; if (current_parameters->Get(key, &value) == 1) parameters->Set(key, value); }
    constexpr const char *uint_keys[] = {"DLSS.Render.Subrect.Dimensions.Width", "DLSS.Render.Subrect.Dimensions.Height",
        "DLSS.Input.Color.Subrect.Base.X", "DLSS.Input.Color.Subrect.Base.Y", "DLSS.Input.Depth.Subrect.Base.X", "DLSS.Input.Depth.Subrect.Base.Y",
        "DLSS.Input.MV.Subrect.Base.X", "DLSS.Input.MV.Subrect.Base.Y", "DLSS.Output.Subrect.Base.X", "DLSS.Output.Subrect.Base.Y"};
    for (auto key : uint_keys) { unsigned value = 0; if (current_parameters->Get(key, &value) == 1) parameters->Set(key, value); }
    int reset = 0; current_parameters->Get("Reset", &reset);
    parameters->Set("Reset", reset_history || reset ? 1 : 0);
    // Native DX11 SR already completed successfully. Transport that exact
    // output, then invoke the verified post-SR consumer with DX12 resources.
    // Do not execute SR a second time just to reach the NR observer. The private
    // SR handle above supplies the consumer's required feature classification.
    D3D12_RESOURCE_BARRIER copies[2] = {};
    copies[0].Type = copies[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    copies[0].Transition = {e.textures[color], D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE};
    copies[1].Transition = {e.textures[output], D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST};
    e.commands->ResourceBarrier(2, copies);
    e.commands->CopyResource(e.textures[output], e.textures[color]);
    for (auto &copy : copies) std::swap(copy.Transition.StateBefore, copy.Transition.StateAfter);
    e.commands->ResourceBarrier(2, copies);
    e.commands->ResourceBarrier(count, barriers);
    using HostPostEvaluate = void (*)(ID3D12GraphicsCommandList *, const Handle *, const Params *, Result);
    const auto post_evaluate = reinterpret_cast<HostPostEvaluate>(reinterpret_cast<std::uintptr_t>(g_target_module) + 0x9BD10);
    post_evaluate(e.commands, feature, parameters, NVSDK_NGX_Result_Success);
    if (delivered < 3)
    {
        char diagnostic[256];
        diagnostic_format(diagnostic, "NR DX11 experimental: command mapping hr=%08X api=%p hook-byte=%u; observer callbacks=%llu.",
            static_cast<unsigned>(mapped), api_commands, field<unsigned char>(g_target_module, 0x27100F), static_cast<unsigned long long>(g_native_gate_calls.load()));
        log_text(reshade::log::level::info, diagnostic);
    }
    for (unsigned i = 0; i < count; ++i) std::swap(barriers[i].Transition.StateBefore, barriers[i].Transition.StateAfter);
    e.commands->ResourceBarrier(count, barriers);
    if (delivered < 3) message("post-SR consumer recorded; no duplicate SR evaluate", 1);
    reset_history = false;
    return true;
}

void after_evaluate(ID3D11DeviceContext *context, const Handle *handle, const Params *p,
                    Result result, unsigned long long ticket)
{
    if (result != 1 || !context || !handle || !p || !ticket) return;
#ifdef NR_DX11_REPLACEMENT_PROBE
    // Isolated test build only: retry on a captured feature from a DIFFERENT
    // native context after the release probe has retired the previous binding.
    if (TryAcquireSRWLockExclusive(&mutex))
    {
        Shape captured;
        const bool eligible = ticket == feature_ticket(handle) && find_feature(context, handle, captured);
        ReleaseSRWLockExclusive(&mutex);
        if (eligible) probe_restart_after_retirement(context);
    }
#endif
    if (!TryAcquireSRWLockExclusive(&mutex)) return;
    if (failed
#ifdef NR_DX11_CORE_RECREATE_ENABLED
        && !graphics_closed
#endif
        ) { ReleaseSRWLockExclusive(&mutex); return; }
    // An evaluation can overlap native release/create or shutdown. Never apply
    // new feature metadata to an older evaluation that used the same address.
    if (ticket != feature_ticket(handle)) { ReleaseSRWLockExclusive(&mutex); return; }
    if (field<int>(g_target_module, kPresetIndexRva) == 0 || field<unsigned char>(g_target_module, 0x27100F) == 0)
    {
        reset_history = true;
        ReleaseSRWLockExclusive(&mutex); return;
    }
    Shape shape;
    const bool known = find_feature(context, handle, shape);
    if (!known)
    {
        static bool reported = false;
        if (!reported) { message("uncaptured native feature; preserving native output", 0); reported = true; }
        ReleaseSRWLockExclusive(&mutex); return;
    }
    nr::backends::dx11::Frame frame;
    const char *keys[] = {"Output", "Depth", "MotionVectors", "Output", "ExposureTexture"};
    bool valid = true;
    for (unsigned i = 0; i < nr::backends::dx11::texture_count; ++i)
    {
        ID3D11Resource *resource = nullptr;
        if (p->Get(keys[i], &resource) != 1 || !resource) { if (i != nr::backends::dx11::exposure) valid = false; continue; }
        if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&frame.textures[i])))) valid = false;
    }
    if (valid)
    {
        // Until subrect copy-back is implemented, never overwrite a larger
        // output allocation with pixels outside this feature's valid rectangle.
        D3D11_TEXTURE2D_DESC out = {}; frame.textures[nr::backends::dx11::output]->GetDesc(&out);
        valid = out.Width == shape.out_width && out.Height == shape.out_height;
        for (auto key : {"DLSS.Output.Subrect.Base.X", "DLSS.Output.Subrect.Base.Y"})
        { unsigned offset = 0; if (p->Get(key, &offset) == 1 && offset) valid = false; }
    }
    if (valid && (!native_context || nr::backends::dx11::same_object(context, native_context)))
    {
#ifdef NR_DX11_CORE_RECREATE_ENABLED
        if (failed && !probe_prepare_fresh_core())
        {
            for (auto *&texture : frame.textures) release(texture);
            ReleaseSRWLockExclusive(&mutex); return;
        }
#endif
        if (!native_context && !initialize(context))
        { failed = true; rollback_before_ngx_init(); }
        if (!failed)
        {
            const auto generation = feature_ticket(handle);
            const bool changing = feature && (!(active_shape == shape) || active_native_feature != handle ||
                active_native_generation != generation);
            if (!changing || transport.idle())
            {
                if (changing)
                {
                    const auto released = release_feature(feature);
                    if (released != 1) failed = true;
                    else feature = nullptr;
                }
                if (!failed)
                {
                    current_shape = shape; current_parameters = p; active_native_feature = handle;
                    active_native_generation = generation;
                    const auto submitted = transport.submit(frame, &record, nullptr);
                    if (submitted == nr::backends::dx11::Result::submitted)
                    { ++delivered; if (delivered <= 3 || delivered % 300 == 0) message("DX11 output copy queued", static_cast<unsigned>(delivered)); }
                    else
                    {
                        reset_history = true;
                        static unsigned skip_messages = 0;
                        if (skip_messages++ < 3)
                        {
                            message("transport skipped", static_cast<unsigned>(submitted));
                            message(transport.error_stage(), static_cast<unsigned>(transport.last_error()));
                        }
                    }
                    current_parameters = nullptr;
                }
            }
        }
    }
    for (auto *&texture : frame.textures) release(texture);
    ReleaseSRWLockExclusive(&mutex);
}

void close_for_device(ID3D11Device *closing)
{
    AcquireSRWLockExclusive(&mutex);
    ID3D11Device *owner = nullptr;
    if (native_context) native_context->GetDevice(&owner);
    const bool matches = nr::backends::dx11::same_object(owner, closing);
    release(owner);
    if (!matches) { ReleaseSRWLockExclusive(&mutex); return; }
    failed = true;
    if (shutdown_seen) { ReleaseSRWLockExclusive(&mutex); return; }
    shutdown_seen = true;
    char statistics[256];
    diagnostic_format(statistics, "NR DX11 experimental: session submitted=%llu gate=%u evaluations=%u scaled=%u.",
        delivered, g_native_gate_calls.load(), g_evaluation_calls.load(), g_scaled_calls.load());
    log_text(reshade::log::level::info, statistics);
    // No new adapter submissions can enter under this mutex. Poll only at this
    // shutdown boundary, for at most 500 ms. Busy/poisoned/removed devices retain
    // resources; never signal a completion fence from the CPU to force success.
    const auto deadline = GetTickCount64() + 500;
    do
    {
        transport_closed = transport.try_close();
        if (transport_closed || GetTickCount64() >= deadline) break;
        Sleep(1);
    } while (true);
    if (transport_closed)
    {
        // Destruction of the completed transport command lists removes their
        // recording pins. The normal collector then proves its own queue fences.
        unsigned remaining = 0;
        bool drained = consumer_device == nullptr;
        do
        {
            if (consumer_device) drained = dx12::retire_device_resources(consumer_device, remaining);
            if (drained || GetTickCount64() >= deadline) break;
            Sleep(1);
        } while (true);
        message(drained ? "tracked consumer resources drained" : "tracked consumer resources retained", remaining);
        if (feature)
        {
            const auto result = release_feature(feature);
            message("private SR release", result);
            if (result == 1) feature = nullptr;
        }
        bool private_parameters_released = parameters == nullptr;
        if (!feature && parameters)
        {
            // A failed destroy has ambiguous ownership. Never retry that pointer.
            auto *owned = parameters; parameters = nullptr;
            const auto result = destroy(owned);
            private_parameters_released = result == 1;
            message("private parameter destroy", result);
        }
        if (drained && !feature && private_parameters_released && consumer_device)
        {
            // Host uses the underlying ReShade API native device, not the COM
            // wrapper returned when this bridge creates its private DX12 device.
            const auto result = dx12::close_empty_consumer(consumer_device,
                reinterpret_cast<IUnknown *>(consumer_device->get_native()));
#if defined(NR_DX11_RECYCLE_PROBE) || defined(NR_DX11_REPLACEMENT_PROBE) || defined(NR_DX11_CORE_SHUTDOWN_ENABLED)
            consumer_detached = result == dx12::ConsumerClose::closed;
#endif
            message(result == dx12::ConsumerClose::closed ? "shared NR binding closed" :
                "shared NR binding retained; close reason", static_cast<unsigned>(result));
        }
        message("transport closed; shared NGX/device state retained", static_cast<unsigned>(transport.allocated_bytes()));
    }
    else message("transport still in flight or unsafe; private resources retained", 0);
#ifdef NR_DX11_CORE_SHUTDOWN_ENABLED
    if (probe_close_private_core()) probe_close_private_graphics();
#endif
    // Ordinary builds retain ownership. Experimental managed lifecycle builds
    // release only after all guards pass; failure never permits fresh init.
#ifdef NR_DX11_CORE_SHUTDOWN_ENABLED
    if (graphics_closed) message(NR_DX11_LIFECYCLE_PREFIX "native shutdown: private ownership released", 0);
    else
#endif
    message("native shutdown: shared-session teardown incomplete; reinitialization disabled", 0);
    ReleaseSRWLockExclusive(&mutex);
}

#if defined(NR_DX11_RECYCLE_PROBE) || defined(NR_DX11_REPLACEMENT_PROBE)
// TEST BUILD ONLY. Restart the NR binding/transport after two source-feature
// releases. The private NGX core/device remains initialized: this deliberately
// does not simulate or claim successful native NGX Shutdown1/reinitialization.
bool probe_restart_after_retirement(ID3D11DeviceContext *replacement)
{
    AcquireSRWLockExclusive(&mutex);
    if (!shutdown_seen || !transport_closed || !consumer_detached || !ready ||
        feature || parameters || !native_context || !replacement || !device || !queue)
    { ReleaseSRWLockExclusive(&mutex); return false; }
#ifdef NR_DX11_REPLACEMENT_PROBE
    ID3D11Device *previous = nullptr, *next = nullptr;
    native_context->GetDevice(&previous); replacement->GetDevice(&next);
    const bool distinct = previous && next && !nr::backends::dx11::same_object(previous, next);
    release(previous); release(next);
    if (!distinct) { ReleaseSRWLockExclusive(&mutex); return false; }
#endif
    // Transport verifies immediate context, same adapter and device ownership.
    auto hr = transport.initialize(replacement, device, queue);
    if (FAILED(hr)) { message("recycle probe transport reopen failed", hr); ReleaseSRWLockExclusive(&mutex); return false; }
    // Consume the one-shot retirement permission even if allocation fails.
    consumer_detached = false;
    transport_closed = false;
    const auto result = allocate(&parameters);
    if (result != 1 || !parameters)
    { message("recycle probe parameter allocation failed", result); ReleaseSRWLockExclusive(&mutex); return false; }
    replacement->AddRef();
    release(native_context); native_context = replacement;
    consumer_device = nullptr;
    active_shape = {}; current_shape = {};
    current_parameters = nullptr; active_native_feature = nullptr;
    active_native_generation = 0;
    reset_history = true;
    shutdown_seen = transport_closed = consumer_detached = failed = false;
#ifdef NR_DX11_REPLACEMENT_PROBE
    message("TEST ONLY: NR session moved to replacement DX11 device on retained private NGX core", 1);
#else
    message("TEST ONLY: NR session reopened on retained private NGX core", 1);
#endif
    ReleaseSRWLockExclusive(&mutex);
    return true;
}
#endif
}
