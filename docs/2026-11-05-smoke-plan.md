# Gravel installed-build smoke plan (live allowed by kole 549126; each stage needs its own reviewed packet)

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

## Installed identity (file-only install GO564244, 2026-11-06 14:56Z)

kole allowed the live smoke while the owner is away (549126); the build below was installed under pali-approved
packets. The 2026-11-05 identity (DLL `E6DAA565`, INI `FB2C615D`) is now the rollback baseline.

| Item | Value |
|---|---|
| `dinput8.dll` | `02F52D7BF62317FAAD79AE3DA2E6FA68348A58B55D869D27CAE13135EC8B6840`, observed clean build of 6cfc5a6 (dist ba0d88d), package e3b8c08 |
| `milestone_mod.ini` | `4433C2DA242DFFF35CB44074A877F2F2113770BA6C3C26F967FA5CA275FED883` = previous `FB2C615D` + the package `[camera]` block (`log=1`, telemetry on, port 8000, `[fmod] discover=1`) |
| `gravel-Win64-Shipping.exe` | `28032B882FFF6DAE07B185B93FD5989ADE09DC36C2EC070ECC1D05B3A73D2A95`, Steam build 3477925 |
| Receipt / rollback | `milestone_install.json` `7A359C58`; installer backup `DBCE-Wheel-Backups/20261106-085556-18a619`; external snapshot `artifacts/install-564244/state` |
| Last mod log | `milestone_mod.log` 2026-10-06, so this build has never run |

If any hash differs at preflight, stop and report it. Do not re-baseline silently.

## Live stage 1 result (2026-11-06, GO568362)

First live run of the installed build (packet `E:/Source/_work/dbce-mods-gravel-release-dd742f3/build/live-01/case07`,
adapted from loki's DR2 runner; pali-approved, kole GO). Evidence: `case07/live/go-568362`.

- One in-process R1 Confirm (`inject raw button 0`, 150 ms) at the press-start screen was accepted, read (down) and ended,
  and the next capture showed the main menu (Off-Road Masters, Free Race, Multiplayer, Time Attack, Weekly Challenges,
  DLC). Gravel was **not** the foreground window before or after the press: the R1 route works without OS focus.
  (Frame transition only; capture time is not exposure time, and foreground snapshots are correlation.)
- Telemetry was turned off for the session (`[telemetry] enabled=0`); the mod logged "UDP disabled" and status lines continued.
- **Gravel ignores WM_CLOSE** (3 of 3 attempts: twice on Nov 1, once here). The run held custody; recovery was an
  exact-identity forced stop of the shipping process under the owner's own-stuck-process policy, then byte-exact restore.
  Future packets need an in-game quit route (the Exit hint / Back then Confirm) instead of relying on WM_CLOSE.
- Steam userdata 558260 changed naturally during the run (archived, never restored).
- Next: menu navigation to an offline race (Free Race is one entry down; the R12 D-pad hat through R1 is the candidate)
  and the in-game quit route, as a reviewed stage 2.

## Live stage 2 result (2026-11-06, GO569238): in-game quit not found yet

Packet `build/live-02/case02` (three operator-gated presses). Confirm again moved press-start -> main menu with Gravel not
in front. **Back (index 1, WheelConfig Button2) did nothing on the main menu** (no quit prompt), so the third press was
skipped. WM_CLOSE was ignored again (4 of 4); recovery was the same exact-identity forced stop and byte-exact restore,
with fresh Steam identity/log checks before restoring. The main-menu Exit hint uses another wheel button: identify it
offline (settings.sav Exit/Back action bindings) before a stage 3.

## Camera-set live smoke passed (2026-11-06, GO571267)

Packet `build/live-03/case03` (case02 with a 30 s answer window and 420 s navigation). Nine in-process presses (all
accepted/down/ended, Gravel not in front) reached a stationary offline race (Alaska, Through the Caves, Abarth 131). Then,
by command file only: F7 -> Realistic (`[triple] fov=rig`), wider 3 -> `fov_offset_realistic_deg=6.0`, reset -> 0.0, Shift+F7
original view and back (INI unchanged), F7 -> Game (`fov=game`); every step matched its log line and INI readback.
Realistic visibly widened the in-car view (installed `vertical_fov` 58.7, eye distance 0); the Shift+F7 frame showed
stretched flat side screens. Cleanup was the integrated forced stop and byte-exact restore. Evidence:
`build/live-03/case03/live/go-571267` in the release worktree. Limits: snapshot frames, position/tilt reset unasserted,
no wheel-feel or FFB claim, telemetry off during the run.

## Live stage 4 (camera cycle, `build/live-04/case02`, plan `1109CE23`): not yet observed

- GO572877: route reached the race; one SwitchCamera press changed the in-car view to the bumper view; `probe_camera`
  saturated at 128 candidates on MovieScene objects, so it cannot name the active camera. The runner then stopped on my
  F7 answer, whose view text contained a refused token even though negated. Operator lesson: never use the refused view
  words (cockpit, in-car, interior, dash, driver, helmet, first-person) in answers, negated or not.
- GO573425 (same sealed case, corrected wording): INCONCLUSIVE, operator-side. nav-1 Confirm worked; the nav-2 answer
  arrived 56 s after its deadline because the operator session's context was compacted mid-run. With no answer, nothing
  was pressed; integrated forced stop, 183/183 byte-exact restore, natural `remotecache.vdf` change archived, Finalize pass.
  Start the next attempt with fresh operator context.
- Identity: probe v2 (`c18621a`) follows PlayerController -> PlayerCameraManager -> ViewTarget/CameraCache and logs raw
  values only; built, packaged and installed (GO574493/575376/575911, `dinput8.dll` `100A7E62`).
- GO576384 (stage 4b, `build/live-05/case01`, plan `D9387890`, hure co-operating): ABORTED before any input. Steam rotated
  `logs/cloud_log.txt` at its ~1 MiB limit in the second of the launch, so the runner's Steam-log guard (log shorter than
  its admission offset) held before adopting the game. Reviewed recovery (`live-05/recovery-576384/Recover-Stop.ps1`,
  pali 576956, napo 576968, kole 576983): census-adopted exact-identity stop, bootstrap exited by itself, closure
  quiescent, rotation-aware Steam scan clean (cloud_log continuity by name/length only), frozen Rollback 205/205
  byte-exact, Finalize ABORTED with the original reason. Probe v2 not exercised. Next: a rotation-robust guard and/or a
  pre-launch cloud_log size check before the rerun.

## Live stage 3 result (2026-11-06, GO570432): offline route proven, camera phase not reached

Packet `build/live-03/case02` (operator-chosen sealed navigation). Eight in-process presses, all accepted/down/ended with
Gravel not in front, reached the loaded track: Confirm (press-start -> main menu), **hat-down (the D-pad hat does move
the menu highlight: Off-Road Masters -> Free Race)**, Confirm Free Race -> Alaska (Cross Country) -> Through the Caves ->
Abarth 131 Rally -> owned livery -> race summary (Day, Clear, AI Easy) -> pre-race menu on the track with START RACE
highlighted; the side screens showed the forest. The ninth answer arrived after its frame deadline, so the runner stopped
with no press and its integrated forced-stop cleanup restored everything. Known route to a race: 9 presses (Confirm,
hat-down, Confirm x7). A successor needs a longer answer window and navigation budget.

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
   Schedule launch when the last input is at least 210 s old (record it): kole found on 2026-11-06 that the ordinary
   focus request then succeeds, so the attach fallback is rarely needed.
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
