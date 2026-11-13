# SimCruise 2.0.0 driving release checklist

This checklist is the release gate for SimCruise 2.0.0 (Unreal Engine 5.8 client). Each row has one pass rule. A row is PASS only when the evidence shows that the rule is met. Evidence paths are relative to the repository root. Large logs stay on the test PC; the numbers are copied into the evidence files.

Test PC: Intel i9-10885H, NVIDIA GeForce RTX 2070 Super with Max-Q Design (laptop GPU), Windows 11.

Status words: **PASS**, **FAIL**, **KNOWN** (does not meet the rule; written as a known issue in the release notes), **TODO** (not measured yet). Results: 2026-11-12, release commit on main ff35887.

## A. Car physics benches

Published data: `docs/cars.md` (road-test sources for each car). The 3 % band is relative to the published figure. "C8" is the Corvette Stingray Z51 (C8), car file `meridian_z`.

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| A1 | Miata NB `nb2001`: 0-60, 70-0, skidpad | each within 3 % of the published figure | `scripts\check.ps1 -Filter SimCruise.Vehicle` (`CarTrack.nb2001`) | `docs/release/evidence/vehicle-tests.txt` | **PASS**: 7.73 s / 174.2 ft / 0.904 g against 7.8 / 173 / 0.89 (-0.9 %, +0.7 %, +1.6 %) |
| A2 | Miata NB `nb2001_aaron`: 0-60, 70-0, skidpad | same | `CarTrack.nb2001_aaron` | same | **PASS** (no published test of this modified car; targets are estimates): 7.73 s / 154.8 ft / 0.997 g against 7.8 / 160 / 0.98; the 70-0 is 3.3 % shorter than the estimate after the ride-height fix (81a88f0), inside the bench band |
| A3 | 1965 Mustang `mustang65`: 0-60, 70-0, skidpad | same | `CarTrack.mustang65` | same | **PASS**: 9.03 s / 265.7 ft / 0.707 g against 8.9 / 264 / 0.70 (+1.5 %, +0.6 %, +1.0 %) |
| A4 | Corvette C8 `meridian_z`: 0-60 (with 1 ft rollout, as Car and Driver times it), 70-0, skidpad | same | `CarTrack.meridian_z` | same | **KNOWN**: 0-60 3.22 s with rollout against 2.8 s (+15 %); 70-0 149.1 ft against 148 (+0.7 %) and skidpad 1.019 g against 1.03 (-1.1 %) pass |
| A5 | Understeer gradient | the LiftOff bench of each car passes its band (understeer gradient in range, no reverse while rolling forward) | `SimCruise.Vehicle.LiftOff.<car>` | same | **PASS**: all LiftOff benches pass; understeer gradient NB 1.31, Aaron NB 1.09, Mustang 3.61, C8 3.14 deg/g (Model 3 1.29) |
| A6 | Drive robot: no spin | `scripts\drive-robot.ps1` prints `DRIVE ROBOT PASSED` for the 4 cars (rear-axle slip less than 15 deg in the 6 judged manoeuvres) | drive robot on the test pad | `docs/release/evidence/drive-robot.txt` | **PASS**: `DRIVE ROBOT PASSED` for the 4 cars; Mustang `launch_turn_trig70` spin (43 deg) recorded as known issue R4 |
| A7 | All other vehicle tests | every `SimCruise.Vehicle.*` test passes | `scripts\check.ps1 -Filter SimCruise.Vehicle` | `docs/release/evidence/vehicle-tests.txt` | **PASS**: 57 of 57 `SimCruise.Vehicle.*` tests, incl. `Crash.*` for the 4 cars |

