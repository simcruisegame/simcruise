# Review 2026-11-12: vehicle dynamics physicist

Scope: `Source/SimCruise/Private/Vehicle/*`, `data/cars/nb2001_lowered.json` (player car), `meridian_z.json`, `mustang65.json`, `Source/SimCruise/Private/Tests/VehicleBench*`.
Method: code reading and hand calculation. No Unreal test was run in this review (no editor in this environment). Each claim carries a label: read in code, computed, or unverified.

## What is good

- Relaxation length is integrated exactly (exponential) and the interval mean is used (`SimCruiseTireModel.cpp:53-59`). Stable at any step.
- Wheel spin and tyre force are solved implicitly (`SimCruiseTireModel.cpp:243-244`, `:299-300`). Locked and spinning wheels stay stable.
- Signs of lateral force, camber thrust, caster camber and anti-roll load are correct (read in code: `SimCruiseVehicleSim.cpp:519-533`, `:564-572`; `SimCruiseTireModel.cpp:86-92`).
- Unit boundary is in one file with the correct 100² for torque and inertia (`SimCruiseUnits.h`).
- Automatic clutch coupling is capped for explicit stability (`SimCruiseVehicleSim.cpp:725-728`).
- Determinism, convergence and static-load benches exist.

## Ranked findings

| # | Sev | Title | Evidence | Proposed fix | How to verify |
|---|---|---|---|---|---|
| 1 | P1 | No bench tests hard braking in a turn; straight-line 70-0 hides yaw instability | `VehicleBenches.cpp:177-191` (70-0 with zero steer); trail-brake case only 0.15 g (`VehicleBenches.cpp:271`, `:326`); computed: the Miata rear axle reaches peak first above about 0.94 g (stock) / 1.05 g (player car), see automotive review #1 | Add an ISO 7975 case: 0.5 g steady on a 100 m circle, full pedal in 0.2 s, wheel frozen. Gate: yaw-rate ratio at 1 s and peak sideslip | New `SimCruise.Vehicle.BrakeInTurn.<car>` test; it must fail on today's Miata data and pass after the brake-balance fix |
| 2 | P1 | Manual clutch coupling has no stability cap; explicit limit exceeded in 1st and reverse when the wheels are light | `SimCruiseVehicleSim.cpp:736-739` (k = 10 N·m·s/rad, uncapped) against the capped automatic path `:725-728`; computed k·Δt·(1/I_e + G²η/(2 I_w)) = 2.3 (1st), 3.3 (reverse), 0.94 (2nd) at 360 Hz | Apply the same cap as the automatic path (`min(k, 1/(Δt·(1/I_e + reflected)))`), or solve engine and wheel side implicitly | Bench: 1st gear, rear wheels off the ground, full throttle, clutch up; clutch torque must not alternate sign each step. Also over a crest at 1st-gear limit |
| 3 | P1 | Manual clutch never locks: permanent viscous slip under load | `SimCruiseVehicleSim.cpp:737-739`; computed 169 N·m / 10 = 16.9 rad/s = 161 rpm slip at peak torque | Add a stick state: when slip speed is near zero and the needed torque is below capacity, lock engine to gearbox (implicit). Keep slip only above capacity | Telemetry: at steady 3rd gear full load, engine rpm equals wheel-implied rpm within 5 rpm |
| 4 | P1 | Roll centre at ground and no anti-dive or anti-squat geometry | `SimCruiseVehicleSim.cpp:892-897` (all forces at the contact point) | Next phase: per-axle roll-centre height and anti-dive/anti-squat fractions in the car file; apply the jacking component through the suspension | Steady 0.5 g roll angle and pitch per g under braking against an owner measurement (phone inclinometer) |
| 5 | P2 | Massless wheel; road profile rate goes straight into the damper | `SimCruiseVehicleSim.cpp:514-515`; `SimCruiseSuspensionModel.cpp:108-153`; `unsprung_corner_kg` only scales damping (`:139`) | Next phase: 2-DOF corner (unsprung mass and tyre vertical stiffness), see proposal 1 | Bump bench: wheel-load spectrum shows a 10-15 Hz wheel-hop mode |
| 6 | P2 | Understeer-gradient gate is too wide to catch a wrong car | `VehicleBenchTests.cpp:142` (0 < K < 8 deg/g for every car) | Per-car target band in the car file (for example Miata 1-3 deg/g, unverified; measure) | The `LiftOff` test logs K and fails outside the car's band |
| 7 | P2 | No transient-handling test (ISO 7401 step steer) | No step-steer case in `VehicleBenches.cpp` | Add step steer at 80 km/h, 0.4 g; report t90, peak time, overshoot | Logged values stable across 360 Hz and the finer rate (convergence) |
| 8 | P2 | Relaxation length does not shrink near saturation | `SimCruiseTireModel.cpp:43-47` (σ depends on load and speed only) | Scale σ with the local slope of the force curve (Pacejka ch. 7) | Step steer at the limit: response time should fall slightly; compare before and after |
| 9 | P2 | TMeasy slip differs from Rill: road-speed denominator, different floors (1.5 m/s x, 1.8 m/s y), no ŝ normalisation | `SimCruiseTireModel.cpp:39`, `:232-237`, `:201` | Keep, but document; use one floor for both directions; add a unit test against Chrono's TMeasy for a slip grid | Unit test: forces within 3 % of Chrono at 9 slip points |
| 10 | P2 | Legacy-law car (Corvette) gets TMeasy default pneumatic trail | `SimCruiseVehicleSim.cpp:884` calls `TmeasyPneumaticTrailM` for every car; `meridian_z.json` has no `tyre_model` | Move the C8 to TMeasy with its own data, or give the legacy law its own trail | Rack torque against slip angle peaks before lateral force peaks on the C8 |

