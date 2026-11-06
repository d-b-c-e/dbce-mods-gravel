# Gravel handoff — November 1, 2026

## Current disposition

Cleanup is complete; no lease or process custody remains. Owner stopped engineering loops and live testing. This documentation wrap makes no new build, install, game launch or rig run. The latest development work is preserved on main; **no new coherent dfbab9a installable was deployed**.

The installed files were rehashed during this documentation wrap and still match the restored installation:

| Item | Identity |
|---|---|
| Installed product | milestone-wheel-tools 0.2.0; receipt installed October 10; Steam 558260 |
| Installed dinput8.dll SHA256 | B35F4D0B795C3CA916F98E76CC95925042FF56F2260A0F9A6C8556C3C08F54AA |
| Installed milestone_mod.ini SHA256 | 2057122EB5E9330E11AE4D8D921A7908F4EFE8B715E26A36BF0D159ED623BB5A |
| Shipping executable SHA256 / Steam build | 28032B882FFF6DAE07B185B93FD5989ADE09DC36C2EC070ECC1D05B3A73D2A95 / 3477925 (diagnostic pin) |
| Development DLL, not installed | FD70FBBA9DD17201E3F3DCBA73B34EFB544A4019CA362737DB6E15C773B82F19; source 8d09284 |
| Retained dist DLL, different from both above | 90EFCFA6CFA939003B058FE5B6F1F7E8F9417BF2B903C25AD9755441232F9AD6; historical 0a47a2d source |

Install directory: `D:/Program Files (x86)/Steam/steamapps/common/Gravel/gravel/Binaries/Win64`.
Rollback/evidence root: `E:/Source/game-modding/dbce-mods-gravel/artifacts/rig-20261101-021340/state`.
The exact original DLL is `before/install/dinput8.dll`, original INI is `before/install/milestone_mod.ini`, and `state.json` pins all 156 original files across install/config/Saved/switches/Steam-cloud roots. Post-run data is retained in `after/`. `RESOLVED.txt`, `restoration-result.txt`, `restored-installed-hashes.json` and display/window receipts are siblings of `state`. Preserve these backups. Any future install needs a fresh closed-game snapshot; do not blindly restore this older snapshot over later owner changes.

## Source progress and checks already completed

- `e227ff6`: saved Original/Angle F7 comparison candidate. It follows superseded policy revision 3, not current STD-041.
- `16aaa97`: native view saves and five camera offsets use one byte-preserving atomic INI replacement. Toolkit implementation `462680d` is pinned under `src/vendor/settings/PROVENANCE.json`. UTF-8/ANSI/BOM/UTF-16LE and unrelated keys/comments/whitespace/line endings are retained; UTF-16BE, malformed/ambiguous input and locked files are refused. Native production-header fixture and disposable disk replacements passed. Camera failure retains the live draft and shows a notice.
- `8d09284`: opt-in `probe_camera` command requests read-only discovery on the observation worker, avoiding concurrent name-cache access. It lists candidates; it neither selects nor proves the active local-player camera. Built, not exercised in a race.
- `18c4495` / `dfbab9a`: single observed menu steps, exact PID/birth/HWND capture wrapper and snapshot/restore helpers. Eight macro dry runs passed. Restore fixture passed changed/deleted/new-file restoration, exact binary/CRLF comparison, post-run archiving and corrupt-backup refusal before mutation.
- Wheel toolkit `58908bc`: E0 arrow-key macros and observation-only exact-HWND mode, with no activation/F24 fallback in that mode. Existing suite passed 95 checks; a foreign HWND produced `macroDispatched=false`, `stepsSent=0`, with no activation request.
- Native build passed with existing pragma/strncpy warnings; span regression passed 32/32. Build outputs/receipts are in `build/`; the source-dirty receipt includes the preserved pre-existing untracked AGENTS.md. Do not remove it to manufacture clean provenance.

## Diagnostic result and cleanup

Private evidence: `artifacts/rig-20261101-021340`. Read-only fence helper `C4884359` beside candidate FD70FBBA acquired the MOZA wheel, delivered raw axis maximum and returned to live readings after expiry. Action commands were refused because no Controls profile was applied. This proves that narrow device/read path, not complete force isolation, physical feel, game-final controls or route playback.

The candidate launched on unchanged Sim Racing, with the force fence active and UDP disabled. WGC captured the title on the center third with black sides. Navigation was aborted on a foreign Steam ATS launch chooser; it was not answered. No navigation, throttle or camera-probe command reached Gravel. Two WM_CLOSE requests failed to close it; CUA activation failed and sent no key. Temporary Steam/Code window minimization was reversed and verified with matching owners.

