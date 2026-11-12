# SimCruise 2.0.0 driving release checklist

This checklist is the release gate for SimCruise 2.0.0 (Unreal Engine 5.8 client). Each row has one pass rule. A row is PASS only when the evidence shows that the rule is met. Evidence paths are relative to the repository root. Large logs stay on the test PC; the numbers are copied into the evidence files.

Test PC: Intel i9-10885H, NVIDIA GeForce RTX 2070 Super with Max-Q Design (laptop GPU), Windows 11.

Status words: **PASS**, **FAIL**, **KNOWN** (does not meet the rule; written as a known issue in the release notes), **TODO** (not measured yet).

## A. Car physics benches

Published data: `docs/cars.md` (road-test sources for each car). The 3 % band is relative to the published figure. "C8" is the Corvette Stingray Z51 (C8), car file `meridian_z`.

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| A1 | Miata NB `nb2001`: 0-60, 70-0, skidpad | each within 3 % of the published figure | `scripts\check.ps1 -Filter SimCruise.Vehicle` (`CarTrack.nb2001`) | `docs/release/evidence/vehicle-tests.txt` | TODO |
| A2 | Miata NB `nb2001_aaron`: 0-60, 70-0, skidpad | same | `CarTrack.nb2001_aaron` | same | TODO |
| A3 | 1965 Mustang `mustang65`: 0-60, 70-0, skidpad | same | `CarTrack.mustang65` | same | TODO |
| A4 | Corvette C8 `meridian_z`: 0-60 (with 1 ft rollout, as Car and Driver times it), 70-0, skidpad | same | `CarTrack.meridian_z` | same | TODO |
| A5 | Understeer gradient | the LiftOff bench of each car passes its band (understeer gradient in range, no reverse while rolling forward) | `SimCruise.Vehicle.LiftOff.<car>` | same | TODO |
| A6 | Drive robot: no spin | `scripts\drive-robot.ps1` prints `DRIVE ROBOT PASSED` for the 4 cars (rear-axle slip less than 15 deg in the 6 judged manoeuvres) | drive robot on the test pad | `docs/release/evidence/drive-robot.txt` | TODO |
| A7 | All other vehicle tests | every `SimCruise.Vehicle.*` test passes | `scripts\check.ps1 -Filter SimCruise.Vehicle` | `docs/release/evidence/vehicle-tests.txt` | TODO |

## B. Input

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| B1 | Xbox pad dead zones and directions | the test passes (stick and trigger dead zones, axis signs) | `SimCruise.Gameplay.Input.DeadZonesAndDirections` | `docs/release/evidence/gameplay-tests.txt` | TODO |
| B2 | Trigger curve and pedal ramps | the test passes | `SimCruise.Gameplay.Steering.PedalRamps` | same | TODO |
| B3 | Steering rate and range | the tests pass | `Steering.RangeMatchesGodot`, `Steering.KeyTapAtHighwaySpeed`, `Steering.CountersteerAllowance` | same | TODO |
| B4 | Self-centring | the stick and the keys bring the rack back to exactly 0 | `Steering.StickAndKeysCentreExactly` | same | TODO |
| B5 | Rumble | the slip rumble test passes | `SimCruise.Gameplay.Haptics.SlipRumble` | same | TODO |
| B6 | Bindings match the docs | every binding is in `docs/controls.md` | `SimCruise.Gameplay.Input.BindingsMatchControlsDoc` | same | TODO |
| B7 | USB wheel force feedback without a wheel | the wheel code compiles in Shipping; the game starts, drives and stops with no wheel connected and with no wheel error in the log | Shipping package and the soak (row K1) | `docs/release/evidence/soak.txt` | TODO |

## C. Cameras

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| C1 | Chase, bumper and cockpit views for the 3 cars (NB, Mustang, C8) | each view shows the car or the cockpit; the camera is not inside a body panel | screenshots from the Shipping build | `docs/images/release-2.0.0/`, `docs/release/evidence/cameras/` | TODO |
| C2 | Steering wheel turns | the cockpit steering wheel turns with the rack | rig tests and a screenshot in a turn | same | TODO |
| C3 | Camera model tests | the tests pass | `SimCruise.Gameplay.Camera.*` | `docs/release/evidence/gameplay-tests.txt` | TODO |

