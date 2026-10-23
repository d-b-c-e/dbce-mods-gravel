# Forza "Data Out" packet, as emitted here

Nothing about this mod is Forza-specific; the packet is used because it is
the most widely parsed telemetry layout there is (SimHub, ShakeIt,
SimRacingStudio, most dashboards and motion software). Three variants are
selectable with `format=` in `milestone_mod.ini`.

## ⚠️ The packet length is checked exactly

The receiver validates by **total length**, and being one byte short fails
silently — no error, no data, just nothing. Measured against SimHub's
`ForzaReader` by sending each candidate size:

| bytes | result |
|-------|--------|
| 232 (sled) | rejected by the Horizon reader |
| 311 (fm7)  | rejected by the Horizon reader |
| **323**    | **rejected** — the intuitive 232 + 12 + 79 |
| **324**    | ✅ `"started receiving valid data"`, `"Game connected"` |
| 331        | accepted but immediately `Paused=True` |

Horizon is `232 sled + 12 pad + 79 dash + 1 trailing byte = 324`. Since the
toolkit encoder took over (`lib/toolkit/include/forza_packet.h`) that trailing
byte carries `'R'` (0x52) rather than zero: no Forza title reads it, and a
sentinel lets `forza_probe.py` say whose packets it is looking at when several
senders share a port. Emitting 323
made SimHub log `UDP Reader on port 8000 started receiving unprocessed data`
at packet rate — 19,880 times in one race — and never connect. If a dash stays
dead while the data looks right, check the length before anything else, and
read the receiver's own log.

## Sled — 232 bytes, offset 0

| off | type   | field                                  |
|-----|--------|----------------------------------------|
| 0   | s32    | IsRaceOn                               |
| 4   | u32    | TimestampMS                            |
| 8   | f32×3  | EngineMaxRpm, EngineIdleRpm, CurrentEngineRpm |
| 20  | f32×3  | AccelerationX, Y, Z                    |
| 32  | f32×3  | VelocityX, Y, Z                        |
| 44  | f32×3  | AngularVelocityX, Y, Z                 |
| 56  | f32×3  | Yaw, Pitch, Roll                       |
| 68  | f32×4  | NormalizedSuspensionTravel FL FR RL RR |
| 84  | f32×4  | TireSlipRatio                          |
| 100 | f32×4  | WheelRotationSpeed                     |
| 116 | s32×4  | WheelOnRumbleStrip                     |
| 132 | f32×4  | WheelInPuddleDepth                     |
| 148 | f32×4  | SurfaceRumble                          |
| 164 | f32×4  | TireSlipAngle                          |
| 180 | f32×4  | TireCombinedSlip                       |
| 196 | f32×4  | SuspensionTravelMeters                 |
| 212 | s32×5  | CarOrdinal, CarClass, CarPI, DrivetrainType, NumCylinders |

## Dash — 79 bytes

`fm7` (311 B): dash at offset 232. `fh4` (324 B): 12 zero bytes at 232, dash
at 244.

| off | type  | field                                       |
|-----|-------|---------------------------------------------|
| 0   | f32×3 | PositionX, Y, Z                             |
| 12  | f32   | Speed (m/s)                                 |
| 16  | f32×2 | Power, Torque                               |
| 24  | f32×4 | TireTemp FL FR RL RR                        |
| 40  | f32   | Boost                                       |
| 44  | f32   | Fuel                                        |
| 48  | f32   | DistanceTraveled                            |
| 52  | f32×4 | BestLap, LastLap, CurrentLap, CurrentRaceTime |
| 68  | u16   | LapNumber                                   |
| 70  | u8    | RacePosition                                |
| 71  | u8×5  | Accel, Brake, Clutch, HandBrake (0-255), Gear |
| 76  | s8    | Steer (-127..127)                           |
| 77  | s8×2  | NormalizedDrivingLine, NormalizedAIBrakeDifference |

## What this mod fills

| field                    | source                                   | real? |
|--------------------------|------------------------------------------|-------|
| Accel/Brake/Clutch/HandBrake/Steer | the game's own `GetDeviceState` reads | yes |
| CurrentEngineRpm, EngineMaxRpm | UE4 HUD widget `VehicleCurrentRPM`, `VehicleMaxRPM` | yes |
| Gear                     | UE4 HUD `VehicleCurrentGear`: -1 reverse → 0, neutral 0 → 1 (Forza has no neutral), 1.. as is | yes |
| Speed, VelocityZ         | UE4 HUD `VehicleCurrentSpeedKMH` ÷ 3.6 (the game's speedometer, m/s) | yes |
|                          | fallback while that reads zero or is missing: FMOD `VehicleSpeed` (0..1 of the car's top speed) × `[fmod] speed_scale` | nominal, not calibrated per car |
| TireSlipRatio ×4         | FMOD `LongitudinalSlip` — wheelspin / lockup | yes |
| TireSlipAngle ×4         | FMOD `LateralSlip` — slide angle         | yes |
| NormalizedSuspensionTravel ×4, SuspensionTravelMeters | FMOD `SuspensionMovement` | yes |
| SurfaceRumble ×4         | FMOD `SuspensionMovement`                | yes |
| Torque, Power            | FMOD `torque`, normalised → nominal Nm   | yes (shape real, units nominal) |
| TireCombinedSlip ×4      | √(lat² + long²)                          | derived from real inputs |
| AccelerationZ            | d(speed)/dt, **plus a decaying spike per collision** | derived from real events |
| AccelerationX            | −(constant FFB force), scaled to ±1 g    | derived — steering torque as a lateral-load proxy |
| WheelOnRumbleStrip ×4    | suspension movement > 0.7                | derived |
| WheelRotationSpeed ×4    | speed / 0.33 m                           | derived |
| IsRaceOn                 | 1 while a live HUD instance is being read (`race_on=always` forces 1) | — |
| EngineIdleRpm            | 12 % of max                              | placeholder |
| everything else          | 0                                        | — |

The slip and suspension channels are the ones worth having for a bass shaker:
they are the game's own physics, not anything inferred from force feedback.

Derived fields exist so effect engines have something to key on; the log
line every 5 s and `tools/forza_listen.py` show the raw channels so the
scaling can be calibrated against reality.

## Receiving

SimHub: *Games → Forza Motorsport 7* (for `fm7`) or *Forza Horizon 4/5* (for
`fh4`), UDP port matching `port=`. SimHub binds the port exclusively, so run
`forza_listen.py` on `mirror_port=` when both are wanted.
