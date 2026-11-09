#pragma once
#include "dbce_triple_compositor_d3d12.hpp"
#include <dxgi1_4.h>
// Present-path warp of a D3D12 swap chain's back buffer onto curved panels (docs/curved-presenter-contract.md): the game
// (or its renderer mod) has drawn each panel's chord-frustum view into its third of the back buffer; this step copies
// the back buffer into one of a ring of source textures, records the compositor warp back into the back buffer and
// executes it on the game's present queue. Call present() from the shared F6 host's before-panel callback
// (dbce-wheel-mod-toolkit SetBeforePanelD3D12), so the panel stays on top. Radius 0 (constants.misc[0]) draws nothing:
// the flat output is the game's own, byte for byte. No back-buffer reference outlives the call, so ResizeBuffers
// needs no notice; the sources follow the buffer's size and format. Link d3d12.lib, dxgi.lib, d3dcompiler.lib.
namespace dbce::triple_screen::compositor {
class D3D12PresentWarp {
public:
    static constexpr UINT Ring = 3;   // sources and constant slots in flight
    // True when the warp was recorded and executed this frame. force_flat: draw the radius-0 identity anyway (tests).
    bool present(IDXGISwapChain* chain, ID3D12CommandQueue* queue, const Constants& constants, bool force_flat = false) {
        if (!chain || !queue || !(constants.misc[0] > 0 || force_flat)) return false;
        IDXGISwapChain3* sc3 = nullptr;
        if (FAILED(chain->QueryInterface(IID_PPV_ARGS(&sc3))) || !sc3) return false;
        ID3D12Device* dev = nullptr;
        if (FAILED(sc3->GetDevice(IID_PPV_ARGS(&dev))) || !dev) { sc3->Release(); return false; }
        if (dev != device_) { release(); device_ = dev; device_->AddRef(); }
        dev->Release();
        ID3D12Resource* back = nullptr;
        const UINT index = sc3->GetCurrentBackBufferIndex();
        sc3->Release();
        if (FAILED(chain->GetBuffer(index, IID_PPV_ARGS(&back))) || !back) return false;
        const D3D12_RESOURCE_DESC bd = back->GetDesc();
        bool ok = bd.Width > 0 && bd.Height > 0 && bd.SampleDesc.Count == 1;
        if (ok && (bd.Width != width_ || bd.Height != height_ || bd.Format != format_)) ok = prepare(bd);
        if (!ok) { back->Release(); return false; }
        const UINT slot = frame_ % Ring;
        if (fence_->GetCompletedValue() < fenceValue_[slot] && SUCCEEDED(fence_->SetEventOnCompletion(fenceValue_[slot], event_)))
            WaitForSingleObject(event_, 1000);
        alloc_[slot]->Reset();
        list_->Reset(alloc_[slot], nullptr);
        D3D12_RESOURCE_BARRIER b[2] = {transition(back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE)};
        list_->ResourceBarrier(1, b);
        list_->CopyResource(source_[slot], back);
        b[0] = transition(back, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        b[1] = transition(source_[slot], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list_->ResourceBarrier(2, b);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        device_->CreateRenderTargetView(back, nullptr, rtv);
        compositor_.record(list_, slot, rtv, (UINT)bd.Width, bd.Height, constants, slot);
        b[0] = transition(back, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        b[1] = transition(source_[slot], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        list_->ResourceBarrier(2, b);
        list_->Close();
        ID3D12CommandList* lists[1] = {list_};
        queue->ExecuteCommandLists(1, lists);
        fenceValue_[slot] = ++fenceNext_;
        queue->Signal(fence_, fenceValue_[slot]);
        back->Release();
        ++frame_;
        return true;
    }
    // Waits for the GPU (the last queue signalled) and drops every device object.
    void release() {
        if (fence_ && event_ && fence_->GetCompletedValue() < fenceNext_ && SUCCEEDED(fence_->SetEventOnCompletion(fenceNext_, event_)))
            WaitForSingleObject(event_, 1000);
        compositor_.release();
        for (UINT i = 0; i < Ring; ++i) { drop(source_[i]); drop(alloc_[i]); fenceValue_[i] = 0; }
        drop(list_); drop(rtvHeap_); drop(fence_); drop(device_);
        if (event_) { CloseHandle(event_); event_ = nullptr; }
        width_ = height_ = 0; format_ = DXGI_FORMAT_UNKNOWN; frame_ = 0; fenceNext_ = 0;
    }
    ~D3D12PresentWarp() { release(); }
private:
    template <class T> static void drop(T*& p) { if (p) { p->Release(); p = nullptr; } }
    static D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
        return b;
    }
    bool prepare(const D3D12_RESOURCE_DESC& bd) {   // (re)build everything for this buffer size and format
        ID3D12Device* dev = device_; dev->AddRef();
        release(); device_ = dev;
        if (FAILED(compositor_.create(device_, bd.Format, Ring))) return false;
        D3D12_HEAP_PROPERTIES def{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC sd{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, bd.Width, bd.Height, 1, 1, D3D12Compositor::typed(bd.Format), {1, 0},
                               D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_NONE};
        for (UINT i = 0; i < Ring; ++i) {
            if (FAILED(device_->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &sd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&source_[i])))) return false;
            if (!compositor_.set_source(i, source_[i])) return false;
            if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc_[i])))) return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC rh{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        if (FAILED(device_->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&rtvHeap_)))) return false;
        if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_[0], nullptr, IID_PPV_ARGS(&list_)))) return false;
        list_->Close();
        if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) return false;
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event_) return false;
        width_ = bd.Width; height_ = bd.Height; format_ = bd.Format;
        return true;
    }
    D3D12Compositor compositor_;
    ID3D12Device* device_ = nullptr; ID3D12GraphicsCommandList* list_ = nullptr; ID3D12DescriptorHeap* rtvHeap_ = nullptr;
    ID3D12Fence* fence_ = nullptr; HANDLE event_ = nullptr;
    ID3D12Resource* source_[Ring]{}; ID3D12CommandAllocator* alloc_[Ring]{}; UINT64 fenceValue_[Ring]{};
    UINT64 width_ = 0, fenceNext_ = 0; UINT height_ = 0, frame_ = 0; DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
};
} // namespace dbce::triple_screen::compositor
