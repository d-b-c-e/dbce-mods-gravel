# Troubleshooting

Almost everything here fails **silently**. That is the theme: no error dialog,
no message, just nothing happening. So each symptom below comes with the place
that actually tells you what went wrong.

Start by setting `log=1` under `[proxy]` in `milestone_mod.ini`. A
`milestone_mod.log` then appears **next to the game exe**, and its status line
(every 5 s) shows every channel at once:

```
[tx] input:live(50066 reads) ffb:live(34617 upd, 3 eff) fmod:live(400325 calls)
     ue4:ready+live | steer -0.00 thr 0.98 brk 0.00 hb 0.00 | cf 0.01 pf 0.00
     | rpm 6972/8200 spd 28.8 gear 2 | slip 0.03/0.05 susp 0.20 trq 0.89
```

Read it left to right — the first thing that is not `live` is your problem.

---

## The dash shows nothing

**Check SimHub's own log first.** `C:\Program Files (x86)\SimHub\Logs\SimHub.txt`.
It states the outcome plainly and nothing else in the chain does:

| what it says | meaning |
|---|---|
| `started receiving valid data` + `Game connected` | working |
| `started receiving unprocessed data`, repeatedly | **wrong packet size** |
| nothing at all about the port | packets are not arriving |

**`unprocessed data`** means `format=` does not match the game you picked in
SimHub. The length is checked exactly:

| SimHub game | `format=` | bytes |
|---|---|---|
| Forza Horizon 4 / 5 | `fh4` | 324 |
| Forza Motorsport 7 | `fm7` | 311 |

**Nothing about the port** means the packets are not getting there. Check
`port=` matches SimHub, and look for `[tx] sendto -> 324 (err 0)` in
`milestone_mod.log`. A non-zero `err` is a Windows socket error.

**Everything looks right but the dash is still dead** — is the dash template
bound to properties this feeds? Speed, RPM and gear are the safe ones.

---

## No wheel option in the game's controls menu

Two separate things are needed and both must be right:

1. **The proxy** makes the game *consider* the device. Look for
   `[proxy] retyped ... 0x00010318 -> 0x00010416` in the log. If that line is
   missing, `product=` does not match your wheel — the log prints the target
   at startup, and `Install.ps1` detects it for you.
2. **The whitelist entry** tells the game how to *map* it. Without a matching
   `[/Wheel.Config/<product>]` section in the game's `WheelConfig.ini`, there
   is nothing to select even once the device is accepted.

Then **run the game's wheel calibration once**.

⚠️ The game **rewrites `WheelConfig.ini` when it exits**, so only edit it while
the game is closed. Steam's *Verify integrity of game files* restores the stock
file and silently removes your entry.

---

## The wheel works but the menus jump around, or nothing will rebind

An axis resting at an extreme reads as permanently deflected. The rebind screen
latches onto it and never sees the button you press.

The cause is nearly always **pedal polarity**. Each pedal line in
`WheelConfig.ini` ends in `&scale&offset`, which must map released → 0.0 and
pressed → 1.0:

| pedal idles at | use | who |
|---|---|---|
| minimum | `&1.0&0.0` | MOZA |
| maximum | `&-1.0&1.0` | Logitech, Fanatec |

Copying a Logitech block onto a MOZA leaves the game convinced a pedal is held
down. Don't guess — `tools\Measure-WheelAxes.ps1` walks you through each pedal
and prints the correct lines.

Also check the action is on the slot the game actually reads.
`tools\dump_bindings.py` prints the real table; Gravel has some surprises, for
instance `GearUp` listens to `Wheel_RightShoulder` and nothing at all reads
`Wheel_GearUp`.

---

## Some buttons will not bind

Check `[proxy] SetDataFormat` in the log:

- `DIJOYSTATE2 (128 buttons)` — all buttons are reachable
- `DIJOYSTATE (32 buttons)` — the game cannot see button 33 and above, whatever
  you put in the config

---

## The shaker is silent while the dash works

Then the telemetry is fine and the problem is downstream in SimHub. Check the
ShakeIt effect is assigned to **the right audio output** — an output pointed at
the wrong device is silent with no warning anywhere.

To confirm the data is really there, set `mirror_port=` to a spare port and run:

```
python tools\forza_listen.py <mirror_port> --lines
```

Watch the `rumble`, `slip` and `susp` columns while you drive. If they move,
the mod has done its job.

---

## Nothing at all in the log, or no log file

The proxy is not being loaded. It must sit beside **the executable that loads
`dinput8.dll`** — for UE4 that is `<game>\<game>\Binaries\Win64\`, *not* the
game's root folder. `Install.ps1` gets this right.

⚠️ Only one `dinput8.dll` can exist in a folder. FFB Arcade Plugin, DevReorder
and similar tools claim the same name; the installer refuses to overwrite a
different one rather than break it.

---

## Speed reads wrong

Speed comes from the HUD speedometer, `[ue4] speed=VehicleCurrentSpeedKMH`
with `speed_scale=0.27778` (km/h to m/s). Older ini files name
`VehicleCurrentSpeed`, which Gravel does not have: the log then says
`property "VehicleCurrentSpeed" not found`. Change the name to
`VehicleCurrentSpeedKMH`.

`speed_scale` under `[fmod]` applies only to the fallback. It converts FMOD's
normalised speed (0..1 of each car's top speed) to m/s, so it can only be
right for one car. To tune it, compare the in-game speedometer with SimHub:

```
new_scale = old_scale × (real_kmh / shown_kmh)
```

---

## Adapting to another Milestone game

Set `discover=1` under both `[fmod]` and `[ue4]`, drive one lap, and read the
log. It lists every FMOD parameter with its observed range, and every property
of the class holding RPM. Put the right names in the ini.

Worth knowing from Gravel: these games set **no RPM parameter** through FMOD,
but they drive engine audio from real physics, so parameters like
`SuspensionMovement` and `LongitudinalSlip` *are* the telemetry. Low call counts
mark event-driven parameters — collisions and gear changes — while high counts
are continuous channels.
