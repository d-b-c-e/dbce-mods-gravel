# Gravel wheel settings

1. Close Gravel. Extract the whole package into a folder, then double-click
   **Install.bat**. Existing bindings, tuning and telemetry are kept.
2. Open **Wheel settings.bat** in Gravel's game folder, or
   **DBCE-Wheel-Setup\WheelSetup.bat** beside its shipping executable.
3. Start in **Simple → Setup**. Bind Steering, Throttle and Brake. Optional
   handbrake and button bindings are under Controls. **Preview device input**
   checks staged assignments without saving; Esc returns.
4. Choose **Save mappings and exit**, then run Gravel's own wheel calibration
   once. FFB and camera views remain controlled by the game.

**Telemetry is optional.** Simple → Telemetry shows the saved receiver and
destination. Match them in SimHub. Off/On and Advanced connection changes apply
at the next game launch; the setup tool does not verify live delivery.

**Advanced** reveals device details and connection tuning. Switching views
preserves your settings. There is no F6 game panel in this mod.

**Update:** close Gravel and run Install.bat from the new package. Each install
records backups beside the game in DBCE-Wheel-Backups. **Remove:** close Gravel
and run Uninstall.bat. Personal configuration, mappings and backups are kept.

**Check only:** run `Install.ps1 -Check` from the complete verified package to
see proposed Create / Replace / Preserve actions without saving or installing.
`-DryRun` and `-WhatIf` do the same. A valid saved wheel identity or explicit
`-Product` is required; hardware is not enumerated. Directory conflicts and
junctions or other reparse points in target paths stop with an error.
Re-run without these switches to install after fresh validation.

Only Gravel is supported by this package. No Python, compiler or SDK is needed
to install or use the setup tool.
