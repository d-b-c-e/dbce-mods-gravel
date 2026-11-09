// See dbce_overlay_d3d12.h.
#include "dbce_overlay_d3d12.h"
#include "dbce_overlay_internal.h"
#include "dbce_panel.h"
#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/backends/imgui_impl_dx12.h"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

namespace dbce { namespace overlay {

namespace {
using detail::logf;

// ---- the present queue from the swap chain itself ------------------------------------------------------------
// The ExecuteCommandLists patch on a probe queue never fires in ADRIFT (UE 5.3, Agility SDK core): the game's queue
// does not share the probe's vtable. DXGI keeps the queue the chain was created with as a plain pointer inside the
// chain object (+320 on Windows 11 dxgi; scanned, not assumed). A candidate counts when it is a readable COM object
// whose vtable and first three entries lie in a D3D12 module, answers QueryInterface for ID3D12CommandQueue, is a
// direct queue and belongs to the chain's device. No reference is kept: the chain holds the queue alive.
bool readable(const void *p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION m;
    if (!p || !VirtualQuery(p, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & PAGE_GUARD)) return false;
    const DWORD prot = m.Protect & 0xFF;
    if (prot != PAGE_READONLY && prot != PAGE_READWRITE && prot != PAGE_EXECUTE_READ && prot != PAGE_EXECUTE_READWRITE &&
        prot != PAGE_WRITECOPY && prot != PAGE_EXECUTE_WRITECOPY) return false;
    return (const char *)p + n <= (const char *)m.BaseAddress + m.RegionSize;
}
bool inD3D12Module(const void *p)
{
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)p, &m) || !m) return false;
    wchar_t path[MAX_PATH] = L"";
    GetModuleFileNameW(m, path, MAX_PATH);
    const wchar_t *name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsnicmp(name, L"d3d12", 5) == 0;
}
ID3D12CommandQueue *queueOfChain(IDXGISwapChain *sc, ID3D12Device *dev, UINT *at)
{
    if (!sc || !dev || !readable(sc, 1024)) return nullptr;
    for (UINT i = 8; i < 1024; i += 8) {
        void *cand = *(void **)((const char *)sc + i);
        if (!cand || ((uintptr_t)cand & 7) || !readable(cand, 8)) continue;
        void **vt = *(void ***)cand;
        if (!readable(vt, 24) || !inD3D12Module(vt) || !inD3D12Module(vt[0]) || !inD3D12Module(vt[1]) || !inD3D12Module(vt[2])) continue;
        ID3D12CommandQueue *q = nullptr;
        if (FAILED(((IUnknown *)cand)->QueryInterface(IID_PPV_ARGS(&q))) || !q) continue;
        ID3D12Device *qdev = nullptr;
        const bool ok = q->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT && SUCCEEDED(q->GetDevice(IID_PPV_ARGS(&qdev))) && qdev == dev;
        if (qdev) qdev->Release();
        q->Release();
        if (ok) { if (at) *at = i; return q; }
    }
    return nullptr;
}

// ---- the game's present queue ------------------------------------------------------------------------------
typedef void(STDMETHODCALLTYPE *EclFn)(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *);
detail::SlotHook g_ecl = {"ExecuteCommandLists"};
ID3D12CommandQueue *volatile g_lastDirect;   // the last direct queue that executed work (the game's; not owned)

void STDMETHODCALLTYPE hkEcl(ID3D12CommandQueue *q, UINT n, ID3D12CommandList *const *lists)
{
    static thread_local int depth;   // entered again: another overlay's hook and ours call each other (detail::SlotHook)
    if (!detail::Reentry(g_ecl, depth)) return;
    detail::Depth in(depth);
    if (depth == 1 && q != g_lastDirect) {
        D3D12_COMMAND_QUEUE_DESC d = q->GetDesc();
        if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            static bool noted;
            if (!noted) { noted = true; logf("overlay: first direct queue seen %p (%u lists)", (void *)q, n); }
            g_lastDirect = q;
        }
    }
    if (g_ecl.looped) { if (auto fn = (EclFn)detail::SystemFunction(g_ecl)) fn(q, n, lists); return; }
    ((EclFn)g_ecl.next)(q, n, lists);
}

