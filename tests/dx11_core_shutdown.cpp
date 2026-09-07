// Guard tests with a real WARP device and MOCKED private-core shutdown. No NGX.
#define NR_LIFETIME_TEST
#define NR_EXPERIMENTAL_DX11
#ifndef NR_DX11_GAME_TEST
#define NR_DX11_CORE_SHUTDOWN_PROBE
#define NR_DX11_CORE_RECREATE_PROBE
#endif
#include "../src/neural_resolution_addon.cpp"
#include <cassert>
#include <cstdio>
using namespace dx11_native;
static unsigned calls = 0;
static Result result = NVSDK_NGX_Result_Fail;
static Result mock_shutdown(ID3D12Device *owner, unsigned *output)
{
    assert(owner == device && core_shutdown_attempted);
    assert(output); *output = 0x12345678;
    ++calls;
    return result;
}
int main()
{
    IDXGIFactory4 *factory = nullptr;
    IDXGIAdapter *adapter = nullptr;
    assert(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))));
    assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    assert(SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    dx11_native::shutdown = &mock_shutdown;
    AcquireSRWLockExclusive(&mutex);
    ready = init_attempted = shutdown_seen = transport_closed = consumer_detached = true;
    bool *guards[] = {&ready, &init_attempted, &shutdown_seen, &transport_closed, &consumer_detached};
    for (auto *guard : guards)
    { *guard = false; assert(!probe_close_private_core() && !calls); *guard = true; }
    unsigned token = 0;
    feature = reinterpret_cast<Handle *>(&token);
    assert(!probe_close_private_core() && !calls); feature = nullptr;
    parameters = reinterpret_cast<Params *>(&token);
    assert(!probe_close_private_core() && !calls); parameters = nullptr;
    dx11_native::shutdown = nullptr; assert(!probe_close_private_core() && !calls); dx11_native::shutdown = &mock_shutdown;
    assert(!probe_close_private_core() && calls == 1 && core_shutdown_attempted);
    assert(ready && init_attempted && !core_closed);
    assert(!probe_close_private_core() && calls == 1); // no retry on ambiguous failure
    core_shutdown_attempted = false; // MOCK-ONLY fresh lifecycle, no actual driver call
    result = NVSDK_NGX_Result_Success;
    assert(probe_close_private_core() && calls == 2 && core_closed);
    assert(!ready && !init_attempted && device);
    assert(!probe_close_private_core() && calls == 2);
    InitializeCriticalSection(&dx12::g_render_mutex);
    // Synthetic API identity is never dereferenced: this fixture owns no API
    // pipelines. Real WARP fences/queues test the cleanup retirement guards.
    unsigned owner_token = 0, foreign_token = 0;
    consumer_device = reinterpret_cast<reshade::api::device *>(&owner_token);
    auto *foreign = reinterpret_cast<reshade::api::device *>(&foreign_token);
    D3D12_COMMAND_QUEUE_DESC desc = {};
    assert(SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))));
    ID3D12Fence *fence = nullptr;
    assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    queue->AddRef(); fence->AddRef();
    dx12::g_tracked_queues[0] = {consumer_device, queue, fence, 1};
    assert(!probe_close_private_graphics()); // fence not complete; no mutation
    assert(device && queue && dx12::g_tracked_queues[0].fence == fence);
    assert(SUCCEEDED(queue->Signal(fence, 1)));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(event && SUCCEEDED(fence->SetEventOnCompletion(1, event)));
    assert(WaitForSingleObject(event, 5000) == WAIT_OBJECT_0); CloseHandle(event);
    dx12::g_resource_sets[0].active = true;
    dx12::g_resource_sets[0].device = consumer_device;
    assert(!probe_close_private_graphics());
    dx12::g_resource_sets[0].device = foreign;
    dx12::g_tracked_command_lists[0].active = true;
    dx12::g_tracked_command_lists[0].device = consumer_device;
    assert(!probe_close_private_graphics()); // even empty recordings must retire
    dx12::g_tracked_command_lists[0].device = foreign;
    core_closed = false; assert(!probe_close_private_graphics()); core_closed = true;
    assert(probe_close_private_graphics());
    assert(graphics_closed && !device && !queue && !consumer_device && !dx11_native::shutdown);
    assert(!dx12::g_tracked_queues[0].native && !dx12::g_tracked_queues[0].fence);
    assert(dx12::g_resource_sets[0].device == foreign && dx12::g_tracked_command_lists[0].device == foreign);
    assert(!probe_close_private_graphics()); // double cleanup is inert
    shutdowns_in_flight = 1; assert(!probe_prepare_fresh_core()); shutdowns_in_flight = 0;
    assert(probe_prepare_fresh_core());
    assert(!graphics_closed && !core_closed && !core_shutdown_attempted && !failed);
    assert(!probe_prepare_fresh_core()); // one-shot; no permission after a failed init
    release(fence);
    // A failed fresh initialization latches disabled without restoring the
    // previous lifecycle permission, even if rollback later clears pointers.
    failed = true; init_attempted = true;
    assert(!probe_prepare_fresh_core()); init_attempted = false;
    assert(!probe_prepare_fresh_core());

    // Device removal is injected ONLY on this new WARP device, never a game or
    // physical GPU device. Removed fences must not masquerade as completion.
    assert(SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    assert(SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))));
    assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    consumer_device = reinterpret_cast<reshade::api::device *>(&owner_token);
    core_closed = core_shutdown_attempted = shutdown_seen = consumer_detached = transport_closed = true;
    queue->AddRef(); fence->AddRef();
    dx12::g_tracked_queues[0] = {consumer_device, queue, fence, 1};
    ID3D12Device5 *removable = nullptr;
    assert(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&removable))));
    HANDLE removed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(removed && SUCCEEDED(fence->SetEventOnCompletion(UINT64_MAX, removed)));
    removable->RemoveDevice();
    assert(WaitForSingleObject(removed, 5000) == WAIT_OBJECT_0); CloseHandle(removed);
    assert(fence->GetCompletedValue() == UINT64_MAX && FAILED(device->GetDeviceRemovedReason()));
    assert(!probe_close_private_graphics() && !graphics_closed);
    assert(device && queue && dx12::g_tracked_queues[0].fence == fence);
    assert(!probe_prepare_fresh_core());
    // Test-owned removed device: discard the fixture after checking retention.
    auto &tracked = dx12::g_tracked_queues[0];
    release(tracked.fence); release(tracked.native); tracked = {};
    release(removable); release(fence); release(queue);
    DeleteCriticalSection(&dx12::g_render_mutex);
    ReleaseSRWLockExclusive(&mutex);
    release(device); release(adapter); release(factory);
    std::puts("Private cleanup guards: core ABI/no-retry, actual fence completion/removal, live sets/recordings, foreign-device isolation, graphics release and failed-fresh-init latch passed. MOCKED NGX; real WARP graphics.");
}
