# Cars

Every car in SimCruise is a JSON file in [`game/cars/`](../game/cars/). Powertrain, suspension, tyres, brakes and aerodynamics read their numbers from it through `game/CarSpec.gd`, so adding a car means writing data, not code. The physics is shared by all cars: `game/Powertrain.gd` (contacts, tyres, engine or motor, gearbox, differentials) and `game/CarSuspension.gd` (springs, anti-roll bars, dampers, limited-slip split).

## The garage

![The five generated cars rendered in Blender, and the Kestrel S in the game on Capitol Avenue, Cheyenne](images/garage.jpg)

*Kestrel S, Tora RS, Volta, Meridian Z and Bison XL as generated, and the Kestrel S in the game. The Meridian Z's physics now carry a real Corvette Stingray (C8) model; see below.*

| File | Name | Layout | Reference car | 0–60 mph | 70–0 mph | Skidpad |
|---|---|---|---|---|---|---|
| `nb1999.json` | 1999 Roadster Sport | front engine, rear drive, 5-speed manual | 1999 Mazda MX-5 Miata Sport | 7.99 s (7.97) | 172.9 ft (173) | 0.884 g (0.89) |
| `nb1999_turbo.json` | 1999 Roadster Turbo, tuned | front engine, rear drive, turbo four, 6-speed manual, coilovers | 2004 Mazdaspeed MX-5 (drivetrain, wheels), Flyin' Miata coilovers | 6.91 s (6.7–7.1) | 60–0: 113.3 ft (113–114) | 0.903 g (0.90¶) |
| `kestrel_s.json` | Kestrel S | front drive, turbo four, 6-speed manual | 2022 Honda Civic Si | 6.92 s (6.8) | 162.8 ft (160) | 0.953 g (0.94) |
| `tora_rs.json` | Tora RS | all-wheel drive, turbo flat four, 6-speed manual | 2022 Subaru WRX | 5.61 s (5.5) | 155.5 ft (153) | 0.953 g (0.95) |
| `volta.json` | Volta | electric, rear motor, single speed | Tesla Model 3 RWD | 5.35 s (5.4) | 167.0 ft (165*) | 0.897 g (0.88) |
| `meridian_z.json` | Corvette Stingray Z51 (C8), formerly Meridian Z | mid-engine V8, rear drive, 8-speed dual clutch | 2020 Chevrolet Corvette Z51 | 3.55 s (2.8†) | 152.0 ft (148) | 1.025 g (1.03) |
| `mustang65.json` | 1965 Mustang 289 | front engine, rear drive, 289 V8 four-barrel, 4-speed manual | 1965 Ford Mustang 289/210 | 9.00 s (8.9) | 259.3 ft (264‡) | 0.699 g (0.70*) |
| `bison_xl.json` | Bison XL | crew-cab pickup, twin-turbo V6, 10-speed automatic, 4WD | Ford F-150 2.7 EcoBoost 4x4 | 5.73 s (5.7) | 189.4 ft (190) | 0.736 g (0.74) |

