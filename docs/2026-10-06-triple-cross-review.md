# Gravel triple-screen cross-review

Codex, 2026-10-06. Reviewed `10f45cf` / runtime source `1d3d1e1`, packaged
at `167b6cc`. Offline review only: no game launch, input, focus, display,
installation or owner-setting changes.

## Findings

1. **P2 — compose side rotation in camera space (`src/triple.cpp:175`).**
   `hookViewOffset` adds the panel angle to Euler `Yaw` while retaining the
   camera's pitch and roll. That rotates around world up, whereas the side
   frustum was constructed in the centre camera's panel coordinates. The views
   therefore stop sharing the same edge rays when the camera pitches or rolls.
   An isolated calculation using the actual `computePanels`, `panelFor` and
   `hookViewOffset` functions, UE's forward/right/up Euler basis, FOV 90 and
   zero bezel measures the centre/right mid-edge mismatch as approximately
   zero at pitch 0, **2.144193 degrees at pitch 10**, and **5.079160 degrees at
   pitch 20**. Zero bezel deliberately removes the physical bezel gap from the
   comparison; a camera-local rotation control restores the common ray at all
   three pitches. Compose the panel yaw about the camera's local up axis (a local
   quaternion/matrix rotation), then convert back to the engine rotator. Add
   pitch/roll seam-ray checks before a bounded in-race visual check. This is a
   numeric defect, not a claim that a particular captured frame measured that
   error.

2. **P2 — null display requests bypass the mode-change guard
   (`src/triple.cpp:542`, `:548`).** Both hooks forward null `DEVMODE` requests
   to Windows. Null is a request to restore the registry display mode, not a
   read-only query; forwarding it leaves a mode-change path despite STD-008.
   Stubbed calls to the two actual hook bodies forward both null/flags-zero
   requests. No Windows display function ran in this test. While the mod owns
   the borderless triple layout, handle reset requests without applying a mode
   change, and distinguish query-only `CDS_TEST` requests. The normal observed
   launch/race result remains valid; the existing logs do not establish that
   this uncovered reset branch ran. See Microsoft's
   [ChangeDisplaySettingsW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-changedisplaysettingsw)
   and [ChangeDisplaySettingsExW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-changedisplaysettingsexw)
   contracts.

3. **P2 — telemetry Off changes menu rendering (`src/triple.cpp:151`).**
   `inGameplay` returns true unconditionally when telemetry or the UE reader
   is disabled. Thus `menu_sides_black=1` stops working when a player disables
   UDP output; unattended output-muted capture would face the same coupling.
   With `g_ue4.live=false`, the actual function returns false with telemetry On
   and true with it Off. Simply removing the condition is insufficient:
   `proxyInit` only starts the telemetry thread when enabled, and that thread
   owns `ue4Poll`. Keep game-state observation alive independently of UDP
   delivery; share it with presentation and future recording. Disabling output
   must not remove the data stream or change the camera/menu policy.

4. **P2 — an existing ResX argument disables span maintenance
   (`src/triple.cpp:661`, `:680`).** The same `spanWindow` boolean controls both
   appending launch arguments and starting `spanThread`. With a valid separate
   triple layout and `span_window=1`, an existing `-ResX=2560` makes it false.
   Resolution and monitor hooks still force a 7680-wide window, but the worker
   that removes borders and corrects placement never starts. The installed-run
   log shows that worker correcting a bordered window, so it is not redundant.
   Separate “should maintain the span” from “should append missing arguments.”
   The source predicate is reproduced offline; the explicit-ResX launch has
   not been tested in a game.

## What checked out

- All **14 installed receipt files** match, including `dinput8.dll` SHA-256
  `ECDDAAFBBEC60F2ED8D89E46A9B6151D66DCED2162F7CEEF4A81BB9902C56CDF`.
- The exact package's **21 file hashes and binary-provenance catalog** validate;
  ZIP SHA-256
  `C511259EA2DCD4CFBDFDB780283340BF9B823B03EB0220BDACA2FCF65F4E4CA9`.
- The local observed-build receipt identifies clean runtime source `1d3d1e1`;
  the retained catalog correctly keeps the older wheelprobe bytes separately.
  Reproducible-build equivalence is not claimed.
- The provenance fixture passes **43 checks** and the installer fixture passes
  **235 checks**, including the real legacy proxy case. Neither fixture
  launches a game or hardware.
- Inspected the existing race frame `build/triple-test-n/n4.png`, plus installed
  and race logs/watchdog completion. These support the observed three-view race
  and successful plain-launch record. Surround, owner comfort/exposure and the
  new edge cases above remain separate qualifications.

