#pragma once
#include <d3d11.h>
#include <cstdio>
#include <vector>

// Development aid: writes a texture as a 24-bit BMP, keeping every `step`-th pixel in each direction. Synchronous GPU
// readback (stalls the frame); call only on request. Formats: R8G8B8A8/B8G8R8A8 (UNORM/SRGB/TYPELESS) and R10G10B10A2.
namespace dbce::triple_screen::compositor {
inline HRESULT dump_bmp(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* tex, const char* path, UINT step) {
    if (!dev || !ctx || !tex || !path || !step) return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC d{};
    tex->GetDesc(&d);
    const DXGI_FORMAT f = d.Format;
    const bool bgra = f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_TYPELESS;
    const bool rgba = f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_R8G8B8A8_TYPELESS;
    const bool ten = f == DXGI_FORMAT_R10G10B10A2_UNORM || f == DXGI_FORMAT_R10G10B10A2_TYPELESS;
    if (!bgra && !rgba && !ten) return E_NOTIMPL;
    D3D11_TEXTURE2D_DESC s = d;
    s.MipLevels = 1; s.ArraySize = 1; s.SampleDesc = {1, 0}; s.Usage = D3D11_USAGE_STAGING;
    s.BindFlags = 0; s.CPUAccessFlags = D3D11_CPU_ACCESS_READ; s.MiscFlags = 0;
    ID3D11Texture2D *staging = nullptr, *resolved = nullptr;
    HRESULT hr = dev->CreateTexture2D(&s, nullptr, &staging);
    if (FAILED(hr)) return hr;
    if (d.SampleDesc.Count > 1) {
        D3D11_TEXTURE2D_DESC r = s;
        r.Usage = D3D11_USAGE_DEFAULT; r.CPUAccessFlags = 0;
        hr = dev->CreateTexture2D(&r, nullptr, &resolved);
        if (FAILED(hr)) { staging->Release(); return hr; }
        // A typeless format cannot be resolved; the three handled families resolve as UNORM.
        ctx->ResolveSubresource(resolved, 0, tex, 0, ten ? DXGI_FORMAT_R10G10B10A2_UNORM : bgra ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM);
        ctx->CopyResource(staging, resolved);
        resolved->Release();
    } else {
        ctx->CopySubresourceRegion(staging, 0, 0, 0, 0, tex, 0, nullptr);
    }
    D3D11_MAPPED_SUBRESOURCE m{};
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m);
    if (FAILED(hr)) { staging->Release(); return hr; }
    const UINT w = (d.Width + step - 1) / step, h = (d.Height + step - 1) / step, row = (w * 3 + 3) & ~3u;
    std::vector<unsigned char> px(row * h);
    for (UINT y = 0; y < h; ++y) {
        const unsigned char* src = (const unsigned char*)m.pData + (size_t)y * step * m.RowPitch;
        unsigned char* out = &px[(size_t)(h - 1 - y) * row];   // bottom-up rows
        for (UINT x = 0; x < w; ++x) {
            const unsigned v = *(const unsigned*)(src + (size_t)x * step * 4);
            unsigned r, g, b;
            if (ten) { r = (v & 1023) >> 2; g = ((v >> 10) & 1023) >> 2; b = ((v >> 20) & 1023) >> 2; }
            else if (bgra) { b = v & 255; g = (v >> 8) & 255; r = (v >> 16) & 255; }
            else { r = v & 255; g = (v >> 8) & 255; b = (v >> 16) & 255; }
            out[x * 3] = (unsigned char)b; out[x * 3 + 1] = (unsigned char)g; out[x * 3 + 2] = (unsigned char)r;
        }
    }
    ctx->Unmap(staging, 0);
    staging->Release();
    FILE* file = nullptr;
    if (fopen_s(&file, path, "wb") || !file) return E_FAIL;
    const unsigned size = 54 + (unsigned)px.size();
    unsigned char hdr[54] = {'B', 'M'};
    auto put = [&](int at, unsigned v) { for (int i = 0; i < 4; ++i) hdr[at + i] = (unsigned char)(v >> (8 * i)); };
    put(2, size); put(10, 54); put(14, 40); put(18, w); put(22, h); hdr[26] = 1; hdr[28] = 24; put(34, (unsigned)px.size());
    const bool ok = fwrite(hdr, 1, 54, file) == 54 && fwrite(px.data(), 1, px.size(), file) == px.size();
    fclose(file);
    return ok ? S_OK : E_FAIL;
}
} // namespace dbce::triple_screen::compositor