Results are from `checks/CarTrack.gd`; published targets in brackets (mostly Car and Driver instrumented tests, cited in each file's `sources`). \* estimated target. ‡ derived from Car Life's measured 20 ft/s² from 80 mph (drum brakes). ¶ Car and Driver's figure as quoted on owner forums; no primary copy of that test was reachable, so it is low confidence. † open item, stated in the file: the magazine omits a 1 ft rollout worth about 0.3 s on a launch this hard while the bench times from rest, and from pedal-down at idle, where the car's launch control holds the engine at launch speed before the brake is released (not modelled). Before the slip-ratio tyre curve (main on 10 November 2026) the same bench read 7.95, 6.83, 5.58, 5.30, 3.60, 9.00 and 5.68 s; the roadster's 0.04 s comes from the driveline fix below, not from its tyres.

The new cars carry made-up names and makers on purpose: the specifications are real and sourced, the brands and body designs are not, which keeps the project clear of trademarks. The 1999 roadster is the original car of the game.

## How a car is calibrated

`checks/CarTrack.gd` runs every car through three tests on a flat pad:

- **0–60 mph**: full throttle in automatic from rest (the same procedure as `checks/Track.gd`), timed from rest without the magazines' 1 ft rollout. Every car gets the same full pedal, with or without traction control: there is no bench driver feeding in the throttle, so the time is what a player on the keyboard (whose accelerator is all or nothing) gets. The speed at which the automatic clutch bites (`launch_rpm` + `launch_rpm_per_throttle` × pedal) is part of each car's file and explained in its `sources.launch` where it was chosen for this curve (Kestrel S 2500 rpm, Corvette 3000 rpm: their launches are tyre-limited, so a higher speed only revs the engine longer before the clutch bites).
- **70–0 mph**: full service brake, distance in feet. Cars with ABS (`abs_enabled`) hold each tyre at its peak slip, in either direction of travel; cars without lock their wheels and slide.
- **Skidpad**: a ramp steer at the speed a 300 ft circle needs for the target grip: steady speed, steering rising 0.6 % a second, and the peak one-second mean of lateral acceleration before the body slip angle passes 10°. It needs no path-following driver, so it does not depend on controller tuning; on the 1999 car it reads 0.884 g where `Track.gd`'s tuned circle driver demonstrates 0.885 g.

Bands: 0–60 ±0.15 s, 70–0 ±5 ft, skidpad ±0.02 g. A car outside them fails `sh scripts/check.sh` unless its file states the gap in `calibration_open`. A car whose tests stopped from 60 mph sets `target_60_0_ft`; the bench then measures the same full-pedal stop from the moment it passes 60 mph. Where two published tests disagree, the file puts the target between them and widens that band to cover both (`tolerance_0_60_s`, `tolerance_stop_ft`, `tolerance_skidpad_g`); the tuned roadster does this for its 0–60 (6.9 ± 0.25 s covers MotorWeek's 7.1 s and Car and Driver's 6.7 s).

### Steering feel: compliance and the keyboard

A road car understeers mildly in the linear range mostly because its steering yields: the front tyres' side force acts behind the steering axis (caster trail plus the tyre's pneumatic trail) and the column, rack, tie rods and bushes give a little, so the wheels steer slightly out of the corner (the aligning-torque and lateral-force compliance-steer terms of a [Bundorf analysis](https://en.wikipedia.org/wiki/Bundorf_analysis)). The tyre model alone has cornering stiffness proportional to load, so without that the cars were close to neutral: the 1999 roadster measured 0.84 / 0.76 / 0.65 / 0.49 deg/g at 30 / 45 / 60 / 75 mph, where surveyed road cars average 2.6 (European) to 4.4 deg/g (American; SAE 840561). `Powertrain.gd` now turns each front wheel by its side force × (caster trail + pneumatic trail) / steering stiffness, with the pneumatic trail collapsing as the tyre saturates. `steering_stiffness_nm_per_deg` (120 N·m/° per wheel) and `pneumatic_trail_m` (15 mm, a sixth of the contact length) are estimates: no car here has a published value. The roadster now measures 1.70 / 1.60 / 1.48 / 1.39 deg/g; at 60 mph its 0.4 Hz yaw response went from 9.1 to 7.2 (deg/s)/deg with the phase lag from −26° to −15°, the lateral-acceleration lag from −52° to −40°, and a step steer's yaw peak from 0.58–0.88 s to 0.52–0.57 s, so it no longer keeps rotating after a correction. Its CarTrack and LiftOff numbers moved by at most 0.001 g and 0.02 in yaw ratio, and every car stayed in its bands.

The keyboard is input mapping, not chassis: no real roadster is steered by a key. A held key now turns the steering wheel at 120°/s, a hand's pace (ISO 7401's emergency step steer is 200–500°/s), divided by the car's `steering_ratio` (the NB about 15:1, [Cars.com](https://www.cars.com/research/mazda-mx_5_miata-1999/specs/106937/); 15 where no ratio is published), from 10 m/s up; it rises to the parking pace only below 3 m/s, and the rate builds over the first 0.15 s of each press. Before, the rack moved at 67°/s at 30 mph, so an ordinary 0.1 s keypress moved a car 3.2 m sideways at 40 mph; now 0.16 m (`checks/KeyboardTap.gd`, gate 1 m at 40 and 60 mph).

### Lift-off and trail braking

`checks/LiftOff.gd` (default suite) is every car's stability gate, after ISO 9816 (power-off reaction in a turn) and ISO 7975 (braking in a turn). A path and speed driver holds the car on a 40 m circle at 0.3, 0.5 and 0.7 g; then the wheel is frozen and the driver either closes the throttle over 0.1 s, or closes it and from 0.2 s squeezes the brake over 0.3 s to a pedal worth 0.15 g. It reports the yaw-rate ratio at 1 s (ISO 9816's measure: above 1 the car tucks in), the yaw overshoot, body sideslip at 1 s and its peak, the settling time and the deceleration. Gate: no car spins (sideslip past 8°) at 0.3 or 0.5 g, and no automatic ever selects reverse while rolling forward; 0.7 g is reported. `--radius=100` repeats it on ISO's larger circle, `--trace` prints each tyre's load, slip angle and slip ratio, and `--set=field=value` overrides a car-file field for experiments.