## Reproduction and handoff

`python build/triple-review-20261006/reproduce.py` extracts the current source
functions into a standalone C++ fixture, replaces external calls with stubs,
compiles with the existing MinGW compiler and writes `result.txt`. It makes no
Win32 display, input or game calls. The reviewed source hash is
`32CD5A805FCAF2F49DA0C1DB0815B233F9C15597EF315D233CDC3729CADCFDD1`.
The generated fixture is review evidence, not an installed mod or proof of
future corrected behavior. Claude retains implementation/installation ownership.

All **64** reviewed evidence files were copied and hash-verified outside the
checkout at
`%LOCALAPPDATA%/Dbce/StagePlayback/SessionEvidence/gravel-triple-cross-review-20261006`.
Manifest SHA-256:
`3E3552F8379979EE806AC9F7C9A903AA814CC78564FAA48900C3737008F0DB45`.
The archive includes source, the runnable fixture, exact package/receipt and
existing race/installed-run frames and logs. It is private evidence.

For reuse, keep camera-local rotation and frustum edge-ray checks in shared
geometry code, and keep Gravel's vtable/UE hooks in this consumer. Likewise,
game-state observation should feed presentation and recording independently
of the UDP output switch.

The accepted install is left in place. No repeated runtime test is requested
until there is a concrete corrected candidate; preserve owner saves and the
usual idle/lease boundaries on its next necessary check.

## Corrected geometry/reset review — 2026-10-06, 17:12 CT

Claude's **4b7fd75** closes findings 1 and 2 in source. Independent MSVC x64
execution extracts the actual cameraAxes, turnInCameraFrame, hookViewOffset,
computePanels/panelFor and display hooks, with fake external boundaries. With
20,006 pitch/yaw/roll poses, +/-8-degree camera tilt and nonzero seat offsets,
120,036 left/right top/middle/bottom edge pairs have maximum unit-ray difference
`3.632e-7` (float rotator) and identical eye positions across all panels. Zero
bezel isolates shared edge rays. Both display hooks forward zero null/non-null
requests with flags 0, CDS_TEST, CDS_RESET and CDS_FULLSCREEN. These checks do not
call Windows display APIs or a game/device. Renderer-specific hooks remain here;
the basis/edge-ray contract is reusable.

Inspected `build/triple-test-frame/f1-tilt8.png`: cockpit race with centred HUD
and continuous barrier/ground presentation across the thirds. This supports
Claude's 8-degree tilt observation; it is not physical owner acceptance or a
recorded trajectory. The retained watchdog reports game closure at 17:03:53;
a separate numeric exit receipt was not supplied. Current installed receipt at
22:05:15Z has **14/14 matching files**, dinput8 SHA-256
`D39FF1B8D4946CB145862350DFD89B1C571C9C29BBB7F4B68A178AF3543F042B`.
The dist provenance catalogue associates it with source 4b7fd75, adopted at
4dc9df7; this readback does not independently reconstruct that build.
Receipt SHA-256: `BD419D3F1258F4FC1AD65DB686BF0DD75B22C123B8778997E710A45391B4E54C`.

Findings **3 and 4 remain open** on this source: `inGameplay` still returns true
when telemetry is off, and `proxyInit` starts the UE observation thread only with
telemetry enabled; an existing ResX still makes spanWindow false and suppresses
the maintenance thread. Keep the observer independent of output, and separate
missing-argument insertion from worker eligibility. Neither fix was claimed by
Claude in this handoff. No speculative runtime change or new launch here.

Forty-eight shared Sonic/Gravel review files (actual source, standalone compiled
fixture, installed receipts/payloads, this race's frames/log/watchdog and readback)
are preserved at `%LOCALAPPDATA%/Dbce/StagePlayback/SessionEvidence/sonic-gravel-fixed-review-20261006`.
Manifest SHA-256: `187563DEA33D0DDCCFD19FB8F8781B5A46880CFB9D92162D247A2D0B6C0C1AA1`.
Run `fixture/compile-msvc.cmd` from that folder to rerun the inert standalone test.
Sonic's review describes its additional DXGI local-copy/factory2 checks; its guard
remains off. Original reports/archives above remain historical evidence.

All 20 toolkit rows are now represented. STD-001/003/004/009/016-018 remain
unchecked; STD-012 pending; STD-005/006/013/015/019/020 partial. The two unresolved
renderer edge cases are explicit in adoption rather than implied passed.
