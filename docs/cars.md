# Cars

Every car in SimCruise is a JSON file in [`game/cars/`](../game/cars/). Powertrain, suspension, tyres, brakes and aerodynamics read their numbers from it through `game/CarSpec.gd`, so adding a car means writing data, not code. The physics is shared by all cars: `game/Powertrain.gd` (contacts, tyres, engine or motor, gearbox, differentials) and `game/CarSuspension.gd` (springs, anti-roll bars, dampers, limited-slip split).

## The garage

![The five generated cars rendered in Blender, and the Kestrel S in the game on Capitol Avenue, Cheyenne](images/garage.jpg)

*Kestrel S, Tora RS, Volta, Meridian Z and Bison XL as generated, and the Kestrel S in the game. The Meridian Z's physics now carry a real Corvette Stingray (C8) model; see below.*

| File | Name | Layout | Reference car | 0–60 mph | 70–0 mph | Skidpad |
|---|---|---|---|---|---|---|
| `nb1999.json` | 1999 Roadster Sport | front engine, rear drive, 5-speed manual | 1999 Mazda MX-5 Miata Sport | 7.99 s (7.97) | 172.9 ft (173) | 0.884 g (0.89) |
| `kestrel_s.json` | Kestrel S | front drive, turbo four, 6-speed manual | 2022 Honda Civic Si | 6.92 s (6.8) | 162.8 ft (160) | 0.953 g (0.94) |
| `tora_rs.json` | Tora RS | all-wheel drive, turbo flat four, 6-speed manual | 2022 Subaru WRX | 5.61 s (5.5) | 155.5 ft (153) | 0.953 g (0.95) |
| `volta.json` | Volta | electric, rear motor, single speed | Tesla Model 3 RWD | 5.35 s (5.4) | 167.0 ft (165*) | 0.897 g (0.88) |
| `meridian_z.json` | Corvette Stingray Z51 (C8), formerly Meridian Z | mid-engine V8, rear drive, 8-speed dual clutch | 2020 Chevrolet Corvette Z51 | 3.55 s (2.8†) | 152.0 ft (148) | 1.025 g (1.03) |
| `mustang65.json` | 1965 Mustang 289 | front engine, rear drive, 289 V8 four-barrel, 4-speed manual | 1965 Ford Mustang 289/210 | 9.00 s (8.9) | 259.3 ft (264‡) | 0.699 g (0.70*) |
| `bison_xl.json` | Bison XL | crew-cab pickup, twin-turbo V6, 10-speed automatic, 4WD | Ford F-150 2.7 EcoBoost 4x4 | 5.73 s (5.7) | 189.4 ft (190) | 0.736 g (0.74) |