On the 1999 roadster a lift at 0.3–0.5 g gives a yaw-rate ratio of 1.01–1.07 with at most 1.2° of sideslip, settling at once; the trail brake 0.99–1.13 and 1.3° (40 m circle). That is the mild tuck-in owners describe ("with power off through a corner the miata tended to understeer slightly", [Grassroots Motorsports forum](https://grassrootsmotorsports.com/forum/grm/practice/12785/page3/); reviewers call the handling "balanced and forgiving", [Cars.com](https://cars.com/research/mazda-mx_5_miata-1999)); no instrumented lift-off test of the NB has been found, so the comparison is qualitative. At 0.7 g it tucks in more (ratio 1.19 lift, 1.43 brake, 4.8° peak) and still recovers.

The Miata's road spins in the November 2026 field test were not its chassis: the automatic selected reverse whenever the brake input was between 0 and the 0.01 brake threshold while rolling forward, and stayed in reverse while the brake was held, so the clutch drove the rear wheels backwards mid-corner. Every car did it (108–1,186 ticks per Tail of the Dragon run); the light rear-drive Miata, whose small brake pedal for a given deceleration spends longest in that band, spun. `Powertrain.gd` now changes direction only below 0.45 m/s, like a real selector's reverse inhibitor. Rear brake bias (50:50 or 75:25), an open differential, zero toe, no rear camber gain, load sensitivity 0–0.2, a 15 % larger yaw inertia and a 4.5 cm higher centre of gravity each moved the 0.7 g results by at most 0.11 in yaw-rate ratio and 0.6° in sideslip, so no chassis value was changed.

The NB's suspension travel is consistent with what owners measure: the car sits about 2 in (51 mm) above its bump stops (14 in to 12 in fender height, [Classic Motorsports forum](https://classicmotorsports.com/forum/grm/miata-bump-stop-question/172527/page1)); the file has 54 mm to the bump rubber and the hard stop 143 mm above ride height. The bump-stop ticks the field test counted on smooth roads came from `RoadDrive.gd` dropping the car from 0.35 m at the start and from the reverse-gear wheel hop; the drive now sets the car down at its ride height and prints where any bump stop is reached.

What is published is used as published: mass, weight split where the maker gives it, engine power and torque, gear ratios, tyre sizes, wheelbase and track. What is not published is estimated and labelled in `sources.estimates`: centre-of-gravity height, inertia (scaled from the 1999 car by mass and size), spring and anti-roll rates (from ride frequencies typical of the class), dampers, brake torque and the tyre model's grip. The tyre grip is the main calibration handle: braking distance sets the longitudinal grip (`static_mu`), the skidpad sets the lateral factor (`lateral_grip_scale`), and launch settings are only tuned inside realistic limits. Published torque is never scaled to make a number.

## Models

The new cars' models are generated in Blender by `authoring/cars/build_car.py` from design files (`authoring/cars/designs/*.py`) whose body lines were measured from Commons photographs of each reference car; see [authoring/cars/README.md](../authoring/cars/README.md). Each car file names its model in `model_scene` and the driver's eye point in `driver_eye`; `game/CarBody.gd` mounts the model, spins and steers the wheels, turns the steering wheel, lights the brake lamps and puts the camera in the driver's seat. The 1999 roadster keeps its detailed `CarFinish.gd` model and cockpit. Pick a car with **Car** in the road atlas (Tab); the drive reopens at the same place. `--car=<id>` selects one on the command line.

### The tuned roadster

![The tuned roadster beside the stock 1999 car in the turntable](images/tuned-roadster.jpg)

*Stock 1999 Roadster Sport (left) and the 1999 Roadster Turbo, tuned (right), from `checks/CarTurntable.gd` at Ultra. Renders of the CC BY-SA 4.0 exterior model, shared under the same licence.*

`nb1999_turbo.json` is what an owner builds from an NB: the 2004 factory turbo car's engine, six-speed, 4.10 final drive, brakes, bars and 17 in wheel size (all from Mazda's 2004 specification sheet), plus coilovers, an alignment, aftermarket-style wheels and a small lip spoiler. It shares the 1999 model and cockpit through `CarFinish.gd`, which reads three finish fields from the file: `finish_wheel` (`multispoke17`: `TunedWheel.gd`, a generic ten-spoke, 17 × 7 at offset +40 with the 269 mm disc and a silver caliper visible through it), `finish_spoiler` (a body-colour lip about 28 mm tall laid along the boot lid's trailing edge) and `paint_color`.

- **Turbo**: `torque_curve` is the off-boost curve and `boost_torque_curve` the full-boost one (225 N·m at 4,500 rpm and 211 N·m at 6,000 rpm are Mazda's; the points between are estimated). Boost is available from `boost_threshold_rpm` 2,500 to `boost_full_rpm` 3,500 (Drivingline: the turbo "takes over at 3,500 rpm"), follows the pedal with a 0.4 s spool lag (`spool_seconds`, estimate) and is vented by the bypass valve within 0.15 s of a lift. The sound adds a compressor whine with boost and a soft recirculated rush on a lift (the factory valve recirculates).
- **Coilovers, and how they differ from the factory turbo car**: Flyin' Miata V-Maxx XXtreme Sport rates, 391 lb/in front and 258 lb/in rear, against the factory car's 215/158 lb/in Bilstein setup; ride frequencies about 1.7 Hz instead of 1.2 Hz; 25 mm under a standard NB (the factory car sits 7 mm under), an estimate of Flyin' Miata's recommended fender heights, which are about an inch below stock. The factory 23/14 mm bars stay. Alignment is Flyin' Miata's street recommendation: 5.0° caster, −1.0° front and −1.5° rear camber, 1/16 in total toe-in. The user asked for coilovers; the V-Maxx Sport is the published street coilover for the NB. Dampers are estimated ratios (the XXtreme is adjustable; mid setting).
- **Calibration**: the car is held to the factory turbo car's tests, since the tyres (205/40R17), drivetrain and mass are that car's. With the coilovers it measures 6.91 s, 113.3 ft (60–0) and 0.903 g; the same file with the factory springs, ride height and alignment gives 6.87 s, 114.2 ft and 0.890 g, so the coilovers add about 0.013 g of skidpad grip in this model and the grip calibration does not depend on them. Lift-off and trail braking match the stock car within 0.02 in yaw-rate ratio (1.01–1.11 at 0.3–0.5 g); SBend passes to 0.74 g.
- **Steering**: lock 31.8° from the factory car's 34.2 ft turning circle (the stock car's 30.2 ft at 36.6°), ratio 13.0:1 from its 2.3 turns lock to lock.
- **Spoiler**: no lift or downforce figure exists for any NB spoiler, and a lip a few centimetres tall makes a few kilograms at most at 60 mph, so it is drawn and not simulated.

### The 1999 roadster against photographs

`checks/CarTurntable.gd` (`--car-turntable DIR --quality=ultra [--lamps]`, windowed) photographs the car as the game builds it, alone on a neutral pad, from eight angles, the top, close-ups of a headlamp, tail lamp, wheel and the cabin, and the driver view; [lab/miata/appearance/turntable](../lab/miata/appearance/turntable/README.md) sets these beside Commons photographs of 1999 cars, including a green 1999 with the body-colour factory hardtop like the game's. From that comparison `CarFinish.gd` now smooths the faceted hardtop and glass, uses a lighter green-tinted glass, black trim, underbody and wheel liners, a slimmer body-colour windscreen frame, the black cowl grille, larger mirrors at the front corner of the doors, darker metallic wheels, a green (not chrome) hood from the seat and a hardtop lining in the driver view. **L** switches the head lamps: the lenses glow, two low-beam spot lights light the road, the amber markers glow and the stop lamps keep a dim tail glow. The headlamp opening, the rear bumper, the hardtop's rear glass and the coarse body shell still differ and need a new model; the physics, mounts and collision are unchanged.

The generated models have accurate proportions, working glass, lamps and wheels, and clean silhouettes, but they are not photoreal. Better models are a welcome contribution: CC0 or CC BY, with the node names the README lists.

## Real models from Sketchfab

The 1965 Mustang and the Corvette Stingray (C8, the physics formerly shipped as Meridian Z) use downloaded models. A car file with `"model_rig": {"fit": true}` is fitted by `game/ModelRig.gd` when the car loads. It finds the four wheels, by name when the author separated them (the C8) or by cutting every triangle within a tyre radius of each ground-contact patch out of the body (the Mustang, whose wheels were merged into the body). It scales the model to the spec's wheelbase, turns it to face −Z, sets the hubs at static ride height, maps brake-lamp and glass materials, and hides badge surfaces. `checks/RigProbe.gd` prints what it found. Fitted sizes match the real cars: Mustang 4.62 m long (4.61 m real), C8 4.64 m (4.63 m).

## Your own model for a car (private)

You can give any car an exterior you own, for example a model you bought, without it entering the repository. Put a binary glTF named after the car's id in the game's user folder:

- Windows: `%APPDATA%\Godot\app_userdata\SimCruise\cars\nb1999.glb`
- macOS: `~/Library/Application Support/Godot/app_userdata/SimCruise/cars/nb1999.glb`
- Linux: `~/.local/share/godot/app_userdata/SimCruise/cars/nb1999.glb`

At start the game reads it at run time (no import), and `ModelRig.gd` fits it like a Sketchfab model: it turns the model upright if needed (the shortest axis is up, tyres below the body), finds the four wheels by name or geometry, scales it to the car's wheelbase and sets it at ride height. The physics stay those of the car file. `--stock-model` ignores the file for one run. Only use a model whose licence lets you use it; the project ships only CC0 and CC BY models (see below), and a file here is never committed or redistributed.

## Traffic models from Sketchfab

Moving and parked traffic use only real models listed in `game/art/traffic/manifest.json`: 1965 Ford Mustang, Corvette Stingray C8, 2018 Honda Accord, 2025 Honda CR-V, 2018 Ford F-150 Raptor, 2013 Jeep Grand Cherokee, Chevrolet Tahoe, Honda Odyssey and Tesla Model 3 (all CC BY 4.0, credited in `licenses/Traffic models.txt`). The old low-poly sedan and pickup and the generated garage bodies are no longer used for traffic. Each merged model gets detail levels rebuilt at load, and `checks/TrafficLineup.gd` renders the fitted fleet in a row to check orientation (a model showing its tail from the front needs `"flip": true`). A model is accepted only if it looks at least as good as these; the Silverado (wheels missing) and Transit Connect (transparent materials) were rejected. `authoring/cars/sketchfab.mjs` fills that manifest:

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

`game/CarSpec.gd` lists every field with its unit and the 1999 car's value as the default; the main groups are mass and body, geometry, tyres, steering, brakes, aerodynamics, engine (`combustion` with a torque curve, or `electric` with peak torque, power and speed), gearbox and clutch, drivetrain (`rwd`, `fwd` or `awd` with a centre split and differential bias ratios), suspension per axle, and the calibration targets. `axle_grip` gives staggered tyres their own grip per axle.

The tyre's longitudinal grip follows a slip-ratio curve: full grip (`static_mu`) up to `slip_ratio_peak` (0.10), falling by `slip_ratio_falloff` toward a locked or spinning wheel (reached at 3.5 × the peak). The five modern cars, on radials and with ABS, set 10 % (dry asphalt slides at 83–94 % of the peak: Wong, *Theory of Ground Vehicles*, table 1.3). The default is flat (0), and the 1999 roadster and the 1965 Mustang keep it: the roadster's only braking figure is a locked-wheel stop without ABS, which fixes the grip of a sliding tyre, and a fall-off would need an unmeasured peak above it (peak 1.07 sliding to 0.963 raised its trail-braking grip 5.3 %); for the Mustang an estimated bias-ply curve (peak 0.15, 25 % fall-off) made the full-throttle 0–60 10.7 s and 10 % made it 9.35 s against Car Life's 8.9 s.

`abs_enabled` and `traction_control` are on only for cars that had them (every car here except the 1999 roadster and the 1965 Mustang). ABS modulates each wheel's brake pressure about the tyre's peak slip. Traction control cuts engine or motor torque to hold the most-slipping driven wheel at the same peak slip (`tc_target_slip` overrides it): it reads the slip through a 20 ms filter and a slew limit, acts proportional (0.15) plus integral (`tc_gain`, 3/s) on the error, holds its state through a gear change, does not wind up while the clutch slips at its capacity, and moves the cut no faster than an engine can (off within 50 ms, back on over 0.15 s). Targets past the peak (1.15 or 1.3 ×) oscillated. Three checks in the default suite guard this: `checks/TractionControlCheck.gd` (torque factor and driveline torque during a full-throttle launch and a power-on corner exit; fails on a burst of 6 or more reversals of 5 % lasting 0.5 s or more), `checks/CombinedSlip.gd` (braking while cornering at 35 % and 70 % of the skidpad target, compared with main's numbers; fails when a car without ABS gains more than 3 %) and `checks/AbsReverse.gd` (ABS stops forwards and in reverse from 6 m/s).

A clutch, torque converter or dual clutch couples the engine to the driven wheels with a torque proportional to their speed difference. The coupling is now limited to the torque that closes the gap in one force substep: in low gears with the tyres at their limit the wheels reflect so little inertia that the explicit coupling overshot, and on main the clutch torque alternated sign for 2.2 s in the Kestrel S launch and 1.75 s in the Corvette's (`TractionControlCheck -- --all-cars`). A dual clutch changes gear without interrupting the drive.
