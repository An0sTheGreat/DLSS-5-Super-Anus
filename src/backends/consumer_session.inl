// Experimental teardown of the verified host's NR binding, not its NGX core.
// Included inside dx12. Caller has permanently stopped this backend's submissions
// and closed its completed transport. No synthetic device-destroy event is used.
enum class ConsumerClose : unsigned
{
    closed, busy, wrong_device, tracked_work, invalid_slots, live_features,
    live_outputs, core_owned_by_host, missing_shutdown, shutdown_failed, missing_output_guard
};

unsigned char *consumer_output_guard()
{
#ifdef NR_LIFETIME_TEST
    static unsigned char test_guard = 0;
    return &test_guard; // no official DLL TLS or resource callbacks in mock tests
#else
    // Verified TLS directory index RVA and byte used by the official device
    // teardown at 0xAC6B0. This uses the existing host TLS, not new grafted TLS.
    auto **table = reinterpret_cast<unsigned char **>(__readgsqword(0x58));
    const auto index = field<unsigned>(g_target_module, 0x26A264);
    if (!table || index == MAXDWORD || !table[index]) return nullptr;
    return table[index] + 0x4B1;
#endif
}

ConsumerClose close_empty_consumer(reshade::api::device *api_device, IUnknown *native_device)
{
    if (!api_device || !native_device || !g_target_module) return ConsumerClose::wrong_device;
    if (!try_lock_native_nr()) return ConsumerClose::busy;
    struct UnlockNative { ~UnlockNative() { unlock_native_nr(); } } native_lock;
    auto *bound = field<IUnknown *>(g_target_module, 0x26D760);
    if (!bound) return ConsumerClose::wrong_device;
    IUnknown *expected_identity = nullptr, *bound_identity = nullptr;
    native_device->QueryInterface(IID_PPV_ARGS(&expected_identity));
    bound->QueryInterface(IID_PPV_ARGS(&bound_identity));
    const bool matches = expected_identity && expected_identity == bound_identity;
    if (expected_identity) expected_identity->Release();
    if (bound_identity) bound_identity->Release();
    if (!matches) return ConsumerClose::wrong_device;
    if (!TryEnterCriticalSection(&g_render_mutex)) return ConsumerClose::busy;
    bool tracked = false;
    for (const auto &set : g_resource_sets)
        if (set.active && (set.device == api_device || set.native_feature)) tracked = true;
    for (const auto &record : g_tracked_command_lists)
        if (record.active && record.references.sets) tracked = true;
    // NR is process-global: a foreign/unknown native feature or NR-referencing
    // recording must also block teardown, even if its host slot was moved or
    // removed upstream. Do not equate an empty host vector with GPU completion.
    LeaveCriticalSection(&g_render_mutex);
    if (tracked) return ConsumerClose::tracked_work;

    auto &passes = native_slots(0x26D8D0);
    auto &retained = native_slots(0x26D8E8);
    const auto pass_count = passes.count(), retained_count = retained.count();
    if (pass_count == SIZE_MAX || retained_count == SIZE_MAX) return ConsumerClose::invalid_slots;
    const auto primary = native_primary_slot();
    if (primary.handle || primary.parameters) return ConsumerClose::live_features;
    for (std::size_t i = 0; i < pass_count; ++i)
        if (passes.begin[i].handle || passes.begin[i].parameters) return ConsumerClose::live_features;
    for (std::size_t i = 0; i < retained_count; ++i)
        if (retained.begin[i].handle || retained.begin[i].parameters) return ConsumerClose::live_features;
    // A host-owned core must follow its own shutdown path. This bridge only
    // accepts an existing external provider initialized by its private session.
    if (field<unsigned char>(g_target_module, 0x26D81B)) return ConsumerClose::core_owned_by_host;

    // Same verified MSVC mutex ABI as the main NR mutex. Take it AFTER the NR
    // mutex, matching the host. Resource callbacks may also use this lock.
    auto *output_mutex = reinterpret_cast<unsigned char *>(g_target_module) + 0x26D6A0;
    auto *srw = reinterpret_cast<PSRWLOCK>(output_mutex + 0x10);
    if (!TryAcquireSRWLockExclusive(srw)) return ConsumerClose::busy;
    if (field<unsigned>(output_mutex, 0x4C))
    { ReleaseSRWLockExclusive(srw); return ConsumerClose::busy; }
    field<DWORD>(output_mutex, 0x48) = GetCurrentThreadId();
    field<unsigned>(output_mutex, 0x4C) = 1;
    const auto begin = field<std::uintptr_t>(g_target_module, 0x26D8B8);
    const auto end = field<std::uintptr_t>(g_target_module, 0x26D8C0);
    const auto capacity = field<std::uintptr_t>(g_target_module, 0x26D8C8);
    struct UnlockOutputs
    {
        unsigned char *mutex;
        ~UnlockOutputs()
        {
            field<unsigned>(mutex, 0x4C) = 0;
            field<DWORD>(mutex, 0x48) = MAXDWORD;
            ReleaseSRWLockExclusive(reinterpret_cast<PSRWLOCK>(mutex + 0x10));
        }
    };
    const bool valid = begin ? end >= begin && capacity >= end &&
        (end - begin) % 0x50 == 0 && (capacity - begin) % 0x50 == 0 &&
        (end - begin) / 0x50 <= 64 : !end && !capacity;
    {
    UnlockOutputs output_lock{output_mutex};
    if (!valid) return ConsumerClose::invalid_slots;
    // Transport input destruction removes first-pass outputs. Later passes may
    // retain intermediate outputs keyed by those outputs. All tracked GPU work
    // is drained above: validate every remaining cache entry before releasing ANY.
    IUnknown *outputs[64] = {};
    const auto count = (end - begin) / 0x50;
    for (std::size_t i = 0; i < count; ++i)
    {
        auto *owned = field<IUnknown *>(reinterpret_cast<void *>(begin + i * 0x50), 8);
        if (!owned) continue;
        for (std::size_t j = 0; j < i; ++j)
            if (outputs[j] == owned) return ConsumerClose::invalid_slots;
        ID3D12Resource *resource = nullptr;
        if (FAILED(owned->QueryInterface(IID_PPV_ARGS(&resource)))) return ConsumerClose::live_outputs;
        IUnknown *resource_device = nullptr, *identity = nullptr, *expected = nullptr;
        const auto hr = resource->GetDevice(IID_PPV_ARGS(&resource_device));
        if (SUCCEEDED(hr)) resource_device->QueryInterface(IID_PPV_ARGS(&identity));
        native_device->QueryInterface(IID_PPV_ARGS(&expected));
        const bool ours = identity && identity == expected;
        if (expected) expected->Release();
        if (identity) identity->Release();
        if (resource_device) resource_device->Release();
        resource->Release();
        if (!ours) return ConsumerClose::live_outputs;
        outputs[i] = owned;
    }
    auto *guard = consumer_output_guard();
    if (count && !guard) return ConsumerClose::missing_output_guard;
    if (count)
    {
        const auto previous_guard = *guard;
        *guard = 1; // official recursive-resource-callback suppression
        field<std::uintptr_t>(g_target_module, 0x26D8C0) = begin;
        for (std::size_t i = 0; i < count; ++i)
        {
            field<IUnknown *>(reinterpret_cast<void *>(begin + i * 0x50), 8) = nullptr;
            if (outputs[i]) outputs[i]->Release();
        }
        *guard = previous_guard;
        log_message(reshade::log::level::info, "NR shared teardown: retired %u intermediate output slots.",
            static_cast<unsigned>(count));
    }
    }
    // Keep NR mutex held across Shutdown1, as the official device teardown does;
    // do NOT hold the render/output mutex across a third-party shutdown call.
    auto shutdown_nr = field<unsigned (__cdecl *)(IUnknown *)>(g_target_module, 0x26D718);
    if (!shutdown_nr) return ConsumerClose::missing_shutdown;
    const unsigned result = shutdown_nr(bound);
    field<unsigned>(g_target_module, 0x26D888) = result;
    if (result != 1) return ConsumerClose::shutdown_failed;

    // Mirror only the successfully closed NR binding's POD state. Keep loaded
    // libraries and parameter-provider pointers alive; do not shut down the core
    // or clear unrelated device caches. Host BindDevice (0x5D390) can rebind when
    // this pointer is null; validation of that path is a separate acceptance gate.
    passes.end = passes.begin;
    retained.end = retained.begin;
    field<std::uint64_t>(g_target_module, 0x26D878) = 0;
    field<std::uint64_t>(g_target_module, 0x26D898) = 0;
    field<unsigned char>(g_target_module, 0x26D900) = 1;
    field<unsigned char>(g_target_module, 0x26D903) = 0;
    field<unsigned char>(g_target_module, 0x26D904) = 0;
    field<unsigned char>(g_target_module, 0x26D905) = 0;
    field<IUnknown *>(g_target_module, 0x26D760) = nullptr;
    bound->Release(); // exactly the host's BindDevice AddRef, not our device ref
    return ConsumerClose::closed;
}
