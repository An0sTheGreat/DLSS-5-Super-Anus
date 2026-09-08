#pragma once
#include <Windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdint>
#include "../dx11_depth_convert_shader.hpp"

namespace nr::backends::dx11
{
// Internal transfer layer, not an NGX hook or motion estimator. The caller must
// supply a same-adapter DX12 device (keep ReShade's proxy), a private direct
// queue, and an immediate DX11 context. No runtime DLL is imported by this file.
enum Texture : unsigned { color, depth, motion, output, exposure, texture_count };
struct Frame { ID3D11Texture2D *textures[texture_count] = {}; };
struct Evaluation
{
    ID3D12GraphicsCommandList *commands;
    ID3D12Resource *textures[texture_count];
};
// Record only; do not submit/reset/close commands. Restore all shared textures
// to COMMON. Return false to discard the recording and preserve native output.
using Record = bool (*)(void *, const Evaluation &);
enum class Result { submitted, busy, invalid_input, unsupported, evaluator_failed, device_failure };

template<class T> inline void release(T *&p) { if (p) { p->Release(); p = nullptr; } }
inline bool same_object(IUnknown *a, IUnknown *b)
{
    IUnknown *ia = nullptr, *ib = nullptr;
    if (a) a->QueryInterface(IID_PPV_ARGS(&ia));
    if (b) b->QueryInterface(IID_PPV_ARGS(&ib));
    const bool equal = ia && ib && ia == ib;
    release(ia); release(ib);
    return equal;
}

class Transport
{
public:
    static constexpr unsigned slot_count = 3;
    static constexpr UINT64 default_budget = 256ull * 1024 * 1024;
    Transport() = default;
    Transport(const Transport &) = delete;
    Transport &operator=(const Transport &) = delete;
    // Explicit try_close() is required. An owner must NOT destroy this object
    // while that returns false. Never release in-flight allocations on timeout.

    HRESULT initialize(ID3D11DeviceContext *context, ID3D12Device *device,
                       ID3D12CommandQueue *queue, UINT64 budget = default_budget)
    {
        if (context_ || !context || !device || !queue || !budget ||
            context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE ||
            queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return E_INVALIDARG;
        ID3D12Device *queue_device = nullptr;
        queue->GetDevice(IID_PPV_ARGS(&queue_device));
        const bool matching_queue = same_object(device, queue_device);
        release(queue_device);
        if (!matching_queue) return E_INVALIDARG;
        ID3D11Device *base11 = nullptr;
        context->GetDevice(&base11);
        IDXGIDevice *dxgi = nullptr;
        IDXGIAdapter *adapter = nullptr;
        DXGI_ADAPTER_DESC desc = {};
        HRESULT hr = base11->QueryInterface(IID_PPV_ARGS(&dxgi));
        if (SUCCEEDED(hr)) hr = dxgi->GetAdapter(&adapter);
        if (SUCCEEDED(hr)) hr = adapter->GetDesc(&desc);
        release(adapter); release(dxgi);
        const auto luid = device->GetAdapterLuid();
        if (SUCCEEDED(hr) && (luid.LowPart != desc.AdapterLuid.LowPart ||
            luid.HighPart != desc.AdapterLuid.HighPart)) hr = E_INVALIDARG;
        if (SUCCEEDED(hr)) hr = base11->QueryInterface(IID_PPV_ARGS(&device11_));
        release(base11);
        if (SUCCEEDED(hr)) hr = context->QueryInterface(IID_PPV_ARGS(&context_));
        if (SUCCEEDED(hr)) hr = context->QueryInterface(IID_PPV_ARGS(&multithread_));
        if (SUCCEEDED(hr))
        {
            device_ = device; device_->AddRef();
            queue_ = queue; queue_->AddRef();
            budget_ = budget;
            // Separate timelines: no queue can signal past unfinished work on
            // another queue. Shared handles are closed immediately after import.
            for (unsigned i = 0; i < 3 && SUCCEEDED(hr); ++i)
            {
                HANDLE handle = nullptr;
                hr = device_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12_[i]));
                if (SUCCEEDED(hr)) hr = device_->CreateSharedHandle(fence12_[i], nullptr,
                    GENERIC_ALL, nullptr, &handle);
                if (SUCCEEDED(hr)) hr = device11_->OpenSharedFence(handle, IID_PPV_ARGS(&fence11_[i]));
                if (handle) CloseHandle(handle);
            }
        }
        if (FAILED(hr)) close_idle();
        return hr;
    }

