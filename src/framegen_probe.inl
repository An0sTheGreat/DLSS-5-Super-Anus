// Verified native DLSS-G observer at 9A4B0. Auto's latched native owner may
// skip NR here; otherwise preserve the upstream call and its return value.
// This never skips the vendor's FG evaluation or changes its parameters.
std::atomic_ullong g_fg_observer_calls = 0;
std::atomic_ullong g_fg_parameter_calls = 0;
std::atomic_ullong g_fg_probe_next_tick = 0;
std::atomic_ullong g_fg_last_entry_tick = 0;
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
    const auto callback_id = g_fg_observer_calls.fetch_add(1,std::memory_order_relaxed) + 1;
    // Only FG-bearing calls participate; ordinary native SR/NR callbacks keep
    // their original behavior. Query through the same verified Get slot below.
    // Manual hook modes at native scale require no parameter inspection or
    // TLS routing. Keep this path byte-for-byte equivalent to the upstream
    // call so diagnostic probing cannot perturb otherwise stable FrameGen.
    const bool transparent_bypass = !auto_source_enabled() && !nr::uses_scaled_path(
        g_scale_percent.load(std::memory_order_relaxed));
#ifdef NR_DAWNWALKER_NO_COPYBACK_TEST
    constexpr bool force_framegen_context = true;
#else
    constexpr bool force_framegen_context = false;
