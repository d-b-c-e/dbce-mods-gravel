#pragma once
#include <d3d12.h>
#include <d3dcompiler.h>
#include <cstring>
#include "dbce_triple_compositor_d3d11.hpp"   // shader_source: one HLSL for both runtimes

// D3D12 runtime for dbce_triple_compositor.hpp. Link d3d12.lib and d3dcompiler.lib (the D3D11 header is included only
// for shader_source; d3d11.lib is not needed). Records into the caller's command list, which owns submission, fences
// and resource states: the source must be in a pixel-shader-resource state and the target in RENDER_TARGET.
// tests/NativeCompositor/compositor12_test.cpp checks it against compositor::sample on WARP.
namespace dbce::triple_screen::compositor {

class D3D12Compositor {
public:
    // target_format: the render-target view format. slots: how many sources and constant sets can be live at once
    // (a constant slot must not be reused while the GPU may still read it, e.g. one per frame in flight).
    HRESULT create(ID3D12Device* device, DXGI_FORMAT target_format, UINT slots = 8) {
        release();
        if (!device || !slots) return E_INVALIDARG;
        device_ = device; device_->AddRef();
        slots_ = slots;
        ID3DBlob *vsBlob = nullptr, *psBlob = nullptr, *sig = nullptr, *errors = nullptr;
        HRESULT hr = D3DCompile(shader_source, sizeof shader_source - 1, "dbce_triple_compositor", nullptr, nullptr, "vs",
                                "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errors);
        if (errors) errors->Release(), errors = nullptr;
        if (SUCCEEDED(hr)) hr = D3DCompile(shader_source, sizeof shader_source - 1, "dbce_triple_compositor", nullptr, nullptr,
                                           "ps", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errors);
        if (errors) errors->Release(), errors = nullptr;
        // Root: b0 as a root CBV, t0 through a one-entry table, s0 static (linear, clamp).
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = {1, &range};
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rs{2, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        if (SUCCEEDED(hr)) hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &errors);
        if (errors) errors->Release();
        if (SUCCEEDED(hr)) hr = device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&root_));
        if (SUCCEEDED(hr)) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
            pd.pRootSignature = root_;
            pd.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
            pd.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
            pd.BlendState.RenderTarget[0].SrcBlend = pd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            pd.BlendState.RenderTarget[0].DestBlend = pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
            pd.BlendState.RenderTarget[0].BlendOp = pd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            pd.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
            pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            pd.SampleMask = UINT_MAX;
            pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            pd.RasterizerState.DepthClipEnable = TRUE;
            pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pd.NumRenderTargets = 1;
            pd.RTVFormats[0] = target_format;
            pd.SampleDesc = {1, 0};
            hr = device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso_));
        }
        if (vsBlob) vsBlob->Release();
        if (psBlob) psBlob->Release();
        if (sig) sig->Release();
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, slots, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        if (SUCCEEDED(hr)) hr = device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap_));
        if (SUCCEEDED(hr)) {   // constants ring in an upload heap, mapped for the compositor's lifetime
            D3D12_HEAP_PROPERTIES up{D3D12_HEAP_TYPE_UPLOAD};
            D3D12_RESOURCE_DESC bd{D3D12_RESOURCE_DIMENSION_BUFFER, 0, (UINT64)kCbStride * slots, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                                   D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
            hr = device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cb_));
        }
        if (SUCCEEDED(hr)) { D3D12_RANGE none{0, 0}; hr = cb_->Map(0, &none, (void**)&mapped_); }
        if (FAILED(hr)) release();
        return hr;
    }

    // Puts a shader-resource view of `texture` (mip 0) in source slot `slot`. Typeless formats get the usual typed view;
    // pass srv_format to choose another.
    bool set_source(UINT slot, ID3D12Resource* texture, DXGI_FORMAT srv_format = DXGI_FORMAT_UNKNOWN) {
        if (!heap_ || !texture || slot >= slots_) return false;
        const D3D12_RESOURCE_DESC d = texture->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.Format = srv_format != DXGI_FORMAT_UNKNOWN ? srv_format : typed(d.Format);
        v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(texture, &v, cpu(slot));
        return true;
    }

    // Records the warp of source `source_slot` into `target` (width x height) with constants slot `cb_slot`. Sets the
    // compositor's own heap, root signature, pipeline, viewport and scissor on the list; the caller restores its own.
    void record(ID3D12GraphicsCommandList* list, UINT source_slot, D3D12_CPU_DESCRIPTOR_HANDLE target, UINT width, UINT height,
                const Constants& constants, UINT cb_slot) {
        if (!pso_ || !list || source_slot >= slots_ || cb_slot >= slots_) return;
        std::memcpy(mapped_ + (size_t)kCbStride * cb_slot, &constants, sizeof constants);
        const D3D12_VIEWPORT vp{0, 0, (float)width, (float)height, 0, 1};
        const D3D12_RECT sc{0, 0, (LONG)width, (LONG)height};
        D3D12_GPU_DESCRIPTOR_HANDLE src = heap_->GetGPUDescriptorHandleForHeapStart();
        src.ptr += (UINT64)source_slot * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        list->SetDescriptorHeaps(1, &heap_);
        list->SetGraphicsRootSignature(root_);
        list->SetPipelineState(pso_);
        list->SetGraphicsRootConstantBufferView(0, cb_->GetGPUVirtualAddress() + (UINT64)kCbStride * cb_slot);
        list->SetGraphicsRootDescriptorTable(1, src);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->RSSetViewports(1, &vp);
        list->RSSetScissorRects(1, &sc);
        list->OMSetRenderTargets(1, &target, FALSE, nullptr);
        list->DrawInstanced(3, 1, 0, 0);
    }

    void release() {
        if (cb_ && mapped_) cb_->Unmap(0, nullptr);
        mapped_ = nullptr;
        for (IUnknown** p : {(IUnknown**)&cb_, (IUnknown**)&heap_, (IUnknown**)&pso_, (IUnknown**)&root_, (IUnknown**)&device_})
            if (*p) (*p)->Release(), *p = nullptr;
        slots_ = 0;
    }
    ~D3D12Compositor() { release(); }

    // The typed format a shader reads from a typeless (or already typed) texture format.
    static DXGI_FORMAT typed(DXGI_FORMAT f) {
        switch (f) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default: return f;
        }
    }

private:
    static constexpr UINT kCbStride = (sizeof(Constants) + 255) & ~255u;   // CBV alignment
    D3D12_CPU_DESCRIPTOR_HANDLE cpu(UINT slot) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = heap_->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (SIZE_T)slot * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return h;
    }
    ID3D12Device* device_ = nullptr; ID3D12RootSignature* root_ = nullptr; ID3D12PipelineState* pso_ = nullptr;
    ID3D12DescriptorHeap* heap_ = nullptr; ID3D12Resource* cb_ = nullptr; unsigned char* mapped_ = nullptr; UINT slots_ = 0;
};
} // namespace dbce::triple_screen::compositor
