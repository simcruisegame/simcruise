# Cars

Every car in SimCruise is a JSON file in [`game/cars/`](../game/cars/). Powertrain, suspension, tyres, brakes and aerodynamics read their numbers from it through `game/CarSpec.gd`, so adding a car means writing data, not code. The physics is shared by all cars: `game/Powertrain.gd` (contacts, tyres, engine or motor, gearbox, differentials) and `game/CarSuspension.gd` (springs, anti-roll bars, dampers, limited-slip split).

## The garage

Three cars:

| File | Name | Layout | Reference car | 0–60 mph | 70–0 mph | Skidpad |
|---|---|---|---|---|---|---|
| `nb1999.json` | 1999 Roadster Sport | front engine, rear drive, 5-speed manual | 1999 Mazda MX-5 Miata Sport | 7.99 s (7.97) | 172.9 ft (173) | 0.884 g (0.89) |
| `meridian_z.json` | Corvette Stingray Z51 (C8), formerly Meridian Z | mid-engine V8, rear drive, 8-speed dual clutch | 2020 Chevrolet Corvette Z51 | 3.55 s (2.8†) | 152.0 ft (148) | 1.025 g (1.03) |
| `mustang65.json` | 1965 Mustang 289 | front engine, rear drive, 289 V8 four-barrel, 4-speed manual | 1965 Ford Mustang 289/210 | 9.00 s (8.9) | 259.3 ft (264‡) | 0.699 g (0.70*) |

Results are from `checks/CarTrack.gd`; published targets in brackets (mostly Car and Driver instrumented tests, cited in each file's `sources`). \* estimated target. ‡ derived from Car Life's measured 20 ft/s² from 80 mph (drum brakes). † open item, stated in the file: the magazine omits a 1 ft rollout worth about 0.3 s on a launch this hard while the bench times from rest, and from pedal-down at idle, where the car's launch control holds the engine at launch speed before the brake is released (not modelled). Before the slip-ratio tyre curve (main on 10 November 2026) the same bench read 7.95, 3.60 and 9.00 s; the roadster's 0.04 s comes from the driveline fix below, not from its tyres.

The 1999 roadster is the original car of the game and keeps its own detailed model (`CarFinish.gd`); the Corvette and the Mustang wear real models downloaded from Sketchfab (see below). Four cars whose bodies were generated in Blender, the Kestrel S (2022 Civic Si), Tora RS (2022 WRX), Volta (Model 3 RWD) and Bison XL (F-150 2.7 EcoBoost), were removed in November 2026 because their generated models did not look like real cars; their calibrated car files remain in the git history for when real models are found. A saved choice of one of them, or `--car=` with an id that no longer exists, opens the 1999 roadster (`checks/CarRoster.gd` checks this).

## How a car is calibrated

`checks/CarTrack.gd` runs every car through three tests on a flat pad:

- **0–60 mph**: full throttle in automatic from rest (the same procedure as `checks/Track.gd`), timed from rest without the magazines' 1 ft rollout. Every car gets the same full pedal, with or without traction control: there is no bench driver feeding in the throttle, so the time is what a player on the keyboard (whose accelerator is all or nothing) gets. The speed at which the automatic clutch bites (`launch_rpm` + `launch_rpm_per_throttle` × pedal) is part of each car's file and explained in its `sources.launch` where it was chosen for this curve (Corvette 3000 rpm: its launch is tyre-limited, so a higher speed only revs the engine longer before the clutch bites).
- **70–0 mph**: full service brake, distance in feet. Cars with ABS (`abs_enabled`) hold each tyre at its peak slip, in either direction of travel; cars without lock their wheels and slide.
- **Skidpad**: a ramp steer at the speed a 300 ft circle needs for the target grip: steady speed, steering rising 0.6 % a second, and the peak one-second mean of lateral acceleration before the body slip angle passes 10°. It needs no path-following driver, so it does not depend on controller tuning; on the 1999 car it reads 0.884 g where `Track.gd`'s tuned circle driver demonstrates 0.885 g.

Bands: 0–60 ±0.15 s, 70–0 ±5 ft, skidpad ±0.02 g. A car outside them fails `sh scripts/check.sh` unless its file states the gap in `calibration_open`.

### Lift-off and trail braking

`checks/LiftOff.gd` (default suite) is every car's stability gate, after ISO 9816 (power-off reaction in a turn) and ISO 7975 (braking in a turn). A path and speed driver holds the car on a 40 m circle at 0.3, 0.5 and 0.7 g; then the wheel is frozen and the driver either closes the throttle over 0.1 s, or closes it and from 0.2 s squeezes the brake over 0.3 s to a pedal worth 0.15 g. It reports the yaw-rate ratio at 1 s (ISO 9816's measure: above 1 the car tucks in), the yaw overshoot, body sideslip at 1 s and its peak, the settling time and the deceleration. Gate: no car spins (sideslip past 8°) at 0.3 or 0.5 g, and no automatic ever selects reverse while rolling forward; 0.7 g is reported. `--radius=100` repeats it on ISO's larger circle, `--trace` prints each tyre's load, slip angle and slip ratio, and `--set=field=value` overrides a car-file field for experiments.