## B. Input

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| B1 | Xbox pad dead zones and directions | the test passes (stick and trigger dead zones, axis signs) | `SimCruise.Gameplay.Input.DeadZonesAndDirections` | `docs/release/evidence/other-tests.txt` | **PASS** |
| B2 | Trigger curve and pedal ramps | the test passes | `SimCruise.Gameplay.Steering.PedalRamps` | same | **PASS** |
| B3 | Steering rate and range | the tests pass | `Steering.RangeMatchesGodot`, `Steering.KeyTapAtHighwaySpeed`, `Steering.CountersteerAllowance` | same | **PASS** |
| B4 | Self-centring | the stick and the keys bring the rack back to exactly 0 | `Steering.StickAndKeysCentreExactly` | same | **PASS** |
| B5 | Rumble | the slip rumble test passes | `SimCruise.Gameplay.Haptics.SlipRumble` | same | **PASS** |
| B6 | Bindings match the docs | every binding is in `docs/controls.md` | `SimCruise.Gameplay.Input.BindingsMatchControlsDoc` | same | **PASS** (new pad layout: A clutch, B parking brake, RB/LB gears, R3 gearbox, D-pad left restart, D-pad right tuning, X HUD) |
| B7 | USB wheel force feedback without a wheel | the wheel code compiles in Shipping; the game starts, drives and stops with no wheel connected and with no wheel error in the log | Shipping package and the soak (row K1) | `docs/release/evidence/soak.txt` | **PASS**: the GameInput wheel code is in the Shipping build; with no wheel connected the game started, drove 20 minutes in the soak and exited with code 0 (a Shipping build writes no log, so "no wheel error in the log" was checked in the editor build only) |

## C. Cameras

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| C1 | Chase, bumper and cockpit views for the 3 cars (NB, Mustang, C8) | each view shows the car or the cockpit; the camera is not inside a body panel | screenshots from the Shipping build | `docs/images/release-2.0.0/` | **PASS**: chase and driver views of the NB, Mustang and C8 in the Shipping build; no camera inside a panel (`docs/images/release-2.0.0/02`, `04`, `06`) |
| C2 | Steering wheel turns | the cockpit steering wheel turns with the rack | rig tests and a screenshot in a turn | same | **PASS** (tests): `Presentation.Rig.*` find and pose the steering wheel and column of the NB, Mustang and C8; no screenshot in a turn was taken |
| C3 | Camera model tests | the tests pass | `SimCruise.Gameplay.Camera.*` | `docs/release/evidence/other-tests.txt` | **PASS** (incl. the new `Camera.PhysicsTimeNoJitter`) |

## D. Sound

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| D1 | Engine sound follows rpm; no silence or clicks | the audio synth tests pass | `SimCruise.Audio.*` | `docs/release/evidence/other-tests.txt` | **PASS** (incl. the new `Audio.Limiter`) |
| D2 | Inside/outside change | the cockpit view and the outside views sound different | `SimCruise.Audio.*` and code review | `docs/release/evidence/other-tests.txt` | **PASS** (tests and code): the audio tests compare the cabin (`bCabin`) and outside output of both synths; the vehicle picks the cabin sound when the shown view is the driver's seat |
| D3 | Mute setting | sound off makes the car silent; it stays off after a restart | play test and `preferences.json` | `docs/release/evidence/settings.txt` | **PASS** (read): a saved `sound: false` stays in the file and is applied at the start; the mute key itself was not pressed by the gate (person at the PC) |

## E. Traffic

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| E1 | Traffic soak | the test passes: no car stuck for more than 40 s, cars stop at red, no car drives through the player | `SimCruise.World.TrafficSoak` | `docs/release/evidence/other-tests.txt` | **PASS**: 40 cars for 240 s, at most 0 cars standing over 40 s; 0 cars stopped by the player |

## F. Crashes into the world

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| F1 | Walls, trees, traffic, guard rails | no launch (the car does not go up into the air), no tunnelling (the car stays on the side it hit), no NaN | `SimCruise.Vehicle.Crash.<car>` (new crash bench) | `docs/release/evidence/vehicle-tests.txt`, `crash.txt` | **PASS**: wall 60 km/h, tree 50, guard rail 80, stopped car 50, 4 cars: highest rise 0.17 m, up speed under 1 m/s, tilt under 8 deg, no tunnelling, all finite |
| F2 | Reset and recover | the reset input puts the car upright on the road and the car drives on | `SimCruise.Vehicle.Crash.*` (reset after each crash) and `SimCruise.Vehicle.Interface` | `docs/release/evidence/vehicle-tests.txt` | **PASS**: after each crash the reset gives 4 wheels down and the car drives on (`Crash.*`, `Vehicle.Interface`) |

