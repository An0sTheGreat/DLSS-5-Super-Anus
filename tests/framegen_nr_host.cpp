// Controlled native-DX12 NR test through the real FrameGen observer. The FG
// inputs are synthetic; this does NOT generate or certify interpolated frames.
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include "native_capture_trigger.hpp"
static void check(HRESULT hr) { if (FAILED(hr)) { printf("FAIL HRESULT %08lx\n",hr); ExitProcess(2); } }
static void ngx(NVSDK_NGX_Result r) { if (NVSDK_NGX_FAILED(r)) { printf("FAIL NGX %08x\n",r); ExitProcess(3); } }
static std::uintptr_t address(HMODULE module,const char *name) {
    char value[32]={}; if (!GetEnvironmentVariableA(name,value,sizeof(value))) ExitProcess(4);
    const auto rva=std::strtoull(value,nullptr,16);
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    const auto *dos=reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    const auto *nt=reinterpret_cast<IMAGE_NT_HEADERS64 *>(base+dos->e_lfanew);
    if (rva<0x292000 || rva+32>=nt->OptionalHeader.SizeOfImage) ExitProcess(5);
    return base+rva;
}
int main(int argc,char **argv) {
    const bool recovery = argc>1 && !strcmp(argv[1],"recovery");
    char churn_value[8]={};
    const bool scale_churn=GetEnvironmentVariableA("NR_TEST_SCALE_CHURN",churn_value,sizeof(churn_value))!=0;
    char mfg_value[8]={};
    const bool mfg_cadence=GetEnvironmentVariableA("NR_TEST_MFG_CADENCE",mfg_value,sizeof(mfg_value))!=0;
    char initial_scale_value[8]={};
    const unsigned initial_scale=GetEnvironmentVariableA("NR_TEST_INITIAL_SCALE",initial_scale_value,sizeof(initial_scale_value)) ?
        static_cast<unsigned>(std::strtoul(initial_scale_value,nullptr,10)) : 0;
    char forced_context_value[8]={};
    const bool forced_context=GetEnvironmentVariableA(
        "NR_TEST_FORCED_FG_CONTEXT",forced_context_value,sizeof(forced_context_value))!=0;
    WNDCLASSW cls={}; cls.lpfnWndProc=DefWindowProcW; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"NRFrameGenFixture";
    RegisterClassW(&cls);
    auto window=CreateWindowW(cls.lpszClassName,L"NR FrameGen callback test",WS_OVERLAPPEDWINDOW,100,100,360,240,nullptr,nullptr,cls.hInstance,nullptr);
    if (!window) return 6;
    ShowWindow(window,SW_SHOWNOACTIVATE);
    IDXGIFactory4 *factory=nullptr; check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));
    ID3D12Device *device=nullptr; check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ID3D12CommandQueue *queue=nullptr; D3D12_COMMAND_QUEUE_DESC q={}; check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    DXGI_SWAP_CHAIN_DESC1 sc={}; sc.Width=320; sc.Height=180; sc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; sc.SampleDesc.Count=1;
    sc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sc.BufferCount=2; sc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1 *swapchain=nullptr; check(factory->CreateSwapChainForHwnd(queue,window,&sc,nullptr,nullptr,&swapchain));
    wchar_t here[MAX_PATH]={}; GetModuleFileNameW(nullptr,here,MAX_PATH); if (auto *slash=wcsrchr(here,L'\\')) *(slash+1)=0;
    const wchar_t *paths[]={here}; NVSDK_NGX_FeatureCommonInfo ci={}; ci.PathListInfo.Path=paths; ci.PathListInfo.Length=1;
    ngx(NVSDK_NGX_D3D12_Init_with_ProjectID("a7d3f0c8-6b21-4e5a-9f14-3c07b1e9d240",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"1.0",here,device,&ci,NVSDK_NGX_Version_API));
    NVSDK_NGX_Parameter *parameters=nullptr; ngx(NVSDK_NGX_D3D12_AllocateParameters(&parameters));
    ID3D12DescriptorHeap *heap=nullptr; D3D12_DESCRIPTOR_HEAP_DESC hd={}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors=3;
    check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    const auto start=heap->GetCPUDescriptorHandleForHeapStart(); const auto stride=device->GetDescriptorHandleIncrementSize(hd.Type);
    const DXGI_FORMAT formats[]={DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R32_FLOAT};
    ID3D12Resource *resources[3]={}; D3D12_HEAP_PROPERTIES memory={}; memory.Type=D3D12_HEAP_TYPE_DEFAULT;
    for (unsigned i=0;i<3;++i) {
        D3D12_RESOURCE_DESC desc={}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width=1920; desc.Height=1080;
        desc.DepthOrArraySize=desc.MipLevels=1; desc.Format=formats[i]; desc.SampleDesc.Count=1;
        desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&resources[i])));
        device->CreateRenderTargetView(resources[i],nullptr,{start.ptr+i*stride});
    }
    parameters->Set("DLSSG.Backbuffer",resources[0]); parameters->Set("DLSSG.HUDLess",static_cast<ID3D12Resource *>(nullptr));
    parameters->Set("DLSSG.MVecs",resources[1]); parameters->Set("DLSSG.Depth",resources[2]);
    parameters->Set("DLSSG.MultiFrameIndex",1u);
    ID3D12CommandAllocator *allocator=nullptr; check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ID3D12GraphicsCommandList *list=nullptr; check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&list))); check(list->Close());
    ID3D12Fence *fence=nullptr; check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    ID3D12Resource *sr_output=nullptr;
    NVSDK_NGX_Handle *sr_handle=nullptr;
    NVSDK_NGX_Parameter *sr_parameters=nullptr;
    if (recovery) {
        auto desc=resources[0]->GetDesc(); desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&sr_output)));
        ngx(NVSDK_NGX_D3D12_AllocateParameters(&sr_parameters));
    }
    auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    unsigned evaluated=0, native_recovered=0, late_fg_evals=0, healthy_fg_evals=0, manual_fg_evals=0;
    unsigned scaled_begin=0, fg_scaled_begin=0, transition_begin=0, prewarm_begin=0, retired_begin=0, manual_fg_scaled=0; unsigned long long bypass_begin=0;
    bool counters_initialized=false, initial_scale_applied=initial_scale==0;
    unsigned mfg_callbacks[5]={};
    for (unsigned frame=0;frame<400;++frame) {
        MSG msg; while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        check(allocator->Reset()); check(list->Reset(allocator,nullptr));
        for (unsigned i=0;i<3;++i) {
            D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource=resources[i];
            b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; b.Transition.StateBefore=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; b.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
            list->ResourceBarrier(1,&b);
            const float colors[3][4]={{.5f,.4f,.3f,1.f},{0,0,0,0},{1,1,1,1}};
            list->ClearRenderTargetView({start.ptr+i*stride},colors[i],0,nullptr);
            b.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET; b.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; list->ResourceBarrier(1,&b);
        }
        auto module=GetModuleHandleW(L"renodx-dlss5-super-anus.addon64");
        if (module) {
            if (!initial_scale_applied) {
                reinterpret_cast<void (*)(int)>(address(module,"NR_TEST_SCALE_RVA"))(initial_scale);
                initial_scale_applied=true;
            }
            auto *success=reinterpret_cast<std::atomic_uint *>(address(module,"NR_FINAL_SUCCESS_RVA"));
            auto *scaled=reinterpret_cast<std::atomic_uint *>(address(module,"NR_FINAL_SCALED_RVA"));
            auto *fg_scaled=reinterpret_cast<std::atomic_uint *>(address(module,"NR_FINAL_FG_SCALED_RVA"));
            auto *fg_bypass=reinterpret_cast<std::atomic_ullong *>(address(module,"NR_FINAL_FG_BYPASS_RVA"));
            auto *transition_native=reinterpret_cast<std::atomic_uint *>(address(module,"NR_FINAL_TRANSITION_NATIVE_RVA"));
            if (!counters_initialized) {
                scaled_begin=scaled->load(); fg_scaled_begin=fg_scaled->load();
                transition_begin=transition_native->load(); bypass_begin=fg_bypass->load(); counters_initialized=true;
                prewarm_begin=reinterpret_cast<std::atomic_uint *>(address(module,"NR_FINAL_PREWARM_RVA"))->load();
                retired_begin=reinterpret_cast<std::atomic_uint *>(address(module,"NR_FINAL_RETIRED_RVA"))->load();
            }
            const auto before=success->load();
            using Callback=std::uint64_t (*)(void *,std::uint64_t,void *);
            auto callback=reinterpret_cast<Callback>(address(module,"NR_FG_CALLBACK_RVA"));
            if (recovery) {
                // Test-owned process ONLY: simulate Auto's stale FG-active flag.
                // Production preset transactions still control passes and NR off.
                auto *base=reinterpret_cast<unsigned char *>(module);
                const bool manual_fg=frame>=290 && frame<310;
                const bool manual_sr=frame>=310 && frame<320;
                base[0x27100C]=(manual_fg || manual_sr)?0:1;
                *reinterpret_cast<float *>(base+0x270FB0)=manual_fg?3.f:manual_sr?2.f:1.f;
                base[0x27100E]=(frame>=200 && frame<220)?0:1;
                if (*reinterpret_cast<int *>(base+0x2677F8)!=0) base[0x27100F]=manual_sr?2:3;
                if (!sr_handle) {
                    NVSDK_NGX_DLSS_Create_Params create={};
                    create.Feature.InWidth=create.Feature.InTargetWidth=1920;
                    create.Feature.InHeight=create.Feature.InTargetHeight=1080;
                    create.Feature.InPerfQualityValue=NVSDK_NGX_PerfQuality_Value_DLAA;
                    create.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
                    ngx(NGX_D3D12_CREATE_DLSS_EXT(list,1,1,&sr_handle,sr_parameters,&create));
                }
                NVSDK_NGX_D3D12_DLSS_Eval_Params eval={};
                eval.Feature.pInColor=resources[0]; eval.Feature.pInOutput=sr_output;
                eval.pInDepth=resources[2]; eval.pInMotionVectors=resources[1];
                eval.InRenderSubrectDimensions={1920,1080}; eval.InReset=frame==0;
                ngx(NGX_D3D12_EVALUATE_DLSS_EXT(list,sr_handle,sr_parameters,&eval));
                const auto native_delta=success->load()-before;
                native_recovered+=native_delta;
                if (frame<40 && native_delta!=0) { puts("FAIL: native NR duplicated healthy FG ownership"); return 10; }
                if (frame>=180) {
                    const auto enabled=*reinterpret_cast<int *>(base+0x2677F8)!=0;
                    const auto expected=enabled && !manual_fg ? *reinterpret_cast<unsigned *>(base+0x266FA4) : 0;
                    if (native_delta!=expected) { printf("FAIL: frame %u native passes %u expected %u\n",frame,native_delta,expected); return 11; }
                }
                // Healthy initial producer, then missing FG work, then late
                // return. Also exercise explicit manual FrameGen and Upscaled.
                if (frame<40 || frame>=180) {
                    const auto after_native=success->load();
                    const auto before_fg_scaled=fg_scaled->load();
                    callback(list,0x12345678,parameters);
                    const auto extra=success->load()-after_native;
                    if (frame<40) healthy_fg_evals+=extra;
                    else if (manual_fg) {
                        manual_fg_evals+=extra;
                        manual_fg_scaled+=fg_scaled->load()-before_fg_scaled;
                    }
                    else late_fg_evals+=extra;
                }
            } else {
                const unsigned generated=mfg_cadence ? (frame<134?2u:(frame<267?3u:4u)) : 1u;
                for (unsigned index=1;index<=generated;++index) {
                    parameters->Set("DLSSG.MultiFrameIndex",index);
                    const auto result=callback(list,0x12345678,parameters);
                    if ((result&255)!=1) return 7;
                    ++mfg_callbacks[index];
                }
            }
            evaluated+=success->load()-before;
            if (frame%30==0) printf("frame=%u NR-evaluations=%u\n",frame,evaluated);
        }
        check(list->Close()); ID3D12CommandList *submit[]={list}; queue->ExecuteCommandLists(1,submit);
        check(swapchain->Present(0,0)); check(queue->Signal(fence,frame+1)); check(fence->SetEventOnCompletion(frame+1,event));
        if (WaitForSingleObject(event,10000)!=WAIT_OBJECT_0) return 8;
        NativeCaptureTestTrigger(frame+1);
        if (scale_churn && (frame+1)%45==0) {
            const unsigned scales[]={50,75,99,100,101,125,150,75};
            const auto index=(frame+1)/45-1;
            if (module && index<8) {
                reinterpret_cast<void (*)(int)>(address(module,"NR_TEST_SCALE_RVA"))(scales[index]);
                printf("TEST ONLY: applied NR scale %u%% at frame %u\n",scales[index],frame+1);
            }
        }
        Sleep(10);
    }
    printf("FrameGen callback fixture: actual NR evaluations=%u; synthetic FG parameters, no generated frames.\n",evaluated);
    const unsigned scaled_total=counters_initialized ? reinterpret_cast<std::atomic_uint *>(address(GetModuleHandleW(L"renodx-dlss5-super-anus.addon64"),"NR_FINAL_SCALED_RVA"))->load()-scaled_begin : 0;
    const unsigned fg_scaled_total=counters_initialized ? reinterpret_cast<std::atomic_uint *>(address(GetModuleHandleW(L"renodx-dlss5-super-anus.addon64"),"NR_FINAL_FG_SCALED_RVA"))->load()-fg_scaled_begin : 0;
    const unsigned transition_total=counters_initialized ? reinterpret_cast<std::atomic_uint *>(address(GetModuleHandleW(L"renodx-dlss5-super-anus.addon64"),"NR_FINAL_TRANSITION_NATIVE_RVA"))->load()-transition_begin : 0;
    const unsigned long long bypass_total=counters_initialized ? reinterpret_cast<std::atomic_ullong *>(address(GetModuleHandleW(L"renodx-dlss5-super-anus.addon64"),"NR_FINAL_FG_BYPASS_RVA"))->load()-bypass_begin : 0;
    const unsigned prewarm_total=counters_initialized ? reinterpret_cast<std::atomic_uint *>(address(GetModuleHandleW(L"renodx-dlss5-super-anus.addon64"),"NR_FINAL_PREWARM_RVA"))->load()-prewarm_begin : 0;
    const unsigned retired_total=counters_initialized ? reinterpret_cast<std::atomic_uint *>(address(GetModuleHandleW(L"renodx-dlss5-super-anus.addon64"),"NR_FINAL_RETIRED_RVA"))->load()-retired_begin : 0;
    printf("Scale routing: scaled=%u FrameGen-scaled=%u transition-native=%u transparent-native=%llu.\n",scaled_total,fg_scaled_total,transition_total,bypass_total);
    printf("Resource churn: prewarmed=%u retired=%u.\n",prewarm_total,retired_total);
    if (mfg_cadence) printf("MFG cadence callbacks: index1=%u index2=%u index3=%u index4=%u.\n",
        mfg_callbacks[1],mfg_callbacks[2],mfg_callbacks[3],mfg_callbacks[4]);
    if (recovery) printf("Auto recovery: native NR=%u healthy FG NR=%u late FG NR=%u explicit manual FG NR=%u scaled=%u\n",native_recovered,healthy_fg_evals,late_fg_evals,manual_fg_evals,manual_fg_scaled);
    if (sr_handle) ngx(NVSDK_NGX_D3D12_ReleaseFeature(sr_handle));
    if (sr_parameters) ngx(NVSDK_NGX_D3D12_DestroyParameters(sr_parameters));
    ngx(NVSDK_NGX_D3D12_DestroyParameters(parameters)); ngx(NVSDK_NGX_D3D12_Shutdown1(device));
    CloseHandle(event); fence->Release(); list->Release(); allocator->Release();
    for (auto *resource:resources) resource->Release();
    if (sr_output) sr_output->Release();
    heap->Release(); swapchain->Release(); queue->Release(); device->Release(); factory->Release(); DestroyWindow(window);
    const bool scale_ok=!scale_churn || (scaled_total>0 && fg_scaled_total>0 && transition_total>0);
    const bool native_transparent_ok=recovery || scale_churn || bypass_total>=370;
    const bool scaled_activity_ok=!scale_churn || evaluated>0;
    const bool manual_route_ok=!recovery || (forced_context ? manual_fg_scaled>0 :
        (scale_churn ? manual_fg_scaled>0 : (manual_fg_scaled==0 && bypass_total>0)));
    const bool recovery_ok=!recovery || (native_recovered>=370 && healthy_fg_evals>0 && late_fg_evals==0 && manual_route_ok);
    const bool cadence_ok=!mfg_cadence || (mfg_callbacks[1]&&mfg_callbacks[2]&&mfg_callbacks[3]&&mfg_callbacks[4]);
    return native_transparent_ok && scaled_activity_ok && scale_ok && recovery_ok && cadence_ok ? 0 : 9;
}
