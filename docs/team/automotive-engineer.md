# Automotive engineer: study notes

Role: check that the three hero cars carry real-car data and real-car systems. Cars: Mazda MX-5 Miata NB 2001 (player car, `data/cars/nb2001_aaron.json`, stock reference `nb2001.json`), Chevrolet Corvette C8 Z51 (`meridian_z.json`), 1965 Ford Mustang 289 (`mustang65.json`).
Style: ASD-STE100 Simplified Technical English. "Unverified" marks a fact that this study could not confirm from a primary source.

## 1. Rules for car data

- Use the maker's published numbers as published: mass, power, torque, gear ratios, tyre sizes, wheelbase, track.
- Label every estimate in the car file's `sources.estimates`.
- Never scale torque or grip to hit one test number. Find the missing physics first.
- Every number gets a unit in its field name or in `CarSpec.gd`. Check lb/in, lbf, in/s and ft conversions.
- When the owner can measure his own car, that measurement beats any magazine.

## 2. Mazda MX-5 Miata NB, 2001 (NB2, US LS, 5-speed)

Published or sourced:
- Engine BP-Z3 1.8 L with sequential valve timing. 142 hp (SAE net) at 7000 rpm; 125 lb-ft (169 N·m) at 5500 rpm (Mazda US specification per `docs/cars.md`; Moss Motors page lists 142 hp and "116" torque without a unit, unresolved).
- Check: 144 N·m at 7000 rpm gives 105.6 kW = 141.6 hp. The file curve is consistent.
- 5-speed ratios 3.136 / 1.888 / 1.330 / 1.000 / 0.814 (Moss Motors 2001 specification). Final drive 4.30 (file; unverified for every 2001 trim).
- 6-speed (other trims) 3.760 / 2.269 / 1.645 / 1.257 / 1.000 / 0.843.
- Limited-slip: Torsen, standard on the LS and optional with the suspension package, manual only (Moss Motors 2001).
- Differential type by year (miata.net diff guide): 1999-2002 Torsen Type II, torque bias ratio "approximately 2.5 or a little less"; 2003-05 Tochigi Fuji Super LSD, TBR about 2.0.
- Curb weight: 2,387 lb (1083 kg) LS in the file (cars.com/autotk listing). Moss Motors lists 2,200 lb (likely base dry or earlier year; unresolved).
- Brakes: from 2001, ABS cars have electronic brake-force distribution (EBD). Non-ABS cars have a proportioning valve. 2002 non-ABS valve: knee 569, break 853, rear at break 702 (psi presumed). The slope after the knee is about 0.47 (computed from those points). Source: Flyin' Miata, "Stock brake proportioning valves". The master cylinder size changed in 2001.
- Steering: rack and pinion, engine-speed-sensing power assist. File card: 15:1, 2.6 turns lock to lock (GoAuto review; secondary). Turning circle 9.2 m (cars-data.com; secondary).
- Tyres: stock LS 2001 195/50R16 per Moss (other sources give 205/45R16; unresolved). The owner's car: 15x8 wheels, 205/50R15, 200-treadwear.

Owner car (from photos, all estimates): lowered about 35 mm on coilovers, springs 7 / 5 kg/mm (392 / 280 lb/in).

