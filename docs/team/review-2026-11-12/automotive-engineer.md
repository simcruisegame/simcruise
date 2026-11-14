# Review 2026-11-12: automotive engineer

Scope: `data/cars/nb2001_lowered.json` (player car), `nb2001.json`, `meridian_z.json` (Corvette C8 Z51), `mustang65.json`, and the systems in `Source/SimCruise/Private/Vehicle/*` that use them.
Method: code and data reading, hand calculation (script: steady braking with static load transfer and the TMeasy peak force at load), and web sources. No Unreal test was run.

## What is good

- Gear ratios, final drive, engine peak torque and power of the 2001 NB match published data (power check: 144 N·m at 7000 rpm = 141.6 hp against 142 hp).
- C8 gearing (TR-9080, 5.17 final) and peak torque (637 N·m = 470 lb-ft) match published data.
- Every estimate in the car files is labelled in `sources`. The calibration method in `docs/cars.md` is sound.
- Spring wheel rate uses the motion ratio squared; damper dyno uses the motion ratio on speed and on force (`SimCruiseSuspensionModel.cpp:35`, `:58`, `:72`).

## Ranked findings

| # | Sev | Title | Evidence | Proposed fix | How to verify |
|---|---|---|---|---|---|
| 1 | P1 | Miata brake balance is rear-heavy and there is no proportioning valve or EBD; without ABS the rear axle locks first in hard braking | `SimCruiseVehicleSim.cpp:812` (brake torque = pedal × fixed per-axle torque); `nb2001_lowered.json` `brake_front_nm` 1150 / `brake_rear_nm` 650 (64 % front); no `abs_enabled` (default false, `CarDefinition.h:128`). Computed: ideal front share at 1 g is 0.69-0.74; rear reaches peak at 80 % pedal / 0.94 g (stock) and 91 % / 1.05 g (player car); fronts reach peak only at about full pedal (stock) or never (player car, mu 1.25). This contradicts the `nb2001` source note "at full pedal the front brakes lock". Real 2001 NB: non-ABS cars have a proportioning valve (2002: knee 569, slope about 0.47, Flyin' Miata); ABS cars have EBD | Add a proportioning model to the brake path: rear torque = linear to a knee pedal fraction, then slope × the excess (car-file fields `brake_rear_knee`, `brake_rear_slope`); for ABS cars, EBD. Then re-check 70-0. Ask the owner whether his car has ABS | The BrakeInTurn bench (physicist #1); the 70-0 BRAKETRACE log shows the front wheels reach peak slip first; 70-0 stays within ±5 ft |
| 2 | P1 | Engine braking is about half of a real 1.8 L engine | `SimCruiseVehicleSim.cpp:712-714`; `nb2001_lowered.json` `engine_friction_nm` 10, `engine_friction_per_rpm` 0.002 → 16 N·m at 3000 rpm, 22 N·m at 6000 rpm. Estimate (FMEP Chen-Flynn example plus closed-throttle PMEP about 0.8 bar): 2-3 bar → 29-44 N·m for 1.84 L (unverified for this engine) | Raise friction and add a pumping term at closed throttle, from an owner coast-down log; keep full-throttle net torque unchanged | Coast-down in 3rd gear from 4000 rpm: deceleration matches the owner's phone log within 10 %; LiftOff bench still passes |
| 3 | P2 | Rear diff bias 2.0 is the 2003-05 Tochigi Fuji value; the 2001 Torsen II is about 2.5; no coast-side locking | `nb2001_lowered.json` `rear_diff_bias` 2.0; `SimCruisePowertrainModel.cpp:22` (open when not drive torque); miata.net diff guide | Set 2.5 for 2001; add a coast bias field (Torsen biases in coast too) | Lift-off in a corner with one rear wheel light: inside wheel speed difference falls; LiftOff bench logs |
| 4 | P2 | Steering lock and ratio disagree with the card | `nb2001_lowered.json` `parking_rack_deg` 36.6, card `steering_ratio` 15.0 and "2.6 turns"; 36.6° × 15 = 549° per side = 3.05 turns; the cockpit wheel turns by ratio × road angle (`SimCruiseCarPresentationComponent.cpp:397`) | Measure lock turns and road-wheel lock on the owner's car; set ratio or lock to match | Cockpit wheel at full lock shows the measured turns; turning circle 9.2 m on a bench |
| 5 | P2 | Lowered player car keeps stock bump travel and camber | `nb2001_lowered.json` `bump_gap_m` 0.054, `static_length_m` 0.217, `static_camber_deg` unchanged; body lowered 35 mm | Measure gap to bump stop and alignment; update the file | StaticLoad bench at the new ride height; bump-stop contacts on the Tail of the Dragon drive log |
| 6 | P2 | Manual clutch slips 161 rpm at peak torque, it never locks | `SimCruiseVehicleSim.cpp:737-739` (see physicist #3) | Stick/slip clutch | Tachometer equals wheel-implied rpm in steady drive |
| 7 | P2 | Corvette has one tyre size for both axles (305/30R20); the Z51 is staggered 245/35ZR19 front | `meridian_z.json` `tire_width_m` 0.305, `tire_aspect_ratio` 0.3, `rim_in` 20 | Per-axle tyre size and radius | Front wheel speed against rear at cruise; skidpad and 70-0 in band |
| 8 | P2 | Corvette runs the legacy tyre law; its aligning torque uses TMeasy default trail | `meridian_z.json` (no `tyre_model`); `SimCruiseVehicleSim.cpp:884` | Move the C8 to TMeasy with its own data | Force-feedback trace: rack torque peak before grip peak |
| 9 | P2 | Corvette Z51 aero and eLSD not modelled | `SimCruiseVehicleSim.cpp:899-906` (drag only); `meridian_z.json` `rear_diff_bias` 3.0 for an electronic clutch LSD | Lift coefficients per axle (source needed); eLSD as a slip-controlled clutch | High-speed stability; corner exit with one wheel light |
| 10 | P2 | Small data conflicts to resolve | Moss Motors lists 2,200 lb and torque "116" for 2001; the file uses 2,387 lb and 125 lb-ft; stock LS tyre size differs between sources | Owner corner weights; keep 125 lb-ft (Mazda US) | Note in `sources` |

## Next phase proposals

Ranked. None of this work starts before the owner agrees.

### 1. Brake system model (hydraulics, valve or EBD, ABS per car)
- Goal: pedal → line pressure → per-wheel torque with a proportioning valve (Miata non-ABS, Mustang), EBD and ABS where the real car has them, and pad friction against temperature.
- Why: braking is half of driving feel. The real NB brakes front-first and never spins under a straight hard stop.
- Scope: brake path in `SimCruiseVehicleSim.cpp:808-833`, new car-file fields, the brake-in-turn bench.
- Acceptance: front-first lock on the non-ABS Miata at all decelerations; 70-0 within ±5 ft for all three cars; brake-in-turn yaw ratio under 1.5 at full pedal.
- Risk: knee and slope data for the 2001 valve are not certain; owner car ABS status unknown.
- Size: M.

### 2. Garage Day: measure the owner's car
- Goal: replace estimates in `nb2001_lowered.json` with measurements: corner weights, alignment, ride height and bump gap, lock turns and lock angle, spring labels, 60-0 stop, coast-downs, diff type.
- Why: the vision is "behaves exactly like the real car". This is the cheapest way to close most open items.
- Scope: an owner checklist (`docs/team/automotive-engineer.md` §6b), a phone-logger import, data-file updates.
- Acceptance: every field in the audit table is "measured" or "sourced"; benches re-run in band.
- Risk: owner time; safe and legal test place for the stop.
- Size: S (team), owner effort M.

### 3. Engine and driveline realism
- Goal: friction plus pumping map, stick/slip clutch, driveline compliance (shuffle 1-5 Hz).
- Why: lift-off feel, rev-matching and the clunk of a light car on throttle changes.
- Scope: engine and clutch block in `SimCruiseVehicleSim.cpp:699-798`; car-file fields.
- Acceptance: coast-down matches the owner log within 10 %; shuffle frequency within 10 % of a Garage Day log.
- Risk: stiff compliance at 360 Hz needs an implicit step.
- Size: M.

### 4. Differential models per real part
- Goal: Torsen drive and coast bias (Miata 2001), clutch eLSD approximation (C8), open or Equa-Lock (Mustang).
- Why: corner exit and lift-off behaviour of a rear-drive car depend on the diff.
- Scope: `SimCruisePowertrainModel.cpp`, car-file fields.
- Acceptance: inside-wheel spin on corner exit matches video or log; benches in band.
- Risk: C8 eLSD control strategy is not public.
- Size: S-M.

### 5. Hero-car tyre data
- Goal: per-axle tyre sizes and TMeasy data for all three cars (C8 staggered radials, Mustang bias-ply).
- Why: one tyre model gives consistent feel; correct radii fix speedometer and gearing.
- Scope: car files, per-axle radius support in the sim.
- Acceptance: all benches in band; wheel speeds match at cruise.
- Risk: recalibration effort.
- Size: M.

## Sources

- Flyin' Miata, stock brake proportioning valves: https://www.flyinmiata.com/tech/stock_bpv.php
- miata.net differential guide: https://miata.net/garage/diffguide
- Moss Motors 2001 Miata specifications: https://mossmotors.com/miata-2001
- x-engineer.org FMEP (Chen-Flynn): https://x-engineer.org/?p=2864
