# Gravel mod

**One Gravel package for wheel setup and optional telemetry.**

The supported package makes modern direct-drive wheels selectable in Gravel,
provides an external control-binding tool, and sends telemetry to compatible
Forza receivers. Shared Milestone research code and history are retained;
motorcycle games are inactive and excluded from the active portfolio.

| you want | run |
|---|---|
| the game to see your wheel, and telemetry in SimHub | double-click `Install.bat` |
| to map controls without fighting the in-game menu | open `Wheel settings.bat` in Gravel's game folder |

The installer finds Gravel through Steam and preserves its saved wheel identity
on updates. A fresh install asks you to choose if several eligible wheels are
connected. The setup tool can also be opened directly with `WheelSetup.bat`.

## First drive

Close the game, extract the complete ZIP, run **Install.bat**, then open
**Wheel settings.bat** in Gravel's game folder. Setup opens in
**Simple**: bind Steering, Throttle and Brake, add any handbrake or buttons you
use, then **Save mappings and exit**. Run the game's own wheel calibration once and drive.
Telemetry is optional. **Simple → Telemetry** shows the saved Off/On choice,
receiver and destination. Match those receiver settings in SimHub. Off/On saves
for the next game launch; this external tool does not control a running sender.

The six pages are **Setup, Controls, FFB, Cameras, Telemetry, Help**. Use
**View: Simple / Advanced** to reveal raw mappings and technical details. The
choice is remembered; switching views changes no bindings, tune or telemetry.
If a damaged view preference is reported, explicitly select Simple or Advanced
to repair it; the previous file is backed up and its location is shown.
Button numbers start at 1. Each axis has its own binding flow, so an
optional clutch or handbrake never holds up basic setup. Cancel keeps the
previous assignment; the final Save mappings and exit writes the pending mappings.
Before saving, **Controls → Preview device input** lets you check the staged
bindings for 10 seconds: Steering shows Left/Centre/Right and pedals/handbrake
show Released through Full. **Esc** returns. This preview reads the device;
the game's final calibration and effective input remain unverified. Custom
assignments the tool cannot interpret show Preview unavailable. Preview never
saves or applies anything; Advanced additionally shows raw device values.
For a custom receiver, select **Telemetry → Connection settings (Advanced)**,
edit the receiver IPv4 address, port and format, then **Apply connection** or
**Cancel**. Connection changes save together and keep Telemetry Off/On unchanged.
Applied telemetry settings are already saved even if you discard pending wheel
mappings when leaving setup.

**This is an external setup tool, not an F6 game panel.** The game still owns
FFB, camera views, final endpoint/deadzone calibration and input combination.
This mod adds no Bonnet/Bumper mounts or camera adjustment shortcuts. See the
[UX adoption notes](docs/UX-OVERNIGHT-2026-09-16.md) for the exact scope and
remaining shared-standard gaps.