// The chain's own queue when the patch has seen none of this device's; remembered as the present queue from then on.
ID3D12CommandQueue *chainQueue(IDXGISwapChain *sc, ID3D12Device *dev)
{
    UINT at = 0;
    ID3D12CommandQueue *q = queueOfChain(sc, dev, &at);
    if (q) {
        static bool noted;
        if (!noted) { noted = true; logf("overlay: present queue %p taken from the swap chain object (+%u); the queue patch had not seen it", (void *)q, at); }
        g_lastDirect = q;
    }
    return q;
}

// ---- per swap chain ------------------------------------------------------------------------------------------
const UINT kMaxBuffers = 8;
struct Frame { ID3D12CommandAllocator *alloc; ID3D12Resource *buffer; D3D12_CPU_DESCRIPTOR_HANDLE rtv; UINT64 fence; };
IDXGISwapChain *g_chain;
ID3D12Device *g_dev;
ID3D12CommandQueue *g_queue;
ID3D12DescriptorHeap *g_rtvHeap, *g_srvHeap;
ID3D12GraphicsCommandList *g_list;
ID3D12Fence *g_fence;
HANDLE g_event;
UINT64 g_fenceNext;
Frame g_frames[kMaxBuffers];
UINT g_count;
DXGI_FORMAT g_format;
bool g_backend, g_queueNoted;
IDXGISwapChain *g_failNoted;   // the last chain whose renderer could not start was logged (once per chain)

void waitIdle()
{
    if (!g_queue || !g_fence) return;
    const UINT64 v = ++g_fenceNext;
    if (SUCCEEDED(g_queue->Signal(g_fence, v)) && g_fence->GetCompletedValue() < v && SUCCEEDED(g_fence->SetEventOnCompletion(v, g_event)))
        WaitForSingleObject(g_event, 1000);
}

void releaseBuffers()
{
    for (UINT i = 0; i < kMaxBuffers; ++i) if (g_frames[i].buffer) { g_frames[i].buffer->Release(); g_frames[i].buffer = nullptr; }
}

void teardown()
{
    waitIdle();
    releaseBuffers();
    if (g_backend) { ImGui_ImplDX12_Shutdown(); g_backend = false; }
    for (UINT i = 0; i < kMaxBuffers; ++i) if (g_frames[i].alloc) { g_frames[i].alloc->Release(); g_frames[i].alloc = nullptr; }
    if (g_list) { g_list->Release(); g_list = nullptr; }
    if (g_rtvHeap) { g_rtvHeap->Release(); g_rtvHeap = nullptr; }
    if (g_srvHeap) { g_srvHeap->Release(); g_srvHeap = nullptr; }
    if (g_fence) { g_fence->Release(); g_fence = nullptr; }
    if (g_event) { CloseHandle(g_event); g_event = nullptr; }
    if (g_queue) { g_queue->Release(); g_queue = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
    g_chain = nullptr;
    g_count = 0;
}

// The back buffers and their render-target views (again after a resize).
bool fetchBuffers(IDXGISwapChain *sc)
{
    const UINT step = g_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < g_count; ++i, h.ptr += step) {
        if (FAILED(sc->GetBuffer(i, IID_PPV_ARGS(&g_frames[i].buffer)))) return false;
        g_dev->CreateRenderTargetView(g_frames[i].buffer, nullptr, h);
        g_frames[i].rtv = h;
    }
    return true;
}

