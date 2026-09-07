// Isolated D3D12/ReShade capture acceptance host. NR is deliberately simulated:
// ON is a known clear color; OFF calls the production deadline-bypass branch.
// Actual swapchain copies, barriers, queue fences, readback and PNG are exercised.
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
static void check(HRESULT hr) { if (FAILED(hr)) { std::printf("FAIL HRESULT %08lx\n",hr); std::exit(2); } }
static std::uintptr_t address(HMODULE module, const char *name)
{
    char value[32] = {};
    if (!GetEnvironmentVariableA(name,value,sizeof(value))) std::exit(3);
    const auto rva = std::strtoull(value,nullptr,16);
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base+dos->e_lfanew);
    if (rva < 0x292000 || rva+32 >= nt->OptionalHeader.SizeOfImage) std::exit(4);
    return base+rva;
}
int main(int argc, char **argv)
{
    const bool hdr = argc > 1 && std::strcmp(argv[1],"hdr") == 0;
    const bool timeout = argc > 1 && std::strcmp(argv[1],"timeout") == 0;
    WNDCLASSW cls = {}; cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"NRFinalCaptureFixture";
    RegisterClassW(&cls);
    HWND window = CreateWindowW(cls.lpszClassName,L"NR capture test (simulated NR)",WS_OVERLAPPEDWINDOW,
        100,100,360,240,nullptr,nullptr,cls.hInstance,nullptr);
    if (!window) return 5;
    ShowWindow(window,SW_SHOWNOACTIVATE);
    IDXGIFactory4 *factory = nullptr; check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));
    ID3D12Device *device = nullptr; check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ID3D12CommandQueue *queue = nullptr; D3D12_COMMAND_QUEUE_DESC q = {}; check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    DXGI_SWAP_CHAIN_DESC1 desc = {}; desc.Width = 320; desc.Height = 180; desc.Format = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1 *sc1 = nullptr; check(factory->CreateSwapChainForHwnd(queue,window,&desc,nullptr,nullptr,&sc1));
    IDXGISwapChain3 *swapchain = nullptr; check(sc1->QueryInterface(IID_PPV_ARGS(&swapchain))); sc1->Release();
    check(swapchain->SetColorSpace1(hdr ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709));
    ID3D12DescriptorHeap *heap = nullptr; D3D12_DESCRIPTOR_HEAP_DESC hd = {}; hd.NumDescriptors = 2; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    ID3D12Resource *buffers[2] = {};
    const auto first = heap->GetCPUDescriptorHandleForHeapStart(); const auto stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (unsigned i = 0; i < 2; ++i) { check(swapchain->GetBuffer(i,IID_PPV_ARGS(&buffers[i]))); device->CreateRenderTargetView(buffers[i],nullptr,{first.ptr+i*stride}); }
    ID3D12CommandAllocator *allocator = nullptr; check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ID3D12GraphicsCommandList *list = nullptr; check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&list))); check(list->Close());
    ID3D12Fence *fence = nullptr; check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE done = CreateEventW(nullptr,FALSE,FALSE,nullptr);
    HMODULE addon = nullptr;
    unsigned requests = 0, skipped = 0;
    for (unsigned frame = 0; frame < 300; ++frame) {
        MSG msg; while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (!addon) addon = GetModuleHandleW(L"renodx-dlss5-super-anus.addon64");
        bool off = false;
        if (addon) {
            const auto until = reinterpret_cast<std::atomic<ULONGLONG> *>(address(addon,"NR_FINAL_OFF_RVA"))->load();
            off = until && GetTickCount64() < until;
            if (off) {
                if (!timeout) {
                unsigned char input[0xA5] = {};
                const auto eval = reinterpret_cast<std::uint64_t (*)(void *,unsigned)>(address(addon,"NR_FINAL_EVAL_RVA"));
                if (eval(input,0) != 0) return 6;
                ++skipped;
                }
            } else reinterpret_cast<std::atomic_uint *>(address(addon,"NR_FINAL_SUCCESS_RVA"))->fetch_add(1);
            if (frame == 60 || frame == 140 || frame == 220) {
                const auto request = reinterpret_cast<void (*)()>(address(addon,"NR_CAPTURE_TEST_RVA"));
                request(); ++requests;
            }
        }
        check(allocator->Reset()); check(list->Reset(allocator,nullptr));
        const unsigned index = swapchain->GetCurrentBackBufferIndex();
        D3D12_RESOURCE_BARRIER barrier = {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffers[index]; barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1,&barrier);
        float color[] = {hdr ? 1.5f : .4f,hdr ? .5f : .6f,hdr ? .25f : .8f,1.f};
        if (off) for (unsigned c = 0; c < 3; ++c) color[c] *= .5f;
        list->ClearRenderTargetView({first.ptr+index*stride},color,0,nullptr);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        list->ResourceBarrier(1,&barrier); check(list->Close());
        ID3D12CommandList *submit[] = {list}; queue->ExecuteCommandLists(1,submit);
        check(swapchain->Present(0,0));
        check(queue->Signal(fence,frame+1));
        check(fence->SetEventOnCompletion(frame+1,done));
        if (WaitForSingleObject(done,5000) != WAIT_OBJECT_0) return 7;
        Sleep(10);
    }
    std::printf("Final-screen fixture: requests=%u bypassed=%u mode=%s. Simulated NR; real D3D12/ReShade copies.\n",requests,skipped,hdr?"scRGB":"SDR");
    CloseHandle(done); fence->Release(); list->Release(); allocator->Release();
    for (auto *buffer : buffers) buffer->Release();
    heap->Release(); swapchain->Release(); queue->Release(); device->Release(); factory->Release(); DestroyWindow(window);
    return requests == 3 && (timeout ? skipped == 0 : skipped >= 3) ? 0 : 8;
}