    Result submit(const Frame &frame, Record record, void *user)
    {
        if (!TryAcquireSRWLockExclusive(&lock_)) return Result::busy;
        const Result result = submit_locked(frame, record, user);
        ReleaseSRWLockExclusive(&lock_);
        return result;
    }

    bool try_close()
    {
        if (!TryAcquireSRWLockExclusive(&lock_)) return false;
        bool idle = !poisoned_;
        for (const auto &slot : slots_) idle = idle && retired(slot);
        if (idle) close_idle();
        ReleaseSRWLockExclusive(&lock_);
        return idle;
    }
    bool idle()
    {
        if (!TryAcquireSRWLockExclusive(&lock_)) return false;
        bool result = !poisoned_;
        for (const auto &slot : slots_) result = result && retired(slot);
        ReleaseSRWLockExclusive(&lock_);
        return result;
    }
    UINT64 allocated_bytes() const { return allocated_; } // owner-thread diagnostics
    HRESULT last_error() const { return last_error_; }
    const char *error_stage() const { return error_stage_; }

private:
    struct Slot
    {
        D3D11_TEXTURE2D_DESC descriptions[texture_count] = {};
        ID3D12Resource *resources12[texture_count] = {};
        ID3D11Texture2D *resources11[texture_count] = {};
        ID3D12CommandAllocator *allocator = nullptr;
        ID3D12GraphicsCommandList *commands = nullptr;
        ID3D11ShaderResourceView *depth_view = nullptr;
        ID3D11Texture2D *depth_source = nullptr; // borrowed from depth_view
        ID3D11UnorderedAccessView *depth_target = nullptr;
        UINT64 bytes = 0, completion = 0;
    };
    // indices: DX11 inputs ready, DX12 evaluation done, DX11 copy-back retired
    SRWLOCK lock_ = SRWLOCK_INIT;
    ID3D11Device5 *device11_ = nullptr;
    ID3D11DeviceContext4 *context_ = nullptr;
    ID3D11Multithread *multithread_ = nullptr;
    ID3D12Device *device_ = nullptr;
    ID3D12CommandQueue *queue_ = nullptr;
    ID3D12Fence *fence12_[3] = {};
    ID3D11Fence *fence11_[3] = {};
    Slot slots_[slot_count] = {};
    UINT64 next_ = 0, allocated_ = 0, budget_ = 0;
    bool poisoned_ = false;
    HRESULT last_error_ = S_OK;
    const char *error_stage_ = "none";
    ID3D11ComputeShader *depth_shader_ = nullptr;
    ID3DDeviceContextState *depth_state_ = nullptr;