bool setup(IDXGISwapChain *sc, ID3D12Device *dev)
{
    DXGI_SWAP_CHAIN_DESC d;
    if (FAILED(sc->GetDesc(&d))) return false;
    if (d.BufferCount == 0 || d.BufferCount > kMaxBuffers) {
        if (g_failNoted != sc) { logf("overlay: the D3D12 renderer cannot take a swap chain of %u buffers; panel not drawn", d.BufferCount); g_failNoted = sc; }
        return false;
    }
    ID3D12CommandQueue *q = g_lastDirect;
    ID3D12Device *qdev = nullptr;
    if (!q || FAILED(q->GetDevice(IID_PPV_ARGS(&qdev))) || qdev != dev) {
        if (qdev) qdev->Release();
        qdev = nullptr;
        q = chainQueue(sc, dev);
        if (!q) {
            if (!g_queueNoted) { logf("overlay: no direct queue of the game's D3D12 device seen yet, none found in the swap chain; panel not drawn"); g_queueNoted = true; }
            return false;
        }
    }
    if (qdev) qdev->Release();
    g_chain = sc;
    g_dev = dev; g_dev->AddRef();
    g_queue = q; g_queue->AddRef();
    g_count = d.BufferCount;
    g_format = d.BufferDesc.Format;
    D3D12_DESCRIPTOR_HEAP_DESC rh = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, g_count, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC sh = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    bool ok = SUCCEEDED(g_dev->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&g_rtvHeap))) &&
              SUCCEEDED(g_dev->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&g_srvHeap))) &&
              SUCCEEDED(g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)));
    g_event = ok ? CreateEventW(nullptr, FALSE, FALSE, nullptr) : nullptr;
    ok = ok && g_event;
    for (UINT i = 0; ok && i < g_count; ++i) ok = SUCCEEDED(g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frames[i].alloc)));
    ok = ok && SUCCEEDED(g_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].alloc, nullptr, IID_PPV_ARGS(&g_list)));
    if (ok) g_list->Close();
    ok = ok && fetchBuffers(sc);
    if (ok) {
        detail::ensureImGui();
        g_backend = ImGui_ImplDX12_Init(g_dev, (int)g_count, g_format, g_srvHeap, g_srvHeap->GetCPUDescriptorHandleForHeapStart(),
                                        g_srvHeap->GetGPUDescriptorHandleForHeapStart());
        ok = g_backend;
    }
    if (ok || g_failNoted != sc)   // a failure once per chain: a chain that cannot be drawn on is tried again now and then
        logf(ok ? "overlay: D3D12 renderer ready (%u buffers, format %d)" : "overlay: D3D12 renderer could not start (%u buffers, format %d)", g_count, (int)g_format);
    if (!ok) { g_failNoted = sc; teardown(); }
    return ok;
}

void barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    g_list->ResourceBarrier(1, &b);
}

