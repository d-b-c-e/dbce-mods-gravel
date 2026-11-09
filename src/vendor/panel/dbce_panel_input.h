// Input for the shared F6 panel without replacing the game's window procedure: a thread-local WH_GETMESSAGE hook on the
// game window's thread sees each posted keyboard/mouse message. F6 toggles the panel, F8 stops force feedback (when the
// mod offers it), Esc closes. While the panel is open, keyboard, mouse and raw-input messages go to the panel instead
// of the game (UX-03), except Alt+Tab, Alt+F4, Shift+Tab (the Steam overlay's hotkey) and the Windows keys. Events are
// queued and handed to ImGui on the render
// thread by DrainInput, so ImGui is only touched from one thread.
#pragma once
#include <windows.h>

namespace dbce { namespace panel {

// Attach to the game window (idempotent per window; a new window moves the hook). Safe from any thread.
bool AttachInput(HWND gameWindow);
// Let go of the window (idempotent), from any thread: the panel closes, the hook goes, queued events are dropped, and
// F6, F8 and every other message reach the game again. It waits for a message already being handled, so none is
// handled after it returns. For a host that stops drawing; AttachInput takes a window again.
void DetachInput();
HWND InputWindow();
// Render thread, after the renderer's NewFrame and before ImGui::NewFrame: queued events into ImGui's IO, with mouse
// positions mapped from the window's client area to the back buffer.
void DrainInput(int backBufferWidth, int backBufferHeight);

// Development channel only: on resets/enables counters, snapshot reports, off
// disables/reports. Never changes input/panel state. Reports at most 8 times per
// process, through the supplied logger after releasing the input lock.
void TraceInput(const char *command, void (*log)(const char *));

}} // namespace dbce::panel
