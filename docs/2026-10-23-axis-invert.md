# Gravel axis inversion source handoff

Branch `nepo/gravel-axis-invert-20261023`, worktree
`E:/Source/games/.worktrees/nepo-gravel-axis-invert-20261023`, based on main
`04f0ed76f68b3a0162d0bdd5ab36b0758f84403f` (use the containing commit as the
candidate identity).

Controls now offers Invert direction on an existing axis without recapture.
Save binding stages the change; the existing Save mappings and exit commits it
with the existing stale-file guard and backup. Cancel keeps the draft; Exit
discards pending mappings. Only the known Axis1-8 normal `(1,0)` and inverted
`(-1,1)` transforms are supported. Missing or ambiguous saved destinations,
missing/duplicate fields and custom transforms refuse. The saved native action
destination is honored, and unrelated mappings, force settings and CRLF stay
unchanged. No new native values, dependency, device or force path was added.

The game-specific WheelConfig adapter remains in `tools/SetupUx.psm1`; native
discovery, input sampling and toolkit vendors retain their existing paths.
This follows the shared binding-edit policy using the existing external editor;
it does not import a managed F6 renderer into a PowerShell setup tool.

## Focused validation

Run `tools/tests/Test-SetupUx.ps1 -AxisInvertOnly` and
`tools/tests/Test-SetupUx.ps1 -NativeBindingsOnly` with each of
`powershell.exe -NoProfile -ExecutionPolicy Bypass -File` and
`pwsh -NoProfile -File`.

PASS: 76 inversion checks and 45 existing native-binding checks on each shell.
The inversion check runs the production editor with fake console boundaries,
and makes device sampling throw. It covers all eight axes, direction/preview,
Cancel, saved destination selection, ambiguous/missing/duplicate/custom refusal,
staging without disk writes, final Save, original backup and stale-save refusal.
`git diff --check` also passed. Existing native DLL bytes remain unchanged;
no native build was needed for the setup-script change.

Retained fixture directories under `%LOCALAPPDATA%/Temp/`:

- PS5.1 inversion: `milestone-ux-196797ba46bd4ce3aa3de6aa3865de0a`
- PS5.1 native bindings: `milestone-ux-95071d826c4b4aef87f2c21d056e96b5`
- PS7 inversion: `milestone-ux-84d19529f7bb4ae6b088ccbd33b509d3`
- PS7 native bindings: `milestone-ux-56be83f321244b5cb4eea5c1e58868c5`

No game, screen, input reader, hardware, installer or owner config was used.
No child process, lease or cleanup remains. No dirty files are intentionally
left at handoff. The next source integration is this isolated commit; a later
private package and coordinated rendered/device check remain separate gates.

## Native evidence and open gates

Current source includes the shared DirectInput force fence and its observer;
`docs/RECORDING.md` retains its fake-COM evidence and remaining live probe/game
gates. `[ffb] gain` scales observed telemetry summaries in `src/proxy.cpp`,
not physical force. WheelConfig has known binding transforms and rotation
limits, but no proven independent endpoint, center or deadzone fields. No
physical strength slider or calibration writer was inferred from these values.
Multi-device input consumption, vehicle response, force feel and save-safe
Steam Cloud runtime admission remain unqualified. No runtime was requested.

The standards comparison found these unchanged adoption gaps:
unchecked STD-001,003,009,016,017,018; pending STD-021,022,023,024,025,034;
missing adoption rows STD-026,027,028,029,030,031,032,036,037. STD-004 is updated
for this bounded external-editor change; no other standard is declared adopted.