On the 1999 roadster a lift at 0.3–0.5 g gives a yaw-rate ratio of 1.01–1.07 with at most 1.2° of sideslip, settling at once; the trail brake 0.99–1.13 and 1.3° (40 m circle). That is the mild tuck-in owners describe ("with power off through a corner the miata tended to understeer slightly", [Grassroots Motorsports forum](https://grassrootsmotorsports.com/forum/grm/practice/12785/page3/); reviewers call the handling "balanced and forgiving", [Cars.com](https://cars.com/research/mazda-mx_5_miata-1999)); no instrumented lift-off test of the NB has been found, so the comparison is qualitative. At 0.7 g it tucks in more (ratio 1.19 lift, 1.43 brake, 4.8° peak) and still recovers.

The Miata's road spins in the November 2026 field test were not its chassis: the automatic selected reverse whenever the brake input was between 0 and the 0.01 brake threshold while rolling forward, and stayed in reverse while the brake was held, so the clutch drove the rear wheels backwards mid-corner. Every car did it (108–1,186 ticks per Tail of the Dragon run); the light rear-drive Miata, whose small brake pedal for a given deceleration spends longest in that band, spun. `Powertrain.gd` now changes direction only below 0.45 m/s, like a real selector's reverse inhibitor. Rear brake bias (50:50 or 75:25), an open differential, zero toe, no rear camber gain, load sensitivity 0–0.2, a 15 % larger yaw inertia and a 4.5 cm higher centre of gravity each moved the 0.7 g results by at most 0.11 in yaw-rate ratio and 0.6° in sideslip, so no chassis value was changed.

The NB's suspension travel is consistent with what owners measure: the car sits about 2 in (51 mm) above its bump stops (14 in to 12 in fender height, [Classic Motorsports forum](https://classicmotorsports.com/forum/grm/miata-bump-stop-question/172527/page1)); the file has 54 mm to the bump rubber and the hard stop 143 mm above ride height. The bump-stop ticks the field test counted on smooth roads came from `RoadDrive.gd` dropping the car from 0.35 m at the start and from the reverse-gear wheel hop; the drive now sets the car down at its ride height and prints where any bump stop is reached.

### The Unreal client's benches

The Unreal client (see [unreal-architecture.md](unreal-architecture.md#vehicle-physics-core)) runs the same procedures as automation tests on a runtime-spawned flat pad, with the physics on Chaos at a fixed 360 Hz (`SimCruise.Vehicle.CarTrack.<car>`, `LiftOff.<car>`, `StaticLoad.<car>`, `Convergence.<car>`; [unreal-building.md](unreal-building.md#automation-tests)). Results on 11 November 2026, against Godot 4.7.2 running `checks/CarTrack.gd` and `checks/LiftOff.gd` on the same day:

| Car | Unreal 360 Hz | Unreal, Godot's 60 Hz scheme | Godot | Target |
|---|---|---|---|---|
| 1999 roadster | 7.97 s · 171.9 ft · 0.884 g | 7.99 s · 172.9 ft · 0.884 g | 7.99 s · 172.9 ft · 0.884 g | 7.97 s · 173 ft · 0.89 g |
| 1965 Mustang | 8.95 s · 263.6 ft · 0.698 g | 9.01 s · 259.3 ft · 0.699 g (car file as is) | 9.00 s · 259.3 ft · 0.699 g | 8.9 s · 264‡ ft · 0.70\* g |
| Corvette C8 | 3.46 s† · 150.1 ft · 1.024 g | 3.47 s · 151.7 ft · 1.025 g | 3.55 s† · 152.0 ft · 1.025 g | 2.8 s · 148 ft · 1.03 g |

With Godot's own stepping (60 Hz contacts, six wheel substeps) the Unreal port reproduces Godot to the printed digit for the roadster and for the Mustang's stop and skidpad, and within 0.01 s for the Mustang's launch. The Corvette's Godot launch is 0.08 s slower because Godot's 60 Hz contact step lets its left and right tyre loads swap every tick under traction control, a numerical roll oscillation that costs grip; the Unreal model does not show it and from 120 Hz up every rate gives 3.46–3.47 s. The 360 Hz results are converged: 720 Hz moves none of them by a fifth of its band.

The automatic follows a shift map that depends on the pedal, as a real transmission controller's does (`SimCruise.Vehicle.Model.ShiftMap`). A light pedal changes up early and lets the engine lug before changing down; the floor runs to `upshift_rpm` and kicks down a gear whenever the lower gear fits under it; the downshift always lands below 92 % of the upshift point, so it never hunts; it never changes up on the brakes; and above 0.3 g of cornering it holds the gear unless the engine would hit the fuel cut or lug. The roadster changes 1–2 at 2550, 3770 and 6700 rpm at 15, 45 and 100 % pedal (it held first to 6700 rpm at any pedal before). Optional fields: `upshift_rpm_light` (default the larger of 2.5 × idle and 36 % of `upshift_rpm`), `downshift_rpm_light` (the larger of 1.5 × idle and 19 % of `upshift_rpm`), `kickdown_rpm` (62 % of `upshift_rpm`); `downshift_rpm` is the half-pedal downshift. With the map and the corner hold the benches read: roadster 7.97 s · 171.9 ft · 0.880 g, Mustang 8.95 s · 265.7 ft · 0.698 g, Corvette 3.46 s · 148.6 ft · 1.024 g (the full-throttle launch is unchanged; the stops and skidpads move by the gear the car is in).

LiftOff matches Godot case for case: the roadster's lift and trail-brake yaw-rate ratios are 1.01–1.07 and 0.99–1.13 at 0.3–0.5 g with at most 1.2° of sideslip, and 1.19 and 1.43 at 0.7 g with a 4.8° peak (Godot 1.19, 1.42, 4.8°); the Mustang finds no steady state at 0.7 g in both (its skidpad is 0.70 g); no car spins at a gated level and no automatic selects reverse while rolling. Static loads carry each car's weight with the file's front share to four digits, level, at the 217 mm static suspension length.

One estimated value is calibrated differently in Unreal, through an overlay that leaves the canonical file alone: `Content/Vehicle/Calibration/mustang65.json` scales the Mustang's estimated drum torques by 0.975 (809 and 488 N·m, bias unchanged). Its 70–0 is brake-torque limited (no wheel locks above 11 m/s), and at the converged rate the original torques stop it in 257.8 ft, 1.2 ft outside the band (Godot's coarser step read 259.3 ft, 0.3 ft inside); 0.975 puts it at Car Life's measured 0.62 g. The bench logs every overlay field it applies.

What is published is used as published: mass, weight split where the maker gives it, engine power and torque, gear ratios, tyre sizes, wheelbase and track. What is not published is estimated and labelled in `sources.estimates`: centre-of-gravity height, inertia (scaled from the 1999 car by mass and size), spring and anti-roll rates (from ride frequencies typical of the class), dampers, brake torque and the tyre model's grip. The tyre grip is the main calibration handle: braking distance sets the longitudinal grip (`static_mu`), the skidpad sets the lateral factor (`lateral_grip_scale`), and launch settings are only tuned inside realistic limits. Published torque is never scaled to make a number.

## Models

Each car file names its model in `model_scene` and the driver's eye point in `driver_eye`; `game/CarBody.gd` mounts the model, spins and steers the wheels, turns the steering wheel, lights the brake lamps and puts the camera in the driver's seat. The 1999 roadster keeps its detailed `CarFinish.gd` model and cockpit; the Corvette and the Mustang use downloaded models (below). Pick a car with **Car** in the road atlas (Tab); the drive reopens at the same place. `--car=<id>` selects one on the command line.

### The 1999 roadster against photographs

`checks/CarTurntable.gd` (`--car-turntable DIR --quality=ultra [--lamps]`, windowed) photographs the car as the game builds it, alone on a neutral pad, from eight angles, the top, close-ups of a headlamp, tail lamp, wheel and the cabin, and the driver view; [lab/miata/appearance/turntable](../lab/miata/appearance/turntable/README.md) sets these beside Commons photographs of 1999 cars, including a green 1999 with the body-colour factory hardtop like the game's. From that comparison `CarFinish.gd` now smooths the faceted hardtop and glass, uses a lighter green-tinted glass, black trim, underbody and wheel liners, a slimmer body-colour windscreen frame, the black cowl grille, larger mirrors at the front corner of the doors, darker metallic wheels, a green (not chrome) hood from the seat and a hardtop lining in the driver view. **L** switches the head lamps: the lenses glow, two low-beam spot lights light the road, the amber markers glow and the stop lamps keep a dim tail glow. The headlamp opening, the rear bumper, the hardtop's rear glass and the coarse body shell still differ and need a new model; the physics, mounts and collision are unchanged. The [taste test of 10 November 2026](../lab/taste/2026-11-10/README.md) ranks what is left. First come the rippled panel reflections, then a windscreen about 15° too upright with a hardtop that sits too high, then a swept-spoke wheel where the 1999 15-inch alloy has straight, flat spokes. After those come the slit headlamp, the stand-in cabin seen through the glass, the black-silhouette paint at dusk and the egg-shaped mirrors on stalks.

The Blender tooling that generated the retired cars' bodies (`authoring/cars/build_car.py` and its modules; see [authoring/cars/README.md](../authoring/cars/README.md)) is kept for future cars, but only a model that looks like the real car ships. Better models are a welcome contribution: CC0 or CC BY, with four separable wheels or the node names that README lists.

## Real models from Sketchfab

The 1965 Mustang and the Corvette Stingray (C8, the physics formerly shipped as Meridian Z) use downloaded models. A car file with `"model_rig": {"fit": true}` is fitted by `game/ModelRig.gd` when the car loads. It finds the four wheels, by name when the author separated them (the C8) or by cutting every triangle within a tyre radius of each ground-contact patch out of the body (the Mustang, whose wheels were merged into the body). It scales the model to the spec's wheelbase, turns it to face −Z, sets the hubs at static ride height, maps brake-lamp and glass materials, and hides badge surfaces. `checks/RigProbe.gd` prints what it found. Fitted sizes match the real cars: Mustang 4.62 m long (4.61 m real), C8 4.64 m (4.63 m).

## Your own model for a car (private)

You can give any car an exterior you own, for example a model you bought, without it entering the repository. Put a binary glTF named after the car's id in the game's user folder:

- Windows: `%APPDATA%\Godot\app_userdata\SimCruise\cars\nb1999.glb`
- macOS: `~/Library/Application Support/Godot/app_userdata/SimCruise/cars/nb1999.glb`
- Linux: `~/.local/share/godot/app_userdata/SimCruise/cars/nb1999.glb`

At start the game reads it at run time (no import), and `ModelRig.gd` fits it like a Sketchfab model: it turns the model upright if needed (the shortest axis is up, tyres below the body), finds the four wheels by name or geometry, scales it to the car's wheelbase and sets it at ride height. The physics stay those of the car file. `--stock-model` ignores the file for one run. Only use a model whose licence lets you use it; the project ships only CC0 and CC BY models (see below), and a file here is never committed or redistributed.

## Traffic models from Sketchfab

Moving and parked traffic use only real models listed in `game/art/traffic/manifest.json`: 1965 Ford Mustang, Corvette Stingray C8, 2018 Honda Accord, 2025 Honda CR-V, 2018 Ford F-150 Raptor, 2013 Jeep Grand Cherokee, Chevrolet Tahoe, Honda Odyssey and Tesla Model 3 (all CC BY 4.0, credited in `licenses/Traffic models.txt`). The old low-poly sedan and pickup are no longer used for traffic, and the generated garage bodies have been removed from the game. Each merged model gets detail levels rebuilt at load, and `checks/TrafficLineup.gd` renders the fitted fleet in a row to check orientation (a model showing its tail from the front needs `"flip": true`). A model is accepted only if it looks at least as good as these; the Silverado (wheels missing) and Transit Connect (transparent materials) were rejected. `authoring/cars/sketchfab.mjs` fills that manifest:

```
bun authoring/cars/sketchfab.mjs search "generic sedan"
SKETCHFAB_API_TOKEN=… bun authoring/cars/sketchfab.mjs fetch <uid> --kind=sedan --length=4.7
```

Only CC0 and CC BY models are accepted. Models with a car maker in the title are refused unless you pass `--allow-brand` (then remove the badges). The token is your own, from sketchfab.com → Settings → Password & API; it is never written anywhere. Models are normalised at load (long axis along the road, scaled to `length_m`, wheels on the ground); set `"flip": true` in the manifest if one drives backwards. Credits go to `licenses/Traffic models.txt`.

## Adding a car

1. Copy the closest file in `game/cars/` and change `id` (the file name), `name`, `make` and `body`.
2. Fill in what is published, with a source for each in `sources`. Prefer manufacturer specification sheets and instrumented magazine tests.
3. Set the targets (`target_0_60_s`, `target_70_0_ft`, `target_skidpad_g`) from one instrumented test.
4. Run `godot --headless --path game --fixed-fps 60 --script res://checks/CarTrack.gd -- --car=<id>` and adjust only the estimated fields until the car is inside its bands. `CARTRACK_TRACE=1` prints the launch every quarter second.
5. Run `sh scripts/check.sh`; the other cars and `Track.gd` must not move.

Use made-up names for real cars, and no manufacturer logos or badges.

## Fields

`game/CarSpec.gd` lists every field with its unit and the 1999 car's value as the default; the main groups are mass and body, geometry, tyres, steering, brakes, aerodynamics, engine (`combustion` with a torque curve, or `electric` with peak torque, power and speed), gearbox and clutch, drivetrain (`rwd`, `fwd` or `awd` with a centre split and differential bias ratios; every car here is `rwd` and combustion, and the other layouts and the electric motor stay supported for future cars), suspension per axle, and the calibration targets. `axle_grip` gives staggered tyres their own grip per axle.

The tyre's longitudinal grip follows a slip-ratio curve: full grip (`static_mu`) up to `slip_ratio_peak` (0.10), falling by `slip_ratio_falloff` toward a locked or spinning wheel (reached at 3.5 × the peak). The Corvette, on radials and with ABS, sets 10 % (dry asphalt slides at 83–94 % of the peak: Wong, *Theory of Ground Vehicles*, table 1.3). The default is flat (0), and the 1999 roadster and the 1965 Mustang keep it: the roadster's only braking figure is a locked-wheel stop without ABS, which fixes the grip of a sliding tyre, and a fall-off would need an unmeasured peak above it (peak 1.07 sliding to 0.963 raised its trail-braking grip 5.3 %); for the Mustang an estimated bias-ply curve (peak 0.15, 25 % fall-off) made the full-throttle 0–60 10.7 s and 10 % made it 9.35 s against Car Life's 8.9 s.

`abs_enabled` and `traction_control` are on only for cars that had them (of the cars here, only the Corvette). ABS modulates each wheel's brake pressure about the tyre's peak slip. Traction control cuts engine or motor torque to hold the most-slipping driven wheel at the same peak slip (`tc_target_slip` overrides it): it reads the slip through a 20 ms filter and a slew limit, acts proportional (0.15) plus integral (`tc_gain`, 3/s) on the error, holds its state through a gear change, does not wind up while the clutch slips at its capacity, and moves the cut no faster than an engine can (off within 50 ms, back on over 0.15 s). Targets past the peak (1.15 or 1.3 ×) oscillated. Three checks in the default suite guard this: `checks/TractionControlCheck.gd` (torque factor and driveline torque during a full-throttle launch and a power-on corner exit; fails on a burst of 6 or more reversals of 5 % lasting 0.5 s or more), `checks/CombinedSlip.gd` (braking while cornering at 35 % and 70 % of the skidpad target, compared with main's numbers; fails when a car without ABS gains more than 3 %) and `checks/AbsReverse.gd` (ABS stops forwards and in reverse from 6 m/s).

A clutch, torque converter or dual clutch couples the engine to the driven wheels with a torque proportional to their speed difference. The coupling is now limited to the torque that closes the gap in one force substep: in low gears with the tyres at their limit the wheels reflect so little inertia that the explicit coupling overshot, and on main the clutch torque alternated sign for 2.2 s in the launch of the (since retired) front-drive Kestrel S and 1.75 s in the Corvette's (`TractionControlCheck -- --all-cars`). A dual clutch changes gear without interrupting the drive.
