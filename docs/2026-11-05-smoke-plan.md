# Gravel installed-build smoke plan (offline draft, not authorized)

Prepared 2026-11-05 by kiri for kole request 539150. **Do not run this until kole
confirms with the owner**: the owner stopped Gravel live loops on November 1
(see `2026-11-01-mevu-handoff.md`). Nothing in this plan was executed. The game,
Steam, devices, displays and input were not touched while preparing it. The
bindings and hashes below were read from files only (route R0).

## What it proves

These are Ready-For-Test facts for the installed build, not camera, curve or FFB-feel acceptance:

1. The installed DLL loads and the wheel, telemetry, FMOD and UE4 channels go live in an offline race.
2. The mod's camera command path changes and saves offsets in game.
3. `probe_camera` returns candidate lists before and after one native camera switch. This is the input needed
   for camera identity (handoff step 2). Resolving identity is offline analysis afterwards, not a pass criterion here.
4. The game closes gracefully and every snapshotted file returns byte-exact.

## Installed identity (R0, read 2026-11-05)

| Item | Value |
|---|---|
| `dinput8.dll` | `E6DAA565E2A424CED280091902C3EDAE580BB24E826F999278AC6EDE2E4828FC`, remi a106620 dev build, file-only deploy 2026-11-03 22:08 |
| `milestone_mod.ini` | `FB2C615D20670B383A62E0C9BD8E32CE835D6135D1EE174AE90E32057F78C407` (Wheelkit rewrite 2026-11-04 20:22; `log=1`, telemetry on, port 8000, `[fmod] discover=1`) |
| `gravel-Win64-Shipping.exe` | `28032B882FFF6DAE07B185B93FD5989ADE09DC36C2EC070ECC1D05B3A73D2A95`, Steam build 3477925 |
| Receipt / backup | `milestone_install.json`; `E:/Source/_archive/2026-11-04/owner-before-gravel-040715330` |
| Last mod log | `milestone_mod.log` 2026-10-06, so this build has never run |

If any hash differs at preflight, stop and report it. Do not re-baseline silently.

## Wheel bindings available for R1 injection (MOZA R12, product 0006346e)

From `settings.sav` via `tools/dump_bindings.py --wheel` and the `[/Wheel.Config/0006346e]` section of
`gravel/Config/WindowsNoEditor/WheelConfig.ini`. Inject indexes are 0-based; `ButtonN` in WheelConfig is 1-based.

| Game action | Wheel slot | Physical | `inject raw button` index |
|---|---|---|---|
| Confirm | Wheel_FaceButton_Bottom | Button1 | 0 |
| Back | Wheel_FaceButton_Right | Button2 | 1 |
| SwitchCamera | Wheel_Special_Left | Button33 | 32 |
| Pause / Start | Wheel_Special_Right | **unbound** | none |
| Menu up/down/left/right | DPad slots bound to Look only | **unbound** | none |

So R1 can confirm, go back and switch camera, but it **cannot move menu focus or pause**. Menu focus movement and
Pause stay on R4 (Escape and E0 arrow keys through `tools/qualification/Invoke-GravelStep.ps1`, under lease with the
owner idle), or the owner drives the menus. Adding wheel bindings for them is an owner settings change; it is out of scope.

R4 keys need Gravel in front, and the elevated owner VS Code usually keeps the foreground. Owner decision 2026-11-06
(via kole, automated-testing SKILL.md section 4): after the ordinary `SetForegroundWindow` is refused, make **one**
`AttachThreadInput` + `SetForegroundWindow` attempt on the pinned UnrealWindow. Before attaching, validate
PID/birth/image/HWND/class and recheck the captured foreground pair, then validate again after. Send no synthesized
input during the attempt, always detach in `finally`, and record a structured result. If Gravel is still not in front,
the R4 rows end at activation: no other bypass, no F24 or global keys, no lock-timeout change, no minimize or restore.
R1 rows do not need focus. This is a separate delta that needs review in the run packet.

## Preflight (game closed)

