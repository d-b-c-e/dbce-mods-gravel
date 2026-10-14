# Recording development checkpoint — October 7, 2026

Gravel has a source candidate for **sampled signal capture**. It builds and
passes native fake-COM and producer-to-managed-reader tests. It has not yet been
installed or qualified in the game. The accepted renderer installation remains
unchanged. There is no gameplay route player yet.

## What is captured

The observation worker runs even with telemetry off. UDP delivery is a separate
sink, and triple-menu state no longer treats disabled telemetry as gameplay.
A capture retains independently sampled DirectInput reads, native effect-request
summaries, FMOD audio parameters, and the available UE HUD fields. Known zero,
missing, invalid and stale observations are distinct. Per-source ages are seconds
since an observation, not a promise that the underlying game value is current.
Fields can come from adjacent instants; the worker is **not** a physics tick.

Input channels are the game's DirectInput reads normalized using the proxy INI,
not the final steering/brake command consumed by physics. Audio slip/suspension/
impact parameters are not per-wheel physical signals. Constant force is the
existing strongest cached request summary; effect Stop/Release and ordered
collision events are not reconstructed. Table overflow is exposed. These files
are deliberately ineligible for full FFB replay or Art normalization evidence.

The worker writes the shared `dbce.wheel.session` v1 format using a separately
pinned native toolkit header. The existing `lib/toolkit/VERSION` stays at its
old device/packet pin. `lib/session/provenance.json` pins the new headers and
managed reader; it is an unreleased development component, not an FFB DLL bump.
The synchronous writer runs only on the observation worker. Actual timestamps
and `sample.interval` expose disk/discovery delays; missed deadlines are skipped.
Files are exclusive, size/time bounded and require a verified completion footer.

## Developer commands

First run the offline checks; they open no devices and launch no game:

```powershell
./tools/tests/Test-NativeCapture.ps1
./tools/tests/Test-RecordingRequest.ps1
./build.ps1
```

Do not deploy the candidate for an unattended wheel test until the full mute
boundary is reviewed. The fake checks verify original force observations,
zero-gain copied effect parameters, stripped auto-start, blocked explicit starts/
escape/device resume, guarded device properties/acquisition, effect-table
capacity and ordinary-session passthrough. Repeated acquisition checks the
existing autocentre-off state if a setter refuses an already acquired device.
The SDK-interface ABI suite now catches the original incorrect device Escape
slot (24, not 23), and exercises factory-created devices and legacy-interface
refusal. `DllGetClassObject` is guarded as well as `DirectInput8Create`.
Both ANSI and Unicode `EnumDevicesBySemantics` callbacks now guard the returned
device before the game sees it; an unguardable device returns an error without
a callback. The configuration UI path is refused only in capture mode. SDK
tests exercise both interface families, including enumeration before any
CreateDevice call.
COM activation resolving system32 directly, shared-vtable concurrency/lifetime,
device-property lifecycle/restoration
and actual game behaviour remain review/runtime gates. The EXE imports both
DirectInput8Create and CoCreateInstance; do not assume the proxy factory catches
the latter. No physical acceptance is implied.

After a reviewed candidate is installed in a coordinated closed-game slot:

```powershell
$capture = ./tools/game/Arm-SignalRecording.ps1 -GameDirectory '<Steam Win64 folder>' `
  -ExpectedModSha256 '<reviewed installed dinput8.dll SHA256>' -Seconds 60
# Plain Steam launch; select a solo offline race. No display/settings changes.
# The capture includes startup, then 60 seconds after the first live UE HUD.
# Close Gravel normally after capture completion. Do not terminate a live wheel game.
./tools/game/Inspect-SignalRecording.ps1 -Result $capture.Result
```

Arming launches nothing, installs nothing and does not edit player settings.
The five-minute request is separate from `milestone_mod.ini`; it is claimed
once by an atomic rename and archived under
`%LOCALAPPDATA%/Dbce/StagePlayback/Gravel/<id>`. The receipt saves exact executable,
DLL and configuration hashes and an original config copy. Physical sink muting
and UDP suppression remain latched until the game process exits, including after
capture completion or failure. Invalid/expired requests also fail closed for
outputs. A normal launch with no request uses the owner's normal settings.

To cancel an **unclaimed** request, with the game closed:

```powershell
./tools/game/Arm-SignalRecording.ps1 -GameDirectory '<Steam Win64 folder>' `
  -Action Cancel -Id $capture.Id
```

Cancellation archives the matching request; a different capture ID is refused.
A claimed request cannot be rearmed by a second launch. A failed archive can
leave `milestone_recording.request.ini.claimed-<id>` beside the EXE: retain it
for diagnosis; it is not read as a new request. If the game exits before a
footer, keep the incomplete file as evidence and start a fresh capture ID.

The inspector verifies the shared wire contract and Gravel producer identity,
then reports availability, gaps and overflow. `complete-sampled-signals` means
the file closed correctly. Its trajectory, force-replay and hardware-mute
qualification fields remain false. It does not launch a game or drive a device.

## Force mute for unattended runs: status (2026-10-14)

Gravel is **not** cleared for unattended wheel runs: placement and other checks stay attended, or wait for a qualified
mute (sana 90041). What exists and what is open:

- **The latch.** Any `milestone_recording.request.ini` beside the DLL latches `recordingMuted()` for the whole process
  (`src/recording.cpp`, `recordingInit`), including a malformed or expired request, which logs "invalid/expired
  request; ... outputs muted" and captures nothing. While latched, the proxy guards the DirectInput surface: guarded
  capture interfaces only; `SendForceFeedbackCommand` a no-op; `Escape`/`SendDeviceData` refused; force gain and
  auto-centring written as 0; created and updated effects get zero-gain copies; `Start` a no-op; semantic enumeration
  guarded; `ConfigureDevices` refused; `DllGetClassObject` guarded for `CLSID_DirectInput8`. The installed
  `dinput8.dll` (`B35F4D0B`, milestone tools 0.2.0, 2026-10-10) contains the latch.
- **Open: device-property restoration** after the latched session, and the **live acquisition path** (acquire,
  unacquire and re-acquire on the real device). Fake tests cannot prove either; the capture inspector keeps reporting
  `hardwareMuteQualified=false`.
- **COM activation (supporting evidence only).** A read-only scan on 2026-10-14 found neither `CLSID_DirectInput8`
  nor `CLSID_DirectInputDevice8` in `gravel-Win64-Shipping.exe` (`28032B88`), which imports `DirectInput8Create` from
  `DINPUT8.dll`, nor in the other game-folder modules that import `CoCreateInstance` (TrueSky, FMOD). That makes a
  registry activation of DirectInput unlikely, but it is **not proof** that no COM path exists: a GUID built at run
  time, or a module loaded from outside the game folder (the Steam overlay, drivers), is not excluded.
- **Not reused.** The toolkit's `DBCE_FFB_MUTE` is a contract (STD-035 item 8), not a native facility, and its
  dinput8 fence (`native/dinput-proxy` `dbce_fence.hpp`/`dbce_test_switches.hpp`, as in GRID, DiRT and V-Rally 4)
  is newer than this repo's `lib/toolkit` v0.8.0. Adopting it here would be adapter work, not done.

## Remaining route-playback work

Find and verify the local player's physics callback, body pose/velocity and
vehicle/stage identity, with an explicit offline/scoring gate before taking any
pose/input ownership. Record at that physics boundary and preserve original
signals before output gating. Implement the cold-start route and exact restoration
through the existing lease runner. Only a repeatable moving-car capture/replay,
checked against its recorded route, can close STD-012. Replaying inputs or a
polled HUD stream cannot establish that.
