// Executes the production DXIL shader on real D3D12; no NGX/model required.
#define NOMINMAX
#include <windows.h>
#undef near
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <bit>
#include "../src/neural_resample_shader.hpp"
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr) { if (FAILED(hr)) { printf("HRESULT %08lx\n",hr); ExitProcess(2); } }
struct Pixel { float r,g,b,a; };
struct Texture { ComPtr<ID3D12Resource> resource; unsigned w,h; };
struct GPU {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT stride; UINT64 serial=0;
    GPU(bool warp) {
        ComPtr<IDXGIFactory4> factory; check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> adapter;
        if (warp) check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        D3D12_COMMAND_QUEUE_DESC q={}; check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
        check(list->Close()); check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
        check(device->CreateRootSignature(0,g_neural_resample_shader,g_neural_resample_shader_size,IID_PPV_ARGS(&root)));
        D3D12_COMPUTE_PIPELINE_STATE_DESC p={}; p.pRootSignature=root.Get(); p.CS={g_neural_resample_shader,g_neural_resample_shader_size};
        check(device->CreateComputePipelineState(&p,IID_PPV_ARGS(&pipeline)));
        D3D12_DESCRIPTOR_HEAP_DESC hd={}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=4; hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap))); stride=device->GetDescriptorHandleIncrementSize(hd.Type);
    }
    void begin() { check(allocator->Reset()); check(list->Reset(allocator.Get(),nullptr)); }
    void finish() {
        check(list->Close()); ID3D12CommandList *commands[]={list.Get()}; queue->ExecuteCommandLists(1,commands);
        check(queue->Signal(fence.Get(),++serial)); HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); assert(event);
        check(fence->SetEventOnCompletion(serial,event)); assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0); CloseHandle(event);
    }
    void barrier(ID3D12Resource *r,D3D12_RESOURCE_STATES old,D3D12_RESOURCE_STATES next) {
        D3D12_RESOURCE_BARRIER b={}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,old,next}; list->ResourceBarrier(1,&b);
    }
    ComPtr<ID3D12Resource> buffer(UINT64 size,D3D12_HEAP_TYPE type) {
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=type;
        D3D12_RESOURCE_DESC d={}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=size; d.Height=1; d.DepthOrArraySize=1; d.MipLevels=1; d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> r; check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r))); return r;
    }
    Texture texture(unsigned w,unsigned h) {
        D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d={}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=w; d.Height=h; d.DepthOrArraySize=1; d.MipLevels=1; d.SampleDesc.Count=1; d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Texture t{{},w,h}; check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&t.resource))); return t;
    }
    void upload(Texture &t,const std::vector<Pixel> &pixels) {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp; UINT64 bytes; auto d=t.resource->GetDesc(); device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
        auto b=buffer(bytes,D3D12_HEAP_TYPE_UPLOAD); void *data; check(b->Map(0,nullptr,&data));
        for(unsigned y=0;y<t.h;++y) memcpy(static_cast<char*>(data)+y*fp.Footprint.RowPitch,pixels.data()+y*t.w,t.w*sizeof(Pixel)); b->Unmap(0,nullptr);
        begin(); barrier(t.resource.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION src={}; src.pResource=b.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint=fp;
        D3D12_TEXTURE_COPY_LOCATION dst={}; dst.pResource=t.resource.Get(); list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        barrier(t.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); finish();
    }
    std::vector<Pixel> read(Texture &t) {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp; UINT64 bytes; auto d=t.resource->GetDesc(); device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);
        auto b=buffer(bytes,D3D12_HEAP_TYPE_READBACK); begin(); barrier(t.resource.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src={}; src.pResource=t.resource.Get();
        D3D12_TEXTURE_COPY_LOCATION dst={}; dst.pResource=b.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint=fp;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr); barrier(t.resource.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); finish();
        void *data; check(b->Map(0,nullptr,&data)); std::vector<Pixel> pixels(t.w*t.h);
        for(unsigned y=0;y<t.h;++y) memcpy(pixels.data()+y*t.w,static_cast<char*>(data)+y*fp.Footprint.RowPitch,t.w*sizeof(Pixel)); b->Unmap(0,nullptr); return pixels;
    }
    void dispatch(Texture &source,Texture &dest,Texture &input,Texture &native,unsigned mode,unsigned sx,unsigned sy,unsigned sw,unsigned sh,unsigned dw,unsigned dh,float sharp=0,float transfer=1,float color=1,unsigned dx=0,unsigned dy=0) {
        Texture *textures[]={&source,&dest,&input,&native}; auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
        for(unsigned i=0;i<4;++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE h{cpu.ptr+i*stride};
            if(i==1) { D3D12_UNORDERED_ACCESS_VIEW_DESC v={}; v.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D; device->CreateUnorderedAccessView(textures[i]->resource.Get(),nullptr,&v,h); }
            else { D3D12_SHADER_RESOURCE_VIEW_DESC v={}; v.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D; v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; v.Texture2D.MipLevels=1; device->CreateShaderResourceView(textures[i]->resource.Get(),&v,h); }
        }
        begin(); barrier(dest.resource.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12DescriptorHeap *heaps[]={heap.Get()}; list->SetDescriptorHeaps(1,heaps); list->SetPipelineState(pipeline.Get()); list->SetComputeRootSignature(root.Get());
        auto gpu=heap->GetGPUDescriptorHandleForHeapStart(); unsigned roots[]={0,1,3,4};
        for(unsigned i=0;i<4;++i) list->SetComputeRootDescriptorTable(roots[i],{gpu.ptr+i*stride});
        unsigned constants[]={sx,sy,sw,sh,dw,dh,mode,std::bit_cast<unsigned>(sharp),dx,dy,std::bit_cast<unsigned>(transfer),std::bit_cast<unsigned>(color)};
        list->SetComputeRoot32BitConstants(2,12,constants,0); list->Dispatch((dw+7)/8,(dh+7)/8,1);
        barrier(dest.resource.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); finish();
    }
};
static bool near(float a,float b) { return std::abs(a-b)<0.00003f; }
int main(int argc,char **) {
    GPU gpu(argc>1); unsigned cases=0;
    for(float brightness : {0.5f,8.f}) {
        constexpr unsigned w=64,h=48;
        auto source=gpu.texture(w+2,h+2), native=gpu.texture(w,h), destination=gpu.texture(w+6,h+4);
        std::vector<Pixel> pixels((w+2)*(h+2));
        for(unsigned y=0;y<h+2;++y) for(unsigned x=0;x<w+2;++x) pixels[y*(w+2)+x]={((x+y)%2?.8f:.2f)*brightness,(.3f+x*.001f)*brightness,-.01f*brightness,.37f};
        gpu.upload(source,pixels); gpu.dispatch(source,native,source,source,2,1,1,w,h,w,h);
        const auto anchor=gpu.read(native);
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) assert(memcmp(&anchor[y*w+x],&pixels[(y+1)*(w+2)+x+1],sizeof(Pixel))==0);
        for(unsigned scale : {25u,33u,50u,75u,99u}) {
            unsigned sw=std::max(2u,((w*scale+50)/100)&~1u),sh=std::max(2u,((h*scale+50)/100)&~1u);
            auto input=gpu.texture(sw,sh),output=gpu.texture(sw,sh);
            gpu.dispatch(native,input,native,native,0,0,0,w,h,sw,sh);
            const auto low=gpu.read(input);
            for(unsigned y=0;y<sh;++y) for(unsigned x=0;x<sw;++x) {
                double sum=0; double x0=double(x)*w/sw,x1=double(x+1)*w/sw,y0=double(y)*h/sh,y1=double(y+1)*h/sh;
                for(int j=int(floor(y0));j<int(ceil(y1));++j) for(int i=int(floor(x0));i<int(ceil(x1));++i)
                    sum+=anchor[j*w+i].r*(std::min(x1,double(i+1))-std::max(x0,double(i)))*(std::min(y1,double(j+1))-std::max(y0,double(j)));
                assert(near(low[y*sw+x].r,float(sum/((x1-x0)*(y1-y0)))));
            }
            for(unsigned mode : {1u,4u}) for(float transfer : {0.f,1.f,2.f}) for(float color : {0.f,1.f}) for(float delta : {0.f,.02f}) {
                auto neural=low; for(auto &p:neural) { p.r+=delta; p.g+=delta; p.b+=delta; }
                gpu.upload(output,neural);
                std::vector<Pixel> sentinel(destination.w*destination.h,{-9,-9,-9,-9}); gpu.upload(destination,sentinel);
                gpu.dispatch(output,destination,input,native,mode,0,0,sw,sh,w,h,0,transfer,color,3,2);
                const auto result=gpu.read(destination);
                for(unsigned y=0;y<destination.h;++y) for(unsigned x=0;x<destination.w;++x) {
                    const auto &p=result[y*destination.w+x];
                    if(x<3||x>=w+3||y<2||y>=h+2) { assert(p.r==-9&&p.a==-9); continue; }
                    const auto &base=anchor[(y-2)*w+x-3]; assert(p.a==base.a); assert(std::isfinite(p.r));
                    if(mode==1 || transfer==0) { assert(near(p.r,base.r+delta*transfer)); assert(near(p.g,base.g+delta*transfer)); assert(near(p.b,base.b+delta*transfer)); }
                }
                ++cases;
            }
            gpu.dispatch(output,destination,input,native,1,0,0,sw,sh,w,h,1,1,1,3,2);
            const auto sharp=gpu.read(destination); assert(!near(sharp[2*destination.w+3].r,anchor[0].r+.02f));
            // A colored edit must respond to ColorStrength, not just grayscale deltas.
            auto tinted=low; for(auto &p:tinted) { p.r+=.04f; p.g-=.01f; p.b+=.02f; }
            gpu.upload(output,tinted);
            for(float color : {0.f,.5f,1.f}) {
                gpu.dispatch(output,destination,input,native,1,0,0,sw,sh,w,h,0,1,color,3,2);
                const auto result=gpu.read(destination);
                const float deltaY=.04f*.2126f-.01f*.7152f+.02f*.0722f;
                for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
                    const auto &p=result[(y+2)*destination.w+x+3]; const auto &base=anchor[y*w+x];
                    assert(near(p.r,base.r+deltaY+(.04f-deltaY)*color));
                    assert(near(p.g,base.g+deltaY+(-.01f-deltaY)*color));
                    assert(near(p.b,base.b+deltaY+(.02f-deltaY)*color));
                }
                ++cases;
            }
        }
    }
    printf("PASS: %u GPU resolves; 25/33/50/75/99%% area reference, native-detail identity, transfer/color, SDR/HDR signed values, alpha, subrect sentinels, sharpening. Production DXIL; no NR model.\n",cases);
}
