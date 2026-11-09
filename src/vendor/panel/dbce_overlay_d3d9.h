// D3D9 overlay for the shared F6 panel: draws dbce::panel on the game's own IDirect3DDevice9.
//
// D3D9 gives every device its own copy of the method table, on the heap (checked on Windows 11, x86 and x64), so a probe
// device cannot reach the game's the way the shared DXGI table does for D3D11: the mod passes the game's device once it
// exists. Its Present and Reset entries are replaced in that device's table with the same loop-safe slot hook as D3D11
// (dbce_overlay_internal.h); no code bytes are written. The first Present attaches panel input to the device's window.
// Present draws only while the panel is open (or the status line is shown) and the device is not lost: between its own
// BeginScene/EndScene, on the back buffer, then the game's render targets, depth surface and viewport are put back
// (ImGui's state block restores the rest). Reset releases ImGui's default-pool objects first, so the game's Reset can
// succeed; they are recreated on the next drawn frame. A pure device (D3DCREATE_PUREDEVICE) is used when it answers
// Get* calls, as the Windows 10/11 runtime does (checked on x86 and x64), and refused when it does not.
// ImGui's backend holds one reference to the device it draws on until StartD3D9 is given another one or StopD3D9 lets
// go of it, which a game that frees its device (before creating the next, in exclusive fullscreen) needs first.
// Not covered: PresentEx/ResetEx (D3D9Ex) and IDirect3DSwapChain9::Present; a recreated device needs another call.
// Also link dbce_overlay_d3d11.cpp and imgui_impl_dx11.cpp: the shared hook core lives there, as for D3D12.
#pragma once

struct IDirect3DDevice9;

namespace dbce { namespace overlay {

// On the thread that renders with the device (it is asked for its state), not from DllMain. False (logged) for a null
// device, a pure device that cannot read its state back, or when its entries could not be replaced. Calling it again
// for the same device changes nothing; a new device (the game recreated it) becomes the one drawn on, and the previous
// device's ImGui objects and reference are released at once.
bool StartD3D9(IDirect3DDevice9 *device, void (*log)(const char *line));

// Just before the game releases the device StartD3D9 was given, on the thread that releases it, while nothing else uses
// the device (the game's own teardown window): the panel closes and lets go of the window's input (F6 and every key
// reach the game; a message already being handled finishes first), ImGui's objects on the device and its reference go,
// and nothing is drawn on it again; its entries stay ours and only forward. That Release is never the device's last
// (the game still holds its own), and nothing here calls Reset, CreateDevice or TestCooperativeLevel, which D3D9 keeps
// on the window's thread. Another device, or none, changes nothing. StartD3D9 with the next device resumes.
void StopD3D9(IDirect3DDevice9 *device);

}} // namespace dbce::overlay