Results are from `checks/CarTrack.gd`; published targets in brackets (mostly Car and Driver instrumented tests, cited in each file's `sources`). \* estimated target. ‡ derived from Car Life's measured 20 ft/s² from 80 mph (drum brakes). † open item, stated in the file: the magazine omits a 1 ft rollout worth about 0.3 s on a launch this hard while the bench times from rest, and from pedal-down at idle, where the car's launch control holds the engine at launch speed before the brake is released (not modelled). Before the slip-ratio tyre curve (main on 10 November 2026) the same bench read 7.95, 6.83, 5.58, 5.30, 3.60, 9.00 and 5.68 s; the roadster's 0.04 s comes from the driveline fix below, not from its tyres.

The new cars carry made-up names and makers on purpose: the specifications are real and sourced, the brands and body designs are not, which keeps the project clear of trademarks. The 1999 roadster is the original car of the game.

## How a car is calibrated

`checks/CarTrack.gd` runs every car through three tests on a flat pad:

- **0–60 mph**: full throttle in automatic from rest (the same procedure as `checks/Track.gd`), timed from rest without the magazines' 1 ft rollout. Every car gets the same full pedal, with or without traction control: there is no bench driver feeding in the throttle, so the time is what a player on the keyboard (whose accelerator is all or nothing) gets. The speed at which the automatic clutch bites (`launch_rpm` + `launch_rpm_per_throttle` × pedal) is part of each car's file and explained in its `sources.launch` where it was chosen for this curve (Kestrel S 2500 rpm, Corvette 3000 rpm: their launches are tyre-limited, so a higher speed only revs the engine longer before the clutch bites).
- **70–0 mph**: full service brake, distance in feet. Cars with ABS (`abs_enabled`) hold each tyre at its peak slip, in either direction of travel; cars without lock their wheels and slide.
- **Skidpad**: a ramp steer at the speed a 300 ft circle needs for the target grip: steady speed, steering rising 0.6 % a second, and the peak one-second mean of lateral acceleration before the body slip angle passes 10°. It needs no path-following driver, so it does not depend on controller tuning; on the 1999 car it reads 0.884 g where `Track.gd`'s tuned circle driver demonstrates 0.885 g.

Bands: 0–60 ±0.15 s, 70–0 ±5 ft, skidpad ±0.02 g. A car outside them fails `sh scripts/check.sh` unless its file states the gap in `calibration_open`.

### Lift-off and trail braking

`checks/LiftOff.gd` (default suite) is every car's stability gate, after ISO 9816 (power-off reaction in a turn) and ISO 7975 (braking in a turn). A path and speed driver holds the car on a 40 m circle at 0.3, 0.5 and 0.7 g; then the wheel is frozen and the driver either closes the throttle over 0.1 s, or closes it and from 0.2 s squeezes the brake over 0.3 s to a pedal worth 0.15 g. It reports the yaw-rate ratio at 1 s (ISO 9816's measure: above 1 the car tucks in), the yaw overshoot, body sideslip at 1 s and its peak, the settling time and the deceleration. Gate: no car spins (sideslip past 8°) at 0.3 or 0.5 g, and no automatic ever selects reverse while rolling forward; 0.7 g is reported. `--radius=100` repeats it on ISO's larger circle, `--trace` prints each tyre's load, slip angle and slip ratio, and `--set=field=value` overrides a car-file field for experiments.