Typical NB geometry (all unverified, measure on the owner's car): caster about 4.5-5.5°, front camber 0 to −1°, rear camber −0.5 to −1.5°, motion ratio front about 0.68, rear about 0.77.

## 3. Chevrolet Corvette C8 Stingray Z51 (2020)

- Mid engine LT2 6.2 L V8, 495 hp, 470 lb-ft with Z51 exhaust (Chevrolet; file peak 637 N·m matches 470 lb-ft).
- TREMEC TR-9080 8-speed dual clutch: 2.91 / 1.76 / 1.22 / 0.88 / 0.65 / 0.51 / 0.40 / 0.33. Final drive 5.17 (file, cited).
- Weight split about 40/60 (Chevrolet). Car and Driver measured 3,647 lb (file mass 1654 kg).
- Tyres Z51: 245/35ZR19 front, 305/30ZR20 rear (Michelin Pilot Sport 4S; well known, verify in the order guide). The file has one tyre size for both axles.
- Z51 has an electronic limited-slip differential (clutch type, controlled). It is not a fixed-bias Torsen. The file uses bias 3.0 as a stand-in.
- Z51 adds aero (front splitter, rear spoiler) with downforce (value unverified). The sim has drag only.
- ABS, traction and stability control standard.
- Targets: 0-60 2.8 s, 70-0 148 ft, skidpad 1.03 g (Car and Driver).

## 4. Ford Mustang 1965, 289 V8

- File: 1310 kg, 56 % front, 4-speed manual 2.78 / 1.93 / 1.36 / 1.00, final drive 3.00, open differential, manual steering 21.7:1 (overall; unverified), bias-ply tyres, drum or front disc brakes by option, no ABS.
- Bias-ply tyres: peak grip about 0.7-0.8, larger peak slip, longer relaxation length than radials (unverified ranges). File skidpad target 0.70 g, 70-0 264 ft.
- Optional limited-slip ("Equa-Lock") existed (unverified for the file's car).

## 5. Systems knowledge

### 5.1 Brakes
- Ideal front share at deceleration a (in g): Φ_f = ψ + (h/L)·a, where ψ is the static front fraction.
- NB stock: ψ 0.515, h 0.435, L 2.266. At 1.0 g the ideal front share is 0.707.
- A fixed split below the ideal share locks the rear axle first. Rear-first lock makes a car yaw-unstable. Real cars prevent it with a proportioning valve (knee plus slope), a load-sensing valve or EBD.
- Brake torque per wheel = 2 · μ_pad · p · A_piston · r_eff (two pad faces). Typical μ_pad 0.35-0.45.
- ABS targets the peak slip ratio (about 0.1-0.15) and cycles at about 5-15 Hz.

### 5.2 Engine
- Closed-throttle engine braking = friction (FMEP) + pumping (PMEP). FMEP rises with speed, about 0.6 bar at 1000 rpm to 2.2 bar at 6000 rpm for a 2 L engine (Chen-Flynn example, x-engineer.org). PMEP at closed throttle adds about 0.7-1.0 bar (estimate).
- Torque from mean effective pressure for a four-stroke: T = p·Vd/(4π).
- For the 1.84 L BP: 2 bar gives 29 N·m; 3 bar gives 44 N·m.

### 5.3 Clutch and driveline
- A dry clutch has static friction. Once locked, engine and gearbox turn together with zero slip.
- Clutch capacity is usually 1.2-1.5 times peak engine torque (estimate). File: 210 / 169 = 1.24.
- Driveline shuffle (1-5 Hz in low gears) comes from driveshaft and half-shaft compliance. SimCruise has no driveline compliance.

### 5.4 Differentials
- Torsen: torque bias ratio TBR = T_high/T_low. Transfer limit = T_half·(TBR−1)/(TBR+1). Works in drive and coast. Type II has different drive and coast bias (values unverified).
- Clutch LSD: preload plus ramp-angle locking. Drive and coast ramps can differ (1-way, 1.5-way, 2-way).
- Open: equal torque; the lighter wheel spins.

### 5.5 Steering
- Overall ratio = steering-wheel angle / mean road-wheel angle. Lock-to-lock turns = 2 · δ_max · ratio / 360.
- Mechanical trail = R·tan(caster) minus the kingpin offset term. The sim uses R·sin(caster).

## 6. How to judge SimCruise

Files: `data/cars/nb2001_aaron.json`, `nb2001.json`, `meridian_z.json`, `mustang65.json`; `Source/SimCruise/Private/Vehicle/SimCruiseVehicleSim.cpp` (brakes 812-833, engine 708-790, clutch 725-750), `SimCruisePowertrainModel.cpp` (diff), `SimCruiseSuspensionModel.cpp` (springs, bars, dampers), `CarDefinitionLoader.cpp` (field names and units), `docs/cars.md`.

Checks:
1. Power check: torque at the power peak × ω equals the rated power within 2 %.
2. Gear check: top speed in each gear at the rev limit matches road tests within 3 %.
3. Brake balance: compute which axle locks first at 0.5, 0.8 and 1.0 g with the brake model in the code. The rear must never lock first on a car without ABS.
4. Engine braking: coast down in 3rd gear from 4000 rpm. Compare with a log of the owner's car.
5. Ride frequency from spring rate, motion ratio and sprung corner mass. Street: 1.0-1.5 Hz. Coilover: 1.6-2.2 Hz.
6. Damping ratio from damper dyno and motion ratio: c_wheel = c_shaft · MR².
7. Steering: lock angle × ratio must match the published turns lock to lock.
8. Tyre radius from the size string: R = rim/2 + width × aspect.
9. Every estimate is labelled in `sources`.

## 6a. Data audit of the player car (nb2001_aaron.json), 2026-11-12

| Field | File | Real or source | Status |
|---|---|---|---|
| mass_kg | 1083 | 2,387 lb LS (cars.com) | OK; Moss lists 2,200 lb (unresolved) |
| front_weight | 0.515 | about 0.52 at curb (unverified) | plausible |
| cg_height_m | 0.40 | stock 0.435 minus 35 mm lowering | estimate |
| gears | 3.136/1.888/1.330/1.000/0.814 | Moss 2001 | OK |
| final_drive | 4.30 | file source "Mazda specification" | OK (verify for LS) |
| torque peak | 169 N·m at 5500 | 125 lb-ft at 5500 | OK |
| power check | 105.6 kW at 7000 | 142 hp = 105.9 kW | OK |
| fuel_cut_rpm | 7250 | redline 7000 (tach) | plausible |
| rear_diff_bias | 2.0 | Torsen II, about 2.5 (miata.net) | fix to 2.5 |
| brake_front_nm / rear | 1150 / 650 | no published torque | estimate; balance wrong (see review) |
| proportioning | none | valve (non-ABS) or EBD (ABS) | missing |
| abs_enabled | false | ABS optional in 2001 | ask the owner |
| engine_friction | 10 + 0.002·rpm N·m | about 29 N·m at 3000 rpm (estimate) | about half of real |
| coil_lb_in | 392 / 280 | owner coilovers, 7/5 kg/mm | estimate; measure |
| static_camber_deg | −0.53 / −1.1 | lowering adds negative camber | measure alignment |
| caster_deg | 6.0 | about 4.5-5.5° (unverified) | measure |
| parking_rack_deg × ratio | 36.6° × 15 = 549° | 2.6 turns = 468° per side | inconsistent |
| tyre | 205/50R15 | owner's car | OK |

## 6b. Owner measurements that close the most estimates (Garage Day)

1. Corner weights on four scales, with the driver: mass and front fraction.
2. Alignment sheet from a shop: camber, caster, toe per wheel.
3. Lock-to-lock turns of the steering wheel, and the road-wheel angle at full lock (turn plates or a phone protractor).
4. Ride height at each corner, and the gap to each bump stop (lift the car, measure droop and bump).
5. Spring labels on the coilovers (rate in kg/mm) and damper brand and setting.
6. Brake test: full pedal from 60 mph on a closed, safe, legal surface only, with a phone logger. Note which axle locks first if the car has no ABS.
7. Coast-down in 3rd gear from 4000 rpm and in neutral from 60 mph: engine braking and road load.
8. Diff check: VIN decode and the tag on the diff (Torsen or Tochigi Fuji).

## 7. Common failure modes

- lb/in to N/m: × 175.127. lbf to N: × 4.448. in/s to m/s: × 0.0254. ft to m: × 0.3048.
- Motion ratio used once instead of squared for the wheel rate.
- Crank torque used as wheel torque, or efficiency applied twice.
- One tyre size for a staggered car.
- Year mix-ups: NB1 (1999-2000) and NB2 (2001-05) differ in engine, master cylinder, brake valves and diff after 2003.
- Brake torque raised to hit a stopping distance, which moves the balance.
- Grip raised on a modified car (stickier tyres) without a change to brake torque. The brake balance and the full-pedal deceleration then change.
- A lowered car with stock geometry numbers: bump travel, camber and roll centre all change when a car is lowered.

## 8. References

- Moss Motors, "Car specifications - Mazda Miata 2001": https://mossmotors.com/miata-2001
- miata.net, "Differential guide" (updated 2010): https://miata.net/garage/diffguide
- Flyin' Miata, "Stock brake proportioning valves": https://www.flyinmiata.com/tech/stock_bpv.php
- cars.com 2001 MX-5 Miata LS specifications: https://www.cars.com/research/mazda-mx_5_miata-2001/specs/6655
- x-engineer.org, "Mechanical efficiency and FMEP" (Chen-Flynn model): https://x-engineer.org/?p=2864
- J. B. Heywood, *Internal Combustion Engine Fundamentals*, McGraw-Hill (book; friction and pumping chapters).
- R. Limpert, *Brake Design and Safety*, SAE R-198 (book; ideal brake distribution).
- T. D. Gillespie, *Fundamentals of Vehicle Dynamics*, SAE R-114 (braking chapter).
- Car and Driver instrumented tests (cited in each car file's `sources.calibration`).
- Mazda MX-5 NB factory workshop manual (paid; owner copy if available). Use for alignment, valve and torque specs.

Licence notes: magazine figures are facts and may be cited. Do not copy text or images. The workshop manual is copyrighted: quote numbers only.

## What I now know (onboarding, 2026-11-14)

**How my area works today.**
- `CarDefinitionLoader.cpp:537-700` reads every car field from `data/cars/<id>.json`. Units are in the names: `coil_lb_in` (converted × 175.126835 at `SimCruiseSuspensionModel.cpp:11`), `rim_in`, `parking_rack_deg`, `target_70_0_ft`. There is an overlay at `Content/Vehicle/Calibration/<id>.json` (`:488-501`).
- Engine (`SimCruiseVehicleSim.cpp:750-762`): torque = (curve + friction) × load, so at full throttle the net torque is the published curve. Drag = friction + pumping × (1 − load). Player car: friction 12 + 0.005·rpm and pumping 8 + 0.001·rpm, which gives 38 N·m at 3000 rpm. The C8 and the Mustang have no friction fields, so they use the defaults in `CarDefinition.h:173-177`: 10 + 0.002·rpm and zero pumping, which gives 16 N·m at 3000 rpm.
- Power checks (hand calculation): NB 144 N·m × 7000 rpm = 141.6 hp against 142 hp (Mazda US, secondary). C8 546 N·m × 6450 rpm = 494.6 hp against 495 hp (Chevrolet, verified). Mustang 340 N·m × 4400 rpm = 210 hp. This is the 1965 SAE gross rating (Car Life 1965, secondary), and the file uses it as net power.
- Manual clutch (`SimCruiseVehicleSim.cpp:775-797`): when the pedal is up, a lock coupling carries the substep's engine torque. NB capacity is 210 N·m against a 169 N·m peak, a ratio of 1.24 (estimate).
- Brakes: `RearBrakePedal` (`SimCruiseSuspensionModel.cpp:87-93`) is a proportioning valve with a knee and a slope. NB: knee 0.30, slope 0.5, 1600/700 N·m (estimates), and the player car has `abs_enabled` true (owner, 13 Nov). The C8 and the Mustang have no valve (defaults 1/1). The Mustang split is 0.62 front against an ideal of 0.68 at 0.62 g, so the rears lock first. The file labels this as period-true (Car Life: "rear lock-up").
- Diff (`SimCruisePowertrainModel.cpp:12-34`): Torsen-style transfer limit = |T/2|·(TBR−1)/(TBR+1), with separate drive and coast biases. NB is 2.0 drive and 1.5 coast (the 2001 Torsen II is about 2.5, miata.net, secondary). The C8 eLSD is a fixed 3.0 stand-in. The Mustang is open (1.0).
- Tuning (`SimCruiseTuning.cpp:44-54`): `TorquePct` can go from 50 to 150 % and the clutch capacity rises with it. Steering ratio can go from 0.6 to 1.4 × stock.
- Steering: the physics uses `parking_rack_deg` (NB 36.6, C8 34, Mustang 33). The cockpit wheel uses `steering_ratio` from `Content/Vehicles/CarPresentation.json` (`SimCruiseCarPresentationComponent.cpp:670`). Turns lock to lock = 2·lock·ratio/360: NB 3.05 against 2.6 (GoAuto), C8 2.97 against 2.5, Mustang 3.98 against 3.73 (Car Life).
- `VehicleCardTests.cpp:50-101` checks the turning circle (±4 %), the mass, the front share and the presentation ratio only. It does not check turns lock to lock. It runs `nb2001`, `mustang65` and `meridian_z`, not the player car `nb2001_aaron`.
- Air (`SimCruiseVehicle.cpp:446-456`): ISA pressure at `ElevationM` + actor Z, the live temperature (`SimCruiseLiveData.cpp:384`) and the live wind. This density goes only to aero drag (`SimCruiseVehicleSim.cpp:965`). The engine torque has no density term.
- Grip under the tyre: `SimCruiseWorldTile.cpp:96-121` gives dry 1.0 and wet loss 0.3, and gravel or earth from the tile surface map (Wong table 1.3). There is no snow or ice and no tyre temperature.
- Fuel and coolant are fixed values: `SimCruiseCarPresentationComponent.cpp:374-376` shows coolant 0.5 and fuel 0.72. No fuel, cooling or brake-temperature model exists.

**Does it work at a random US place?**
- General: the car data does not depend on the place. Grade comes from 3DEP terrain through the contact rays, so long grades exist everywhere. The surface grip comes from tile data. The air density uses absolute height, because world Z is NAVD88 metres (`unreal-architecture.md:196`) and the rebase does not move Z (`SimCruiseGeo.cpp:84`).
- Rawlins WY (about 2070 m): density is about 1.00 kg/m³ at 0 °C, so drag falls 18 %. A real naturally aspirated engine loses about 19 % power there (SAE J1349 correction, hand calculation, estimate). The game gives full sea-level power. All three hero cars are naturally aspirated, so they climb and accelerate too well. Delta UT (about 1400 m) is about −13 % (estimate). Colorado passes at 3400 m are about −30 % (estimate).
- Welch WV, Hazard KY, Jasper AR (long steep downgrades): the V8 cars have about 1/4 to 1/6 of real engine braking (estimate: a 4.7 or 6.2 L engine at 2 bar FMEP is 75-99 N·m against the 16 N·m default), and there is no brake fade. The Mustang's drums never fade on a descent. Real 1965 drums do.
- Casselton ND, Anchorage AK, Rawlins WY in winter: live temperature changes only the drag. There is no cold-air power gain, no snow or ice surface and no cold tyre. Gilbert AZ heat: there is no power loss from hot air and no overheating (estimate).
- Hilo HI, Bronx NY, Seattle WA: no car-side dependence on map quality. Seattle grades rely on 3DEP only (estimate: good).

**Main gaps against "realistic everywhere".**
1. P1: the engine ignores air density and temperature. `SimCruiseVehicleSim.cpp:755` needs a J1349-type factor from the density already computed at `SimCruiseVehicle.cpp:453`. A turbo car needs a separate rule.
2. P1: C8 and Mustang engine braking is far too low (defaults `CarDefinition.h:173-177`). It matters on every US mountain descent. Add friction and pumping fields to `meridian_z.json` and `mustang65.json` from FMEP for the displacement, labelled as estimates.
3. P1: Mustang power is the 1965 SAE gross rating used as net. `mustang65.json` `torque_curve` is about 15-20 % strong (estimate) on grades and at top speed. Label it, find a net or dyno source, and do not rescale it to hit the 0-60.
4. P1: there is no brake temperature or fade model (`SimCruiseVehicleSim.cpp` brake path). This matters on long grades for the drum-braked Mustang. Next phase, with the owner's consent.
5. P2: steering lock and ratio conflict. NB at 15:1 and 2.6 turns needs a 31.2° lock. That lock gives a 10.2 m circle with the test formula (hand calculation), against 9.2 m. Both numbers cannot hold with a fixed ratio. Resolve on Garage Day. `nb2001_aaron.json` `parking_rack_deg`, `VehicleCardTests.cpp`.
6. P2: the C8 has one tyre size (305/30R20) on both axles, and the real Z51 front is 245/35ZR19. There is no snow or ice grip class (`SimCruiseWorldTile.cpp:96`). No fuel or cooling model exists (gauges are fixed).

**My first three actions.**
1. Altitude and temperature power factor: write a proposal for `SimCruiseVehicleSim.cpp:755` and `SimCruiseVehicle.cpp:453`. Use SAE J1349 for naturally aspirated cars. Add a new car field for turbo cars. Done when a bench at a 2070 m density gives 0.80-0.82 × full-throttle torque on `nb2001_aaron`, the sea-level CarTrack bench is unchanged, and `scripts\preflight.ps1 -Roads us-rawlins-wy,us-delta-ut -Cars nb2001_aaron,mustang65` logs density and power factor in the report.
2. V8 engine braking: write friction and pumping estimates with sources (Heywood ch. 13; Chen-Flynn) for `meridian_z.json` and `mustang65.json`. Done when the 3rd-gear coast-down torque at 3000 rpm is within the FMEP estimate band, LiftOff and CarTrack stay in band, and `scripts\preflight.ps1 -Roads random -Cars meridian_z,mustang65` on 3 random places (including us-welch-wv) shows no runaway descents in `report.md`.
3. Turns-lock-to-lock card check: add a `turns_lock_to_lock` card field and a check in `VehicleCardTests.cpp`, and add `nb2001_aaron` to the card tests. Done when the test reports turns and circle for all three cars against GoAuto, Chevrolet and Car Life, and the NB conflict is listed as an open item for the Garage Day measurement.

**Docs that are wrong or old.**
- `docs/cars.md:3,94,139` says the physics reads through `godot-final:game/CarSpec.gd`, `Powertrain.gd` and `CarSuspension.gd`. The current loader is `CarDefinitionLoader.cpp`. `cars.md:145` says only the Corvette has ABS, but `nb2001_aaron.json` has `abs_enabled` true.
- `docs/team/automotive-engineer.md` §6a is old. It lists brakes 1150/650, no proportioning, friction 10 + 0.002·rpm and ABS false. The file now has 1600/700, knee 0.30, slope 0.5, friction 12 + 0.005·rpm with pumping, and ABS true.
- `docs/team/research/steering-ratio.md:25` says 36.6° gives about 8.1 m. The card-test geometry (`VehicleCardTests.cpp:50-56`) gives about 8.98 m by hand calculation (estimate), which is inside the 4 % band against 9.2 m.
- `docs/team/ownership.md:55` points to `docs/team/test-places.md`. The test places are in `data/test-places.json`.

Key files: /home/claude/simcruise/Source/SimCruise/Private/Vehicle/SimCruiseVehicleSim.cpp, /home/claude/simcruise/Source/SimCruise/Private/Vehicle/SimCruiseVehicle.cpp, /home/claude/simcruise/Source/SimCruise/Public/Vehicle/CarDefinition.h, /home/claude/simcruise/data/cars/mustang65.json, /home/claude/simcruise/data/cars/meridian_z.json, /home/claude/simcruise/data/cars/nb2001_aaron.json, /home/claude/simcruise/Source/SimCruise/Private/Tests/VehicleCardTests.cpp. Nothing was run. All numbers marked (estimate) are hand calculations.
