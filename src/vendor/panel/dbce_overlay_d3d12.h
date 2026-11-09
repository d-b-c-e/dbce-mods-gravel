// D3D12 support for the shared F6 panel's overlay (dbce_overlay_d3d11.h): call EnableD3D12() before StartD3D11().
//
// The DXGI present hook is shared; for a D3D12 swap chain the panel is drawn with the game's own present queue, which
// is captured from ID3D12CommandQueue::ExecuteCommandLists (the queue table entry is patched on a temporary queue, as
// the swap-chain entries are). Per back buffer: a command allocator, a render-target view and a fence value, so a
// buffer is reused only after the GPU has finished with it; ResizeBuffers waits for the GPU and releases them first.
//
// Start it once the game's own D3D12 device exists: GRID (2019) crashed 3 s in when the probe device was created during
// its initialization (from the first DirectInput8Create); started 15 s later it works (2026-10-09). A UE4SS Lua mod
// (ADRIFT) is already late enough.
#pragma once

struct IDXGISwapChain;
struct ID3D12CommandQueue;

namespace dbce { namespace overlay {

void EnableD3D12();

// Optional: called at every Present of the adopted D3D12 chain, before the panel draws, with the game's present queue
// (the last direct queue of the chain's device). A mod records its own command list on that queue, e.g. a triple-screen
// compositor warping the back buffer; the panel stays on top. Not called until the game's queue has been seen. Off
// unless set; null turns it off.
void SetBeforePanelD3D12(void (*hook)(IDXGISwapChain *chain, ID3D12CommandQueue *queue));

// The direct queue a D3D12 swap chain was created with, found inside the chain object (DXGI keeps it as a plain
// pointer) and validated as that device's direct queue; null when none is found. No reference is added. The host
// uses it when its ExecuteCommandLists patch has seen no queue of the chain's device (ADRIFT: UE 5.3 never hits
// the patch); exposed for the offline check.
ID3D12CommandQueue *FindPresentQueue(IDXGISwapChain *chain);

}} // namespace dbce::overlay