The owner later superseded the old no-hard-kill rule in hub `docs/agent-work-policy.md`, section "Force-kill of your own stuck test processes". At cleanup, fresh CIM census found both retained PIDs already absent and no other Gravel process. **Mevu performed no forced stop; exit cause is unknown.** All original snapshot hashes and file sets restored successfully; 20 old CrashReportClient INIs and three install files had differed or been removed before restoration. Post-run data was archived first. Displays remained Sim Racing; switch files restored; named mevu lease released. The older `CUSTODY.txt` is historical and superseded by `RESOLVED.txt`. No physical wheel-rest/force-feel measurement was made.

## Why there is no new deployment

The development DLL still contains saved Original/Angle F7 behavior; current per-mounted-camera Game/Realistic sets are unfinished. `tools/Package.ps1` / `BinaryProvenance.psm1` / installer currently admit only pinned historical retained binaries, not the new observed-build receipt. They cannot honestly label FD70FBBA as a supported new package without extending that provenance path. Do not relabel the retained 90EFCFA6 binary as dfbab9a or install the diagnostic DLL as completed F7 work. This is a payload/behavior gap, not a request for another review cycle. Guna received NONE for the file-only deployment pass.

## Exact next steps when engineering is resumed

1. Reuse `tools/qualification/README.md` and `Invoke-GravelStep.ps1`. First resolve supported foreground/quit cleanup for the exact render HWND. Record one observed, checkpointed route to an offline race; do not blindly repeat Enter or answer Steam prompts. No route is currently qualified. Obtain a new coordinated slot only when live testing is reauthorized.
2. In that stationary offline race, issue `probe_camera`, switch the native camera via its observed binding, and probe again. Resolve actual local controller -> camera manager -> view target / vehicle and per-car identity. A first matching UObject or HUD-live flag is not camera identity.
3. Implement STD-041/042: always angle-corrected; Game/Realistic saved sets per mounted camera; chase retains dynamic Game FOV plus saved offset; Shift+F7 is a session-only developer comparison. Keep the owned-key atomic writer and provide camera/set notices. Global offsets alone do not meet per-car/per-set persistence.
4. For curve, follow `docs/2026-11-01-pre-slate-trace.md`: recover the actual stereo callback and typed caller, then native D3D11 textures and execution order before Slate/HUD. Slot 14's queued RHI state is only a candidate. Reuse shared curve geometry/compositor, preserve HUD and apply the measured-rig/source-eye contract. No curved renderer exists; whole-backbuffer Present warping is insufficient.
5. Coordinate with Wheelkit owner lili. Gravel still reads legacy `[triple]` keys; native controls Apply has dry-run history, not current closed-loop game acceptance. Add/admit camera/screens rows only against a real runtime consumer; verify every applied binding from its own device. External WheelSetup remains the available configuration tool; there is no in-game F6 renderer.
6. Extend package/installer provenance to accept a validated observed-build receipt without weakening retained-binary checks. Build and freeze a coherent candidate, run focused existing checks, package once, then request file-only deployment on a closed game with a fresh backup/hash verification. Public pushes contain checked source/docs only; private raw binary-derived evidence stays ignored. **2026-11-05:** the provenance half is source work done (observed-build adoption, `docs/BINARY-PROVENANCE.md`); a clean `build.ps1 -UpdateDist` and one package remain. The live smoke plan for the installed build is `docs/2026-11-05-smoke-plan.md` (not authorized).
7. Keep the remaining cells explicit: sampled-signal recording is not physics-tick route capture; no route player exists; clean install still needs fresh-profile qualification; native FFB feel and actual wheel/menu behavior remain owner/device gates. No fresh evidence was collected for the older Ready-for-Test triples/menu/camera cells.

## Owner actions and retained constraints

No cleanup action remains for the owner. The restored 0.2.0 installation is available for ordinary owner testing; newer F7/curve/F6 behavior is not installed. Owner may report wheel/menu/FFB/framing feedback when convenient, but no new acceptance run is being requested under the stopped loops. On resumption, engineering must first produce the coherent candidate above before asking for acceptance of those missing features.

Pending/unchecked adoption: STD-001/003/016/017/018/021/022/023/024/025/034; missing ledger rows STD-026/027/028/029/030/031/032/036/037/038/039/040/043 remain unaudited. STD-041/042/044/045 are recorded explicitly in TOOLKIT-ADOPTION.md. No new family standard was introduced. Only pre-existing untracked AGENTS.md remains outside this work.
