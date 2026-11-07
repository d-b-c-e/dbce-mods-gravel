# Gravel handoff — November 7, 2026 (clean stop, kole 577852)

Supersedes the "current state" and "next" of `2026-11-06-kiri-handoff.md` (kept for history). Owner away; kole
coordinated; pali and napo reviewed every packet; every rig step ran under an exact-hash GO with a lease.

## Residual state at the stop

None. No Gravel process, no lease or `claude-lease-kiri*` receipt, no `%LOCALAPPDATA%\dbce\gravel` switch files, no
`dbce-camera-cmd.txt`. Installed files equal the GO575911 baseline below. main is local only (public repo; push needs the
owner).

## Installed build

| Item | Value |
|---|---|
| Package | `milestone-wheel-tools-0.2.0-d91b2202.zip` `139B14CC`, manifest `84809BAE` (GO575376) |
| Install | file-only upgrade GO575911, result `7145B872`: `dinput8.dll` `100A7E62`, setup `wheelprobe.exe` `AA3A4216`, receipt `A8B998D6`; `milestone_mod.ini` `4433C2DA`, WheelConfig `F7EB7F4A`, launcher `4FC2FCA4` unchanged |
| Rollback | installer backup `gravel/Binaries/Win64/DBCE-Wheel-Backups/20261106-182436-ece373` (the `e3b8c08` install, manifest `8473EBF0`); snapshot `artifacts/install-575911` (`state.json` `253F1B89`) |

## What ran since the November 6 handoff

| GO | What | Result |
|---|---|---|
| 573425 | stage 4 rerun (live-04 case02) | INCONCLUSIVE: operator answer late (session compaction); forced stop, restore 183/183 |
| 574493 | CPU build of `0ec8816` (= `c18621a`, probe v2) | PASS 48 s; dist committed as `d91b220` |
| 575376 | package | PASS; ZIP `139B14CC` |
| 575911 | install upgrade | PASS; see above |
| 576384 | stage 4b live (live-05 case01, plan `D9387890`) | ABORTED before any input: Steam rotated `cloud_log.txt` in the launch second; reviewed recovery `Recover-Stop.ps1` `73C1514B` (pali 576956, napo 576968, kole 576983): census-adopted exact stop, restore 205/205, Finalize ABORTED, recoveryUsed |

Evidence (git-ignored) in the release worktree `E:/Source/_work/dbce-mods-gravel-release-c18621a/build/`:
`release-cpu/cpu-574493` (result `BF49C0B5`), `pkg-575376` (result `8D7C497E`), `install-575911` (result `7145B872`),
`live-05/case01/live/go-576384` (Finalize ABORTED; `recovery/`, `closure.json`, `recovery-final.json`),
`live-05/recovery-576384/Recover-Stop.ps1`. Older evidence: `E:/Source/_work/dbce-mods-gravel-release-dd742f3/build/`.

## Source on main (local)

- `c18621a`: `probe_camera` v2 (pali SOURCE APPROVED 573932). Walks the one live PlayerController -> PlayerCameraManager
  -> ViewTarget/CameraCache with uniquely resolved owners bound by pointer, typed/bounded members, double-read weak
  pointers; logs raw values only and ends every path with one `[camera-probe] v2 done status=` line.
  `tools/tests/Test-CameraProbe.ps1` (49 checks over synthetic reflection memory).
- `d91b220`: dist from the bounded build. Docs: `2b3a240`, `b150343`, `c2100a9`, this handoff.

## Pending packet (approved, not run)

Stage 4b case03, `E:/Source/_work/dbce-mods-gravel-release-c18621a/build/live-05/case03`:
plan `193F39C40C929BDCACE97373476F340A60283D01B6AB8CBF03A984DC924A5E7A`, Contract `A598662E`, Invoke-Live `80742339`
(napo 577434, pali 577481). Hashed-prefix, rotation-robust Steam log guard (boundary: bounded normal Steam rotation, not
an atomic filesystem snapshot). kole GO577496 was issued but **withdrawn at the clean stop before launch**; a rerun needs a
fresh GO. Operator continuity: run from a fresh session start to finish, or with a co-operator on the half-window
create-new split (`build/live-05/CO-OPERATOR.md`). case02 (`508552E8`) was rejected (pali 577330) and never ran.

## Next

1. Fresh GO for live-05 case03: reach a race (known route), `probe_camera` at baseline and after each switch; read the
   v2 status, manager fields and POV values per view. That is the first runtime evidence for per-camera identity.
2. If the v2 values separate the views, design camera-keyed Game/Realistic sets (source-first).
3. After reviewers close their audits: move the two release worktrees' `build/` evidence into this repo's `build/`,
   update paths, remove the worktrees (`kiri/release-dd742f3`, `kiri/release-c18621a` are merged).

## Limitations

- Probe v2 has never run in the game; its values are raw (single PlayerController is a candidate; CameraCache is a
  sampled cache; Relative* attachment unchecked) and are not an identity until compared across views.
- Gravel ignores WM_CLOSE; forced-stop recovery is the normal cleanup. Physical wheel rest is never measured.
- Realistic FOV still uses `vertical_fov` 58.7 because `eye_distance_mm` is 0 (owner's rig says 660 mm; owner decision).
