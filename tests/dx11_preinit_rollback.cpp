// Real WARP devices/transport, staged PRE-NGX failures only. Never initializes
// NGX, installs hooks, executes the official binary, or submits GPU work.
#define NR_LIFETIME_TEST
#define NR_EXPERIMENTAL_DX11
#include "../src/neural_resolution_addon.cpp"
#include <cassert>
#include <cstdio>

int main()
{
    using nr::backends::dx11::release;
    IDXGIFactory4 *factory = nullptr;
    IDXGIAdapter *adapter = nullptr;
    ID3D11Device *device11 = nullptr;
    ID3D11DeviceContext *context = nullptr;
    ID3D12Device *device12 = nullptr;
    ID3D12CommandQueue *commands = nullptr;
    assert(SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))));
    assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    assert(SUCCEEDED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device11, nullptr, &context)));
    assert(SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12))));
    D3D12_COMMAND_QUEUE_DESC desc = {};
    assert(SUCCEEDED(device12->CreateCommandQueue(&desc, IID_PPV_ARGS(&commands))));
    for (unsigned cycle = 0; cycle < 32; ++cycle)
    {
        const auto stage = cycle % 4;
        dx11_native::device = device12; device12->AddRef();
        if (stage >= 1) { dx11_native::queue = commands; commands->AddRef(); }
        if (stage >= 2)
        {
            assert(SUCCEEDED(dx11_native::transport.initialize(context, device12, commands)));
            dx11_native::native_context = context; context->AddRef();
        }
        if (stage >= 3)
        {
            dx11_native::d3d12_module = LoadLibraryW(L"d3d12.dll");
            assert(dx11_native::d3d12_module);
            // Simulate only the ownership boundary flag, not a driver call.
            dx11_native::init_attempted = true;
            dx11_native::rollback_before_ngx_init();
            assert(dx11_native::device == device12 && dx11_native::queue == commands);
            assert(dx11_native::native_context == context && dx11_native::d3d12_module);
            dx11_native::init_attempted = false;
        }
        dx11_native::rollback_before_ngx_init();
        assert(!dx11_native::device && !dx11_native::queue && !dx11_native::native_context);
        assert(!dx11_native::d3d12_module && !dx11_native::ngx_module);
        assert(dx11_native::transport.allocated_bytes() == 0);
        dx11_native::rollback_before_ngx_init(); // safe repeated cleanup
    }
    release(commands); release(device12); release(context); release(device11);
    release(adapter); release(factory);
    std::puts("DX11 pre-NGX rollback: 32 staged WARP cycles; device/queue/transport/module cleanup, repeat cleanup and attempted-init retention passed. No NGX calls.");
}
