// Real WARP contexts and shared fences; parameter allocation is mocked. No NGX/NR.
// This specifically tests the older synthetic retained-core probe. The managed
// game-test configuration is exercised separately by dx11_core_shutdown.cpp.
#ifdef NR_DX11_GAME_TEST
#undef NR_DX11_GAME_TEST
#endif
#define NR_LIFETIME_TEST
#define NR_EXPERIMENTAL_DX11
#define NR_DX11_REPLACEMENT_PROBE
#include "../src/neural_resolution_addon.cpp"
#include <cassert>
#include <cstdio>
using namespace dx11_native;
static unsigned allocations = 0;
static bool allocation_fails = false;
static unsigned parameter_token = 0;
static Result mock_allocate(Params **out)
{
    ++allocations;
    *out = allocation_fails ? nullptr : reinterpret_cast<Params *>(&parameter_token);
    return allocation_fails ? NVSDK_NGX_Result_Fail : NVSDK_NGX_Result_Success;
}

int main()
{
    IDXGIFactory4 *factory = nullptr;
    IDXGIAdapter *adapter = nullptr;
    ID3D11Device *a = nullptr, *b = nullptr;
    ID3D11DeviceContext *ca = nullptr, *cb = nullptr, *deferred = nullptr;
    assert(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))));
    assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    assert(SUCCEEDED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &a, nullptr, &ca)));
    assert(SUCCEEDED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &b, nullptr, &cb)));
    assert(SUCCEEDED(b->CreateDeferredContext(0, &deferred)));
    assert(SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    D3D12_COMMAND_QUEUE_DESC q = {};
    assert(SUCCEEDED(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue))));
    native_context = ca; ca->AddRef();
    allocate = &mock_allocate;
    ready = true; failed = true; shutdown_seen = transport_closed = true;
    assert(!probe_restart_after_retirement(cb)); // incomplete consumer retirement
    consumer_detached = true;
    assert(!probe_restart_after_retirement(ca)); // same device isn't replacement evidence
    assert(!probe_restart_after_retirement(deferred)); // transport rejects before allocation
    assert(!allocations && native_context == ca && consumer_detached);
    for (unsigned cycle = 0; cycle < 16; ++cycle)
    {
        auto *next = cycle % 2 ? ca : cb;
        assert(probe_restart_after_retirement(next));
        assert(native_context == next && !failed && !shutdown_seen && !transport_closed);
        assert(!consumer_detached && parameters && reset_history && !active_native_generation);
        assert(!probe_restart_after_retirement(next)); // cannot reopen a live session
        assert(transport.try_close()); // no recordings or GPU work in this mock test
        parameters = nullptr; // mock token, not an NGX allocation
        failed = shutdown_seen = transport_closed = consumer_detached = true;
    }
    assert(allocations == 16);
    allocation_fails = true;
    assert(!probe_restart_after_retirement(cb));
    assert(allocations == 17 && failed && !consumer_detached && !transport_closed);
    assert(native_context == ca && !parameters);
    assert(!probe_restart_after_retirement(cb) && allocations == 17); // no ambiguous retry
    assert(transport.try_close());
    release(native_context); release(queue); release(device);
    release(deferred); release(cb); release(ca); release(b); release(a);
    release(adapter); release(factory);
    std::puts("DX11 replacement session: 16 WARP device switches, same-device/deferred/incomplete-retirement rejection, live-session guard and allocation-failure no-retry passed. Parameters mocked; no NGX/NR.");
}
