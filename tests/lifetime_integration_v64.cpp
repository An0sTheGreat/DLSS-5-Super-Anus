// Runs the actual collector against real WARP D3D12 queues/fences. NGX release
// calls are mocks and host slot storage is synthetic; this does NOT run NR.
#define NR_LIFETIME_TEST
#include "../src/neural_resolution_addon.cpp"
#include <cassert>
#include <cstdio>
using namespace dx12;

static unsigned releases = 0, parameter_releases = 0;
static bool release_fails = false;
static unsigned __cdecl mock_release(void *)
{
    if (release_fails) return 0xBAD00001;
    ++releases;
    return 1;
}
static unsigned __cdecl mock_parameters(void *) { ++parameter_releases; return 1; }
static unsigned shutdown_calls = 0;
static bool shutdown_fails = false;
static IUnknown *expected_shutdown_device = nullptr;
static unsigned __cdecl mock_nr_shutdown(IUnknown *device)
{
    assert(device == expected_shutdown_device);
    assert(!try_lock_native_nr()); // Match the official shutdown lock order.
    auto *output_lock = reinterpret_cast<PSRWLOCK>(reinterpret_cast<unsigned char *>(g_target_module) + 0x26D6B0);
    assert(TryAcquireSRWLockExclusive(output_lock)); // callbacks can acquire it
    ReleaseSRWLockExclusive(output_lock);
    ++shutdown_calls;
    return shutdown_fails ? 0xBAD00001 : 1;
}

static void complete_fence(ID3D12Fence *fence, std::uint64_t value)
{
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(event != nullptr);
    assert(SUCCEEDED(fence->SetEventOnCompletion(value, event)));
    assert(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0);
    CloseHandle(event);
}

