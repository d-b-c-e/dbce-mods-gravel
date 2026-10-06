# Toolkit standards adoption

Which entries of the wheel toolkit's standards ledger
(`E:\Source\toolkits\dbce-wheel-mod-toolkit\STANDARDS.md`) this mod has brought in.
Update a row in the same commit that adopts it. Statuses: `adopted`, `partial`,
`pending`, `n/a` (say why), `unchecked` (nobody has looked yet).
Seeded 2026-10-04 from what was verified that day; `unchecked` rows need a look.

| Standard | Title | Status | Notes |
|---|---|---|---|
| STD-001 | One mod per game | unchecked |  |
| STD-002 | Recording and playback from launch | n/a | Superseded by STD-012; input-only determinism is not the current replay requirement. |
| STD-003 | Normalized FFB strength | unchecked |  |
| STD-004 | Consistent settings UX | unchecked |  |
| STD-005 | Camera numpad layout 8/2 9/3 4/6 7/1 +/- 5 | partial | Triple layouts only (applied through the stereo views), in a race; docs/TRIPLES.md. |
| STD-006 | Camera step sizes are settings | partial | `[camera]` move/tilt/FOV steps in milestone_mod.ini (no in-game panel). |
| STD-007 | Triple screens in one wide window | adopted | 2026-10-06 emulated-stereo triples (`src/triple.cpp`, docs/TRIPLES.md): centre = game camera, sides at the panel angle, off-axis; HUD and menus on the centre. Owner rig check pending. |
| STD-008 | Display changes: game applies once | adopted | On triple layouts resolution/exclusive requests are guarded. Source 4b7fd75 also refuses null display resets; actual null/non-null/flag hooks pass with fake display calls. See docs/2026-10-06-triple-cross-review.md for scope. |
| STD-009 | Dashboard telemetry matches the HUD | unchecked |  |
| STD-010 | Install the latest build for testing | adopted | Installed 4b7fd75 renderer bytes, adopted/package 4dc9df7; 14 receipt files match (dinput8 D39FF1B8). Tilted race frame inspected. No physical owner acceptance inferred. |
| STD-015 | Triples on Surround and on separate monitors | partial | Surround and separate-monitor races seen unattended. Camera-frame fix passes independent seam checks and tilted race image review. Existing ResX arguments still suppress span maintenance; review finding 4 remains open. |
| STD-019 | Menus on the centre screen; side screens only in gameplay | partial | Observed front end/loading/pause/HUD centred with normal telemetry configuration. Telemetry Off still implies gameplay and stops observation, defeating menu black sides; review finding 3 remains open. |
| STD-011 | Work lands on main | adopted | Renderer fixes, reviewed evidence and adoption rows pushed on main. |
| STD-012 | Reproduce the route and preserve original signals | pending | No qualified gameplay recorder/player. UE observation remains coupled to telemetry output; separate producer and delivery before muted recording. |
| STD-013 | The installed build launches plainly | partial | Plain Steam launch verified on prior renderer; corrected race/tilt and receipt checked. Broader option/transition cases remain open. |
| STD-014 | Request reciprocal review when progress stalls | adopted | Codex reviewed source and exact install; fixes 1/2 independently checked, findings 3/4 handed back to Claude. |
| STD-016 | Forza Horizon telemetry on by default | unchecked | Packet/default/owner-display audit not established by this renderer check. |
| STD-017 | Hide empty settings pages | unchecked | Setup panel inventory not part of this renderer review. |
| STD-018 | Handling changes never reach online scores | unchecked | Recording/offline/scoring eligibility requires its own investigation before taking pose ownership. |
| STD-020 | Standard portfolio feature checklist | partial | All 20 rows represented; limited and unchecked features remain explicit. |
