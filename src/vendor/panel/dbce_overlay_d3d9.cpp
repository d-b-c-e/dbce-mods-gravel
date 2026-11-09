// See dbce_overlay_d3d9.h.
#include "dbce_overlay_d3d9.h"
#include "dbce_overlay_internal.h"
#include "dbce_panel.h"
#include "dbce_panel_input.h"
#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/backends/imgui_impl_dx9.h"
#include <windows.h>
#include <d3d9.h>

namespace dbce { namespace overlay {

namespace {
using detail::logf;
typedef HRESULT(STDMETHODCALLTYPE *PresentFn)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT(STDMETHODCALLTYPE *ResetFn)(IDirect3DDevice9 *, D3DPRESENT_PARAMETERS *);
detail::SlotHook g_reset = {"D3D9 Reset"}, g_present = {"D3D9 Present"};

CRITICAL_SECTION g_cs;              // the state below: StartD3D9, Present and Reset can come from different threads
IDirect3DDevice9 *g_target;         // the device last passed to StartD3D9, the only one drawn on; not owned
IDirect3DDevice9 *g_drawn;          // the device ImGui's DX9 backend was started on (the backend holds a reference)
bool g_backend, g_adopted;

// One device table entry. The first value found is what the hook calls next (d3d9's own function unless something
// replaced it first); a later device's table must hold that same value, since one hook serves every patched table.
bool patch(void **vt, int index, void *hook, detail::SlotHook &h)
{
    void *now = vt[index];
    if (now == hook) return true;
    if (h.next && now != h.next) {
        logf("overlay: %s in device table %p is not the function hooked before; not replaced", h.name, (void *)vt);
        return false;
    }
    if (!h.next) {
        HMODULE m = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)now, &m);
        h.system = m && m == GetModuleHandleW(L"d3d9.dll") ? now : nullptr;
        char by[MAX_PATH] = "memory outside any module";   // x86: Windows' apphelp.dll replaces Reset in every device table
        if (m) GetModuleFileNameA(m, by, MAX_PATH);
        if (!h.system) logf("overlay: %s was already replaced by %s; ours runs first, then it", h.name, by);
        h.next = now;
    }
    DWORD old;
    if (!VirtualProtect(&vt[index], sizeof(void *), PAGE_READWRITE, &old)) return false;
    vt[index] = hook;
    VirtualProtect(&vt[index], sizeof(void *), old, &old);
    return true;
}

HWND windowOf(IDirect3DDevice9 *dev, HWND override)
{
    if (override) return override;
    HWND w = nullptr;
    IDirect3DSwapChain9 *sc = nullptr;
    if (SUCCEEDED(dev->GetSwapChain(0, &sc)) && sc) {
        D3DPRESENT_PARAMETERS pp;
        if (SUCCEEDED(sc->GetPresentParameters(&pp))) w = pp.hDeviceWindow;
        sc->Release();
    }
    D3DDEVICE_CREATION_PARAMETERS cp;
    if (!w && SUCCEEDED(dev->GetCreationParameters(&cp))) w = cp.hFocusWindow;
    return w;
}

// ImGui's objects on the drawn device and its reference to it go (under g_cs).
void letGo()
{
    if (g_backend) { ImGui_ImplDX9_Shutdown(); g_backend = false; }
    g_drawn = nullptr;
}

void draw(IDirect3DDevice9 *dev)
{
    if (dev != g_drawn) {   // first frame, or the game recreated its device
        letGo();
        detail::ensureImGui();
        g_backend = ImGui_ImplDX9_Init(dev);
        g_drawn = dev;
        if (!g_backend) logf("overlay: ImGui DX9 backend could not start");
    }
    if (!g_backend) return;
    IDirect3DSurface9 *back = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) return;
    D3DSURFACE_DESC d;
    back->GetDesc(&d);
    const int w = (int)d.Width, h = (int)d.Height;
    if (dbce::panel::Prepare(w, h)) { dbce::panel::BuildFonts(); ImGui_ImplDX9_InvalidateDeviceObjects(); }
    ImGui_ImplDX9_NewFrame();
    detail::drawFrame(w, h);
    // Draw on the back buffer, then give the game its targets back; setting target 0 resets the viewport, so it goes last.
    IDirect3DSurface9 *saved[4] = {}, *depth = nullptr;
    for (DWORD i = 0; i < 4; ++i) if (FAILED(dev->GetRenderTarget(i, &saved[i]))) saved[i] = nullptr;
    if (FAILED(dev->GetDepthStencilSurface(&depth))) depth = nullptr;
    D3DVIEWPORT9 vp;
    const bool haveVp = SUCCEEDED(dev->GetViewport(&vp));
    dev->SetRenderTarget(0, back);
    for (DWORD i = 1; i < 4; ++i) if (saved[i]) dev->SetRenderTarget(i, nullptr);
    dev->SetDepthStencilSurface(nullptr);
    if (SUCCEEDED(dev->BeginScene())) { ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData()); dev->EndScene(); }
    for (DWORD i = 0; i < 4; ++i) if (saved[i]) { dev->SetRenderTarget(i, saved[i]); saved[i]->Release(); }
    dev->SetDepthStencilSurface(depth);
    if (depth) depth->Release();
    if (haveVp) dev->SetViewport(&vp);
    back->Release();
}

