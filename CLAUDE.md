# Working on this codebase

Orientation for anyone — human or agent — picking this up cold. The README
explains what the project *does*; this explains how it works, what has actually
been verified, and which traps cost real debugging time.

## The one-line summary

Milestone's UE4 racing games refuse direct-drive wheels, cannot rebind controls
with one attached, and ship no telemetry. A `dinput8.dll` proxy beside the game
exe fixes the wheel and manufactures telemetry from three sources, emitting it
as Forza "Data Out" UDP; `WheelSetup.ps1` handles control mapping from outside
the game entirely.

The repo is `milestone-wheel-tools` - renamed from `milestone-telemetry`, since
the wheel fix is the more widely useful half. The config and log files are
still `milestone_mod.ini` / `milestone_mod.log`; renaming those would break
every existing install.

## Architecture

Four modules in one DLL. They are independent and can be worked on separately.

| file | job | how it gets its data |
|---|---|---|
| `src/proxy.cpp` | re-type the wheel; tap inputs and FFB | patches DirectInput vtables |
| `src/fmod_tap.cpp` | engine-audio parameters | hooks the delay-load IAT |
| `src/ue4.cpp` | RPM / gear from the HUD | UE4 reflection, no signatures |
| `src/telemetry.cpp` | assemble and send packets | 60 Hz thread, reads shared atomics |
| `WheelSetup.ps1` | bind axes/buttons and preview input | drives `tools/wheelprobe`, parses `settings.sav` |
| `tools/wheelprobe/` | live 128-button / 8-axis reader | `c_dfDIJoystick2`, which PowerShell cannot reach |

All taps are **observe-only**. The single thing altered in the game's view of
the world is `dwDevType`. Hooks run on the game's threads and publish into
plain atomics; the sender thread reads them.

## Build, deploy, test

The 2026-09-16 settings work adopts the toolkit's Simple/Advanced presentation
contract in the existing **external** PowerShell tool. Read
`docs/UX-OVERNIGHT-2026-09-16.md` before claiming full UX-1 compliance: there is
no F6 renderer or camera/FFB control layer here. The shared guidance commit is
`a84bebab5ec2abdcd5140b9c63c139ccff86a7d3`; the runtime toolkit pin stays v0.8.0.
Presentation state lives in LocalAppData, separately from game configuration.
`tools/SetupUx.psm1` owns pure view/mapping helpers; do not couple view choice to
runtime defaults or device selection. `Install.ps1` preserves existing tuning
unless a specific override was supplied. Ordinary uninstall retains settings.
The Telemetry page reads the selected game's installed INI, supports saved
Off/On in Simple, and applies Advanced connection drafts atomically. The native
sender calls `loadConfig()` only once from `proxyInit()`, so label these settings
for the **next game launch**, never live delivery/stop controls. Its unchanged
`inet_pton(AF_INET)` route accepts IPv4 addresses, not DNS names or IPv6.
Controls offers an on-demand, read-only device preview using staged mappings.
`Read-WheelInput` in SetupUx.psm1 owns the single input-helper process for
preview and capture, with cancellation and a supervised timeout. The preview
understands only the normal/inverted Axis1-8 transforms this setup tool writes;
do not present custom transforms as known or describe device normalization as
verified game-final input. No preview action writes owner settings.

