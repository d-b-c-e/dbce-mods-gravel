# Toolkit standards adoption

Which entries of the wheel toolkit's standards ledger
(`E:\Source\toolkits\dbce-wheel-mod-toolkit\STANDARDS.md`) this mod has brought in.
Update a row in the same commit that adopts it. Statuses: `adopted`, `partial`,
`pending`, `n/a` (say why), `unchecked` (nobody has looked yet).
Seeded 2026-10-04 from what was verified that day; `unchecked` rows need a look.

| Standard | Title | Status | Notes |
|---|---|---|---|
| STD-001 | One mod per game | unchecked |  |
| STD-002 | Recording and playback from launch | unchecked |  |
| STD-003 | Normalized FFB strength | unchecked |  |
| STD-004 | Consistent settings UX | unchecked |  |
| STD-005 | Camera numpad layout 8/2 9/3 4/6 7/1 +/- 5 | partial | Triple layouts only (applied through the stereo views), in a race; docs/TRIPLES.md. |
| STD-006 | Camera step sizes are settings | partial | `[camera]` move/tilt/FOV steps in milestone_mod.ini (no in-game panel). |
| STD-007 | Triple screens in one wide window | adopted | 2026-10-06 emulated-stereo triples (`src/triple.cpp`, docs/TRIPLES.md): centre = game camera, sides at the panel angle, off-axis; HUD and menus on the centre. Owner rig check pending. |
| STD-008 | Display changes: game applies once | adopted | On a triple layout every resolution request becomes the layout's size; exclusive fullscreen and ChangeDisplaySettings mode changes are refused. |
| STD-009 | Dashboard telemetry matches the HUD | unchecked |  |
| STD-010 | Install the latest build for testing | unchecked |  |
| STD-015 | Triples on Surround and on separate monitors | adopted | Both seen unattended 2026-10-06: borderless span over "Sim Racing" and windowed fullscreen on "Sim Racing Surround". Owner rig check pending. |
| STD-019 | Menus on the centre screen; side screens only in gameplay | adopted | Front end, loading and pause menu on the centre; sides black outside a race. |
