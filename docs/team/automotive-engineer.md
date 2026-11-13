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