int main()
{
    IDXGIFactory4 *factory = nullptr;
    assert(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))));
    IDXGIAdapter *adapter = nullptr;
    assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    ID3D12Device *device = nullptr;
    assert(SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    ID3D12CommandQueue *queues[2] = {};
    ID3D12Fence *fences[2] = {}, *block = nullptr;
    for (unsigned q = 0; q < 2; ++q)
    {
        assert(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queues[q]))));
        assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fences[q]))));
        g_tracked_queues[q] = {nullptr, queues[q], fences[q], 0};
    }
    assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&block))));
    g_target_module = static_cast<HMODULE>(VirtualAlloc(nullptr, 0x280000,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(g_target_module != nullptr);
    InitializeCriticalSection(&g_render_mutex);
    // Maintenance must never block or recursively enter an already-owned NR lock.
    assert(try_lock_native_nr());
    assert(!try_lock_native_nr());
    assert(field<unsigned>(g_target_module, 0x26D69C) == 1);
    unlock_native_nr();
    assert(field<unsigned>(g_target_module, 0x26D69C) == 0);
    assert(try_lock_native_nr());
    unlock_native_nr();
    field<unsigned (__cdecl *)(void *)>(g_target_module, 0x26D710) = &mock_release;
    field<unsigned (__cdecl *)(void *)>(g_target_module, 0x26D778) = &mock_parameters;
    field<int>(g_target_module, kPresetIndexRva) = 1;
    g_scale_percent = 75;
    g_scale_generation = 1;

    // Exercise the production registry with more live identities than slots.
    // Synthetic pointers are identities only: claim never dereferences them.
    for (unsigned i = 1; i <= 4096; ++i)
    {
        auto *record = claim_command_list_locked(reinterpret_cast<reshade::api::command_list *>(
            static_cast<std::uintptr_t>(i)), nullptr, i, 1);
        assert(record && record->native_command_list == i);
    }
    assert(g_command_slots_recycled == 4096 - kMaximumTrackedCommandLists);
    for (auto &record : g_tracked_command_lists) {
        record.references.sets = 1; record.references.resetting();
    }
    assert(!claim_command_list_locked(reinterpret_cast<reshade::api::command_list *>(5000), nullptr, 5000, 1));
    assert(g_command_slots_exhausted == 1);
    g_tracked_command_lists[17].references.recording();
    assert(claim_command_list_locked(reinterpret_cast<reshade::api::command_list *>(5000), nullptr, 5000, 1)
        == &g_tracked_command_lists[17]);
    assert(find_command_list_locked(reinterpret_cast<reshade::api::command_list *>(5000))
        == &g_tracked_command_lists[17]);
    g_tracked_command_lists = {};
    puts("Production command registry: 4096 identities, bounded recycling, all-pinned rejection and PRE-Reset preservation passed.");

    // Preset/pass/hook changes each create one stream epoch. Every evaluation
    // in the first observed native frame stays on the original path; the next
    // frame releases the guard without blocking or replaying work.
    g_stream_signature = 0;
    g_transition_generation = 0;
    g_transition_native_frame = 0;
    field<int>(g_target_module, kPresetIndexRva) = 1;
    field<unsigned>(g_target_module, 0x266FA4) = 1;
    field<float>(g_target_module, 0x270FB0) = 2.0f;
    observe_stream_configuration();
    assert(g_scale_generation == 1 && g_transition_generation == 0);
    field<float>(g_target_module, 0x270FB0) = 3.0f;
    observe_stream_configuration();
    assert(g_scale_generation == 2 && g_transition_generation == 2);
    assert(transition_uses_native(2, 100));
    assert(transition_uses_native(2, 100));
    assert(!transition_uses_native(2, 101));
    assert(g_transition_generation == 0);
    g_scale_generation = 1;
    g_stream_signature = 0;

    // Repeated source identities recycle one allocation after real queue-fence
    // completion. Texture rotation does not create a new stream-history reset.
    auto &pooled = g_resource_sets[0];
    pooled.active = pooled.valid = true;
    pooled.device = reinterpret_cast<reshade::api::device *>(1);
    pooled.allocation_generation = 1;
    pooled.allocated_bytes = 93ull << 20;
    auto *stream_history = g_scale_history.find(1, 0);
    assert(stream_history);
    unsigned stream_resets = 0;
    const unsigned retired_before_rotation = g_retired_sets.load();
    for (unsigned identity = 1; identity <= 64; ++identity)
    {
        pooled.pooled = false;
        pooled.valid = true;
        pooled.source_color = {identity};
        pooled.last_use = static_cast<ULONGLONG>(identity) * 3000;
        pooled.queue_mask = 1;
        if (stream_history->generation != 1)
        {
            ++stream_resets;
            stream_history->generation = 1;
        }
        collect_resources_locked(pooled.last_use + 2000);
        if (!pooled.pooled)
        {
            assert(pooled.retiring);
            complete_fence(fences[0], g_tracked_queues[0].serial);
            collect_resources_locked(pooled.last_use + 2001);
        }
        assert(pooled.active && pooled.pooled && !pooled.retiring);
        assert(pooled.allocated_bytes == (93ull << 20));
    }
    assert(stream_resets == 1);
    assert(g_retired_sets == retired_before_rotation);
    assert(g_pooled_sets == 1 && g_cached_mib == 93);
    g_scale_percent = 100;
    collect_resources_locked(2002);
    assert(!pooled.active && g_pooled_sets == 0);
    g_scale_percent = 75;
    g_resource_sets = {};
    puts("Stream transition and fence-safe working-texture pooling passed.");

    auto feature = [](std::uintptr_t id) {
        ResourceSet set;
        set.active = set.valid = set.native_feature = true;
        set.native_handle = reinterpret_cast<void *>(id);
        set.native_parameters = reinterpret_cast<void *>(id + 100);
        set.allocation_generation = 1;
        set.queue_mask = 3;
        return set;
    };
    auto set_primary = [](const ResourceSet &set) {
        field<void *>(g_target_module, 0x26D828) = set.native_handle;
        field<void *>(g_target_module, 0x26D820) = set.native_parameters;
    };

    g_resource_sets[0] = feature(1);
    set_primary(g_resource_sets[0]);
    g_tracked_command_lists[0].active = true;
    g_tracked_command_lists[0].references.sets = 1;
    collect_resources_locked(5000);
    assert(releases == 0 && !g_resource_sets[0].retiring); // Recorded but not replaced.
    g_tracked_command_lists[0].references.resetting();
    collect_resources_locked(5000);
    assert(releases == 0 && !g_resource_sets[0].retiring); // PRE-Reset alone is not enough.
    g_tracked_command_lists[0].references.recording();
    assert(SUCCEEDED(queues[1]->Wait(block, 1)));
    collect_resources_locked(5000);
    complete_fence(fences[0], g_tracked_queues[0].serial);
    collect_resources_locked(5001);
    assert(releases == 0 && g_resource_sets[0].active); // Second queue is still blocked.
    assert(SUCCEEDED(block->Signal(1)));
    complete_fence(fences[1], g_tracked_queues[1].serial);
    collect_resources_locked(5002);
    assert(releases == 1 && parameter_releases == 1 && !g_resource_sets[0].active);
    assert(field<void *>(g_target_module, 0x26D828) == nullptr);

    // An incomplete screenshot must not map or dispose resources before the
    // same recording pins and every queue fence drain. No buffers here: this
    // exercises the production abort/retirement path, not the image-copy path.
    g_resource_sets[0] = {};
    g_resource_sets[0].active = g_resource_sets[0].capture = true;
    g_resource_sets[0].device = reinterpret_cast<reshade::api::device *>(1);
    g_resource_sets[0].queue_mask = 3;
    capture::slot = 0; capture::pair_complete = false; capture::status.store(2);
    g_tracked_command_lists[0].references.sets = 1;
    collect_resources_locked(5003);
    assert(capture::slot == 0 && capture::status.load() == 2);
    g_tracked_command_lists[0].references.resetting();
    collect_resources_locked(5003);
    assert(capture::slot == 0);
    g_tracked_command_lists[0].references.recording();
    assert(SUCCEEDED(queues[1]->Wait(block, 2)));
    collect_resources_locked(5004);
    complete_fence(fences[0], g_tracked_queues[0].serial);
    collect_resources_locked(5005);
    assert(capture::slot == 0 && capture::status.load() == 2);
    assert(SUCCEEDED(block->Signal(2)));
    complete_fence(fences[1], g_tracked_queues[1].serial);
    collect_resources_locked(5006);
    assert(capture::slot == SIZE_MAX && capture::status.load() == 5);
    assert(!g_resource_sets[0].active && !capture::writing.load());
    capture::status.store(0);

    // A final pair remains owned while waiting for OFF, even between command
    // recordings. Expiry stops suppression but is never proof of GPU completion.
    g_resource_sets[0] = {};
    g_resource_sets[0].active = g_resource_sets[0].capture = true;
    g_resource_sets[0].device = reinterpret_cast<reshade::api::device *>(1);
    g_resource_sets[0].queue_mask = 3;
    capture::slot = 0; capture::pair_complete = false; capture::status.store(2);
    capture::final_phase.store(2); capture::final_timing.start(6000,0,0);
    g_capture_off_until.store(6500);
    collect_resources_locked(6499);
    assert(g_resource_sets[0].active && !g_resource_sets[0].retiring && g_capture_off_until.load() == 6500);
    ID3D12Fence *final_block = nullptr;
    assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&final_block))));
    assert(SUCCEEDED(queues[1]->Wait(final_block,1)));
    collect_resources_locked(6500);
    assert(g_capture_off_until.load() == 0 && capture::final_phase.load() == 3);
    complete_fence(fences[0],g_tracked_queues[0].serial);
    collect_resources_locked(6501);
    assert(g_resource_sets[0].active); // second queue still owns the aborted pair
    assert(SUCCEEDED(final_block->Signal(1)));
    complete_fence(fences[1],g_tracked_queues[1].serial);
    collect_resources_locked(6502);
    assert(capture::slot == SIZE_MAX && capture::final_phase.load() == 0 && !g_resource_sets[0].active);
    final_block->Release();

    // Dedicated final-copy fence: no application command-list events exist.
    // Completed CPU recording is not sufficient while the submitted copy waits.
    assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&final_block))));
    ID3D12Fence *copy_fence = nullptr;
    assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&copy_fence))));
    g_resource_sets[0] = {};
    g_resource_sets[0].active = g_resource_sets[0].capture = true;
    g_resource_sets[0].device = reinterpret_cast<reshade::api::device *>(1);
    g_resource_sets[0].capture_fence = copy_fence; g_resource_sets[0].capture_fence_value = 1;
    capture::slot = 0; capture::pair_complete = false; capture::status.store(2); capture::final_phase.store(3);
    assert(SUCCEEDED(queues[1]->Wait(final_block,1)));
    assert(SUCCEEDED(queues[1]->Signal(copy_fence,1)));
    collect_resources_locked(6600);
    assert(g_resource_sets[0].active && !g_resource_sets[0].unsafe_tracking);
    assert(SUCCEEDED(final_block->Signal(1))); complete_fence(copy_fence,1);
    g_resource_sets[0].unsafe_tracking = true;
    collect_resources_locked(6601); assert(g_resource_sets[0].active); // uncertainty holds even a signaled fence
    g_resource_sets[0].unsafe_tracking = false;
    collect_resources_locked(6602);
    assert(capture::slot == SIZE_MAX && !g_resource_sets[0].active && capture::status.load() == 5);
    final_block->Release(); // copy_fence was released by the real collector

    // Native feature at 100% scale is not a disposable scaling texture.
    g_resource_sets[0] = feature(2);
    g_resource_sets[0].last_use = 5000;
    set_primary(g_resource_sets[0]);
    g_scale_percent = 100;
    collect_resources_locked(5001);
    assert(!g_resource_sets[0].retiring);
    // Off must retire it after references/fences drain, even with no further NR calls.
    field<int>(g_target_module, kPresetIndexRva) = 0;
    collect_resources_locked(5001);
    for (unsigned q = 0; q < 2; ++q) complete_fence(fences[q], g_tracked_queues[q].serial);
    collect_resources_locked(5002);
    assert(releases == 2 && !g_resource_sets[0].active);

    std::array<NativeFeatureSlot, 3> retained;
    g_resource_sets[0] = feature(3);
    retained[0].handle = reinterpret_cast<void *>(999);
    retained[1].handle = g_resource_sets[0].native_handle;
    retained[1].parameters = g_resource_sets[0].native_parameters;
    retained[2].handle = reinterpret_cast<void *>(1000);
    native_slots(0x26D8E8) = {retained.data(), retained.data() + 3, retained.data() + 3};
    assert(release_native_feature(g_resource_sets[0]));
    assert(native_slots(0x26D8E8).count() == 2);
    assert(retained[0].handle == reinterpret_cast<void *>(999));
    assert(retained[1].handle == reinterpret_cast<void *>(1000));
    g_resource_sets[0] = {};

    std::array<NativeFeatureSlot, 2> passes;
    g_resource_sets[0] = feature(4);
    passes[1].handle = g_resource_sets[0].native_handle;
    passes[1].parameters = g_resource_sets[0].native_parameters;
    native_slots(0x26D8D0) = {passes.data(), passes.data() + 2, passes.data() + 2};
    assert(release_native_feature(g_resource_sets[0]));
    assert(passes[1].handle == nullptr && passes[1].performance == 3 && passes[1].preset == 1);
    assert(native_slots(0x26D8D0).count() == 2); // Preserve host vector indexing/capacity.

    g_resource_sets[0] = feature(5);
    set_primary(g_resource_sets[0]);
    release_fails = true;
    assert(!release_native_feature(g_resource_sets[0]));
    assert(g_resource_sets[0].unsafe_tracking);
    assert(field<void *>(g_target_module, 0x26D828) == g_resource_sets[0].native_handle);
    release_fails = false;

    g_resource_sets[0] = feature(6);
    g_resource_sets[0].queue_mask = 0;
    set_primary(g_resource_sets[0]);
    collect_resources_locked(5000);
    assert(g_resource_sets[0].unsafe_tracking && g_resource_sets[0].active);
    assert(releases == 4 && parameter_releases == 4);

    // Experimental explicit device retirement uses this same production fence
    // policy. Synthetic API identities are never dereferenced in these NR-only
    // sets; GPU queues/fences are real, NGX release remains mocked.
    g_resource_sets = {};
    g_tracked_command_lists = {};
    native_slots(0x26D8D0) = {};
    native_slots(0x26D8E8) = {};
    auto *owned_device = reinterpret_cast<reshade::api::device *>(1);
    auto *other_device = reinterpret_cast<reshade::api::device *>(2);
    field<int>(g_target_module, kPresetIndexRva) = 1;
    g_scale_percent = 75;
    g_resource_sets[0] = feature(7);
    g_resource_sets[0].device = owned_device;
    set_primary(g_resource_sets[0]);
    g_resource_sets[1] = feature(8);
    g_resource_sets[1].device = other_device;
    g_resource_sets[1].last_use = GetTickCount64();
    g_tracked_command_lists[1].active = true;
    g_tracked_command_lists[1].references.sets = 2; // other device remains live
    unsigned remaining = 0;
    assert(!retire_device_resources(nullptr, remaining) && remaining == UINT_MAX);
    assert(try_lock_native_nr());
    assert(!retire_device_resources(owned_device, remaining) && remaining == UINT_MAX);
    unlock_native_nr();
    g_tracked_command_lists[0].active = true;
    g_tracked_command_lists[0].references.sets = 1;
    assert(!retire_device_resources(owned_device, remaining) && remaining == 1);
    assert(g_resource_sets[0].active && !g_resource_sets[0].valid);
    assert(g_resource_sets[1].active && g_resource_sets[1].valid);
    g_tracked_command_lists[0] = {};
    assert(SUCCEEDED(queues[1]->Wait(block, 3)));
    assert(!retire_device_resources(owned_device, remaining));
    complete_fence(fences[0], g_tracked_queues[0].serial);
    assert(!retire_device_resources(owned_device, remaining) && remaining == 1);
    assert(releases == 4);
    assert(SUCCEEDED(block->Signal(3)));
    complete_fence(fences[1], g_tracked_queues[1].serial);
    assert(retire_device_resources(owned_device, remaining) && remaining == 0);
    assert(releases == 5 && parameter_releases == 5);
    assert(retire_device_resources(owned_device, remaining)); // idempotent
    assert(releases == 5 && g_resource_sets[1].active && g_resource_sets[1].valid);
    g_resource_sets[0] = feature(9);
    g_resource_sets[0].device = owned_device;
    g_resource_sets[0].unsafe_tracking = true;
    set_primary(g_resource_sets[0]);
    assert(!retire_device_resources(owned_device, remaining) && remaining == 1);
    assert(releases == 5); // Shutdown request never overrides an unsafe hold.
    g_resource_sets[0] = feature(10);
    g_resource_sets[0].device = owned_device;
    set_primary(g_resource_sets[0]);
    release_fails = true;
    assert(!retire_device_resources(owned_device, remaining));
    for (unsigned q = 0; q < 2; ++q) complete_fence(fences[q], g_tracked_queues[q].serial);
    assert(!retire_device_resources(owned_device, remaining) && remaining == 1);
    assert(g_resource_sets[0].unsafe_tracking && releases == 5);
    release_fails = false;
    assert(!retire_device_resources(owned_device, remaining)); // no unsafe retry
    assert(releases == 5 && field<void *>(g_target_module, 0x26D828) == reinterpret_cast<void *>(10));

    // Narrow shared NR binding teardown. No real NGX calls: use the real WARP
    // device for identity/owned COM reference and synthetic host cache storage.
    g_resource_sets = {};
    g_tracked_command_lists = {};
    field<void *>(g_target_module, 0x26D828) = nullptr;
    field<void *>(g_target_module, 0x26D820) = nullptr;
    field<IUnknown *>(g_target_module, 0x26D760) = device;
    device->AddRef(); // stand in for the host's BindDevice ownership
    expected_shutdown_device = device;
    field<unsigned (__cdecl *)(IUnknown *)>(g_target_module, 0x26D718) = &mock_nr_shutdown;
    assert(close_empty_consumer(owned_device, queues[0]) == ConsumerClose::wrong_device);
    assert(try_lock_native_nr());
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::busy);
    unlock_native_nr();
    g_resource_sets[0].active = true;
    g_resource_sets[0].device = owned_device;
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::tracked_work);
    g_resource_sets[0] = {};
    g_resource_sets[0].active = g_resource_sets[0].native_feature = true;
    g_resource_sets[0].device = other_device;
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::tracked_work);
    g_resource_sets[0] = {};
    g_tracked_command_lists[0].active = true;
    g_tracked_command_lists[0].device = owned_device;
    g_tracked_command_lists[0].references.sets = 1;
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::tracked_work);
    g_tracked_command_lists[0].device = other_device;
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::tracked_work);
    g_tracked_command_lists[0].references.sets = 0; // unused list does not pin NR
    field<void *>(g_target_module, 0x26D820) = reinterpret_cast<void *>(123);
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::live_features);
    field<void *>(g_target_module, 0x26D820) = nullptr;
    native_slots(0x26D8D0) = {passes.data(), passes.data() + 1, passes.data()};
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::invalid_slots);
    passes = {};
    passes[0].parameters = reinterpret_cast<void *>(321);
    native_slots(0x26D8D0) = {passes.data(), passes.data() + 2, passes.data() + 2};
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::live_features);
    passes[0].parameters = nullptr;
    field<unsigned char>(g_target_module, 0x26D81B) = 1;
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::core_owned_by_host);
    field<unsigned char>(g_target_module, 0x26D81B) = 0;
    auto *output_lock = reinterpret_cast<PSRWLOCK>(reinterpret_cast<unsigned char *>(g_target_module) + 0x26D6B0);
    assert(TryAcquireSRWLockExclusive(output_lock));
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::busy);
    ReleaseSRWLockExclusive(output_lock);
    std::array<std::array<unsigned char, 0x50>, 2> output_slots = {};
    const auto output_begin = reinterpret_cast<std::uintptr_t>(output_slots.data());
    field<std::uintptr_t>(g_target_module, 0x26D8B8) = output_begin;
    field<std::uintptr_t>(g_target_module, 0x26D8C0) = output_begin + 1;
    field<std::uintptr_t>(g_target_module, 0x26D8C8) = output_begin + 0xA0;
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::invalid_slots);
    field<std::uintptr_t>(g_target_module, 0x26D8C0) = output_begin + 0x50;
    field<IUnknown *>(output_slots.data(), 8) = queues[0]; // not a resource; must not release
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::live_outputs);
    field<IUnknown *>(output_slots.data(), 8) = nullptr;
    field<std::uintptr_t>(g_target_module, 0x26D8C0) = output_begin;
    assert(shutdown_calls == 0);
    shutdown_fails = true;
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::shutdown_failed);
    assert(field<IUnknown *>(g_target_module, 0x26D760) == device);
    assert(native_slots(0x26D8D0).count() == 2); // failed close preserves host state
    shutdown_fails = false;
    // Test-only retry of a mocked failure. Production shutdown_seen forbids it.
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::closed);
    assert(!field<IUnknown *>(g_target_module, 0x26D760));
    assert(native_slots(0x26D8D0).count() == 0 && shutdown_calls == 2);
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::wrong_device);
    assert(shutdown_calls == 2); // no repeated shutdown or COM Release
    for (unsigned cycle = 0; cycle < 16; ++cycle)
    {
        device->AddRef();
        field<IUnknown *>(g_target_module, 0x26D760) = device;
        retained = {};
        retained[0].handle = reinterpret_cast<void *>(456);
        native_slots(0x26D8E8) = {retained.data(), retained.data() + 1, retained.data() + 3};
        assert(close_empty_consumer(owned_device, device) == ConsumerClose::live_features);
        retained[0].handle = nullptr;
        field<unsigned (__cdecl *)(IUnknown *)>(g_target_module, 0x26D718) = nullptr;
        assert(close_empty_consumer(owned_device, device) == ConsumerClose::missing_shutdown);
        field<unsigned (__cdecl *)(IUnknown *)>(g_target_module, 0x26D718) = &mock_nr_shutdown;
        field<unsigned>(g_target_module, 0x26D878) = 123;
        assert(close_empty_consumer(owned_device, device) == ConsumerClose::closed);
        assert(!field<IUnknown *>(g_target_module, 0x26D760));
        assert(field<unsigned>(g_target_module, 0x26D878) == 0);
        assert(field<unsigned char>(g_target_module, 0x26D900) == 1);
        assert(native_slots(0x26D8E8).count() == 0);
    }
    assert(shutdown_calls == 18);

    // Real owned intermediate output: inspect ownership before mutating, refuse
    // duplicate aliases, release one cache reference and preserve the TLS guard.
    D3D12_HEAP_PROPERTIES output_heap = {};
    output_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC output_desc = {};
    output_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    output_desc.Width = output_desc.Height = 16;
    output_desc.DepthOrArraySize = output_desc.MipLevels = 1;
    output_desc.Format = DXGI_FORMAT_R32_FLOAT;
    output_desc.SampleDesc.Count = 1;
    output_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource *owned_output = nullptr;
    assert(SUCCEEDED(device->CreateCommittedResource(&output_heap, D3D12_HEAP_FLAG_NONE,
        &output_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&owned_output))));
    owned_output->AddRef(); // cache ref; retain the original reference in the test
    device->AddRef();
    field<IUnknown *>(g_target_module, 0x26D760) = device;
    field<IUnknown *>(output_slots[0].data(), 8) = owned_output;
    field<IUnknown *>(output_slots[1].data(), 8) = owned_output;
    field<std::uintptr_t>(g_target_module, 0x26D8C0) = output_begin + 0xA0;
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::invalid_slots);
    assert(field<IUnknown *>(output_slots[0].data(), 8) == owned_output && shutdown_calls == 18);
    field<IUnknown *>(output_slots[1].data(), 8) = nullptr;
    field<std::uintptr_t>(g_target_module, 0x26D8C0) = output_begin + 0x50;
    *consumer_output_guard() = 7;
    assert(close_empty_consumer(owned_device, device) == ConsumerClose::closed);
    assert(*consumer_output_guard() == 7 && shutdown_calls == 19);
    assert(field<std::uintptr_t>(g_target_module, 0x26D8C0) == output_begin);
    assert(!field<IUnknown *>(output_slots[0].data(), 8));
    assert(owned_output->GetDesc().Width == 16);
    owned_output->Release();

    VirtualFree(g_target_module, 0, MEM_RELEASE);
    g_target_module = nullptr;
    DeleteCriticalSection(&g_render_mutex);
    block->Release();
    for (unsigned q = 0; q < 2; ++q) { fences[q]->Release(); queues[q]->Release(); }
    device->Release(); adapter->Release(); factory->Release();
    std::puts("Production collector integration passed: WARP multi-queue fences; recording pins; NR-off; 100% feature retention; mock NGX primary/retained/pass-slot release; device-targeted retirement and error holds. No actual NGX or game execution.");
    std::puts("Shared NR binding teardown: identity, lock, tracking, feature/output cache, core-ownership and missing/failing shutdown guards; 16 repeat bindings passed with MOCKED NR shutdown.");
}
