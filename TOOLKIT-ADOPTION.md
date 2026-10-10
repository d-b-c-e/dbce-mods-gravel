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
| STD-019 | Menus on the centre screen; side screens only in gameplay | partial | Source separates UE observation from UDP and removes telemetry Enabled from menu-side policy. Candidate builds; telemetry-off rendered regression remains a live gate. Earlier normal-telemetry menu evidence stays historical. |
| STD-011 | Work lands on main | adopted | Renderer fixes, reviewed evidence and adoption rows pushed on main. |
| STD-012 | Reproduce the route and preserve original signals | partial | Native sampled-signal producer/delivery split, bounded session writer and actual-producer/managed-reader checks pass. Diagnostic sink fake-COM checks pass. Not installed/live-qualified; no physics-tick recorder, pose player or exact force replay. See docs/RECORDING.md. Second review: ANSI/Unicode semantic-enumeration devices are guarded before callbacks (SDK tests); full COM/vtable/device-property mute qualification remains open, no install. |
| STD-013 | The installed build launches plainly | partial | Plain Steam launch verified on prior renderer; corrected race/tilt and receipt checked. Broader option/transition cases remain open. |
| STD-014 | Request reciprocal review when progress stalls | adopted | Codex reviewed source and exact install; fixes 1/2 independently checked, findings 3/4 handed back to Claude. |
| STD-016 | Forza Horizon telemetry on by default | unchecked | Packet/default/owner-display audit not established by this renderer check. |
| STD-017 | Hide empty settings pages | unchecked | Setup panel inventory not part of this renderer review. |
| STD-018 | Handling changes never reach online scores | unchecked | Recording/offline/scoring eligibility requires its own investigation before taking pose ownership. |
| STD-020 | Standard portfolio feature checklist | partial | All 25 rows represented; limited and unchecked features remain explicit. |
| STD-021 | Art at 50 is the FFB reference | pending | Native force passthrough, no calibrated mod model. Sampled summaries cannot establish equivalent feel or exact force replay. |
| STD-022 | Off / Surround / Separate monitors selector | pending | Existing native config uses Auto/On/Off; three-way standard not adopted. |
| STD-023 | Frame-rate readout and log | pending | Shared frame-rate window not implemented. |
| STD-024 | Optional on-screen frame-rate counter | pending | Not implemented. |
| STD-025 | Shared tyre-force model and adapters | pending | Current Gravel mod observes/passes through native game FFB. No replacement tyre adapter or qualified raw tyre signals yet; no silent native force replacement in recorder work. |
| STD-033 | Rig profile controls reach the game, and a test proves it | partial | Wheelkit `GravelControlProfile` (main `2a266fc`, 2026-10-10) writes WheelConfig.ini layer 1 through the slots settings.sav names (read only). Configured offline only (dry runs on a copy of the install, `_archive\2026-10-10\gravel-controls-dryrun-0847`); no observed result yet. |
| STD-034 | A rig profile Apply is qualified by a closed loop, not by a file | pending | The proxy has no session-bound test injection and no force interlock; the saves sync to Steam Cloud, so a launch needs a save-safe plan. |
| STD-035 | Profile-controls writers and test injection: identity, provenance and failure rules | partial | Writer side in Wheelkit: reader switch = this mod's receipt hash + `[proxy] product`/`retype=1`; merged-result clashes over every slot a saved action reads (plus `Wheel_Handbrake`) refuse; the save pinned through the commit; bounded save parser. Runtime side (identity at the open, epochs, no-force interlocks) not started. |
