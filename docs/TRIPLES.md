# Triple screens (Gravel, UE 4.17)

2026-10-06, Claude. `src/triple.cpp`, part of the same `dinput8.dll` as the wheel fix and telemetry.

## What it does

Three projected views in one window: the centre screen shows the game camera, the side screens
continue it, turned by the rig's panel angle with an off-axis projection (STD-007). Menus,
loading screens and the HUD sit on the centre screen; the side screens are black outside a race
(STD-019). It turns on by itself when the display is three screens wide (`[triple] mode=auto` in
`milestone_mod.ini`):

- **three separate monitors** side by side (the "Sim Racing" layout, STD-015): one borderless
  window across them;
- **Surround / Eyefinity** (one display at least 4:1): the game's window at the desktop size.

On a single screen nothing is patched and the game runs stock. A plain Steam launch is enough
(STD-013). The game never changes a display mode on a triple layout (STD-008).

## How

Unreal builds a fake stereo device when its command line has `-emulatestereo`, then renders each
frame as several views of one scene. The mod is a static import of the game, so `tripleAttach`
runs from `DllMain` before the game's entry point and only writes memory:

1. **Command line.** The exe's `GetCommandLineW` import answers with `-emulatestereo` appended
   (separate monitors also get `-windowed -ResX=<span> -ResY=<height>`).
2. **Three views.** `UGameViewportClient::Draw` renders 2 views in stereo, or 3 when the view
   family's monoscopic far field is on, which 4.17 allows only on mobile feature levels. Two bytes
   (`xor ecx,ecx` -> `mov cl,1`) make it 3 whenever stereo is on; the family keeps the far field
   off, so the renderer treats the third view as an ordinary view.
3. **Placement.** Three slots of the fake device's vtable (in the exe's `.rdata`, patched before
   the device exists): `AdjustViewRect` (thirds), `CalculateStereoViewOffset` (side views turn by
   their panel's yaw, no eye offset; the caller reads the rotation back from memory) and
   `GetStereoProjectionMatrix` (Kooima off-axis frustum per panel, the toolkit's TripleGeometry).
   Passes: 1 (view 0, the player's view state) = centre, 2 = right, 3 = left.
4. **Field of view.** `fov=game` (default): the eye distance comes from the game camera's
   horizontal FOV, which the engine passes to `GetStereoProjectionMatrix` (the stock fake device
   ignores it), so the centre shows what the game shows on one screen and the sides continue it
   at the rig's panel angle. `fov=rig` uses the measured rig (eye distance or vertical FOV).
5. **UI on the centre.** All UMG/Slate UI lives under `SGameLayerManager`'s `SDPIScaler`; its
   `OnArrangeChildren` (vtable slot 64) gets the window geometry cut to the centre third, so the
   UI lays out as on one 2560x1440 screen.
6. **Menus centre-only.** Outside gameplay (the mod is not reading a live vehicle HUD, see
   `ue4.cpp`) the side views shrink to 4x4 corners. `UGameViewportClient::Draw` clears what no
   view covers, except in stereo ("the HMD clears"); one `je` -> `jmp` makes it clear in stereo too.
   In a race the three thirds cover the window exactly and the clear never runs.
7. **Display requests.** `FSystemResolution::RequestResolutionChange` (saved settings, the video
   menu, Alt+Enter, `r.SetRes`) is inline-hooked: on separate monitors every request becomes the
   span size, windowed, and a small thread keeps that window borderless at the span without
   activating it; on Surround it becomes the desktop size, with exclusive fullscreen turned into
   windowed fullscreen. The game also asks DXGI for exclusive fullscreen at start and at every
   race load, which on separate monitors moved the window onto the primary monitor and left a
   2560-wide back buffer: the game's DXGI factory is wrapped so swap chains are created windowed
   and `SetFullscreenState(TRUE)` is refused but reported as granted (`GetFullscreenState` answers
   what the engine asked for), keeping the engine's state consistent. `ChangeDisplaySettings`
   mode changes are refused too.

Every code pattern must match exactly once in the running exe, or nothing is patched and
`milestone_mod.log` says which (`[triple]` lines).

## Camera keys (STD-005/006)

In a race on a triple layout, with the game in front: numpad 8/2 forward/back, 9/3 up/down, 4/6 left/right,
7/1 tilt down/up, +/- field of view, 5 reset. Steps 0.02 m, 1 deg, 2 deg (`[camera]` `move_step_m`,
`tilt_step_deg`, `fov_step_deg`). The offsets move the eye of all three views together (in the camera's own axes)
and are saved in `[camera]` 2 s after the last change. They apply to whichever camera is active. For unattended
tests the same actions can be written to `dbce-camera-cmd.txt` beside the exe (`forward 10`, `tiltdown 5`,
`wider 5`, `reset`; consumed and deleted). Seen 2026-10-06 06:14: forward 20 cm, tilt 5 deg and FOV +10 moved all
three views together; reset returned to stock.

## Settings (`[triple]` in milestone_mod.ini)

| key | default | |
|---|---|---|
| `mode` | `auto` | `auto`, `on`, `off` |
| `fov` | `game` | `game` (centre = the game camera) or `rig` |
| `panel_width_mm`, `panel_height_mm` | 708.4, 398.5 | one panel's visible size (32" 16:9) |
| `side_angle` | 70 | side panel angle, degrees |
| `bezel_mm` | 8 | gap between panels |
| `eye_distance_mm`, `vertical_fov` | 0, 58.715 | `fov=rig` only |
| `menu_sides_black` | 1 | side screens black outside a race |
| `ui_centre` | 1 | menus and HUD on the centre screen |

## Seen at the rig (unattended, "Sim Racing", 2026-10-06 03:55-05:35)

- Title, main-menu garage and an offline Cross Country race (Alaska, a championship event): three projected views at 7680x1440 in a race; in the cockpit the A-pillars and side
  windows are on the side screens, the bonnet on the centre, chevron boards and checkpoint posts
  continue across the bezels. HUD (position, completion, race time, minimap, speedo) and the pause
  menu on the centre screen; front-end menus on the centre with black sides. Virtual-pad throttle.
- Every run: owner saves (`%LOCALAPPDATA%\Gravel\Saved\SaveGames`; the game rewrites
  `settings.sav`) backed up first and restored exactly after; the installed mod restored; display
  watchdog running; game-window captures only (`build/triple-test-*`, ignored).

Installed 2026-10-06 05:50 with the packaged installer (`167b6cc`, dinput8.dll `ecddaafb`); a plain Steam
launch then came up in triple mode with the menu on the centre screen and black sides.

**Surround (05:58-06:01):** profile `Sim Racing Surround` applied through the switcher CLI (verified, 35 s settle),
the installed build from a plain Steam launch: race in three projected views with the HUD on the centre, front-end
menus centre-only; the exclusive-fullscreen request was refused there too (windowed fullscreen at 7680x1440). Back
on `Sim Racing` afterwards (verified).

## Open

- Owner check at the rig: side angle and FOV feel, chase cameras, brightness of the side views
  (each view has its own exposure history), whether loading-screen transitions flash.
- Fades and full-screen UI effects that span the window are drawn on the centre only.