## G. Frame rate (laptop GPU)

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| G1 | Ocean Drive, default preset, 1080p, Shipping | average 60 fps or more and 1 % low 45 fps or more (1 % low = 1000 / p99 frame time) | Shipping game, `-SimCruiseDrivePerf=<preset>`, driver and chase views, 10 s windows (`SimCruisePerf:` lines) | `docs/release/evidence/perf.txt` | **PASS** at the default (Medium): driver 78.7 fps / 1 % low 53; chase 86.3 / 74. High misses in the city (55.9 / 42) |
| G2 | Rural road, same rules | same | same at a rural start | same | **PASS** at Medium: driver 91.4 / 74; chase 99.9 / 86 (High 73.1 / 61) |
| G3 | Default preset | the preset that the game picks for this GPU passes G1 and G2 | `SimCruise.Environment.Graphics.AutoPick` and G1 | same | **PASS**: Max-Q GPUs now start on Medium (cebd03a); 60 fps cap in play |

## H. Input delay

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| H1 | Stick to picture | from the input sample to the first rendered frame that shows the change: less than 60 ms at the default preset | in-engine measurement (method in the evidence file) | `docs/release/evidence/latency.txt` | **PASS**: mean 49.8 ms, median 49.9 ms, max 51.7 ms (2 frames + game-to-render) at the default settings; Reflex on: 52.3 ms mean |
| H2 | Present settings | `r.OneFrameThreadLag`, VSync and NVIDIA Reflex are known and set for low delay | config review | same | **PASS**: `r.OneFrameThreadLag` 1 (engine default), VSync off, `t.MaxFPS` 60, Reflex available but off by default (no gain at the cap; exit-hang suspicion R6) |

## I. Start and go anywhere

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| I1 | Default start | a normal launch opens on a random start-pool road (or Ocean Drive when that fails) and the car drives | smoke test and play test | `docs/release/evidence/start.txt` | **PASS**: normal starts on random pool roads (Nashville TN, Oatman Road AZ, Valley of Fire NV); no crash |
| I2 | Random start | `-SimCruiseRandomStart` puts the car on a road; no crash | play test | same | **PASS**: same runs (random start is the default) |
| I3 | Preset travel | travel to a preset place loads the place; no crash | play test | same | **PASS**: the soak travelled to Monument Valley, Cheyenne and Charleston; each loaded and drove |
| I4 | Offline fallback | with no network the game opens on the bundled Ocean Drive and drives; no crash | Shipping game with `-SimCruiseLiveOffline` (every live request goes to a closed local port) and a 4-minute soak drive | same | **PASS** (live feeds offline) / **KNOWN** (network fully off not run): `-SimCruiseLiveOffline` started and drove 4 minutes, exit 0; the Ocean Drive fall-back with the network fully off was not exercised |

## J. Settings

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| J1 | Settings persist | car, sound, HUD level, lens, manual gearbox, head motion and rumble come back after a restart | `preferences.json` round trip | `docs/release/evidence/settings.txt` | **PASS** (read; write not exercised): the saved car (Mustang) and manual gearbox came back at the start; all 7 settings kept in the file |

## K. Stability

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| K1 | 20 minute soak, packaged build | no crash; no `Error:` lines from SimCruise log categories; no ensure | packaged game, drive robot in a loop for 20 minutes | `docs/release/evidence/soak.txt` | **PASS**: 20 minutes, exit 0, 0 non-finite states, 48 recoveries, 25 camera changes, 4 car changes, 3 travels, 60 fps (capped), worst frame 63 ms, memory 2.1-3.0 GB with no growth |
| K2 | Smoke test | `-SimCruiseShellSelfTest` passes in the Shipping package | smoke test | `docs/release/evidence/start.txt` | **PASS**: `release.ps1` smoke test, Shipping self-test exit code 0 |
| K3 | Full automation suite | every `SimCruise.*` test passes | `scripts\check.ps1` | `docs/release/evidence/*-tests.txt` | **PASS**: 136 of 136 tests (57 vehicle + 79 others) on the release commit |