// Called by the shared present hook (under its lock): with draw while the panel is open, without it to find out whether
// a chain the panel has no input on yet can be drawn on (detail::render12).
bool render12(IDXGISwapChain *sc, bool draw)
{
    ID3D12Device *dev = nullptr;
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&dev))) || !dev) return false;   // not a D3D12 swap chain
    if (sc != g_chain || dev != g_dev) { teardown(); if (!setup(sc, dev)) { dev->Release(); return false; } }
    dev->Release();
    if (!g_frames[0].buffer) {   // after a resize: the same buffers again, or a full rebuild if their count or format changed
        DXGI_SWAP_CHAIN_DESC d;
        if (FAILED(sc->GetDesc(&d))) return true;
        if (d.BufferCount != g_count || d.BufferDesc.Format != g_format) {
            teardown();
            ID3D12Device *dv = nullptr;
            const bool ok = SUCCEEDED(sc->GetDevice(IID_PPV_ARGS(&dv))) && dv && setup(sc, dv);
            if (dv) dv->Release();
            if (!ok) return false;
        } else if (!fetchBuffers(sc)) return true;
    }
    if (!draw) return true;
    IDXGISwapChain3 *sc3 = nullptr;
    if (FAILED(sc->QueryInterface(IID_PPV_ARGS(&sc3))) || !sc3) return true;
    const UINT idx = sc3->GetCurrentBackBufferIndex();
    sc3->Release();
    if (idx >= g_count) return true;
    Frame &f = g_frames[idx];
    if (g_fence->GetCompletedValue() < f.fence && SUCCEEDED(g_fence->SetEventOnCompletion(f.fence, g_event))) WaitForSingleObject(g_event, 200);

    D3D12_RESOURCE_DESC rd = f.buffer->GetDesc();
    const int w = (int)rd.Width, h = (int)rd.Height;
    if (dbce::panel::Prepare(w, h)) { dbce::panel::BuildFonts(); ImGui_ImplDX12_InvalidateDeviceObjects(); }
    ImGui_ImplDX12_NewFrame();
    detail::drawFrame(w, h);

    f.alloc->Reset();
    g_list->Reset(f.alloc, nullptr);
    barrier(f.buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    g_list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
    g_list->SetDescriptorHeaps(1, &g_srvHeap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_list);
    barrier(f.buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    g_list->Close();
    ID3D12CommandList *lists[1] = {g_list};
    if (g_ecl.looped) { if (auto fn = (EclFn)detail::SystemFunction(g_ecl)) fn(g_queue, 1, lists); }
    else if (g_ecl.next) ((EclFn)g_ecl.next)(g_queue, 1, lists);
    else g_queue->ExecuteCommandLists(1, lists);
    f.fence = ++g_fenceNext;
    g_queue->Signal(g_fence, f.fence);
    return true;
}

void resize12(IDXGISwapChain *sc)
{
    if (sc != g_chain) return;
    waitIdle();
    releaseBuffers();   // fetched again on the next open frame; a changed buffer count or format rebuilds everything
}

void probe12()
{
    HMODULE m = LoadLibraryW(L"d3d12.dll");
    auto create = m ? (PFN_D3D12_CREATE_DEVICE)GetProcAddress(m, "D3D12CreateDevice") : nullptr;
    if (!create) { logf("overlay: d3d12.dll not available; D3D12 panel off"); return; }
    ID3D12Device *dev = nullptr;
    if (FAILED(create(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev))) || !dev) { logf("overlay: no D3D12 device for the queue probe; D3D12 panel off"); return; }
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue *q = nullptr;
    if (SUCCEEDED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))) && q) {
        void **vt = *(void ***)q;
        if (detail::PatchSlot(vt, 10, (void *)&hkEcl, g_ecl)) logf("overlay: D3D12 queue capture installed");
        q->Release();
    }
    dev->Release();
}
void (*volatile g_before)(IDXGISwapChain *, ID3D12CommandQueue *);

void before12(IDXGISwapChain *sc)
{
    static bool noted;   // one line on the first Present that cannot reach the hook, so a silent mod step can be diagnosed
    auto *hook = g_before;
    if (!hook) return;
    ID3D12Device *dev = nullptr, *qdev = nullptr;
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&dev))) || !dev) { if (!noted) { noted = true; logf("overlay: before-panel hook skipped: the swap chain gives no D3D12 device"); } return; }
    ID3D12CommandQueue *q = g_lastDirect;
    if (q && (FAILED(q->GetDevice(IID_PPV_ARGS(&qdev))) || qdev != dev)) q = nullptr;   // the patch saw some other device's queue
    if (qdev) qdev->Release();
    if (!q) q = chainQueue(sc, dev);
    if (q) hook(sc, q);
    else if (!noted) { noted = true; logf("overlay: before-panel hook waits: no direct queue of the game's device seen yet, none found in the swap chain"); }
    dev->Release();
}
} // namespace

ID3D12CommandQueue *FindPresentQueue(IDXGISwapChain *chain)
{
    ID3D12Device *dev = nullptr;
    if (!chain || FAILED(chain->GetDevice(IID_PPV_ARGS(&dev))) || !dev) return nullptr;
    ID3D12CommandQueue *q = queueOfChain(chain, dev, nullptr);
    dev->Release();
    return q;
}

void SetBeforePanelD3D12(void (*hook)(IDXGISwapChain *, ID3D12CommandQueue *))
{
    g_before = hook;
    detail::before12 = hook ? &before12 : nullptr;
}

void EnableD3D12()
{
    detail::render12 = &render12;
    detail::resize12 = &resize12;
    detail::probe12 = &probe12;
}

}} // namespace dbce::overlay
