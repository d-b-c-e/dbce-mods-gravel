#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>
#include "dbce_triple_compositor.hpp"

// D3D11 runtime for dbce_triple_compositor.hpp. Link d3d11.lib and d3dcompiler.lib. The shader below is the same
// math as compositor::sample (single precision); tests/NativeCompositor checks them against each other on WARP.
namespace dbce::triple_screen::compositor {
inline const char shader_source[] = R"(
cbuffer Warp : register(b0) {
    float4 panelOrigin[3], panelRight[3], panelUp[3], panelNormal[3], outputRect[3];
    float4 sourceRight[4], sourceUp[4], sourceForward[4], sourceTan[4], sourceRegion[4];
    float4 misc;
    float4 tone;   // x: exposure for a scene-linear (HDR) source, then Reinhard and sRGB; 0 = output the source as is
    float4 match;  // x > 0: exposure match at the seams; y: mip level sampled there; z: maximum gain change
    float4 drawnOrigin[3], drawnRight[3], drawnUp[3];   // Layout::source_on: w > 0 samples the source panel's point
};
Texture2D<float4> source : register(t0);
SamplerState linearClamp : register(s0);
float3 finish(float3 c) {
    if (tone.x <= 0) return c;
    c = max(c, 0) * tone.x;
    c = c / (1 + c);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1 / 2.4) - 0.055;
}
float3 viewRay(int s, float nu, float nv) {
    return sourceForward[s].xyz + sourceRight[s].xyz * lerp(sourceTan[s].x, sourceTan[s].y, nu) +
           sourceUp[s].xyz * lerp(sourceTan[s].z, sourceTan[s].w, nv);
}
float2 regionUV(int s, float3 d) {
    float depth = max(dot(d, sourceForward[s].xyz), 1e-6);
    float nu = saturate((dot(d, sourceRight[s].xyz) / depth - sourceTan[s].x) / (sourceTan[s].y - sourceTan[s].x));
    float nv = saturate((dot(d, sourceUp[s].xyz) / depth - sourceTan[s].z) / (sourceTan[s].w - sourceTan[s].z));
    float2 uv = sourceRegion[s].xy + float2(nu, 1 - nv) * (sourceRegion[s].zw - sourceRegion[s].xy);
    // Stay one texel of the sampled mip inside the view's region: its neighbour in the texture must not filter in.
    float w, h;
    source.GetDimensions(w, h);
    float2 inset = exp2(match.y) / float2(w, h);
    return clamp(uv, sourceRegion[s].xy + inset, sourceRegion[s].zw - inset);
}
// Views with their own auto-exposure differ in brightness: scale view s to its neighbour toward the middle view by the
// colour ratio at the direction halfway between their touching edges (three heights, low mip).
float3 viewGain(int s) {
    int n = (int)misc.y, ref = n / 2;
    if (match.x <= 0 || n < 2 || s == ref) return 1;
    int b = s < ref ? s + 1 : s - 1;
    float edgeS = s < b ? 1 : 0;
    float3 mine = 0, theirs = 0;
    [unroll] for (int k = 1; k <= 3; ++k) {
        float nv = 0.25 * k;
        float3 d = normalize(viewRay(s, edgeS, nv)) + normalize(viewRay(b, 1 - edgeS, nv));
        mine += source.SampleLevel(linearClamp, regionUV(s, d), match.y).rgb;
        theirs += source.SampleLevel(linearClamp, regionUV(b, d), match.y).rgb;
    }
    return clamp((theirs + 0.01) / (mine + 0.01), 1 - match.z, 1 + match.z);
}
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 ps(float4 position : SV_Position) : SV_Target {
    int panel = -1;
    float2 uv = 0;
    [unroll] for (int i = 0; i < 3; ++i) {
        float2 q = (position.xy - outputRect[i].xy) / outputRect[i].zw;
        if (panel < 0 && all(q >= 0) && all(q < 1)) { panel = i; uv = float2(q.x, 1 - q.y); }
    }
    if (panel < 0) return 0;
    if (misc.w > 0)   // fit: the whole source, undistorted, on one panel (videos, menus); the others black
        return panel == (int)misc.w - 1 ? float4(finish(source.SampleLevel(linearClamp, float2(uv.x, 1 - uv.y), 0).rgb), 1) : 0;
    float chord = panelRight[panel].w, half = panelNormal[panel].w, radius = misc.x;
    float3 p = panelOrigin[panel].xyz + panelUp[panel].xyz * (uv.y * panelUp[panel].w);
    if (half == 0) {
        p += panelRight[panel].xyz * (chord * uv.x);
    } else {
        float phi = (2 * uv.x - 1) * half;
        float sag = -2 * sin((half + phi) / 2) * sin((half - phi) / 2) * radius;
        p += panelRight[panel].xyz * (chord / 2 + radius * sin(phi)) + panelNormal[panel].xyz * sag;
    }
    if (drawnOrigin[panel].w > 0) {   // the views are drawn for a virtual eye: the real eye's chord-plane point on that rig
        float3 c = p * (dot(panelOrigin[panel].xyz, panelNormal[panel].xyz) / dot(p, panelNormal[panel].xyz)) - panelOrigin[panel].xyz;
        p = drawnOrigin[panel].xyz + drawnRight[panel].xyz * dot(c, panelRight[panel].xyz) + drawnUp[panel].xyz * dot(c, panelUp[panel].xyz);
    }
    float best = -1, weights = 0;
    float2 tex = 0;
    int bestSource = 0;
    float3 blended = 0;
    // Views are ranked by how deep the ray is horizontally inside them (covering also needs it inside vertically), so
    // overlapping views switch on one vertical seam in the middle of their overlap, where the exposure match samples;
    // with a band, only views within it of the best one blend. Ranking by the nearest edge of any side let a view
    // whose top edge was close lose whole corners to its neighbour, and averaging every covering view mixed their
    // auto-exposures (DiRT 3 rig 2026-10-29, views at +-76 deg, 90.8 deg wide).
    for (int s = 0; s < (int)misc.y; ++s) {
        float depth = dot(p, sourceForward[s].xyz);
        if (depth <= 0) continue;
        float nu = (dot(p, sourceRight[s].xyz) / depth - sourceTan[s].x) / (sourceTan[s].y - sourceTan[s].x);
        float nv = (dot(p, sourceUp[s].xyz) / depth - sourceTan[s].z) / (sourceTan[s].w - sourceTan[s].z);
        if (min(min(nu, 1 - nu), min(nv, 1 - nv)) < 0) continue;
        float rank = min(nu, 1 - nu);
        if (rank > best) { best = rank; tex = sourceRegion[s].xy + float2(nu, 1 - nv) * (sourceRegion[s].zw - sourceRegion[s].xy); bestSource = s; }
    }
    if (best >= 0 && misc.z > 0) {   // crossfade: views within the band of the best one, weighted by how close they are
        for (int s = 0; s < (int)misc.y; ++s) {
            float depth = dot(p, sourceForward[s].xyz);
            if (depth <= 0) continue;
            float nu = (dot(p, sourceRight[s].xyz) / depth - sourceTan[s].x) / (sourceTan[s].y - sourceTan[s].x);
            float nv = (dot(p, sourceUp[s].xyz) / depth - sourceTan[s].z) / (sourceTan[s].w - sourceTan[s].z);
            if (min(min(nu, 1 - nu), min(nv, 1 - nv)) < 0) continue;
            float w = saturate(1 + (min(nu, 1 - nu) - best) / misc.z);
            float2 here = sourceRegion[s].xy + float2(nu, 1 - nv) * (sourceRegion[s].zw - sourceRegion[s].xy);
            blended += w * source.SampleLevel(linearClamp, here, 0).rgb * viewGain(s);
            weights += w;
        }
    }
    if (best < 0) return 0;
    if (weights > 0) return float4(finish(blended / weights), 1);
    return float4(finish(source.SampleLevel(linearClamp, tex, 0).rgb * viewGain(bestSource)), 1);
}
)";

