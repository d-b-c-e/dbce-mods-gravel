# Milestone's wheel whitelist and binding model

Milestone's UE4 racers (Gravel, MXGP, MotoGP, Ride, Supercross) use a
two-layer input model. Neither layer is reachable from the in-game rebind
screen when the wheel is misbehaving, but both are plain files.

## Layer 1 — `WheelConfig.ini`: physical button → logical slot

```
<game>\<game>\Config\WindowsNoEditor\WheelConfig.ini
```

One section per supported device, keyed by `<PID><VID>` in lowercase hex.
The Logitech G920 (VID `046D`, PID `C262`) is `[/Wheel.Config/c262046d]`. The
game builds the key at runtime as `/Wheel.Config/%08x` from
`guidProduct.Data1`, which is why it matches the DirectInput product key.

A device not in the file gets **no wheel option at all**. Gravel ships 32
devices, all pre-2018; no direct-drive base is present. `tools/wheelconfig.py`
adds one by cloning a shipped block so every field the parser expects is
present, in order.

### Axis numbering

Derived from the shipped data (cross-checked on the G29, the Fanatec
ClubSport base and the ClubSport V3 pedals), not from documentation:

```
Axis1=X  Axis2=Y  Axis3=Z  Axis4=Rx  Axis5=Ry  Axis6=Rz  Axis7=Slider0  Axis8=Slider1
```

### Polarity — the trap

Each pedal line ends in `&scale&offset`, mapping the axis's 0..1 travel onto
the game's pedal: released must give 0.0, pressed 1.0.

| pedal idles at | use          | who                    |
|----------------|--------------|------------------------|
| minimum (0)    | `&1.0&0.0`   | MOZA                   |
| maximum        | `&-1.0&1.0`  | Logitech, Fanatec      |

Copying a Logitech block onto a MOZA leaves the game convinced a pedal is
permanently held. It will drive, but the rebind screen breaks: the "press a
control" listener latches onto the stuck axis and never sees the button.
**Measure, don't guess** — `tools/Measure-WheelAxes.ps1` records which axis
each pedal moves and which way, and prints the lines.

MOZA R12, measured 2026-08-29:

```
Wheel_Steer=Axis1&1.0&0.0
Wheel_Accelerator=Axis3&1.0&0.0
Wheel_Brake=Axis6&1.0&0.0
Wheel_Handbrake=Axis7&1.0&0.0
```

Buttons are `ButtonN`, 1-indexed as in joy.cpl. Gravel reads
**DIJOYSTATE2** (confirmed by the proxy's `SetDataFormat` log), so buttons
above 32 are reachable.

## Layer 2 — `settings.sav`: logical slot → action

```
%LOCALAPPDATA%\<Game>\Saved\SaveGames\settings.sav
```

A UE4 GVAS save. Its `ignitioninput` block holds every binding as
`ActionName` → `KeyName`, where the key is a keyboard key, a `Gamepad_*` key,
or a `Wheel_*` slot from layer 1. `tools/dump_bindings.py` prints the table.

This is what makes config-file binding possible: find which `Wheel_*` slot
the action listens to, then put the button on that slot in layer 1. Gravel's
defaults hold some surprises:

| action           | listens to              | note                                   |
|------------------|-------------------------|----------------------------------------|
| `GearUp`         | `Wheel_RightShoulder`   | **not** `Wheel_GearUp`, which nothing reads |
| `GearDown`       | `Wheel_LeftShoulder`    |                                        |
| `Handbrake`      | `Wheel_RightTrigger`    | a button; the `Wheel_Handbrake` axis is unused |
| `RewindActivate` | `Wheel_LeftTrigger`     |                                        |
| `SwitchCamera`   | `Wheel_Special_Left`    |                                        |
| `Confirm`        | `Wheel_FaceButton_Bottom` |                                      |
| `Back`           | `Wheel_FaceButton_Right` |                                       |

### Standalone native gear bindings

The external setup's existing button-binding flow now exposes `GearReverse`,
`Gear1` through `Gear8` and `LookBackCamera` only when the read-only saved
layout names them. It writes the existing destination in the selected wheel's
INI section; a missing field or multiple distinct saved destinations refuses
the edit. Duplicate records naming the same destination are harmless. No new
save action, neutral gear, transmission option or native capability is invented.
The clutch axis and saved game-native gearbox options remain independent.

The parser and action resolver live in the existing `tools/SetupUx.psm1` and
are reused by `WheelSetup.ps1`. The existing UX fixture's `-NativeBindingsOnly`
mode passed32 synthetic save/INI checks on Windows PowerShell5.1 and PowerShell7,
including reverse/gears/look-back destinations, ambiguity, no guessed defaults,
unchanged save bytes, clutch/custom-field preservation and CRLF. Original logs:
`artifacts/native-bindings-01/` in `livo-gravel-native-bindings-20261016`.
No native reader, game or installer ran; actual gear/reverse/clutch consumption
and a separate USB shifter remain runtime/adapter gaps.

The affected continuation also preserves existing native paired-button mappings
such as the shipped TS-XW profile's `b660044f_Button1` through7 and reverse8.
Capture reads that existing product and retains its prefix rather than silently
sampling the wheel and replacing it with an unqualified `ButtonN`. Missing or
duplicate connected products refuse before capture. This supports editing an
existing pairing, not creating arbitrary new DS8X routes. Assignment labels name
the product, and conflict checks keep paired buttons distinct from wheel buttons.
Steering, throttle, brake and clutch now follow their saved native destination;
an ambiguous saved axis refuses instead of editing an unused default. The
independent handbrake axis field is kept separate from its saved button action.
The final affected fixture passed45 checks on each supported shell; logs at
`artifacts/native-bindings-02/`, lease released `2026-10-17T00:32:17.1204211Z`.
No hardware, game, production save or installer ran. Actual gear/reverse/clutch
behavior and creating new separate-device pairings remain unqualified.

## Housekeeping

- The file is **CRLF**. A `.*?$` regex eats the `\r` and converts every
  edited line to a bare LF. Match `[^\r\n]*` instead.
- The game **rewrites `WheelConfig.ini` on exit**. Edit only while it is
  closed.
- Steam's *Verify integrity of game files* restores the stock file. Keep a
  copy of the edited one.
- Gravel writes no logs of its own; the proxy's `milestone_mod.log` is the
  only diagnostic.
