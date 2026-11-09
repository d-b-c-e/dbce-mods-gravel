# Shared F6 panel for native mods

Internal hosts may call `SetStatusLine(text, StatusTone)` from a worker thread;
`ClearStatusLine()` removes it. The default is hidden. Text is copied into a
255-byte UTF-8 buffer. Use the existing `SessionStatusPresentation` wording and
report Saved only after a complete sealed save; the host owns expiry. This is
a passive banner on the center display, rendered by the existing D3D11/D3D12
path even when F6 is closed. It never takes input or draws a closed-panel
cursor. It adds no recorder, hotkey, config switch, force path or public UI.

Help draws the host's `drawPage(PAGE_HELP, advanced)` content before the small
repository text links. A host can place its separate issue/report action there;
the shared renderer owns the link ordering and styling. This does not change
report preparation, browser admission, input handling or overlay behavior.

The standard **Wheel settings** panel (STD-004, STD-017, STD-022, STD-030, STD-053) for mods that
are native DLLs (DirectInput proxies and similar), drawn with Dear ImGui (`native/third_party/imgui`, vendored, MIT).

| File | What it does |
|---|---|
| `dbce_panel.h/.cpp` | Frame and widgets: title, `View: [Simple] [Advanced]`, Stop FFB, Close, flat tabs in the standard order (Controls, FFB, Cameras, Telemetry, Help; empty pages hidden, Help always), Off/On and choice segments, percent sliders with Default, status rows, bounded groups, compact right-aligned commands, a "Saved" note. Opaque dark panel, one accent; 8/16/32 grid and 16/14/20 text, scaled with the window height (1080 = 100%, never below 100%) times the Help page's UI scale. On a three-screen span it sits on the centre screen (STD-019). Also `Slider` with its own unit (degrees). |
| `dbce_frame_stats.hpp` | STD-023 frame rate: the overlay calls `NoteFrame()` on every present of the game's swap chain; ten-second windows (average fps, 1% low, worst frame) shown under the Cameras page (Help when there is none) and logged every 30 s, with `Config::displayText` (the mod's own words for the display, for example a planar span) or else `Config::triplesOn` when set. Pure statistics tested by `test_frame_stats.cpp`. STD-024: beside it, **Show frame rate on screen** (`[Panel] ShowFrameRate`, off by default) draws a smoothed fps and the 1% low in the top-right of the centre screen (centre third of a span at least 2.5:1) on every frame, panel open or closed, through the foreground draw list: no window, no input. Nothing to call beyond `Configure`. |
| `dbce_panel_input.h/.cpp` | Input without replacing the window procedure: a thread-local `WH_GETMESSAGE` hook on the game window's thread. F6 toggles, F8 stops force feedback, Esc closes; while open, keyboard, mouse and raw input go to the panel (Alt+Tab, Alt+F4, Shift+Tab and Windows keys excepted, so the Steam overlay's Shift+Tab still opens over an open panel). Events are queued and drained on the render thread. `DetachInput()` lets go of the window (a host that stops drawing, such as `StopD3D9`), from any thread: the panel closes and F6 and every message reach the game until `AttachInput` again; a message already being handled finishes first (the hook checks its window and handles a message under the same lock). |
| `dbce_overlay_d3d11.h/.cpp` | D3D11 overlay: patches the DXGI swap-chain `Present`/`Present1`/`ResizeBuffers` table entries (found on a temporary device on a hidden window, from a worker thread), adopts the largest swap chain's window (a new chain on that same window, the game recreating it, replaces the old one), draws only while the panel is open and restores the game's render targets, holding no view of the back buffer past its Present (one would keep a released swap chain alive and block its flip-model replacement). The back-buffer format is logged once when it changes; on an `_SRGB` or float format the panel's sRGB colours are linearized before the render-target write so its contrast is correct (the washed-out panel found on a single-screen fullscreen window in art of rally). The panel's input hook is on only while the adopted chain can be drawn on: a chain it cannot draw on (neither D3D11 nor a D3D12 one the enabled renderer can start on) closes the panel and leaves the window's input to the game, and is looked at again every 30th Present, so a D3D12 renderer that starts later gets the input back. D3D12 swap chains are drawn by the optional D3D12 renderer when enabled. |
| `dbce_overlay_d3d12.h/.cpp` | Optional D3D12 support (`EnableD3D12()` before `StartD3D11`): the shared present hook draws D3D12 swap chains with the game's own present queue, captured from `ID3D12CommandQueue::ExecuteCommandLists` (patched on a probe queue); per back buffer an allocator, RTV and fence value; ResizeBuffers waits for the GPU and releases the buffers. Checked offline on WARP (`test_overlay_d3d12.cpp`: the real hooks, a flip-model swap chain, the panel read back from the presented buffer). |
| `dbce_overlay_d3d9.h/.cpp` | D3D9 overlay: `StartD3D9(device, log)` with the game's own `IDirect3DDevice9`, since D3D9 gives every device its own method table (on the heap) and a probe device cannot reach the game's. Replaces that device's Present and Reset entries with the shared loop-safe slot hook, attaches input to the device window on the first Present, draws only while open on the back buffer inside its own BeginScene/EndScene when the device is not lost, and puts back the game's render targets, depth surface and viewport. Reset releases ImGui's default-pool objects first. A pure device is used only when it answers the Get* calls the restore needs (viewport, world/view/projection, a render state), as the Windows 10/11 runtime does against the documented rule; otherwise refused. A new device releases the previous one's ImGui objects and reference at once; `StopD3D9(device)`, before the game releases its device, closes the panel, gives the window's input back to the game and lets go of the device; the next adopted Present takes input again. Not covered: D3D9Ex PresentEx/ResetEx and `IDirect3DSwapChain9::Present`; a recreated device needs another call. Also link `dbce_overlay_d3d11.cpp` and `imgui_impl_dx11.cpp` (the shared hook core). Checked offline on x64 and x86 with the GPU's D3D9 device on a hidden window (`test_overlay_d3d9.cpp`: closed/open/closed pixels, game state kept, Reset at a new size). |
| `dbce_panel_descriptor.h/.cpp`, `samples/` | UI-1: the settings descriptor and its strict reader (see "Pages from a settings descriptor" below). `samples/` holds descriptors written from GRID's and art of rally's current pages; the tests draw them. |
| `test_panel_render.cpp`, `build_test.ps1` | Offline render check on WARP: every hand-drawn page in Simple (and Advanced at 2560x1440) at 1280x720, 1920x1080, 2560x1440, 3840x2160 and 7680x1440, and every page of both sample descriptors (More options closed and open, and the Developer tab) at 1280x720, 1920x1080 and 7680x1440, plus toasts. PNGs in `build\` for visual review, with geometry checks: inside the window, on the centre screen of a span, every row drawn without overlap, and every page without More options fits with no scrolling. Mouse clicks check the events (a toggle, More options, a binding capture, an action). `test_panel_descriptor.cpp` (x64 and x86, CPU only) parses the samples and checks that unknown kinds, levels, pages and fields, and over-full pages, are refused. |

## Pages from a settings descriptor (UI-1)

Set `Config::descriptor` and the panel draws the standard pages itself instead of calling `drawPage`. The spec is
`docs/architecture/2026-11/05-ux-simplification.md` section 1 in the hub (`dbce-project-mgmt`).

- **Pages**, in this order: Controls, Force feedback, Display, Cameras, Telemetry, Help. A page without rows is hidden.
  Help always shows, and so does Display, which carries the frame rate. A **Developer** tab follows Help only while
  `%LOCALAPPDATA%\dbce\developer.on` exists (`SetDeveloper` in tests); it lists every developer-level row.
- **Rows** (`dbce_panel_descriptor.h`): `id`, `kind` (toggle, choice, percent, slider, number, binding, action,
  status), `label`, `help` (12 words at most), `level` (simple; advanced, shown under the page's **More options** row;
  developer), `apply` (live, or next-start, which shows "Takes effect next time you start the game." after a change),
  `choices`, `min`/`max`/`default`, `unit`, `decimals`, `text`. At most five simple controls and two simple status
  lines per page. Anything else is refused by `ParseDescriptor`/`LoadDescriptor`.
- **The panel's own rows:** the frame-rate status line and Show frame rate (Display), the version and keys lines, the
  Mod page link (`modPage`) and Text size (Help). The header is the product name and x; the footer reads "F6 close ·
  F8 force feedback off" with "Saved" for two seconds.
- **The mod's side**, on its own thread: push values with `SetToggle`, `SetChoice`, `SetNumber` and `SetText` (status
  lines and a binding's current input), hide rows that don't apply with `SetRowVisible`, and read the player's changes
  and actions with `PollEvent`. The mod saves its own config (STD-044) and reports the result with `NoteSaved`.
  A binding click sends `Capture`, and the row shows its help as the prompt until `SetText`; Esc cancels the capture
  first and closes the panel second.
- **Toasts and labels:** `ShowToast(line1, line2)` draws up to two lines for 3 s at the bottom of the centre screen,
  panel open or closed (camera name, the F7 view, notices). The frame-rate counter is unchanged. Overlays draw a
  closed frame when `DrawWanted()`.
- **Navigation (UI-2):** `Navigate(Nav)` takes Up, Down, Left, Right, Select, Back, Open and Close, from any thread,
  and the input hook sends the arrows, Enter and Space there too, so the keyboard and the wheel move the same focus.
  Up/Down go between the tab row and the page's controls (More options and the panel's own rows included); Left/Right
  set Off/On, step a choice or a slider (5 for percent, 1 for a slider, one decimal place for a number) or, on the tab
  row, change the page; Select flips, steps, presses an action, starts a binding capture or opens More options; Back
  is Esc. Open, Close and Back apply at once, the others one per drawn frame. The focused control has an accent box once
  a command was used; `NavFocus` reports its id. Changes made this way are reported by `PollEvent` like clicks.
  `Configure` may now run while an overlay draws (`dbce_ui` configures after its hooks are in).
- **Dev channel:** `open <page> [more]` takes a page id or name (`open telemetry more`).
- The descriptor (and its strings) must outlive the panel. A mod that reads one also compiles
  `dbce_panel_descriptor.cpp`; the hand-drawn pages need nothing new.

No game uses descriptor pages yet; the spike (UI-4 onward) moves art and GRID onto them. Non-native mods get the same
panel through `native/ui-host` (`dbce_ui.dll`).

## Using it in a mod

1. Vendor `native/panel` and `native/third_party/imgui` into the mod (as with `forza_packet.h`), compile the panel
   files, `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp` and `backends/imgui_impl_dx11.cpp`.
2. At initialization (not in `DllMain`): fill `dbce::panel::Config` (pages the mod offers, `drawPage`, `ffbOn` and
   `stopFfb` when it can stop forces, the canonical ini path), call `Configure`, then `dbce::overlay::StartD3D11(log)`.
   A D3D9 game instead calls `dbce::overlay::StartD3D9(device, log)` once its device exists, and also compiles
   `dbce_overlay_d3d9.cpp` and `backends/imgui_impl_dx9.cpp`.
3. Draw each page with the panel widgets; save settings to the mod's own ini (Wheelkit reads the same file).
4. While `dbce::panel::IsOpen()`, suppress driving input the mod can see (DirectInput buttons, keyboard, mouse).

### Help repository links

Set `Config::modRepositoryUrl` and `Config::wheelkitRepositoryUrl` to the mod and
Wheelkit repository URLs. Help renders each as a small text hyperlink using the
existing secondary font and Dear ImGui's text-link widget. Empty or null values
omit a link, preserving existing callers. These links have no filled button or
full-row hit area; opening a URL requires an explicit click. The existing mod
Help callback and any issue/report action remain separate.

The descriptor strings must outlive the panel, as with `modName`. Rebuild and
vendor the header and implementation together; do not mix compiled Config layouts.
The existing offline panel-render fixture supplies both URLs for its Help frames,
without clicking them. On October 16 the first ADRIFT report consumer compiled
this exact renderer/ordering with its existing DLL target: no reported warnings
or errors, sole Lua export verified. DLL SHA256
`BCADDFEE4A1095D524A7B1CF9CFB5698FCCC5A966A6B5FACBA9A74C75A0E58A1`;
original log `E:/Source/games/.worktrees/mako-adrift-native-report-20261016/build/native-report-20261016.log`.
The fixture and rendered/browser checks were not run. The link renderer adds no
report generator, uploader or issue submission; hosts adopt `native/support`
separately for reviewed report preparation and data-only browser drafts.

## Limits (2026-10-08)

Rendered offline only; not yet seen in a game. No binding capture, wheel/pad navigation of the panel or Settings button
yet. XInput pads are not suppressed while the panel is open. The D3D9 overlay (2026-10-23) is checked offline only;
no D3D9 game uses it yet.

## Living with the Steam overlay (2026-10-09)

Steam's overlay can take our Present hook as its "original". It patches the first bytes of dxgi's Present (5 bytes,
`e9` to a relay), puts dxgi's entry back in the vtable slot, and keeps what it found in the slot (ours) as the function
it calls next. Ours then called dxgi's entry, which jumped back to Steam, until the stack ran out. This killed V-Rally 4
on its first frame; the CrashRpt dumps are in `_archive\2026-10-09\vr4-crash-*`.

- **Installed builds (fb90c22, 80f2456):** each slot hook counts its nesting per thread. On the first re-entry it calls
  the system function with the module file's original bytes put back for that call. Only V-Rally 4 takes this path
  today; the other six panel games show no loop.
- **Review (Astra, hcom 2916/2948, inbox 2026-10-09-astra-overlay-3e7ed72-followup.md):**
  - rewriting live code is not safe against other threads;
  - a restore can overwrite a newer patch;
  - a code copy rebuilt later from disk is not bound to the loaded build;
  - skipping a Present or ExecuteCommandLists is not a valid fallback.

  3e7ed72 (copy from disk with a jump back, no live writes) answers the first two. It is committed but **not
  installed anywhere**.
- **Agreed next version (not started):**
  - relocate instructions with Microsoft Detours;
  - copy them from the live code at install, only when it matches the module file and its headers;
  - otherwise, or if the copy cannot be made, do not install the hook at all;
  - D3D11 first; the D3D12 queue hook stays out unless forwarding is guaranteed;
  - tests count real forwarding calls.