Offline checks (synthetic input-reader process and disposable fixture only):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/tests/Test-SetupUx.ps1
pwsh -NoProfile -File tools/tests/Test-SetupUx.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/tests/Test-InstallPackage.ps1
pwsh -NoProfile -File tools/tests/Test-InstallPackage.ps1
```

These check file migration, capture cancellation and terminal navigation, not
physical input, terminal readability at different DPI, force output or a drive.

```powershell
.\build.ps1 -UpdateDist       # x64 proxy + input helper, PE/export/import checks
# Commit source and dist before making an attributable package:
.\tools\Package.ps1          # complete ZIP and per-file SHA256 manifest
```

The 0.2.0 package supports **Gravel only**. `Install.ps1` requires its shipping
executable and preserves existing config/profile bytes. `tools/InstallPackage.psm1`
preflights all targets, backs up before writes, uses atomic replacement and rolls
back partial changes. It also creates an empty-binding whitelist profile without
Python when the existing verified G920 template is present. The owned setup tool
is deployed to `DBCE-Wheel-Setup` beside the game exe; the game-root launcher passes
the exact game/config paths. Uninstall uses the receipt hashes to leave edited
files alone. No game assets or owner configurations enter the distributable.
Install requires the generated manifest and checks product/version/source commit,
the exact expected file set and every SHA256 before discovery or mutation.
Uninstall backs up all planned removals first and restores completed deletions
if a later file is locked. Both install and removal rollback respect the closed
game guard and preserve externally changed files, reporting recovery backups.

The profile adapter has **no endpoint/centre/deadzone writer**. Its known fields
are `AxisN&scale&offset`, wheel rotation limits and bindings. Native input hooks
observe values; they do not apply calibration. An affine transform alone cannot
represent independent left/centre/right endpoints or deadzones. Keep the UI at
Bind / Save binding and distinguish device preview from game-final calibration.
Do not invent calibration values in `settings.sav`; it is read only for actions.

`lib/toolkit/` is vendored from `E:\Source\dbce-wheel-mod-toolkit` (the shared
racing-mod core): `include/forza_packet.h` is the Forza encoder `telemetry.cpp`
compiles against, `powershell/DbceWheel.psm1` is what the three scripts import
for Steam discovery and DirectInput enumeration. Fix packet or enumeration bugs
**there**, release, then `tools\Sync-Toolkit.ps1 -Version vX.Y.Z -Parts
include,powershell` here. Do not edit the vendored files in place.

`build.sh` hardcodes `/e/msys64` — that is the development machine, adjust it.
A 32-bit game needs a 32-bit build.

The test loop that worked:

```powershell
# 1. close the game FIRST - it locks the DLL and rewrites its own configs on exit
# 2. deploy from the complete, verified package
.\Install.ps1 -Game Gravel
# 3. launch, drive, then read the log next to the game exe
```

`milestone_mod.log` (with `log=1`) has a status line every 5 s showing every
channel at once. Read it left to right; the first thing not `live` is the
problem. `mirror_port=` sends a copy to a second port so
`tools/forza_listen.py <port> --lines` can decode packets while SimHub holds
the real one.

**Never hard-kill a game while a wheel is attached.** A running DirectInput
constant-force effect is not released when the process dies, so the base keeps
applying the last torque and the next launch reads as "no FFB". Close it
gracefully.

## Verified facts (Gravel, UE 4.17, Steam 558260)

Measured on hardware 2026-08-30 with a MOZA R12. Do not re-derive these.

**UE4 reflection**
- `GNames` and `GObjects` are found in ~10 ms by scanning `.data` for a
  structure that *validates* — not by code signature, so it survives rebuilds.
- `UProperty::Offset_Internal` is at **`+0x50`** here. No published table lists
  that; 4.19+ is `+0x44`, older SDK dumps say `+0x4C`. It is probed at runtime
  using a sibling property that must read a *different* in-range offset — a
  lone plausible value passes by accident.
- The HUD class is **`IgnitionGaugeLogicWidget`**:
  `VehicleCurrentRPM +0x300`, `VehicleMaxRPM +0x2fc`, `VehicleCurrentGear +0x30c`.
  It has **no speed property** — that is why speed comes from FMOD.
- ~123k objects in the front end, ~141k with a track loaded.

**FMOD parameters** — the key insight: these games set **no RPM parameter**,
but they drive engine audio from real physics, so the parameters *are* the
telemetry. A parameter's **call count** tells you its nature.

| parameter | calls/race | range | use |
|---|---|---|---|
| `VehicleSpeed` | ~175,000 | 0–0.70 | speed, normalised to top speed |
| `Wet` | ~161,000 | 0–0 | unused (dry track) |
| `LateralSlip` | ~65,000 | 0–0.67 | slide angle |
| `LongitudinalSlip` | ~65,000 | −1–1 | wheelspin / lockup |
| `torque`, `DamageState` | ~13,500 | 0–1, 0–0 | engine torque |
| `SuspensionMovement` | ~13,000 | 0–1 | suspension travel |
| `BrakingForce` | ~900 | 0–1 | brake load |
| **`Intensity`** | ~550 | 0–1 | **collision** — see below |
| **`Speed`** | ~450 | 0–0.49 | **collision velocity** |
| `GearUpDown` | ~120 | −1–1 | shift event |

Car-specific extras appear too (`TurbineRpm`, `AntiLagIntervention`,
`Throttle`), so the set is not fixed across vehicles.

**Collisions** — over 368 logged impacts, `Intensity` is effectively binary
(median 1.000, mean 0.878): it says *that* you hit something, not how hard.
The collision `Speed` parameter (median 0.23, max 0.46) is the real
discriminator, so magnitude is weighted towards it. Current scaling gives a
mean of ~4 g across a 0.7–77.7 m/s² range.

**DirectInput**
- The MOZA R12 reports `dwDevType 0x18` (1STPERSON/SIXDOF), 8 axes, 128
  buttons. Six or more axes is what triggers the misclassification.
- Gravel reads **DIJOYSTATE2** (272 bytes), so all 128 buttons are reachable.
- Axis ranges stay at DirectInput's default 0..65535.
- Pedals idle **low** on MOZA, **high** on Logitech/Fanatec.

**Packet** — Horizon is exactly **324 bytes** (`232 + 12 pad + 79 dash + 1
trailing`). 323 is rejected silently. Confirmed by probing every candidate size
against SimHub's `ForzaReader`.

## Traps

Each of these cost real time and none produces an error message.

1. **The delay-load thunk self-unhooks.** `fmodstudio64.dll` is a delay-load
   import. Calling the original slot value resolves the import *and* overwrites
   our hook. Resolve the real function with `GetProcAddress` once the DLL is
   loaded; only fall back to the thunk if it is not, then re-install.

2. **Vtable indices are load-bearing.** `IDirectInput8`: 3 CreateDevice,
   4 EnumDevices. `IDirectInputDevice8`: 3 GetCapabilities, 5 GetProperty,
   6 SetProperty, 9 GetDeviceState, 11 SetDataFormat, 15 GetDeviceInfo,
   18 CreateEffect. `IDirectInputEffect`: 6 SetParameters. Vtables are shared
   per class, so every hook must re-check the object.

3. **Blank device twins.** Gravel creates three device objects for the same
   wheel; only one is acquired. The others return all-zero state at poll rate
   and will overwrite good input. Once an object has produced a non-blank
   state, ignore the ones that never have.

4. **Two constant-force effects run at once.** A single shared magnitude takes
   whichever updated last — usually the smaller. Track per effect, strongest
   wins.

5. **`WheelConfig.ini` is CRLF**, and the game **rewrites it on exit**. A
   `.*?$` regex eats the `\r` and silently converts edited lines to bare LF;
   match `[^\r\n]*`. Steam's file verification restores the stock file.

6. **PowerShell 7 cannot marshal a delegate through a COM interface.** For the
   DirectInput tools, pass the enumeration callback as a raw function pointer
   via `Marshal.GetFunctionPointerForDelegate`, and give `DirectInput8Create` a
   real `GetModuleHandleW(IntPtr.Zero)` — a null string marshals to
   `E_INVALIDARG`.

7. **Steam is often not under Program Files.** Read `SteamPath` /
   `InstallPath` from the registry; it was on `D:` on the development machine
   and a Program-Files-only scan found nothing.

8. **FMOD stops updating between races.** Without clearing them, the last
   in-race values persist through the menus and a shaker sits on a constant
   output.

## What is assumed rather than measured

- `speed_scale = 55.0` converts normalised `VehicleSpeed` to m/s. Eyeballed
  against the in-game speedo; close but not calibrated.
- `Dash.torque` is scaled to a nominal 400 Nm — the *shape* is real, the units
  are invented.
- `EngineIdleRpm` is 12 % of max, a placeholder.
- `WheelRotationSpeed` assumes a 0.33 m wheel radius.
- Only Gravel has been tested. MXGP / MotoGP / Ride / Supercross are untried.

## Adding a game

1. Set `discover=1` under both `[fmod]` and `[ue4]`, drive a lap, read the log.
   It lists every FMOD parameter with its range and every property of the class
   holding RPM.
2. Put the right names in a new `games/<name>/milestone_mod.ini`.
3. Add a verified discovery/layout/preset adapter and fixtures before widening
   the installer's Gravel-only support gate.
4. Record verified values in `games/<name>/README.md`.

Expect the FMOD names to differ per title but the *approach* to hold: high call
counts are continuous channels, low counts are events.

## Conventions

- Comments explain **why**, not what. The traps above are why the code looks
  the way it does.
- Prefer measuring over reasoning. Every wrong turn in this project came from
  inferring a value that could have been read from a log.
- Config is read at game start, so tuning needs no rebuild.
- Keep `dist/dinput8.dll` in step with `src/` when committing — the installer
  and the release both use it.

## Toolkit standards

At the start of every session, compare the wheel toolkit's ledger
(`E:\Source\toolkits\dbce-wheel-mod-toolkit\STANDARDS.md`) with this repo's
`TOOLKIT-ADOPTION.md`. Report any entry that is `pending`, `unchecked` or missing
from the adoption file, and bring it in when your work touches that area. When you
adopt one (or find it does not apply), update `TOOLKIT-ADOPTION.md` in the same
commit. When you set a new family-wide standard, append it to the toolkit ledger
and commit it in the same turn; do not leave it only in an uncommitted file.