On the 1999 roadster a lift at 0.3–0.5 g gives a yaw-rate ratio of 1.01–1.07 with at most 1.2° of sideslip, settling at once; the trail brake 0.99–1.13 and 1.3° (40 m circle). That is the mild tuck-in owners describe ("with power off through a corner the miata tended to understeer slightly", [Grassroots Motorsports forum](https://grassrootsmotorsports.com/forum/grm/practice/12785/page3/); reviewers call the handling "balanced and forgiving", [Cars.com](https://cars.com/research/mazda-mx_5_miata-1999)); no instrumented lift-off test of the NB has been found, so the comparison is qualitative. At 0.7 g it tucks in more (ratio 1.19 lift, 1.43 brake, 4.8° peak) and still recovers.

The Miata's road spins in the November 2026 field test were not its chassis: the automatic selected reverse whenever the brake input was between 0 and the 0.01 brake threshold while rolling forward, and stayed in reverse while the brake was held, so the clutch drove the rear wheels backwards mid-corner. Every car did it (108–1,186 ticks per Tail of the Dragon run); the light rear-drive Miata, whose small brake pedal for a given deceleration spends longest in that band, spun. `Powertrain.gd` now changes direction only below 0.45 m/s, like a real selector's reverse inhibitor. Rear brake bias (50:50 or 75:25), an open differential, zero toe, no rear camber gain, load sensitivity 0–0.2, a 15 % larger yaw inertia and a 4.5 cm higher centre of gravity each moved the 0.7 g results by at most 0.11 in yaw-rate ratio and 0.6° in sideslip, so no chassis value was changed.

The NB's suspension travel is consistent with what owners measure: the car sits about 2 in (51 mm) above its bump stops (14 in to 12 in fender height, [Classic Motorsports forum](https://classicmotorsports.com/forum/grm/miata-bump-stop-question/172527/page1)); the file has 54 mm to the bump rubber and the hard stop 143 mm above ride height. The bump-stop ticks the field test counted on smooth roads came from `RoadDrive.gd` dropping the car from 0.35 m at the start and from the reverse-gear wheel hop; the drive now sets the car down at its ride height and prints where any bump stop is reached.

What is published is used as published: mass, weight split where the maker gives it, engine power and torque, gear ratios, tyre sizes, wheelbase and track. What is not published is estimated and labelled in `sources.estimates`: centre-of-gravity height, inertia (scaled from the 1999 car by mass and size), spring and anti-roll rates (from ride frequencies typical of the class), dampers, brake torque and the tyre model's grip. The tyre grip is the main calibration handle: braking distance sets the longitudinal grip (`static_mu`), the skidpad sets the lateral factor (`lateral_grip_scale`), and launch settings are only tuned inside realistic limits. Published torque is never scaled to make a number.

## Models

The new cars' models are generated in Blender by `authoring/cars/build_car.py` from design files (`authoring/cars/designs/*.py`) whose body lines were measured from Commons photographs of each reference car; see [authoring/cars/README.md](../authoring/cars/README.md). Each car file names its model in `model_scene` and the driver's eye point in `driver_eye`; `game/CarBody.gd` mounts the model, spins and steers the wheels, turns the steering wheel, lights the brake lamps and puts the camera in the driver's seat. The 1999 roadster keeps its detailed `CarFinish.gd` model and cockpit. Pick a car with **Car** in the road atlas (Tab); the drive reopens at the same place. `--car=<id>` selects one on the command line.

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

## The road a car drives on

Paved roads on streamed tiles are driven on, and drawn as, the ribbons `game/RoadSurface.gd` builds from the helper's road profile. The ground shader `game/terrain.gdshader` (only that one: the field-tile ground in `FieldTerrain.gdshader` has no ribbons and no lane wear) draws lane wear on them from each vertex's place in the road: wheel paths 0.9 m either side of each lane centre, an oil-drip strip between them, sealed paving joints and transverse cracks, and patches. Lanes are fitted inside the carriageway at every station, from the paved width there, by `RoadSurface.lane_fit()`: per side, a further 3.6 m lane is counted once three quarters of it fits, and the lanes of that side then narrow to share the half width. A lane is therefore 2.7–3.6 m with one lane per side (AASHTO's 9–12 ft range: a 5.4–7.2 m road is two narrowed lanes) and 3.15–3.6 m with two or more; what is left of the half width (under 2.7 m) is shoulder or parking, and a carriageway under 5.4 m is one centred lane. Every wear feature is masked by the carriageway, so nothing is drawn on the shoulder beyond it. How worn a street looks is the street condition `game/RoadRoughness.gd` gives the tyres (class IRI × a stable per-street hash), taken at every 2 m station of the ribbon from the street the helper exports with that station, so with `--rough-roads` a road that looks worn also rides worn, even on a street only a few metres long; the condition also scales the texture's own sealed cracks before they set the pavement colour. The street, the side and the distance along come from the map piece under the station (one road of one z14 vector tile), not from the tile that draws it, so where a road runs along a tile seam and both tiles draw it, the two copies show the same cracks, joints, patches and condition, and a station whose street lies in the neighbouring tile is no longer drawn at a default condition. Each source line starts its distance along at its own offset (a hash of its end points and its vector tile), so two roads that meet do not repeat the same cracks and patches from the junction. The tyres take the street nearest the wheel from the tile's own roads, which at a junction can be the crossing road and which can name an unnamed road from a different label than the helper's piece does: at Bozeman 92 % of the ribbon centre vertices drew the condition the tyres feel at that point. `--road-condition=X` (0 new to 1 worn) forces a condition for comparisons and `--without-lane-wear` turns the layer off.

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
