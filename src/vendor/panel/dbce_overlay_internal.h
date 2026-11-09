// Private link between the overlay's shared DXGI present hook (dbce_overlay_d3d11.cpp) and its optional D3D12 renderer
// (dbce_overlay_d3d12.cpp, enabled with EnableD3D12). Not for mods to include.
#pragma once
#include <windows.h>
#include <dxgi.h>

namespace dbce { namespace overlay { namespace detail {

extern void (*g_log)(const char *); // set by StartD3D11/StartD3D9
void logf(const char *fmt, ...);
void ensureImGui();                 // one ImGui context for the process, whichever renderer draws it
void drawFrame(int w, int h);       // after the renderer's NewFrame: size, time, queued input, the panel, ImGui::Render
// Set by EnableD3D12(); null for D3D11-only mods.
// On a D3D12 swap chain: start the renderer when needed and, when draw is set, draw the panel. False: not a D3D12 chain,
// or its renderer cannot start (the panel then has no input on it); true: drawable (this frame may still be skipped).
extern bool (*render12)(IDXGISwapChain *sc, bool draw);
extern void (*resize12)(IDXGISwapChain *sc);    // before ResizeBuffers: release back-buffer references
extern void (*probe12)();                       // from the overlay worker: install the D3D12 queue capture
extern void (*before12)(IDXGISwapChain *sc);    // every Present of the adopted chain, before the panel (SetBeforePanelD3D12)

// A hook on a shared vtable slot that survives another overlay taking ours as its "original". Steam's overlay patches
// the first bytes of the system function (dxgi's Present: a 5-byte jump) and, when the game creates its swap chain
// after ours is in the slot, keeps our hook as what it calls next, while ours calls the system function, which jumps
// to Steam: the two call each other until the stack overflows (V-Rally 4, 2026-10-09: ~12,800 nested Steam and dbce
// frames in the dumps). A hook entered again on the same thread is that loop. From then on ours calls the system
// function through a trampoline: its first instructions copied from the module file (at least 16 bytes on x64, 8 on
// x86, so the usual patch sizes are covered; only plain prologue instructions, no relative addressing) followed by a
// jump to the rest of it in place. No live code is ever written, so other threads running that code are unaffected.
// The chain becomes game -> other overlay -> ours -> system. Before each such call the bytes after the copied ones are
// checked against the file; if another patch reaches past them, or the loop goes on even so, the nested call is
// skipped rather than overflow. Without another overlay, or with one that does not take ours as its original, nothing
// changes.
struct SlotHook {
    const char *name;
    void *volatile next;          // what the hook calls until a loop is seen: the slot's previous value
    void *system;                 // the slot's value before anyone patched it (null when unknown)
    volatile long looped;         // set once the loop was seen and the fields below are ready
    volatile long preparing, skipNoted, tailNoted;
    bool fileOk;                  // original[] holds the module file's bytes
    unsigned char original[32];   // the system function's first bytes as in the module file
    void *trampoline;             // the copy of its first instructions + jump back (null: not built)
    int copied;                   // how many bytes the trampoline copied
};
bool PatchSlot(void **vt, int index, void *hook, SlotHook &h);   // installs the hook; resolves h.system
bool Reentry(SlotHook &h, int depth);   // first thing in each hook (depth before this call): false = skip this call
void *SystemFunction(SlotHook &h);      // once h.looped: what to call instead of h.next (null: skip the call)
struct Depth {                  // per-thread nesting count for one hook
    int &d;
    explicit Depth(int &x) : d(x) { ++d; }
    ~Depth() { --d; }
    Depth(const Depth &) = delete;
    Depth &operator=(const Depth &) = delete;
};

}}} // namespace dbce::overlay::detail
