// Real DX11/DX12 resource-sharing test. The evaluator is a COPY, not NGX/NR.
#define NOMINMAX
#include "../src/backends/dx11_transport.hpp"
#include <d3d12sdklayers.h>
#include <d3d11sdklayers.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
#include <cmath>
using namespace nr::backends::dx11;

static void check(HRESULT hr) { if (FAILED(hr)) { std::printf("HRESULT %08lX\n", hr); std::abort(); } }
static unsigned evaluations = 0;
static bool copy_evaluator(void *user, const Evaluation &evaluation)
{
    ++evaluations;
    const auto input = *static_cast<unsigned *>(user);
    auto *src = evaluation.textures[input], *dst = evaluation.textures[output];
    assert(src && dst);
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    for (auto &b : barriers) { b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; }
    barriers[0].Transition = {src, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE};
    barriers[1].Transition = {dst, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST};
    evaluation.commands->ResourceBarrier(2, barriers);
    evaluation.commands->CopyResource(dst, src);
    for (auto &b : barriers) { const auto state = b.Transition.StateBefore;
        b.Transition.StateBefore = b.Transition.StateAfter; b.Transition.StateAfter = state; }
    evaluation.commands->ResourceBarrier(2, barriers);
    return true;
}
static bool reject_evaluator(void *, const Evaluation &) { ++evaluations; return false; }
struct Reentrant { Transport *transport; const Frame *frame; };
static bool reentrant_evaluator(void *user, const Evaluation &)
{
    auto &state = *static_cast<Reentrant *>(user);
    assert(state.transport->submit(*state.frame, reject_evaluator, nullptr) == Result::busy);
    assert(!state.transport->try_close());
    return false;
}

static void make_frame(ID3D11Device *device, Frame &frame, unsigned width,
                       DXGI_FORMAT format, unsigned pixel_bytes, bool with_exposure)
{
    for (unsigned i = 0; i < texture_count; ++i)
    {
        if (i == exposure && !with_exposure) continue;
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = width; d.Height = 24; d.MipLevels = 1; d.ArraySize = 1;
        d.Format = format; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<unsigned char> data(width * d.Height * pixel_bytes, static_cast<unsigned char>(17 + i * 23));
        D3D11_SUBRESOURCE_DATA initial = {data.data(), width * pixel_bytes, 0};
        check(device->CreateTexture2D(&d, &initial, &frame.textures[i]));
    }
}
static void clear_frame(Frame &frame) { for (auto *&t : frame.textures) release(t); }
static void verify(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *texture,
                   unsigned pixel_bytes, unsigned expected)
{
    D3D11_TEXTURE2D_DESC d = {};
    texture->GetDesc(&d); d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    ID3D11Texture2D *staging = nullptr;
    check(device->CreateTexture2D(&d, nullptr, &staging));
    context->CopyResource(staging, texture);
    D3D11_MAPPED_SUBRESOURCE map = {};
    check(context->Map(staging, 0, D3D11_MAP_READ, 0, &map));
    for (UINT y = 0; y < d.Height; ++y)
        for (UINT x = 0; x < d.Width * pixel_bytes; ++x)
            assert(static_cast<const unsigned char *>(map.pData)[y * map.RowPitch + x] == expected);
    context->Unmap(staging, 0); release(staging);
}
static void drain(ID3D11DeviceContext *context)
{
    ID3D11Device *device = nullptr; context->GetDevice(&device);
    ID3D11Query *query = nullptr;
    D3D11_QUERY_DESC desc = {D3D11_QUERY_EVENT, 0};
    check(device->CreateQuery(&desc, &query));
    context->End(query); context->Flush();
    const auto deadline = GetTickCount64() + 5000;
    HRESULT hr;
    while ((hr = context->GetData(query, nullptr, 0, 0)) == S_FALSE)
    { assert(GetTickCount64() < deadline); SwitchToThread(); }
    check(hr); release(query); release(device);
}