## D. Sound

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| D1 | Engine sound follows rpm; no silence or clicks | the audio synth tests pass | `SimCruise.Audio.*` | `docs/release/evidence/other-tests.txt` | TODO |
| D2 | Inside/outside change | the cockpit view and the outside views sound different | code review and play test | `docs/release/evidence/sound.txt` | TODO |
| D3 | Mute setting | sound off makes the car silent; it stays off after a restart | play test and `preferences.json` | `docs/release/evidence/settings.txt` | TODO |

## E. Traffic

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| E1 | Traffic soak | the test passes: no car stuck for more than 40 s, cars stop at red, no car drives through the player | `SimCruise.World.TrafficSoak` | `docs/release/evidence/other-tests.txt` | TODO |

## F. Crashes into the world

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| F1 | Walls, trees, traffic, guard rails | no launch (the car does not go up into the air), no tunnelling (the car stays on the side it hit), no NaN | crash drive in the game, telemetry log | `docs/release/evidence/crash.txt` | TODO |
| F2 | Reset and recover | the reset input puts the car upright on the road and the car drives on | `SimCruise.Vehicle.Interface` (reset) and play test | `docs/release/evidence/crash.txt` | TODO |

## G. Frame rate (laptop GPU)

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| G1 | Ocean Drive, default preset, 1080p, Shipping | average 60 fps or more and 1 % low 45 fps or more (1 % low = 1000 / p99 frame time) | Shipping game, `-SimCruiseDrivePerf=<preset>`, driver and chase views, 10 s windows (`SimCruisePerf:` lines) | `docs/release/evidence/perf.txt` | TODO |
| G2 | Rural road, same rules | same | same at a rural start | same | TODO |
| G3 | Default preset | the preset that the game picks for this GPU passes G1 and G2 | `SimCruise.Environment.Graphics.AutoPick` and G1 | same | TODO |

## H. Input delay

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| H1 | Stick to picture | from the input sample to the first rendered frame that shows the change: less than 60 ms at the default preset | in-engine measurement (method in the evidence file) | `docs/release/evidence/latency.txt` | TODO |
| H2 | Present settings | `r.OneFrameThreadLag`, VSync and NVIDIA Reflex are known and set for low delay | config review | same | TODO |

## I. Start and go anywhere

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| I1 | Default start | a normal launch opens on a random start-pool road (or Ocean Drive when that fails) and the car drives | smoke test and play test | `docs/release/evidence/start.txt` | TODO |
| I2 | Random start | `-SimCruiseRandomStart` puts the car on a road; no crash | play test | same | TODO |
| I3 | Preset travel | travel to a preset place loads the place; no crash | play test | same | TODO |
| I4 | Offline fallback | with no network the game opens on the bundled Ocean Drive; no crash | play test with the network blocked | same | TODO |

## J. Settings

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| J1 | Settings persist | car, sound, HUD level, lens, manual gearbox, head motion and rumble come back after a restart | `preferences.json` round trip | `docs/release/evidence/settings.txt` | TODO |

## K. Stability

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| K1 | 20 minute soak, packaged build | no crash; no `Error:` lines from SimCruise log categories; no ensure | packaged game, drive robot in a loop for 20 minutes | `docs/release/evidence/soak.txt` | TODO |
| K2 | Smoke test | `-SimCruiseShellSelfTest` passes in the Shipping package | smoke test | `docs/release/evidence/start.txt` | TODO |
| K3 | Full automation suite | every `SimCruise.*` test passes | `scripts\check.ps1` | `docs/release/evidence/*-tests.txt` | TODO |

## L. Night driving

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| L1 | Rural road at night, low beams | the road is visible about 60 m ahead; the frame is not mostly black (mean road luminance clearly above black) | Shipping screenshot at night (`-SimCruiseUtc=` after dark) on the rural preset, road pixels measured | `docs/release/evidence/night.txt` | TODO (re-test after the cars thread's low-beam and night-exposure fixes are on main) |
| L2 | Ocean Drive at night, low beams | same | same on Ocean Drive | same | TODO (same) |

## M. Cockpit field of view

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| M1 | Cockpit and bumper views use the field of view of the human eye | the horizontal FOV comes from the screen size and the eye distance (default 15.6 inch at 60 cm, about 32 deg); the True and Wide presets and the size and distance settings work; the eye point is each car's driver head position; the chase view keeps its wide FOV | Shipping build screenshots and the lens settings | `docs/release/evidence/cameras/` | TODO (re-test when the cars thread's change is on main) |
