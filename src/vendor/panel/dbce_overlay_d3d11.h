// D3D11 overlay for the shared F6 panel: draws dbce::panel on the game's own swap chain.
//
// A worker thread creates a temporary D3D11 device and swap chain on a hidden window to find the DXGI swap-chain
// method table, then patches its Present, Present1 and ResizeBuffers entries (the table is shared by every swap chain of
// that class, including the game's). The first presented frame attaches panel input to the swap chain's window; the
// panel is drawn only while it is open, with the game's render targets restored afterwards. A D3D12 swap chain (no
// D3D11 device) is left alone. Nothing is done from DllMain; call Start once from the mod's initialization.
#pragma once

struct IDXGISwapChain;
struct ID3D11Device;

namespace dbce { namespace overlay {

// Returns false when the worker could not be started; the outcome is logged.
bool StartD3D11(void (*log)(const char *line));

// Optional: called on the game's swap chain at every Present, before the panel draws, so a mod can post-process the
// frame (for example a triple-screen compositor) with the panel staying on top. Present's thread; keep it short.
// nullptr clears it. Not called for D3D12 chains.
void SetBeforePanelD3D11(void (*hook)(IDXGISwapChain *chain, ID3D11Device *device));
// Optional: called after the panel (or the frame counter / status line) has been drawn, on frames the overlay draws,
// so a mod can capture the finished frame (development frame dumps of the F6 UI). Not called on the usual closed frame.
void SetAfterPanelD3D11(void (*hook)(IDXGISwapChain *chain, ID3D11Device *device));

}} // namespace dbce::overlay