#endif
    if (transparent_bypass)
    {
        g_framegen_transparent_bypass.fetch_add(1, std::memory_order_relaxed);
        // Keep the normal path untouched. An explicitly requested trace may
        // inspect parameters below, but still never changes them or GPU work.
        if (!force_framegen_context && !g_frame_trace.enabled())
            return original(command,feature,parameters);
    }
    ID3D12Resource *fg_color = nullptr, *fg_hudless = nullptr;
    unsigned multi_frame_index = UINT_MAX;
    bool multi_frame_index_valid = false;
    if (parameters && command && feature) {
        const auto *vt = *reinterpret_cast<std::uintptr_t **>(parameters);
        using Get = unsigned (*)(void *,const char *,ID3D12Resource **);
        using GetUnsigned = unsigned (*)(void *,const char *,unsigned *);
        const auto get = reinterpret_cast<Get>(vt[0x48/8]);
        const auto get_unsigned = reinterpret_cast<GetUnsigned>(vt[0x60/8]);
        get(parameters,"DLSSG.Backbuffer",&fg_color);
        get(parameters,"DLSSG.HUDLess",&fg_hudless);
        multi_frame_index_valid = get_unsigned(parameters,"DLSSG.MultiFrameIndex",&multi_frame_index) == 1;
    }
    const bool framegen_inputs = fg_color || fg_hudless;
    const bool other_auto = framegen_inputs && auto_source_enabled();
    const auto source_frame = framegen_inputs ? native_application_frame() : 0;
    const auto entry_tick = GetTickCount64();
    const auto previous_entry_tick = framegen_inputs ?
        g_fg_last_entry_tick.exchange(entry_tick,std::memory_order_relaxed) : 0;
    const auto entry_gap = previous_entry_tick ? entry_tick-previous_entry_tick : 0;
    const auto trace_callback = [&](nr::TraceKind kind, std::uint64_t result, bool original_called)
    {
        if (!framegen_inputs || !g_frame_trace.enabled()) return;
        nr::FrameTraceEvent event;
        event.kind = kind;
        event.tick = kind == nr::TraceKind::framegen_entry ? entry_tick : GetTickCount64();
        event.gap = entry_gap;
        event.thread = GetCurrentThreadId();
        event.callback = callback_id;
        event.frame = source_frame;
        event.mfg_index = multi_frame_index_valid ? multi_frame_index : UINT_MAX;
        event.hook = static_cast<unsigned>(field<float>(g_target_module,0x270FB0));
        event.pass = field<unsigned>(g_target_module,0x266FA4);
        event.command = reinterpret_cast<std::uint64_t>(command);
        event.color = reinterpret_cast<std::uint64_t>(fg_color);
        event.output = reinterpret_cast<std::uint64_t>(fg_hudless);
        event.original_called = original_called;
        event.evaluations = g_evaluation_calls.load(std::memory_order_relaxed)-before;
        event.successes = g_successful_evaluations.load(std::memory_order_relaxed)-success_before;
        event.result = result;
        g_frame_trace.push(event);
    };
    trace_callback(nr::TraceKind::framegen_entry,0,false);
    if (transparent_bypass && !force_framegen_context)
    {
        const auto result = original(command,feature,parameters);
        trace_callback(nr::TraceKind::framegen_exit,result,true);
        return result;
    }
    if (other_auto && !g_auto_source.enter_other(source_frame))
    {
        trace_callback(nr::TraceKind::framegen_exit,1,false);
        return 1;
    }
    void *previous_transition_frame = nullptr;
    bool framegen_transition = false;
    if ((fg_color || fg_hudless) && g_framegen_transition_tls != TLS_OUT_OF_INDEXES)
    {
        previous_transition_frame = TlsGetValue(g_framegen_transition_tls);
        FrameGenTransitionContext context {
            source_frame,callback_id,multi_frame_index,multi_frame_index_valid};
        framegen_transition = TlsSetValue(g_framegen_transition_tls, &context) != FALSE;
        const auto result = original(command,feature,parameters);
        if (framegen_transition)
            TlsSetValue(g_framegen_transition_tls, previous_transition_frame);
        if (other_auto) g_auto_source.leave_other(GetTickCount64(),g_successful_evaluations.load()!=success_before);
        const unsigned guard_after = tls ? tls[0x4B0] : 255;
        g_fg_guard_on_entry.store(guard_before,std::memory_order_relaxed);
        g_fg_guard_on_exit.store(guard_after,std::memory_order_relaxed);
        trace_callback(nr::TraceKind::framegen_exit,result,true);
        if (!parameters || !command || !feature) return result;
        const auto after = g_evaluation_calls.load(std::memory_order_relaxed);
        if (g_trace_status.load(std::memory_order_relaxed) != 1) return result;
        const auto *vtable = *reinterpret_cast<std::uintptr_t **>(parameters);
        using GetResource = unsigned (*)(void *,const char *,ID3D12Resource **);
        auto get_resource = reinterpret_cast<GetResource>(vtable[0x48/8]);
        ID3D12Resource *backbuffer = nullptr, *hudless = nullptr;
        get_resource(parameters,"DLSSG.Backbuffer",&backbuffer);
        get_resource(parameters,"DLSSG.HUDLess",&hudless);
        if (!backbuffer && !hudless) return result;
        g_fg_parameter_calls.fetch_add(1,std::memory_order_relaxed);
        const auto now = GetTickCount64();
        auto next = g_fg_probe_next_tick.load(std::memory_order_relaxed);
        if (now < next || !g_fg_probe_next_tick.compare_exchange_strong(next,now+2000,std::memory_order_relaxed)) return result;
        ID3D12Resource *motion = nullptr, *depth = nullptr;
        get_resource(parameters,"DLSSG.MVecs",&motion);
        get_resource(parameters,"DLSSG.Depth",&depth);
        DWORD foreground_process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(),&foreground_process);
        log_message(reshade::log::level::info,
            "NR FG PROBE 3: focused=%u thread=%lu callback=%llu fg-inputs=%llu hook=%u passes=%u route=%u/%u/%u/%u guard=%u->%u eval-global=%u->%u index=%u valid=%u color=%p hudless=%p motion=%p depth=%p feature=0x%llx original-result=0x%llx.",
            foreground_process==GetCurrentProcessId()?1u:0u,GetCurrentThreadId(),callback_id,g_fg_parameter_calls.load(),
            static_cast<unsigned>(field<float>(g_target_module,0x270FB0)),field<unsigned>(g_target_module,0x266FA4),
            static_cast<unsigned>(field<unsigned char>(g_target_module,0x27100C)),static_cast<unsigned>(field<unsigned char>(g_target_module,0x27100D)),
            static_cast<unsigned>(field<unsigned char>(g_target_module,0x27100E)),static_cast<unsigned>(field<unsigned char>(g_target_module,0x27100F)),
            guard_before,guard_after,before,after,multi_frame_index,multi_frame_index_valid?1u:0u,
            backbuffer,hudless,motion,depth,feature,result);
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
    const auto result = original(command,feature,parameters);
    const auto after = g_evaluation_calls.load(std::memory_order_relaxed);
    if (other_auto) g_auto_source.leave_other(GetTickCount64(),g_successful_evaluations.load()!=success_before);
    const unsigned guard_after = tls ? tls[0x4B0] : 255;
    g_fg_guard_on_entry.store(guard_before,std::memory_order_relaxed);
    g_fg_guard_on_exit.store(guard_after,std::memory_order_relaxed);
    trace_callback(nr::TraceKind::framegen_exit,result,true);
    if (!parameters || !command || !feature) return result;
    if (g_trace_status.load(std::memory_order_relaxed) != 1) return result;

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
    unsigned index = multi_frame_index;
    const auto index_result = multi_frame_index_valid ? 1u : get_unsigned(parameters,"DLSSG.MultiFrameIndex",&index);
    ID3D12Resource *motion = nullptr, *depth = nullptr;
    get_resource(parameters,"DLSSG.MVecs",&motion);
    get_resource(parameters,"DLSSG.Depth",&depth);
    DWORD foreground_process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground_process);
    log_message(reshade::log::level::info,
        "NR FG PROBE 3: focused=%u thread=%lu callback=%llu fg-inputs=%llu hook=%u passes=%u route=%u/%u/%u/%u guard=%u->%u eval-global=%u->%u index=%u index-result=0x%08x color=%p hudless=%p motion=%p depth=%p feature=0x%llx original-result=0x%llx.",
        foreground_process==GetCurrentProcessId()?1u:0u,
        GetCurrentThreadId(),callback_id,g_fg_parameter_calls.load(),
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
