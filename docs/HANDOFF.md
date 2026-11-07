# Gravel handoff — 2026-11-07 (GRV-2 launch check)

- Installed on the rig: `artifacts/milestone-wheel-tools-0.2.0-0ede6ebe` (GRV-1). Built binaries are not in git; `build.ps1` writes `build/`, `tools/Package.ps1` puts them in the ZIP's `dist\` folder.
- Launch check passed 09:05 with Steam signed in: the mod loaded with the force fence on, the wheel read live, and the game reached its main menu after one posted Enter (capture in ignored `build/grv2-menu2.png`). Nothing needed fixing.
- Restore: the run rewrote `milestone_mod.log` and replaced the 20 `Saved\Config\CrashReportClient\UE4CC-*` folders with one new one (UE4 does this each start). Those were put back, and the 227-file pre-launch snapshot now matches. Saves, `WheelConfig.ini` and Steam's cloud folder were unchanged.
- Rollback for the install: installer backup `Win64\DBCE-Wheel-Backups\20261107-055750-a93fa3`; full pre-install snapshot in ignored `build/rig-grv1-20261107-055719` (only the four installed files differ from it).
- Repeat a launch check with the ignored scripts in `build/`: `grv1-rig-backup.ps1 -Out <dir>`, then `grv2-launch.ps1` (creates the fence switch, launches 558260, waits for a fresh `[tx]` line). Stop the exact PID (Gravel ignores WM_CLOSE), delete `%LOCALAPPDATA%\dbce\gravel\ffb-fence.on`, run `grv2-restore.ps1 -Backup <dir>`, then `grv1-rig-verify.ps1 -Backup <dir>`.
- Camera work (probe v2, live-05 case03) is unchanged; see `2026-11-07-kiri-handoff.md`.