Computed numbers used above (player car): tyre radius 0.293 m; wheel rates 31.7 / 29.1 N/mm; ride 1.77 / 1.75 Hz; h/L 0.177; file brake share 0.64 against ideal 0.69 at 1 g.

## Next phase proposals

Ranked. None of this work starts before the owner agrees.

### 1. Unsprung mass and tyre vertical compliance (road feel)
- Goal: each corner gets a wheel mass and a tyre spring and damper. The road reaches the body through two stages, like a real car.
- Why: "feel the road" is in the vision. Kerbs, potholes and rough asphalt then give the correct jolt, wheel hop and grip loss. Force feedback gets the right texture.
- Scope: `SimCruiseVehicleSim.cpp` wheel loop and `SimCruiseSuspensionModel.cpp`; car-file fields `unsprung_corner_kg` (exists) and `tyre_vertical_stiffness` (exists for patch length). Implicit or semi-implicit wheel integration at 360 Hz.
- Acceptance: wheel-hop mode 10-15 Hz on a bump bench; 0-60, 70-0 and skidpad stay inside bands; convergence and determinism green.
- Risk: stiff tyre spring (about 200 kN/m) at 360 Hz needs an implicit step; CPU cost per wheel.
- Size: M.

### 2. Handling test suite with real targets
- Goal: ISO 4138 per-car understeer band, ISO 7401 step steer, ISO 7975 braking in a turn, ISO 3888-1 lane change, in the Unreal bench.
- Why: today only straight-line and gentle lift-off cases exist. Yaw stability and transient feel have no gate.
- Scope: `VehicleBenches.cpp`, `VehicleBenchTests.cpp`, target fields in the car files, `docs/cars.md`.
- Acceptance: each hero car has logged K, t90, overshoot, brake-in-turn yaw ratio; gates fail on known bad data.
- Risk: targets for the NB are partly unknown; owner measurements close them.
- Size: S-M.

### 3. Suspension kinematics: roll centre and anti-geometry
- Goal: per-axle roll-centre height and anti-dive/anti-squat, applied as jacking forces.
- Why: correct split of geometric and elastic load transfer gives the right roll, dive and turn-in delay.
- Scope: car-file fields, force application in `SimCruiseVehicleSim.cpp`.
- Acceptance: roll per g and pitch per g within 15 % of owner measurement; lift-off and skidpad bands hold.
- Risk: NB geometry data are not published; needs measurement or a kinematic model.
- Size: M.

### 4. Tyre model conformity and one model for all hero cars
- Goal: one TMeasy implementation checked against Chrono; slip-dependent relaxation; C8 on TMeasy.
- Why: the same physics for all three cars; correct aligning torque for force feedback.
- Scope: `SimCruiseTireModel.cpp`, unit tests, `meridian_z.json`.
- Acceptance: unit test within 3 % of Chrono on a slip grid; C8 benches in band.
- Risk: recalibration of the C8.
- Size: S.

### 5. Aero lift and crosswind yaw moment
- Goal: front and rear lift coefficients and a crosswind yaw moment.
- Why: high-speed stability of the C8; open-top Miata buffeting feel.
- Scope: aero block in `SimCruiseVehicleSim.cpp:899-906`, car-file fields.
- Acceptance: C8 axle loads at 200 km/h match published downforce (if found).
- Risk: few published numbers.
- Size: S.