static void close_completed(Transport &transport)
{
    // A completed DX11 query does not guarantee that every cross-API fence
    // completion has become CPU-visible in the same instant. Poll real fences;
    // never signal them to make teardown pass.
    const auto deadline = GetTickCount64() + 5000;
    while (!transport.try_close())
    { assert(GetTickCount64() < deadline); SwitchToThread(); }
    assert(transport.allocated_bytes() == 0);
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    bool debug = false;
    ID3D12Debug *debug12 = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug12))))
    { debug12->EnableDebugLayer(); debug = true; release(debug12); }
    IDXGIFactory4 *factory = nullptr;
    check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
    IDXGIAdapter *adapter = nullptr;
    if (argc > 1 && std::strcmp(argv[1], "warp") == 0) check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    else check(factory->EnumAdapters(0, &adapter));
    DXGI_ADAPTER_DESC ad = {}; check(adapter->GetDesc(&ad));
    std::printf("DX11/DX12 adapter: %ls; DX12 debug: %s\n", ad.Description, debug ? "on" : "unavailable");
    ID3D11Device *device11 = nullptr;
    ID3D11DeviceContext *context = nullptr;
    HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_DEBUG,
        nullptr, 0, D3D11_SDK_VERSION, &device11, nullptr, &context);
    bool debug11 = SUCCEEDED(hr);
    if (FAILED(hr)) check(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device11, nullptr, &context));
    std::printf("DX11 debug: %s\n", debug11 ? "on" : "unavailable");
    ID3D12Device *device12 = nullptr;
    check(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12)));
    ID3D12CommandQueue *queue = nullptr;
    D3D12_COMMAND_QUEUE_DESC q = {};
    check(device12->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
    Transport transport;
    ID3D11DeviceContext *deferred = nullptr;
    check(device11->CreateDeferredContext(0, &deferred));
    assert(transport.initialize(deferred, device12, queue) == E_INVALIDARG);
    release(deferred);
    check(transport.initialize(context, device12, queue, 16ull * 1024 * 1024));
    assert(transport.initialize(context, device12, queue) == E_INVALIDARG);
    Frame frame;
    unsigned source = color;
    assert(transport.submit(frame, copy_evaluator, &source) == Result::invalid_input);
    unsigned roundtrips = 0;
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT};
    const unsigned sizes[] = {4, 8, 4};
    for (unsigned f = 0; f < 3; ++f)
        for (unsigned cycle = 0; cycle < 72; ++cycle)
        {
            make_frame(device11, frame, 32 + (cycle % 3) * 16, formats[f], sizes[f], cycle % 2 != 0);
            source = cycle % 3;
            const auto result = transport.submit(frame, copy_evaluator, &source);
            if (result != Result::submitted)
                std::printf("format %u cycle %u: result %u hr %08lX stage %s\n", f, cycle, static_cast<unsigned>(result), transport.last_error(), transport.error_stage());
            assert(result == Result::submitted);
            verify(device11, context, frame.textures[output], sizes[f], 17 + source * 23);
            if (frame.textures[exposure])
            {
                source = exposure;
                assert(transport.submit(frame, copy_evaluator, &source) == Result::submitted);
                verify(device11, context, frame.textures[output], sizes[f], 17 + source * 23);
            }
            drain(context); clear_frame(frame);
            assert(transport.allocated_bytes() <= 16ull * 1024 * 1024);
            ++roundtrips;
        }
    // Real native NGX hosts commonly expose an R32_TYPELESS depth resource.
    // Copy into R32_FLOAT storage preserves the same 32-bit depth payload.
    make_frame(device11, frame, 32, DXGI_FORMAT_R32_FLOAT, 4, false);
    release(frame.textures[depth]);
    D3D11_TEXTURE2D_DESC depth_desc = {};
    frame.textures[color]->GetDesc(&depth_desc);
    depth_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    depth_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
    std::vector<unsigned char> depth_bytes(32 * 24 * 4, 17 + depth * 23);
    D3D11_SUBRESOURCE_DATA depth_initial = {depth_bytes.data(), 32 * 4, 0};
    check(device11->CreateTexture2D(&depth_desc, &depth_initial, &frame.textures[depth]));
    source = depth;
    assert(transport.submit(frame, copy_evaluator, &source) == Result::submitted);
    verify(device11, context, frame.textures[output], 4, 17 + depth * 23);
    drain(context); assert(transport.idle()); clear_frame(frame);

    // FFXIV's packed D24 + stencil. Check normalized depth (not stencil bits)
    // and restoration even when the application keeps the depth target bound.
    for (unsigned cycle = 0; cycle < 12; ++cycle)
    {
        const unsigned width = 32 + (cycle % 3) * 16;
        make_frame(device11, frame, width, DXGI_FORMAT_R32_FLOAT, 4, false);
        release(frame.textures[depth]);
        frame.textures[color]->GetDesc(&depth_desc);
        depth_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
        depth_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
        const unsigned values[] = {0, 1, 0x7FFFFF, 0x800000, 0xFFFFFF};
        std::vector<unsigned> packed(width * 24);
        for (unsigned i = 0; i < packed.size(); ++i) packed[i] = values[i % 5] | 0xAB000000;
        D3D11_SUBRESOURCE_DATA initial = {packed.data(), width * 4, 0};
        check(device11->CreateTexture2D(&depth_desc, &initial, &frame.textures[depth]));
        D3D11_DEPTH_STENCIL_VIEW_DESC vd = {};
        vd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; vd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        ID3D11DepthStencilView *dsv = nullptr, *restored = nullptr;
        check(device11->CreateDepthStencilView(frame.textures[depth], &vd, &dsv));
        ID3D11ShaderResourceView *saved_srv = nullptr, *restored_srv = nullptr;
        check(device11->CreateShaderResourceView(frame.textures[color], nullptr, &saved_srv));
        context->OMSetRenderTargets(0, nullptr, dsv);
        context->CSSetShaderResources(3, 1, &saved_srv);
        source = depth;
        const auto converted = transport.submit(frame, copy_evaluator, &source);
        if (converted != Result::submitted)
            std::printf("D24 conversion: %u, hr=%08lX stage=%s\n", static_cast<unsigned>(converted), transport.last_error(), transport.error_stage());
        assert(converted == Result::submitted);
        context->OMGetRenderTargets(0, nullptr, &restored);
        context->CSGetShaderResources(3, 1, &restored_srv);
        assert(same_object(dsv, restored) && same_object(saved_srv, restored_srv));
        release(restored); release(restored_srv); release(dsv); release(saved_srv);
        context->ClearState();
        D3D11_TEXTURE2D_DESC read = {}; frame.textures[output]->GetDesc(&read);
        read.Usage = D3D11_USAGE_STAGING; read.BindFlags = 0; read.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D *staging = nullptr;
        check(device11->CreateTexture2D(&read, nullptr, &staging));
        context->CopyResource(staging, frame.textures[output]);
        D3D11_MAPPED_SUBRESOURCE mapped = {}; check(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped));
        for (unsigned y = 0; y < 24; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            const auto actual_depth = reinterpret_cast<const float *>(static_cast<const char *>(mapped.pData) + y * mapped.RowPitch)[x];
            const auto expected_depth = static_cast<float>(values[(y * width + x) % 5]) / 16777215.0f;
            assert(std::isfinite(actual_depth) && std::fabs(actual_depth - expected_depth) <= 1.0e-7f);
        }
        context->Unmap(staging, 0); release(staging); drain(context); clear_frame(frame);
        assert(transport.allocated_bytes() <= 16ull * 1024 * 1024);
    }
    std::puts("Packed D24: 12 resize/source cycles, normalized float pixels, stencil exclusion, DSV/CS state restoration passed.");

    // R16G16_FLOAT motion must also survive the shared-resource round trip.
    make_frame(device11, frame, 32, DXGI_FORMAT_R16G16_FLOAT, 4, false);
    source = motion;
    assert(transport.submit(frame, copy_evaluator, &source) == Result::submitted);
    verify(device11, context, frame.textures[output], 4, 17 + motion * 23);
    drain(context); clear_frame(frame);

    make_frame(device11, frame, 32, formats[0], 4, false);
    Reentrant reentrant = {&transport, &frame};
    assert(transport.submit(frame, reentrant_evaluator, &reentrant) == Result::evaluator_failed);
    assert(transport.submit(frame, reject_evaluator, nullptr) == Result::evaluator_failed);
    verify(device11, context, frame.textures[output], 4, 17 + output * 23);
    assert(transport.submit(frame, nullptr, nullptr) == Result::invalid_input);
    // Explicitly block the private DX12 queue. Three in-flight slots is the
    // bound, then skip. Closing must not free resources the GPU still references.
    ID3D12Fence *block = nullptr;
    check(device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&block)));
    check(queue->Wait(block, 1));
    source = motion;
    for (unsigned i = 0; i < Transport::slot_count; ++i)
        assert(transport.submit(frame, copy_evaluator, &source) == Result::submitted);
    const auto before = evaluations;
    assert(transport.submit(frame, copy_evaluator, &source) == Result::busy);
    assert(evaluations == before);
    assert(!transport.try_close());
    assert(!transport.idle());
    // This is a TEST-owned blocker, NOT faking transport completion.
    check(block->Signal(1));
    verify(device11, context, frame.textures[output], 4, 17 + motion * 23);
    drain(context);
    assert(transport.try_close());
    assert(transport.allocated_bytes() == 0);
    clear_frame(frame); release(block);

    // Budget failure submits nothing and preserves output.
    check(transport.initialize(context, device12, queue, 1));
    make_frame(device11, frame, 32, formats[0], 4, false);
    assert(transport.submit(frame, copy_evaluator, &source) == Result::unsupported);
    assert(transport.allocated_bytes() == 0);
    verify(device11, context, frame.textures[output], 4, 17 + output * 23);
    assert(transport.try_close()); clear_frame(frame);

    // Reject foreign device inputs before any copy/recording. Even two devices
    // on the same adapter have different ownership of their resource objects.
    ID3D11Device *foreign = nullptr;
    check(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &foreign, nullptr, nullptr));
    check(transport.initialize(context, device12, queue));
    make_frame(foreign, frame, 32, formats[0], 4, false);
    assert(transport.submit(frame, copy_evaluator, &source) == Result::invalid_input);
    assert(transport.try_close()); clear_frame(frame); release(foreign);

    // Reuse the same transport object through completed session boundaries.
    // This tests transport lifetime only, not private NGX session recreation.
    for (unsigned session = 0; session < 24; ++session)
    {
        check(transport.initialize(context, device12, queue));
        make_frame(device11, frame, 32 + (session % 3) * 16, formats[0], 4, false);
        source = session % 3;
        assert(transport.submit(frame, copy_evaluator, &source) == Result::submitted);
        verify(device11, context, frame.textures[output], 4, 17 + source * 23);
        drain(context);
        close_completed(transport);
        assert(transport.try_close());
        clear_frame(frame);
    }

    // A NEW native DX11 device/context for each replacement, on the retained
    // same-adapter DX12 device/queue. Old-device textures must never be accepted.
    // No native NGX init/shutdown or NR evaluation is involved in this test.
    Frame stale_frame;
    make_frame(device11, stale_frame, 32, formats[0], 4, false);
    for (unsigned session = 0; session < 16; ++session)
    {
        ID3D11Device *next_device = nullptr;
        ID3D11DeviceContext *next_context = nullptr;
        check(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            debug11 ? D3D11_CREATE_DEVICE_DEBUG : 0, nullptr, 0, D3D11_SDK_VERSION,
            &next_device, nullptr, &next_context));
        assert(!same_object(next_device, device11));
        check(transport.initialize(next_context, device12, queue));
        const auto calls = evaluations;
        assert(transport.submit(stale_frame, copy_evaluator, &source) == Result::invalid_input);
        assert(evaluations == calls && transport.allocated_bytes() == 0);
        make_frame(next_device, frame, 32 + (session % 3) * 16, formats[0], 4, false);
        source = session % 3;
        assert(transport.submit(frame, copy_evaluator, &source) == Result::submitted);
        verify(next_device, next_context, frame.textures[output], 4, 17 + source * 23);
        drain(next_context);
        close_completed(transport);
        clear_frame(frame);
        release(next_context); release(next_device);
    }
    clear_frame(stale_frame);

    ID3D12InfoQueue *info12 = nullptr;
    if (SUCCEEDED(device12->QueryInterface(IID_PPV_ARGS(&info12))))
    {
        for (UINT64 i = 0; i < info12->GetNumStoredMessages(); ++i)
        {
            SIZE_T size = 0; check(info12->GetMessage(i, nullptr, &size));
            std::vector<unsigned char> data(size);
            auto *m = reinterpret_cast<D3D12_MESSAGE *>(data.data()); check(info12->GetMessage(i, m, &size));
            if (m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
            { std::printf("DX12 validation: %s\n", m->pDescription); std::abort(); }
        }
    }
    release(info12);
    ID3D11InfoQueue *info11 = nullptr;
    if (SUCCEEDED(device11->QueryInterface(IID_PPV_ARGS(&info11))))
    {
        for (UINT64 i = 0; i < info11->GetNumStoredMessages(); ++i)
        {
            SIZE_T size = 0; check(info11->GetMessage(i, nullptr, &size));
            std::vector<unsigned char> data(size);
            auto *m = reinterpret_cast<D3D11_MESSAGE *>(data.data()); check(info11->GetMessage(i, m, &size));
            if (m->Severity <= D3D11_MESSAGE_SEVERITY_ERROR)
            { std::printf("DX11 validation: %s\n", m->pDescription); std::abort(); }
        }
    }
    release(info11);
    release(queue); release(device12); release(context); release(device11); release(adapter); release(factory);
    std::printf("PASS: %u resize/format cycles, %u record calls; 24 transport recreations, 16 replacement DX11 devices; exact pixels, stale-device rejection, bounded slots, failure/retirement. COPY evaluator only.\n", roundtrips, evaluations);
}