void render(IDirect3DDevice9 *dev, HWND override)
{
    // All of it under the host lock, so StopD3D9 on another thread cannot interleave with any of it (the lock is
    // uncontended on every other frame).
    EnterCriticalSection(&g_cs);
    if (dev == g_target) {
        dbce::panel::NoteFrame();
        dbce::panel::PollDev();
        if (!g_adopted) {
            g_adopted = true;
            HWND w = windowOf(dev, override);
            const bool hooked = w && dbce::panel::AttachInput(w);
            logf("overlay: drawing on D3D9 device %p, window %p%s", (void *)dev, (void *)w, hooked ? "; F6 input attached" : "; input hook FAILED");
        }
        // A lost device (or one waiting for Reset) draws nothing until the game has reset it.
        if (dbce::panel::DrawWanted() && dev->TestCooperativeLevel() == D3D_OK) draw(dev);
    }
    LeaveCriticalSection(&g_cs);
}

// As for D3D11: entered again on the same thread means another overlay's hook and ours are calling each other (see
// detail::SlotHook); from then on ours calls d3d9's function, and only the outermost call does the panel's work.
HRESULT STDMETHODCALLTYPE hkPresent(IDirect3DDevice9 *dev, const RECT *src, const RECT *dst, HWND w, const RGNDATA *dirty)
{
    static thread_local int depth;
    if (!detail::Reentry(g_present, depth)) return D3D_OK;
    detail::Depth in(depth);
    if (depth == 1) render(dev, w);
    if (g_present.looped) { auto fn = (PresentFn)detail::SystemFunction(g_present); return fn ? fn(dev, src, dst, w, dirty) : D3D_OK; }
    return ((PresentFn)g_present.next)(dev, src, dst, w, dirty);
}
HRESULT STDMETHODCALLTYPE hkReset(IDirect3DDevice9 *dev, D3DPRESENT_PARAMETERS *pp)
{
    static thread_local int depth;
    if (!detail::Reentry(g_reset, depth)) return D3DERR_INVALIDCALL;
    detail::Depth in(depth);
    if (depth == 1) {
        // ImGui's default-pool buffers and font texture must go first, or the game's Reset fails.
        EnterCriticalSection(&g_cs);
        if (dev == g_drawn && g_backend) ImGui_ImplDX9_InvalidateDeviceObjects();
        LeaveCriticalSection(&g_cs);
    }
    if (g_reset.looped) { auto fn = (ResetFn)detail::SystemFunction(g_reset); return fn ? fn(dev, pp) : D3DERR_INVALIDCALL; }
    return ((ResetFn)g_reset.next)(dev, pp);
}

INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
BOOL CALLBACK initOnce(PINIT_ONCE, PVOID, PVOID *) { InitializeCriticalSection(&g_cs); return TRUE; }
} // namespace

bool StartD3D9(IDirect3DDevice9 *device, void (*log)(const char *))
{
    InitOnceExecuteOnce(&g_once, initOnce, nullptr, nullptr);
    detail::g_log = log;
    if (!device) { logf("overlay: no D3D9 device given; no panel"); return false; }
    // Drawing restores the game's state from Get* calls (the viewport here, the three transforms in ImGui's backend) and
    // ImGui's state block. A pure device may refuse Get* (the documented rule); the Windows 10/11 runtime answers them,
    // so the device is asked, without changing anything.
    D3DDEVICE_CREATION_PARAMETERS cp;
    if (FAILED(device->GetCreationParameters(&cp))) {
        logf("overlay: D3D9 device %p did not report how it was created; no panel", (void *)device);
        return false;
    }
    D3DVIEWPORT9 vp;
    D3DMATRIX m;
    DWORD v;
    if ((cp.BehaviorFlags & D3DCREATE_PUREDEVICE) &&
        (FAILED(device->GetViewport(&vp)) || FAILED(device->GetTransform(D3DTS_WORLD, &m)) || FAILED(device->GetTransform(D3DTS_VIEW, &m)) ||
         FAILED(device->GetTransform(D3DTS_PROJECTION, &m)) || FAILED(device->GetRenderState(D3DRS_CULLMODE, &v)))) {
        logf("overlay: D3D9 device %p is a pure device that cannot read its state back to restore it; no panel", (void *)device);
        return false;
    }
    void **vt = *(void ***)device;
    EnterCriticalSection(&g_cs);
    const bool ok = patch(vt, 16, (void *)&hkReset, g_reset) && patch(vt, 17, (void *)&hkPresent, g_present);
    if (ok && device != g_target) {
        letGo();   // the previous device's, now rather than on the next drawn frame
        g_target = device;
        g_adopted = false;
    }
    LeaveCriticalSection(&g_cs);
    logf(ok ? "overlay: D3D9 present hook installed on device %p (F6 opens the panel)" : "overlay: D3D9 present hook could not be installed on device %p", (void *)device);
    return ok;
}

void StopD3D9(IDirect3DDevice9 *device)
{
    InitOnceExecuteOnce(&g_once, initOnce, nullptr, nullptr);
    EnterCriticalSection(&g_cs);
    const bool current = device && device == g_target;
    if (current) {
        letGo();
        g_target = nullptr;
        g_adopted = false;
        // Nothing would show the panel: it closes and stops taking the game's input or F6 (an F6 already being handled
        // finishes first); the next adopted Present attaches again.
        dbce::panel::DetachInput();
    }
    LeaveCriticalSection(&g_cs);
    if (current) logf("overlay: let go of D3D9 device %p before the game releases it; no panel until the next device", (void *)device);
}

}} // namespace dbce::overlay