1. kole GO, then `tools\Rig-Lease.ps1 -Enter -Name kiri -Purpose 'Gravel installed-build smoke'`. Run
   `pwsh E:\Source\dbce-project-mgmt\tools\Test-DesktopRoutes.ps1` and keep its JSON. Record the live
   `SPI_GETFOREGROUNDLOCKTIMEOUT` (read only; kole restored it to 200000 ms on 2026-11-06) in the receipt before and
   after the run. If it reads 2147483647 again, report the time and what ran just before to kole; never change it.
2. Census: no Gravel process and no pending Steam launch by any other agent. The November 1 run stalled on a foreign
   American Truck Simulator chooser, so ask kole to hold other Steam launches for this slot.
3. Recheck the identity table above. Snapshot the Win64 folder, game Config, `LocalAppData/Gravel/Saved`,
   `LocalAppData/dbce/gravel` and Steam userdata 558260 with `Save-GravelQualificationState` into a fresh
   `artifacts/rig-<timestamp>/state`.
4. The `dbce/gravel` switch folder must hold no stale `ffb-fence.on`, `inject.on`, `inject.txt` or recording request.
   Stage `ffb-fence.on` and `inject.on` for the slot. FFB stays off: the fence answers every effect in software.
5. Add `mirror_port=<free port>` under `[telemetry]` in the installed INI (snapshot restores it). Do not touch SimHub.

## Run (15-minute budget)

| # | Action | Route | Pass |
|---|---|---|---|
| 1 | Plain Steam launch of 558260; record exact PID, birth time, image path and UnrealWindow HWND | R0/R2 | one new shipping process; never answer a Steam or KickingOtherSession dialog |
| 2 | Capture the title screen by HWND | R2 | frame saved |
| 3 | Menu steps to the offline Cross Country event: one action at a time, capture after each | R1 Confirm/Back (`inject raw button 0 dev=<guid> value=1 ms=150`, `<guid>` = MOZA instance GUID from the preflight enumeration), R4 for focus moves | each frame inspected before the next step; stop at anything unexpected |
| 4 | Stationary in race, read 2 status lines from `milestone_mod.log` | R0 | `input:live`, `ffb:live` behind the fence, `fmod:live`, `ue4` RPM above 0 |
| 5 | `py -3 -I tools/forza_listen.py <mirror_port> --lines` for 10 s | R0 | 324-byte packets with non-zero RPM |
| 6 | `dbce-camera-cmd.txt`: `wider 3`, capture, `reset`, capture | R1 + R2 | log shows `[camera] command wider 3`, framing changes then returns, `[camera]` saved after 2 s |
| 7 | `dbce-camera-cmd.txt`: `probe_camera`; keep log | R1 | candidate list logged |
| 8 | `inject raw button 32 ... ms=150` (SwitchCamera), capture, then `probe_camera` again | R1 + R2 | view changed in the frame; second candidate list logged |
| 9 | Quit: Escape (R4), Back/Confirm through the quit prompts (R1), capture each step | R1/R4 | process exits by itself within 30 s |

Numpad FOV keys need the game in front (`gameInFront()`), so step 6 tests the shared offset path, not the key
direction from 25a70ad. Key direction stays an R4 check, or the owner checks it.

## Close and restore

- If step 9 fails: send one WM_CLOSE to the verified HWND. If the process is still alive after 30 s, apply the
  November 1 "force-kill of your own stuck test processes" rule. That means exact PID, birth time and image match,
  the fence still on, then stop and record it. Never stop by name, and never touch a process you did not launch.
- When the process is gone: `Restore-GravelQualificationState`, then verify the file set and hashes. Remove the switches
  and the command and inject files, confirm the display layout is unchanged, exit the lease, and report to kole with
  the route that proved each row.
- Abort the slot without further input if the owner returns, a foreign window appears, or the identity changes.

## Not covered

Rendered triple framing, curve, per-camera Game/Realistic sets, FFB feel, menu feel with the real wheel and
clean-profile install. The current F7 is the obsolete Original/Angle design and is not part of this smoke.
