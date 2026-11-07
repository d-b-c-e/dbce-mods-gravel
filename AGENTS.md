# Gravel mod — agent guide

Portfolio rules: `E:\Source\dbce-project-mgmt\HOW-WE-WORK.md` (read first; it replaces older process, lease, review and testing instructions). Current focus: `E:\Source\dbce-project-mgmt\NOW.md`. Feature status lives in the portfolio grid (`python E:\Source\dbce-project-mgmt\tools\portfolio_grid.py plan <grid-id>`), not in this file. Grid id: `gravel`.

## Game and mod
- Gravel (Milestone, UE 4.17, Steam 558260), x64. This repo is **public** (`d-b-c-e/milestone-wheel-tools`, formerly `milestone-telemetry`): do not push or publish releases without the owner.
- Milestone's UE4 games refuse direct-drive wheels, cannot rebind controls with one attached, and ship no telemetry. A `dinput8.dll` proxy beside the game exe re-types the wheel (the only value altered is `dwDevType`) and builds Forza "Data Out" UDP telemetry (Horizon packet, exactly 324 bytes). `WheelSetup.ps1` maps controls from outside the game. Only Gravel is supported; MXGP, MotoGP, Ride and Supercross are untried.
- Source: `src/proxy.cpp` (DirectInput vtable patches), `src/fmod_tap.cpp` (engine-audio parameters, delay-load IAT hook), `src/ue4.cpp` (RPM/gear from UE4 reflection), `src/telemetry.cpp` (60 Hz sender); `tools/wheelprobe/` reads 128 buttons / 8 axes.
- Config and log keep the old names `milestone_mod.ini` / `milestone_mod.log`; renaming would break installs.

## Build
- `.\build.ps1`: x64 proxy plus input helper into `build/`, PE/export/import checks. Built binaries are not committed.
- `.\tools\Package.ps1`: commit source first; it runs `build.ps1`, then writes the complete ZIP with a per-file SHA256 manifest. The binaries keep their `dist\` place inside the ZIP and in the installed `DBCE-Wheel-Setup` (install receipts list those paths).
- `build.sh` hardcodes `/e/msys64`; adjust it. Source validation (both test suites): `tools\Invoke-SourceValidation.ps1 -Engine pwsh|powershell`.

## Test
- Unit tests (disposable fixtures only): `tools/tests/Test-SetupUx.ps1`, `tools/tests/Test-InstallPackage.ps1` (run with both `powershell.exe` and `pwsh`), `tools/tests/Test-CameraProbe.ps1 -OutputRoot build/test-camera-probe-<stamp>`.
- In game: with `log=1` in `milestone_mod.ini`, `milestone_mod.log` beside the game exe has a status line every 5 s showing every channel; the first channel not `live` is the problem. `mirror_port=` sends a copy to a second port so `python tools/forza_listen.py <port> --lines` can decode packets while SimHub holds the real one. Config is read at game start.
- Back up and restore around a game run: `WheelConfig.ini` (the game rewrites it on exit, CRLF; Steam file verification restores the stock file), `settings.sav`, the profile files, and the mod's `milestone_mod.ini`. Exact folders: unknown — check README and `docs/wheelconfig.md`.
- Close the game first; it locks the DLL.

## Install on the rig
- Install from an extracted package (`artifacts\milestone-wheel-tools-<version>-<commit>\`), not the repo root: `.\Install.ps1 -Game Gravel` (also `-GamePath "<...\gravel\Binaries\Win64>"`, `-Port`, `-Format`, `-Product`); `-Check` or `-DryRun` previews. It verifies the manifest hashes, backs up before writing and rolls back partial changes.
- `.\Uninstall.ps1` removes only receipt-matching files and keeps edited ones. Closed-game guard applies to both.
- The setup tool is deployed as `DBCE-Wheel-Setup` beside the exe.

## Shared code
`lib/toolkit/` is vendored (Forza encoder `include/forza_packet.h`, `powershell/DbceWheel.psm1`). Change shared code in the toolkit (E:\Source\game-modding\_toolkits\dbce-wheel-mod-toolkit) and re-sync; never edit the vendored copy. Re-sync here: `tools\Sync-Toolkit.ps1 -Version vX.Y.Z -Parts include,powershell`.

## Game-specific rules
- Never use the proxy in a game with kernel anti-cheat. Handling is not changed, so online times are not affected.
- Tool order: physical button to logical slot in `WheelConfig.ini`, then slot to action in `settings.sav`. `settings.sav` is read only; the profile adapter has no endpoint/centre/deadzone writer, so do not invent calibration values.
- The sender reads its config once in `proxyInit()`; label telemetry settings "applies next launch". It accepts IPv4 only.
- The delay-load `fmodstudio64.dll` thunk unhooks itself when called; resolve the real function with `GetProcAddress`.
- DirectInput vtable indices are load-bearing and shared per class: re-check the object in each hook. Gravel makes three device objects per wheel (only one is acquired; ignore the all-zero twins) and runs two constant-force effects at once (track per effect, strongest wins).
- `WheelConfig.ini` is CRLF: match `[^\r\n]*`, not `.*?$`. PowerShell 7 cannot marshal a delegate through COM: pass the enumeration callback as a raw function pointer.
- FMOD stops updating between races; clear the values or a shaker sticks on a constant. Speed comes from FMOD `VehicleSpeed` (the HUD class `IgnitionGaugeLogicWidget` has RPM/gear but no speed); `UProperty::Offset_Internal` is at +0x50, probed at runtime.
- Read the Steam path from the registry; it is not always under Program Files.

## Key docs
- `README.md`, `QUICKSTART.md`
- `docs/GRAVEL-PRODUCT.md`, `docs/wheelconfig.md`, `docs/forza-format.md`
- `docs/archive/CLAUDE-2026-11-06.md` (old working notes with measured FMOD/UE4 facts)
- `TOOLKIT-ADOPTION.md`

<!-- BEGIN project-file-location -->
## Project files and temporary work

Keep project-related research, evidence, scripts, packages, and other working files inside the relevant project folder whenever practical. Keep disposable or machine-specific outputs git-ignored when they do not belong in version control; never ignore files that should be committed merely to hide them. When a project folder is unsuitable, use `E:\Source\temp\<project>` instead of creating scattered drive-root folders. Record the location of material needed by later work and update references when relocating it.

Clean up temporary files and disposable copies promptly once they are no longer useful. Preserve active work, user data, settings, saves, required evidence, backups, rollback artifacts, and worktrees until their retention or replacement is established. Follow existing project, rig, and data-loss safeguards; do not remove files merely because they are old or untracked.
<!-- END project-file-location -->