> **Wheel not detected in Gravel?** That is the first thing this fixes — see
> [what it actually does](#what-it-actually-does) below, or jump to
> [troubleshooting](docs/troubleshooting.md).

---

## What it actually does

**1. Makes a direct-drive wheel selectable.**
DirectInput classifies any joystick with six or more axes as a
*six-degrees-of-freedom controller*, not a wheel. A MOZA R12 exposes eight, so
games written before direct-drive existed are told it is a SpaceOrb and offer
it as neither wheel nor gamepad — in Gravel the controls menu flickers
endlessly between keyboard and controller. The proxy re-types it as
`DI8DEVTYPE_DRIVING`. Force feedback passes through untouched, which routing
the wheel through XInput would cost you.
→ [docs/directinput-sixdof.md](docs/directinput-sixdof.md)

**2. Reads what the game already knows.**
Steering and pedal positions come from the same `GetDeviceState` calls the game
itself makes; force magnitudes come from the effects it sends to your wheel.
That is the game's own data in flight, not anything reconstructed.

**3. Finds the vehicle state.**
RPM, gear and max RPM are read from the game's own dashboard widget through
UE4's reflection system — located by property *name*, so there are no
version-specific offsets to break.

**4. Uses the engine audio as a physics feed.**
The real find: these games have no telemetry, but they drive their engine
*sound* from live physics. So the FMOD parameters **are** telemetry — wheel
slip, suspension travel, engine torque, brake load and collisions, all per
frame. That is far better material for a bass shaker than anything inferred
from force feedback.

All of it goes out as **Forza "Data Out"** UDP — the most widely parsed
telemetry format there is — so SimHub, ShakeIt, dashboards and motion rigs pick
it up with no integration work.
→ [docs/forza-format.md](docs/forza-format.md)

## Status

| game | engine | wheel fix | inputs | FFB | physics | dash | impacts |
|---|---|---|---|---|---|---|---|
| **Gravel** | UE 4.17 | ✅ | ✅ | ✅ | ✅ | ✅ | 🔧 |

✅ verified on hardware · 🔧 implemented, being validated · ▫ untested

Verified with a MOZA R12: working speedometer and tachometer in SimHub, with
speed, RPM, gear, pedals, wheel slip and suspension travel all live.

MXGP, MotoGP, Ride and Supercross are inactive historical research targets.
Their shared code and documentation remain available; this product supports
Gravel only. Shared engine technology does not establish title compatibility.

The MOZA R12 hardware evidence is dated August 30, 2026. The September 19
0.2.0 deployment has installer and offline fixture evidence, with a drive and
physical-force acceptance still pending on that package. Native FFB passes
through unchanged. Normalized rig FFB, true triples and recorded gameplay
acceptance are not established by this release.

Repository naming may change during portfolio organization. Existing package,
configuration, receipt and launcher names remain compatibility contracts.

## Install

Extract the **whole package**, then double-click **Install.bat**, or run:

```powershell
.\Install.ps1
```

It finds Gravel in Steam, installs the DLL and the complete external setup tool,
and creates **Wheel settings.bat** in the game's main folder. No Python,
compiler or SDK is needed. Existing INIs and wheel profiles are preserved byte
for byte unless an explicit override is supplied. New wheel profiles start
with empty bindings; the setup tool measures your assignments.

Every install backs up existing targets under **DBCE-Wheel-Backups** beside the
shipping executable. A failed write restores earlier files from that transaction.
Close the game first; the installer rechecks before each write. The installed
setup files and their hashes are listed in **milestone_install.json**.
The complete package manifest is required and every payload hash is checked
before discovery or changes. Source developers must package before installing.

Updates verify existing proxy, setup and launcher files against a validated
prior install receipt. Unknown or edited files stop the update without replacing
them. A damaged receipt also stops installation/removal. Without a receipt, only
the exact known 0.1.0/0.2.0 proxy hashes are recognized; marker text is not ownership.
Receiptless setup or launcher files are retained and block installation.
Uninstall keeps edited receipt-owned files and removes only matching files;
unknown receiptless proxies are left untouched. Keep receipts and backups when
moving an installation; resolve a refused update before retrying.

Advanced installer switches (ordinary updates keep existing settings):

```powershell
.\Install.ps1 -Port 8000 -Format fh4              # match your SimHub setup
.\Install.ps1 -Game Gravel                        # supported game
.\Install.ps1 -GamePath "D:\...\gravel\Binaries\Win64" # explicit Gravel folder
.\Install.ps1 -Product 0006346e                   # if it picks the wrong device
```

The runtime DLL belongs beside `gravel-Win64-Shipping.exe` in
`Gravel\gravel\Binaries\Win64`; the launcher belongs in `Gravel`.
Keep the complete package when moving or reinstalling; copying the DLL alone
omits control setup, documentation and update recovery.

⚠️ Only one `dinput8.dll` can live in a folder. FFB Arcade Plugin, DevReorder
and similar tools use the same name; the installer refuses to overwrite a
different one rather than break it.

⚠️ Never use this in a game with kernel anti-cheat.

## Mapping your controls

Milestone's rebind screen is unusable with many direct-drive wheels — an axis
resting at an extreme reads as permanently deflected, so the "press a control"
listener latches onto it and never sees the button you press. Do it from
outside the game instead:

```powershell
.\WheelSetup.ps1
```

It finds the game, reads the game's own action list out of its save file,
detects your wheel, and then:

- **Controls → Steering / Throttle / Brake / Handbrake (axis) / Clutch** — select
  only the input you want to bind. Measure rest and full movement, check its
  direction, then Save binding or Cancel. Esc cancels a running input
  capture. Run the game calibration afterwards for endpoints and deadzones.
- **Controls → Driving and menu buttons** — choose *Rewind*, *Change camera*
  or another action, press the button, then save or cancel. A handbrake button
  can stay assigned alongside its axis. The game determines how they combine.
- **Advanced → Controls → Raw mapping details** — inspect the underlying
  profile when troubleshooting; Simple shows readable assignments instead.

That second one hides an indirection you would otherwise have to work out
yourself. These games bind in two layers — physical button → logical slot
(`WheelConfig.ini`), then logical slot → game action (`settings.sav`) — so
putting rewind on a button really means finding which `Wheel_*` slot
`RewindActivate` listens to and pointing *that* at your button. The defaults
hold surprises: in Gravel `GearUp` listens to `Wheel_RightShoulder`, and
nothing at all reads `Wheel_GearUp`.

Reading all 128 buttons needs `c_dfDIJoystick2`, which is why
`tools/wheelprobe` is a small native helper rather than pure PowerShell.

Run the game's wheel calibration once afterwards. Changes are staged until
Save mappings and exit. A failed save leaves the old file intact, and each successful
save creates a dated backup. If the file changes while setup is open, reopen
setup before saving so another tool's changes are not overwritten.

## Setting up SimHub

Pick a Forza game in SimHub, note the UDP port it shows, and match both in
**WheelSetup.bat → Telemetry → Connection settings (Advanced)**. Use `127.0.0.1`
for a receiver on this PC. This mod's sender supports IPv4 addresses only,
not hostnames or IPv6. Configuration is read once when the game launches.

| SimHub game | `format=` | bytes |
|---|---|---|
| Forza Horizon 4 / **Horizon 5** | `fh4` | 324 |
| Forza Motorsport 7 | `fm7` | 311 |

The external tool shows saved configuration, not delivery or receiver
acknowledgment. Advanced also offers the original 232-byte Sled format for a
compatible physics-only receiver. Rate, mirror output and channel mappings
remain advanced `milestone_mod.ini` settings; ordinary setup preserves them.

**The length must match the game you picked.** A mismatch fails silently — no
error, the dash simply stays dead. SimHub's own log (`SimHub\Logs\SimHub.txt`)
is the one place that says which happened: `started receiving valid data` and
`Game connected`, or `started receiving unprocessed data` at packet rate.

For the shaker, the channels worth binding in ShakeIt are **wheel slip**
(wheelspin and lockup), **suspension travel** (surface texture and landings)
and **acceleration** (impacts). Those are real physics, not derived.

## Something not working?

→ **[docs/troubleshooting.md](docs/troubleshooting.md)**

Nearly every failure in this chain is silent, so that page is organised by
symptom and, for each, names the log that actually tells you the answer. Set
`log=1` in the ini and `milestone_mod.log` appears next to the game exe with a
status line covering every channel at once.

## Uninstall

Double-click **Uninstall.bat** (or run `.\Uninstall.ps1`). Personal mod settings,
backups and wheel bindings are kept for a future reinstall. Updates also keep
existing tuning, telemetry Off/On, custom destinations and the saved wheel;
explicit `-Port`, `-Format` or `-Product` switches update only those choices.

Uninstall removes matching installed files and the launcher. Files edited since
installation are kept and reported. The `WheelConfig.ini` entry stays; Steam's
*Verify integrity of game files* restores that file to the game's stock version.
Uninstall also backs up its planned removals and restores earlier files if a
later removal fails. If Gravel starts during a transaction, further writes stop
and the error identifies recovery backups; close the game before recovery.

## Build

Only needed when working from source. MSYS2 MinGW-w64 GCC 16 is required;
pass `-Toolchain` to use another bin folder. Build checks x64 format, all six
proxy exports and missing compiler runtime dependencies, without opening a device.

```powershell
.\build.ps1 -UpdateDist        # build proxy + input helper, refresh dist/
.\tools\tests\Test-SetupUx.ps1
.\tools\tests\Test-InstallPackage.ps1
# Commit source and dist before packaging:
.\tools\Package.ps1           # complete ZIP + SHA256 manifest in artifacts/
```

A 32-bit game needs a 32-bit build. The Forza encoder and the installer's
Steam/DirectInput helpers are vendored from
[dbce-wheel-mod-toolkit](https://github.com/d-b-c-e/dbce-wheel-mod-toolkit)
under `lib/toolkit` (pinned in `lib/toolkit/VERSION`); bump with
`tools\Sync-Toolkit.ps1 -Version vX.Y.Z -Parts include,powershell`.

## Tools

| tool | purpose |
|---|---|
| `WheelSetup.ps1` | bind axes/buttons and preview device input; game owns endpoints/deadzones |
| `Install.ps1` / `Uninstall.ps1` | install or remove the mod, detecting game and wheel |
| `tools/Sync-Toolkit.ps1` | re-vendor the toolkit pieces under `lib/toolkit` |
| `tools/wheelprobe/` | native helper that reads all 128 buttons and 8 axes live |
| `tools/forza_listen.py` | decode and print the packets — validate without SimHub |
| `tools/Measure-WheelAxes.ps1` | walks you through each pedal, works out which axis moves and which way, prints the config lines |
| `tools/wheelconfig.py` | add or update a device in `WheelConfig.ini` (CRLF-safe, clones a shipped block) |
| `tools/dump_bindings.py` | print the real action → button bindings from `settings.sav` |

## Adapting to another game

Set `discover=1` under both `[fmod]` and `[ue4]`, drive one lap, and read
`milestone_mod.log`. It lists every FMOD parameter with its observed range and
every property of the class holding RPM; put the right names in the ini.

Two things learned from Gravel that should generalise: these games set **no RPM
parameter** through FMOD (RPM comes from the HUD widget instead), and a
parameter's **call count** tells you its nature — hundreds of thousands means a
continuous channel, a few hundred means an event such as a collision or gear
change.

The UE4 scanner keys on data *shapes* rather than code signatures, so it
survives across builds, and the property-offset layout is probed at runtime
(Gravel's 4.17 sits at `+0x50`, which no published table lists).

## How it is put together

```
src/proxy.cpp        DirectInput proxy: re-type, input tap, FFB tap
src/fmod_tap.cpp     FMOD Studio parameter hook (delay-load IAT)
src/ue4.cpp          UE4 reflection: GNames/GObjects, property lookup
src/telemetry.cpp    60 Hz sender, Forza packet assembly
games/<game>/        per-game config and notes
tools/               listener, axis measurer, config editors
docs/                the techniques, written down properly
```

MIT licensed. Not affiliated with Milestone, Microsoft or SimHub.
