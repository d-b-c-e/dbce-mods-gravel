# Gravel handoff — November 6, 2026

Supersedes the "current state" of `2026-11-01-mevu-handoff.md` (kept for history). Owner was away; kole coordinated,
pali/napo reviewed every packet, and every live step ran under an exact-hash GO with byte-exact restoration.

## Current state

| Item | State |
|---|---|
| Installed build | 0.2.0 package `d91b220` (probe v2, ZIP `139B14CC`), file-only upgrade GO575911 (2026-11-07 00:24Z): `dinput8.dll` `100A7E62`, setup `wheelprobe.exe` `AA3A4216`, receipt `A8B998D6`; `milestone_mod.ini` `4433C2DA` and launcher `4FC2FCA4` unchanged |
| Rollback | installer backup `gravel/Binaries/Win64/DBCE-Wheel-Backups/20261106-182436-ece373` (previous `e3b8c08` install); external snapshot `artifacts/install-575911/state`; older: `...-085556-18a619`, `artifacts/install-564244/state` |
| Portfolio grid | `camera_sets` and `perspective` ready_for_test (hub `f9d023eb`); `curve` unchanged (no curved renderer) |
| Owner checklist | hub `docs/owner-checks/2026-11-05-owner-checklist.md` section 12 |
| main | local commits not pushed (repo is PUBLIC; push only on an explicit owner/coordinator decision) |

## What landed (source)

- `dd742f3`/`6cfc5a6`: STD-041 set switch. F7 = Game/Realistic (saved as `[triple] fov`), Realistic = seat-true rig vertical
  FOV + its own `fov_offset_realistic_deg`; always angle-corrected; Shift+F7 = session-only original/angle comparison;
  one `tunedFov` rule for projection and notice (`tools/tests/Test-FovSets.ps1`).
- `a70af7b`/`e3b8c08`: installer appends the package `[camera]` block once to settings that predate it, only when the file
  re-encodes byte-exactly (otherwise kept and explained); `Test-InstallPackage` 254 checks.
- `ba0d88d`: dist = bounded observed build of `6cfc5a6` (CPU GO559598).

## Live results (installed owner Gravel, telemetry off, FFB fenced)

| Stage | GO | Result |
|---|---|---|
| 1 | 568362 | One in-process Confirm moved press-start → main menu with Gravel not foreground. WM_CLOSE ignored → held, recovered by forced stop. |
| 2 | 569238 | Back (Wheel_FaceButton_Right) does nothing on the main menu; Pause/Start (Wheel_Special_Right) is unbound on the owner MOZA profile, so no wheel-only in-game quit. kole: forced-stop recovery is the normal cleanup. |
| 3 | 570432 | Navigation proven to the loaded pre-race menu (hat-down moves the menu highlight); 9th answer late, no press. |
| 3 | 571267 | **OBSERVED.** 9 presses to a stationary offline race (Free Race, Alaska, Through the Caves, Abarth 131), then command-file camera checks all passed: F7 → Realistic (`fov=rig`), wider 3 → `fov_offset_realistic_deg=6.0`, reset → 0, Shift+F7 original and back (INI unchanged), F7 → Game. pali verified receipts and frames. |

Route to a race from the press-start screen: Confirm, hat-down (Free Race), Confirm ×7 (Free Race, Alaska, Through the
Caves, Abarth 131, owned livery, summary START RACE, pre-race START RACE).

Evidence (git-ignored, release worktree `E:/Source/_work/dbce-mods-gravel-release-dd742f3/build/`): `release-cpu/`
(build/package/install), `live-01/case07`, `live-02/case02`, `live-03/case02` and `case03` (each `live/go-<GO>`), recovery
folders. Keep the worktree until reviewers close their audits; then move `build/` here and update these paths. Its branch
`kiri/release-dd742f3` is merged into main.

## Limits and owner checks

- The run used the mod's command file, not the physical F7/Shift+F7/numpad keys; the seat view is an owner judgement.
- Realistic used `vertical_fov` 58.7 because `eye_distance_mm` is 0; the owner's Wheelkit rig profile says 660 mm (~33.6°).
- One set for all cameras: per-mounted-camera sets still need camera identity (`probe_camera`, not yet run in a race).
- Gravel ignores WM_CLOSE (4 of 4); Pause/Start is unbound on the wheel profile (owner checklist note).
- Physical wheel rest is never measured (FFB fenced only).

## Next

1. Owner check (checklist section 12); optional: set `eye_distance_mm=660` with the owner's consent.
2. Per-mounted-camera sets: the old `probe_camera` saturated in a race (stage 4, smoke plan). Probe v2 (`c18621a`,
   pali SOURCE APPROVED 573932, `tools/tests/Test-CameraProbe.ps1` 49 checks) walks PlayerController ->
   PlayerCameraManager -> ViewTarget/CameraCache and logs raw values only, ending in one `v2 done status=` line. Built
   (CPU GO574493 -> `d91b220`), packaged (GO575376) and installed (GO575911); evidence in the release worktree
   `E:/Source/_work/dbce-mods-gravel-release-c18621a/build/release-cpu`. Next: a NEW short live case whose parser reads
   v2 (including `owner-unresolved`); `build/live-04/case02` cannot be reused. Run it only when the operator can stay
   in the run start to finish (kole 573950), or with a peer co-operating the answers.
3. Curve (curved renderer) remains unstarted work (`2026-11-01-pre-slate-trace.md`).
4. Live packets should persist Steam log offsets (stage 3 recipe does) and keep the integrated forced-stop cleanup.
