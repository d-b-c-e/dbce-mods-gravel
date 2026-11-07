# Gravel handoff — 2026-11-07 (GRV-1 cleanup)

- Built binaries are no longer in git: `build.ps1` writes `build/`; `tools/Package.ps1` builds the commit and puts the binaries in the ZIP's unchanged `dist\` folder.
- Installed on the rig: `artifacts/milestone-wheel-tools-0.2.0-0ede6ebe` (upgrade changed only `dinput8.dll`, setup `WheelSetup.ps1`, setup `wheelprobe.exe` and the receipt). The DLL is the same code as the previous install; only the PE timestamp and checksum differ.
- Rollback: installer backup `Win64\DBCE-Wheel-Backups\20261107-055750-a93fa3`; full pre-install snapshot (205 files plus `hashes.json`) in this repo's ignored `build/rig-grv1-20261107-055719`.
- Launch check not run: Steam showed "Refresh Sign In — this account logged in on another computer". Never sign in or press Exit Steam; that would end the owner's other session.
- Next: once Steam is signed in here, take the lease, create `%LOCALAPPDATA%\dbce\gravel\ffb-fence.on`, launch 558260, and look for a fresh `[tx]` line in `milestone_mod.log`. Then stop the exact PID (Gravel ignores WM_CLOSE), delete the switch, and run `build/grv1-rig-verify.ps1 -Backup <snapshot>`; only the four files above should differ.
- Camera work (probe v2, live-05 case03) is unchanged; see `2026-11-07-kiri-handoff.md`.