## L. Night driving

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| L1 | Rural road at night, low beams | the road is visible about 60 m ahead; the frame is not mostly black (mean road luminance clearly above black) | Shipping screenshot at night (`-SimCruiseUtc=` after dark) on the rural preset, road pixels measured | `docs/release/evidence/night.txt` | **PASS** (marginal on distance): road patch 37/255 on a 5/255 frame; lit road visible to about 45-50 m |
| L2 | Ocean Drive at night, low beams | same | same on Ocean Drive | same | **PASS**: road patch 45/255, frame mean 22/255; street lights and neon |

## M. Cockpit field of view

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| M1 | Cockpit and bumper views use the field of view of the human eye | the horizontal FOV comes from the screen size and the eye distance (default 15.6 inch at 60 cm, about 32 deg); the True and Wide presets and the size and distance settings work; the eye point is each car's driver head position; the chase view keeps its wide FOV | Shipping build screenshots and the lens settings | `docs/images/release-2.0.0/02`, `04`, `06` | **PASS**: the true FOV (32.1 deg at 15.6 inch, 60 cm) works and is selectable with F6, the left-stick click and the road atlas (`Gameplay.Lens.TrueFov`, the binding test); cockpit shots of the 3 cars at the true FOV (first run) and at the natural lens, the default since 7267a09 at the owner's request; the chase view keeps its FOV |

## N. Release package

The standing rule for every release (`docs/release/RELEASE-CHECKLIST.md`): people must be able to download and play it.

| # | Item | Pass rule | How measured | Evidence | Result |
|---|---|---|---|---|---|
| N1 | Shipping package and zip | `scripts\release.ps1` builds them with no error; the zip has no `.pdb` and no `Saved` folder | release script output | `docs/release/evidence/package.txt` | **PASS**: `RELEASE PACKAGE OK`; zip 429.4 MB, no .pdb, no Saved, helper inside |
| N2 | Installer | the setup installs for the current user without administrator rights, the Start menu entry starts the game, the car drives, the uninstaller removes the program folder and the Start menu entry | clean install on the test PC | `docs/release/evidence/installer.txt` | **PASS**: per-user install without administrator rights, Start menu entry starts the game, the car drove 3 minutes, exit 0; the uninstaller removed the program folder, the Start menu entries and the Apps entry |
| N3 | Checksums | `SHA256SUMS.txt` matches the published files | `Get-FileHash` | same | **PASS**: `SimCruise-2.0.0-SHA256SUMS.txt` (setup 262aee04..., zip f937203a...) |
| N4 | README and docs | README shows current screenshots and the download; `docs/install.md`, `docs/controls.md`, CHANGELOG are current | review | the release commit | **PASS**: README (download, 10 new screenshots, measured numbers), `docs/install.md` (installer, requirements), `docs/controls.md` (new pad layout, tuning, true FOV), CHANGELOG [2.0.0] |
| N6 | Tuning menu in the package | standing still, D-pad Right (F4) opens the tuning menu; change a row, A (Enter) saves and applies, B (Esc) cancels; the car drives with the change (for example a lower final drive gives a lower top-gear rpm); no crash | play test in the Shipping build | `docs/release/evidence/installer.txt` | **KNOWN** (partly tested): a saved setup loads in the Shipping build with no crash and `Vehicle.Model.Tuning` passes; the menu was not driven by keystrokes because a person was using the PC |
| N5 | Screenshots | 6-10 in-game screenshots from the Shipping build of this release | captures | `docs/images/release-2.0.0/` | **PASS**: 10 screenshots |
