// Verified native DLSS-G observer at 9A4B0. Auto's latched native owner may
// skip NR here; otherwise preserve the upstream call and its return value.
// This never skips the vendor's FG evaluation or changes its parameters.
std::atomic_ullong g_fg_observer_calls = 0;
std::atomic_ullong g_fg_parameter_calls = 0;
std::atomic_ullong g_fg_probe_next_tick = 0;
std::atomic_uint g_fg_guard_on_entry = 0, g_fg_guard_on_exit = 0;
nr::AutoSourcePolicy g_auto_source;

bool auto_source_enabled()
{
    return g_target_module && (field<unsigned char>(g_target_module,0x27100C)&1) != 0
        && field<unsigned char>(g_target_module,0x27100F) != 0;
}
std::uint64_t native_application_frame()
{
    return reinterpret_cast<std::uint64_t (*)()>(reinterpret_cast<std::uintptr_t>(g_target_module)+0xEC910)();
}
extern "C" __declspec(dllexport) bool auto_native_source(unsigned original_method, std::uint64_t frame)
{
    if (original_method == 2) return true;
    if (original_method != 3 || !auto_source_enabled()) return false;
    const bool was_fallback = g_auto_source.fallback();
    const bool selected = g_auto_source.select_native(frame,GetTickCount64());
    if (selected && !was_fallback)
        log_message(reshade::log::level::info,
            "NR AUTO RECOVERY 3: FrameGen NR idle; selected native SR input at frame=%llu. Auto stays on native input; manual hook choices unchanged.",frame);
    return selected;
}

unsigned char *native_thread_data()
{
    // The pinned image's IMAGE_TLS_DIRECTORY.AddressOfIndex is base+26A264.
    // Read only this thread's loader-owned TLS, never a different thread's TEB.
    if (!g_target_module) return nullptr;
    auto **slots = reinterpret_cast<unsigned char **>(__readgsqword(0x58));
    const auto index = field<unsigned>(g_target_module,0x26A264);
    return slots ? slots[index] : nullptr;
}

extern "C" __declspec(dllexport) std::uint64_t observed_framegen_callback(
    void *command, std::uint64_t feature, void *parameters)
{
    using Original = std::uint64_t (*)(void *,std::uint64_t,void *);
    const auto original = reinterpret_cast<Original>(reinterpret_cast<std::uintptr_t>(g_target_module)+0x9A4B0);
    auto *tls = native_thread_data();
    const unsigned guard_before = tls ? tls[0x4B0] : 255;
    const auto before = g_evaluation_calls.load(std::memory_order_relaxed);
    const auto success_before = g_successful_evaluations.load();
    g_fg_observer_calls.fetch_add(1,std::memory_order_relaxed);
    // Only FG-bearing calls participate; ordinary native SR/NR callbacks keep
    // their original behavior. Query through the same verified Get slot below.
    ID3D12Resource *fg_color = nullptr, *fg_hudless = nullptr;
    if (parameters && command && feature) {
        const auto *vt = *reinterpret_cast<std::uintptr_t **>(parameters);
        using Get = unsigned (*)(void *,const char *,ID3D12Resource **);
        const auto get = reinterpret_cast<Get>(vt[0x48/8]);
        get(parameters,"DLSSG.Backbuffer",&fg_color);
        get(parameters,"DLSSG.HUDLess",&fg_hudless);
    }
    const bool other_auto = (fg_color || fg_hudless) && auto_source_enabled();
    if (other_auto && !g_auto_source.enter_other(native_application_frame())) return 1;
    const auto result = original(command,feature,parameters);
    const auto after = g_evaluation_calls.load(std::memory_order_relaxed);
    if (other_auto) g_auto_source.leave_other(GetTickCount64(),g_successful_evaluations.load()!=success_before);
    const unsigned guard_after = tls ? tls[0x4B0] : 255;
    g_fg_guard_on_entry.store(guard_before,std::memory_order_relaxed);
    g_fg_guard_on_exit.store(guard_after,std::memory_order_relaxed);
    if (!parameters || !command || !feature) return result;

    // These two Get overload slots are verified against 9A4B0, not inferred
    // from a different NGX SDK's overloaded C++ vtable ordering.
    const auto *vtable = *reinterpret_cast<std::uintptr_t **>(parameters);
    using GetResource = unsigned (*)(void *,const char *,ID3D12Resource **);
    using GetUnsigned = unsigned (*)(void *,const char *,unsigned *);
    auto get_resource = reinterpret_cast<GetResource>(vtable[0x48/8]);
    auto get_unsigned = reinterpret_cast<GetUnsigned>(vtable[0x60/8]);
    ID3D12Resource *backbuffer = nullptr, *hudless = nullptr;
    get_resource(parameters,"DLSSG.Backbuffer",&backbuffer);
    get_resource(parameters,"DLSSG.HUDLess",&hudless);
    // This observer also sees SR/NR calls. Do not describe those as FG failures.
    if (!backbuffer && !hudless) return result;
    g_fg_parameter_calls.fetch_add(1,std::memory_order_relaxed);
    const auto now = GetTickCount64();
    auto next = g_fg_probe_next_tick.load(std::memory_order_relaxed);
    if (now < next || !g_fg_probe_next_tick.compare_exchange_strong(next,now+2000,std::memory_order_relaxed)) return result;
    unsigned index = UINT_MAX;
    const auto index_result = get_unsigned(parameters,"DLSSG.MultiFrameIndex",&index);
    ID3D12Resource *motion = nullptr, *depth = nullptr;
    get_resource(parameters,"DLSSG.MVecs",&motion);
    get_resource(parameters,"DLSSG.Depth",&depth);
    DWORD foreground_process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground_process);
    log_message(reshade::log::level::info,
        "NR FG PROBE 2: focused=%u thread=%lu callback=%llu fg-inputs=%llu hook=%u passes=%u route=%u/%u/%u/%u guard=%u->%u eval-global=%u->%u index=%u index-result=0x%08x color=%p hudless=%p motion=%p depth=%p feature=0x%llx original-result=0x%llx.",
        foreground_process==GetCurrentProcessId()?1u:0u,
        GetCurrentThreadId(),g_fg_observer_calls.load(),g_fg_parameter_calls.load(),
        static_cast<unsigned>(field<float>(g_target_module,0x270FB0)),field<unsigned>(g_target_module,0x266FA4),
        static_cast<unsigned>(field<unsigned char>(g_target_module,0x27100C)),
        static_cast<unsigned>(field<unsigned char>(g_target_module,0x27100D)),
        static_cast<unsigned>(field<unsigned char>(g_target_module,0x27100E)),
        static_cast<unsigned>(field<unsigned char>(g_target_module,0x27100F)),
        guard_before,guard_after,before,after,index,index_result,backbuffer,hudless,motion,depth,feature,result);
    // Descriptors only; no AddRef, resource transition, copies, or GPU evaluation.
    ID3D12Resource *resources[] = {backbuffer,hudless,motion,depth};
    for (unsigned i=0;i<4;++i) if (resources[i]) {
        const auto desc = resources[i]->GetDesc();
        log_message(reshade::log::level::info,
            "NR FG PROBE resource=%u extent=%llux%u array=%u mips=%u format=%u samples=%u flags=0x%x.",
            i,desc.Width,desc.Height,desc.DepthOrArraySize,desc.MipLevels,
            static_cast<unsigned>(desc.Format),desc.SampleDesc.Count,static_cast<unsigned>(desc.Flags));
    }
    return result;
}