// Draws the warp into a render target, saving and restoring every pipeline state it touches. Create on the game's
// device; call from the thread that owns the immediate context (normally inside a Present hook).
class D3D11Compositor {
public:
    HRESULT create(ID3D11Device* device) {
        release();
        ID3DBlob *vsBlob = nullptr, *psBlob = nullptr, *errors = nullptr;
        HRESULT hr = D3DCompile(shader_source, sizeof shader_source - 1, "dbce_triple_compositor", nullptr, nullptr, "vs",
                                "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errors);
        if (errors) errors->Release(), errors = nullptr;
        if (SUCCEEDED(hr)) hr = D3DCompile(shader_source, sizeof shader_source - 1, "dbce_triple_compositor", nullptr, nullptr,
                                           "ps", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errors);
        if (errors) errors->Release();
        if (SUCCEEDED(hr)) hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_);
        if (SUCCEEDED(hr)) hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_);
        if (vsBlob) vsBlob->Release();
        if (psBlob) psBlob->Release();
        D3D11_BUFFER_DESC cb{(sizeof(Constants) + 15) & ~15u, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
        if (SUCCEEDED(hr)) hr = device->CreateBuffer(&cb, nullptr, &cb_);
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (SUCCEEDED(hr)) hr = device->CreateSamplerState(&sd, &sampler_);
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
        if (SUCCEEDED(hr)) hr = device->CreateRasterizerState(&rd, &raster_);
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].SrcBlend = bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlend = bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOp = bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (SUCCEEDED(hr)) hr = device->CreateBlendState(&bd, &blend_);
        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
        if (SUCCEEDED(hr)) hr = device->CreateDepthStencilState(&dd, &depth_);
        if (FAILED(hr)) release();
        return hr;
    }

    // Warps `source` (a view of a texture other than the target) into `target`, whose size is width x height.
    void draw(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* source, ID3D11RenderTargetView* target,
              UINT width, UINT height, const Constants& constants) {
        if (!vs_ || !ctx || !source || !target) return;
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(cb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        std::memcpy(m.pData, &constants, sizeof constants);
        ctx->Unmap(cb_, 0);
        Saved s{};
        save(ctx, s);
        const D3D11_VIEWPORT vp{0, 0, (float)width, (float)height, 0, 1};
        const float factor[4]{};
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs_, nullptr, 0);
        ctx->HSSetShader(nullptr, nullptr, 0);
        ctx->DSSetShader(nullptr, nullptr, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->PSSetShader(ps_, nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, &cb_);
        ctx->PSSetShaderResources(0, 1, &source);
        ctx->PSSetSamplers(0, 1, &sampler_);
        ctx->RSSetState(raster_);
        ctx->RSSetViewports(1, &vp);
        ctx->OMSetBlendState(blend_, factor, 0xffffffff);
        ctx->OMSetDepthStencilState(depth_, 0);
        ctx->OMSetRenderTargets(1, &target, nullptr);
        ctx->Draw(3, 0);
        restore(ctx, s);
    }

    // Present-hook convenience: copies (or resolves) the back buffer, then warps the copy back into it.
    // shortcut: assumes a UNORM-family colour format with a typed SRV; add a typeless mapping table when a game needs it.
    HRESULT draw_in_place(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11Texture2D* backBuffer, const Constants& constants) {
        if (!vs_ || !device || !ctx || !backBuffer) return E_INVALIDARG;
        D3D11_TEXTURE2D_DESC d{};
        backBuffer->GetDesc(&d);
        // Exposure matching samples a low mip level of the copy: then the copy carries a generated mip chain.
        UINT autogen = 0;
        const bool mips = constants.match[0] > 0 && SUCCEEDED(device->CheckFormatSupport(d.Format, &autogen)) &&
                          (autogen & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN);
        if (!copy_ || d.Width != copyDesc_.Width || d.Height != copyDesc_.Height || d.Format != copyDesc_.Format || mips != copyMips_) {
            releaseCopy();
            D3D11_TEXTURE2D_DESC c = d;
            c.MipLevels = mips ? 0 : 1; c.ArraySize = 1; c.SampleDesc = {1, 0}; c.Usage = D3D11_USAGE_DEFAULT;
            c.BindFlags = D3D11_BIND_SHADER_RESOURCE | (mips ? D3D11_BIND_RENDER_TARGET : 0); c.CPUAccessFlags = 0;
            c.MiscFlags = mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
            HRESULT hr = device->CreateTexture2D(&c, nullptr, &copy_);
            if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(copy_, nullptr, &copyView_);
            if (FAILED(hr)) { releaseCopy(); return hr; }
            copyDesc_ = d;
            copyMips_ = mips;
        }
        if (d.SampleDesc.Count > 1) ctx->ResolveSubresource(copy_, 0, backBuffer, 0, d.Format);
        else ctx->CopySubresourceRegion(copy_, 0, 0, 0, 0, backBuffer, 0, nullptr);
        if (copyMips_) ctx->GenerateMips(copyView_);
        ID3D11RenderTargetView* rtv = nullptr;
        HRESULT hr = device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
        if (FAILED(hr)) return hr;
        draw(ctx, copyView_, rtv, d.Width, d.Height, constants);
        rtv->Release();
        return S_OK;
    }

    void release() {
        releaseCopy();
        for (IUnknown** p : {(IUnknown**)&vs_, (IUnknown**)&ps_, (IUnknown**)&cb_, (IUnknown**)&sampler_, (IUnknown**)&raster_,
                             (IUnknown**)&blend_, (IUnknown**)&depth_})
            if (*p) (*p)->Release(), *p = nullptr;
    }
    ~D3D11Compositor() { release(); }

private:
    struct Saved {
        D3D11_PRIMITIVE_TOPOLOGY topology; ID3D11InputLayout* layout;
        ID3D11VertexShader* vs; ID3D11HullShader* hs; ID3D11DomainShader* ds; ID3D11GeometryShader* gs; ID3D11PixelShader* ps;
        ID3D11ClassInstance *vsI[256], *hsI[256], *dsI[256], *gsI[256], *psI[256]; UINT vsN, hsN, dsN, gsN, psN;
        ID3D11Buffer* cb; ID3D11ShaderResourceView* srv; ID3D11SamplerState* sampler;
        ID3D11RasterizerState* raster; UINT viewports; D3D11_VIEWPORT viewport[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        ID3D11BlendState* blend; float factor[4]; UINT mask; ID3D11DepthStencilState* depth; UINT ref;
        ID3D11RenderTargetView* rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]; ID3D11DepthStencilView* dsv;
    };
    static void save(ID3D11DeviceContext* c, Saved& s) {
        c->IAGetPrimitiveTopology(&s.topology); c->IAGetInputLayout(&s.layout);
        s.vsN = s.hsN = s.dsN = s.gsN = s.psN = 256;
        c->VSGetShader(&s.vs, s.vsI, &s.vsN); c->HSGetShader(&s.hs, s.hsI, &s.hsN); c->DSGetShader(&s.ds, s.dsI, &s.dsN);
        c->GSGetShader(&s.gs, s.gsI, &s.gsN); c->PSGetShader(&s.ps, s.psI, &s.psN);
        c->PSGetConstantBuffers(0, 1, &s.cb); c->PSGetShaderResources(0, 1, &s.srv); c->PSGetSamplers(0, 1, &s.sampler);
        c->RSGetState(&s.raster); s.viewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetViewports(&s.viewports, s.viewport);
        c->OMGetBlendState(&s.blend, s.factor, &s.mask); c->OMGetDepthStencilState(&s.depth, &s.ref);
        c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, s.rtv, &s.dsv);
    }
    template <class T> static void drop(T*& p) { if (p) p->Release(), p = nullptr; }
    static void dropAll(ID3D11ClassInstance** p, UINT n) { for (UINT i = 0; i < n; ++i) drop(p[i]); }
    static void restore(ID3D11DeviceContext* c, Saved& s) {
        c->IASetPrimitiveTopology(s.topology); c->IASetInputLayout(s.layout);
        c->VSSetShader(s.vs, s.vsI, s.vsN); c->HSSetShader(s.hs, s.hsI, s.hsN); c->DSSetShader(s.ds, s.dsI, s.dsN);
        c->GSSetShader(s.gs, s.gsI, s.gsN); c->PSSetShader(s.ps, s.psI, s.psN);
        c->PSSetConstantBuffers(0, 1, &s.cb); c->PSSetShaderResources(0, 1, &s.srv); c->PSSetSamplers(0, 1, &s.sampler);
        c->RSSetState(s.raster); c->RSSetViewports(s.viewports, s.viewport);
        c->OMSetBlendState(s.blend, s.factor, s.mask); c->OMSetDepthStencilState(s.depth, s.ref);
        c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, s.rtv, s.dsv);
        drop(s.layout); drop(s.vs); drop(s.hs); drop(s.ds); drop(s.gs); drop(s.ps);
        dropAll(s.vsI, s.vsN); dropAll(s.hsI, s.hsN); dropAll(s.dsI, s.dsN); dropAll(s.gsI, s.gsN); dropAll(s.psI, s.psN);
        drop(s.cb); drop(s.srv); drop(s.sampler); drop(s.raster); drop(s.blend); drop(s.depth); drop(s.dsv);
        for (auto*& r : s.rtv) drop(r);
    }
    void releaseCopy() { drop(copyView_); drop(copy_); copyDesc_ = {}; }

    ID3D11VertexShader* vs_ = nullptr; ID3D11PixelShader* ps_ = nullptr; ID3D11Buffer* cb_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr; ID3D11RasterizerState* raster_ = nullptr;
    ID3D11BlendState* blend_ = nullptr; ID3D11DepthStencilState* depth_ = nullptr;
    ID3D11Texture2D* copy_ = nullptr; ID3D11ShaderResourceView* copyView_ = nullptr; D3D11_TEXTURE2D_DESC copyDesc_{};
    bool copyMips_ = false;
};
} // namespace dbce::triple_screen::compositor