    bool retired(const Slot &slot) const
    {
        if (!slot.completion) return true;
        for (auto *fence : fence12_)
        {
            const auto completed = fence->GetCompletedValue();
            if (completed == UINT64_MAX || completed < slot.completion) return false;
        }
        return true;
    }
    void clear(Slot &slot)
    {
        release(slot.depth_view); release(slot.depth_target);
        release(slot.commands); release(slot.allocator);
        for (unsigned i = 0; i < texture_count; ++i)
        { release(slot.resources11[i]); release(slot.resources12[i]); }
        allocated_ -= slot.bytes;
        slot = {};
    }
    void close_idle()
    {
        for (auto &slot : slots_) clear(slot);
        release(depth_state_); release(depth_shader_);
        for (unsigned i = 0; i < 3; ++i) { release(fence11_[i]); release(fence12_[i]); }
        release(queue_); release(device_); release(multithread_); release(context_); release(device11_);
        next_ = 0; budget_ = 0;
    }
    static bool description_matches(const D3D11_TEXTURE2D_DESC &a, const D3D11_TEXTURE2D_DESC &b)
    { return a.Width == b.Width && a.Height == b.Height && a.Format == b.Format; }
    static D3D12_RESOURCE_DESC shared_description(const D3D11_TEXTURE2D_DESC &d, unsigned index)
    {
        D3D12_RESOURCE_DESC r = {};
        r.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        r.Width = d.Width; r.Height = d.Height; r.DepthOrArraySize = 1;
        r.MipLevels = 1; r.Format = d.Format; r.SampleDesc.Count = 1;
        r.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        if (index == output || (index == depth && d.Format == DXGI_FORMAT_R32_FLOAT))
            r.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return r;
    }
    HRESULT build(Slot &slot, const D3D11_TEXTURE2D_DESC *descs)
    {
        UINT64 bytes = 0;
        for (unsigned i = 0; i < texture_count; ++i)
        {
            if (!descs[i].Width) continue;
            const auto rd = shared_description(descs[i], i);
            const auto allocation = device_->GetResourceAllocationInfo(0, 1, &rd);
            if (allocation.SizeInBytes == UINT64_MAX || allocation.SizeInBytes > budget_ - bytes)
                return E_OUTOFMEMORY;
            bytes += allocation.SizeInBytes;
        }
        if (bytes > budget_ - (allocated_ - slot.bytes)) return E_OUTOFMEMORY;
        clear(slot); // only called for a fully retired slot
        UINT64 actual_bytes = 0;
        HRESULT hr = S_OK;
        for (unsigned i = 0; i < texture_count && SUCCEEDED(hr); ++i)
        {
            slot.descriptions[i] = descs[i];
            if (!descs[i].Width) continue;
            // Allocate on the source API. On the tested NVIDIA driver the
            // opposite direction fails OpenSharedResource1 with E_INVALIDARG.
            auto td = descs[i];
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (i == output || (i == depth && td.Format == DXGI_FORMAT_R32_FLOAT))
                td.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
            td.CPUAccessFlags = 0;
            td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
            error_stage_ = "CreateTexture2D(shared)";
            hr = device11_->CreateTexture2D(&td, nullptr, &slot.resources11[i]);
            IDXGIResource1 *shared = nullptr;
            HANDLE handle = nullptr;
            if (SUCCEEDED(hr)) hr = slot.resources11[i]->QueryInterface(IID_PPV_ARGS(&shared));
            if (SUCCEEDED(hr)) { error_stage_ = "DXGI CreateSharedHandle";
                hr = shared->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                    nullptr, &handle); }
            if (SUCCEEDED(hr)) { error_stage_ = "DX12 OpenSharedHandle";
                hr = device_->OpenSharedHandle(handle, IID_PPV_ARGS(&slot.resources12[i])); }
            if (handle) CloseHandle(handle);
            release(shared);
            if (SUCCEEDED(hr))
            {
                const auto rd = slot.resources12[i]->GetDesc();
                const auto allocation = device_->GetResourceAllocationInfo(0, 1, &rd);
                error_stage_ = "shared allocation budget";
                if (allocation.SizeInBytes == UINT64_MAX || allocation.SizeInBytes > budget_ - allocated_ - actual_bytes)
                    hr = E_OUTOFMEMORY;
                else actual_bytes += allocation.SizeInBytes;
            }
        }
        if (SUCCEEDED(hr)) error_stage_ = "CreateCommandAllocator";
        if (SUCCEEDED(hr)) hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&slot.allocator));
        if (SUCCEEDED(hr)) error_stage_ = "CreateCommandList";
        if (SUCCEEDED(hr)) hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            slot.allocator, nullptr, IID_PPV_ARGS(&slot.commands));
        if (SUCCEEDED(hr)) error_stage_ = "initial command-list Close";
        if (SUCCEEDED(hr)) hr = slot.commands->Close();
        if (FAILED(hr)) { clear(slot); return hr; }
        slot.bytes = actual_bytes; allocated_ += actual_bytes;
        error_stage_ = "none";
        return S_OK;
    }
    HRESULT prepare_packed_depth(Slot &slot, ID3D11Texture2D *source)
    {
        error_stage_ = "packed depth conversion setup";
        HRESULT hr = S_OK;
        if (!depth_shader_)
            hr = device11_->CreateComputeShader(g_dx11_depth_convert_shader,
                sizeof(g_dx11_depth_convert_shader), nullptr, &depth_shader_);
        if (SUCCEEDED(hr) && !depth_state_)
        {
            const auto level = device11_->GetFeatureLevel();
            D3D_FEATURE_LEVEL selected = {};
            hr = device11_->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION,
                __uuidof(ID3D11Device), &selected, &depth_state_);
        }
        if (SUCCEEDED(hr) && !slot.depth_target)
            hr = device11_->CreateUnorderedAccessView(slot.resources11[depth], nullptr, &slot.depth_target);
        if (SUCCEEDED(hr) && slot.depth_source != source)
        {
            release(slot.depth_view); slot.depth_source = nullptr;
            D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
            view.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = 1;
            hr = device11_->CreateShaderResourceView(source, &view, &slot.depth_view);
            if (SUCCEEDED(hr)) slot.depth_source = source;
        }
        if (SUCCEEDED(hr)) error_stage_ = "none";
        return hr;
    }
    void convert_packed_depth(Slot &slot)
    {
        // Preserve ALL application state, including writable DSV/OM UAV bindings
        // that would otherwise null our depth SRV. Work stays on the game queue.
        ID3DDeviceContextState *previous = nullptr;
        context_->SwapDeviceContextState(depth_state_, &previous);
        context_->CSSetShader(depth_shader_, nullptr, 0);
        context_->CSSetShaderResources(0, 1, &slot.depth_view);
        context_->CSSetUnorderedAccessViews(0, 1, &slot.depth_target, nullptr);
        const auto &d = slot.descriptions[depth];
        context_->Dispatch((d.Width + 7) / 8, (d.Height + 7) / 8, 1);
        context_->ClearState(); // private state must not retain application inputs
        context_->SwapDeviceContextState(previous, nullptr);
        release(previous);
    }
    Result submit_locked(const Frame &frame, Record record, void *user)
    {
        if (poisoned_ || !context_) return Result::device_failure;
        if (!record) return Result::invalid_input;
        error_stage_ = "input description unsupported"; last_error_ = E_INVALIDARG;
        bool packed_depth = false;
        D3D11_TEXTURE2D_DESC descs[texture_count] = {};
        for (unsigned i = 0; i < texture_count; ++i)
        {
            auto *texture = frame.textures[i];
            if (!texture) { if (i == exposure) continue; return Result::invalid_input; }
            ID3D11Device *owner = nullptr;
            texture->GetDevice(&owner);
            const bool ours = same_object(owner, device11_);
            release(owner);
            if (!ours) return Result::invalid_input;
            texture->GetDesc(&descs[i]);
            auto &d = descs[i];
            // Subresources and MSAA need explicit conversions; do
            // not silently substitute zeros or reinterpret a depth/stencil plane.
            if (!d.Width || !d.Height || d.MipLevels != 1 || d.ArraySize != 1 ||
                d.SampleDesc.Count != 1 || d.Usage != D3D11_USAGE_DEFAULT) return Result::unsupported;
            // R32 typeless depth and R32_FLOAT are the same DXGI copy family;
            // typed shared storage preserves every depth bit without a shader.
            if (i == depth && d.Format == DXGI_FORMAT_R24G8_TYPELESS &&
                (d.BindFlags & D3D11_BIND_SHADER_RESOURCE))
            { packed_depth = true; d.Format = DXGI_FORMAT_R32_FLOAT; }
            else if (i == depth && d.Format == DXGI_FORMAT_R32_TYPELESS) d.Format = DXGI_FORMAT_R32_FLOAT;
            else if (d.BindFlags & D3D11_BIND_DEPTH_STENCIL) return Result::unsupported;
        }
        Slot *candidate = nullptr;
        bool matching = false;
        for (auto &slot : slots_)
        {
            if (!retired(slot)) continue;
            bool match = slot.commands != nullptr;
            for (unsigned i = 0; i < texture_count; ++i)
                match = match && description_matches(slot.descriptions[i], descs[i]);
            if (match) { candidate = &slot; matching = true; break; }
            if (!candidate) candidate = &slot;
        }
        if (!candidate) return Result::busy;
        auto &slot = *candidate;
        if (!matching)
        {
            last_error_ = build(slot, descs);
            if (FAILED(last_error_)) return Result::unsupported;
        }
        if (packed_depth)
        {
            last_error_ = prepare_packed_depth(slot, frame.textures[depth]);
            if (FAILED(last_error_)) return Result::unsupported;
        }
        error_stage_ = "CommandAllocator Reset";
        HRESULT hr = slot.allocator->Reset();
        if (SUCCEEDED(hr))
        {
            error_stage_ = "command-list Reset";
            hr = slot.commands->Reset(slot.allocator, nullptr);
        }
        if (FAILED(hr))
        {
            last_error_ = hr;
            clear(slot); // retired and not submitted: rebuild this slot next frame
            return Result::device_failure;
        }
        Evaluation evaluation = {slot.commands, {}};
        for (unsigned i = 0; i < texture_count; ++i) evaluation.textures[i] = slot.resources12[i];
        const bool evaluated = record(user, evaluation);
        error_stage_ = "recorded command-list Close";
        hr = slot.commands->Close();
        if (FAILED(hr))
        {
            last_error_ = hr;
            clear(slot); // Close failed before ExecuteCommandLists; no GPU ownership
            return Result::device_failure;
        }
        if (!evaluated)
        {
            last_error_ = S_FALSE; error_stage_ = "consumer rejected recording";
            return Result::evaluator_failed; // nothing submitted, native output untouched
        }
        if (next_ == UINT64_MAX - 1) { poisoned_ = true; return Result::device_failure; }
        const UINT64 serial = ++next_;
        // Caller serializes the game's context at the interception boundary.
        // Honor its existing multithread protection without changing that mode.
        multithread_->Enter();
        if (packed_depth) convert_packed_depth(slot);
        error_stage_ = "DX11 input copy";
        for (unsigned i = 0; i < texture_count; ++i)
            if (frame.textures[i] && i != output && !(packed_depth && i == depth))
                context_->CopyResource(slot.resources11[i], frame.textures[i]);
        slot.completion = serial; // retain even if any subsequent submission fails
        error_stage_ = "DX11 signal input-ready fence";
        hr = context_->Signal(fence11_[0], serial);
        context_->Flush();
        if (SUCCEEDED(hr)) error_stage_ = "DX12 wait input-ready fence";
        if (SUCCEEDED(hr)) hr = queue_->Wait(fence12_[0], serial);
        if (SUCCEEDED(hr))
        {
            ID3D12CommandList *lists[] = {slot.commands};
            queue_->ExecuteCommandLists(1, lists);
            error_stage_ = "DX12 signal evaluation-done fence";
            hr = queue_->Signal(fence12_[1], serial);
        }
        if (SUCCEEDED(hr)) error_stage_ = "DX11 wait evaluation-done fence";
        if (SUCCEEDED(hr)) hr = context_->Wait(fence11_[1], serial);
        if (SUCCEEDED(hr))
        {
            error_stage_ = "DX11 output copy";
            context_->CopyResource(frame.textures[output], slot.resources11[output]);
            error_stage_ = "DX11 signal copy-back fence";
            hr = context_->Signal(fence11_[2], serial);
            context_->Flush();
        }
        multithread_->Leave();
        if (FAILED(hr))
        {
            last_error_ = hr;
            poisoned_ = true; // work may be in flight; retain all ownership
            return Result::device_failure;
        }
        last_error_ = S_OK; error_stage_ = "none";
        return Result::submitted; // queued, NOT proof of completed NR or delivery
    }
};
}
