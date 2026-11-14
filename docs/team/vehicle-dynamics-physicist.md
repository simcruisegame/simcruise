# Vehicle dynamics physicist: study notes

Role: check that SimCruise cars obey tyre and vehicle physics. Scope: tyre models, load transfer, handling metrics, standard test methods, integration stability.
Style: ASD-STE100 Simplified Technical English. "Unverified" marks a fact that this study could not confirm from a primary source.

## 1. Sign and axis conventions (read this first)

- SimCruise model axes: Unreal frame, X forward, Y right, Z up, left-handed. Units are SI inside the model (`Source/SimCruise/Public/Vehicle/SimCruiseUnits.h`).
- Chaos receives forces in kg·cm/s² and torques in kg·cm²/s². Use only `SimCruiseUnits::ToUnrealForce` and `ToUnrealTorque`. A torque converts with 100², not 100.
- Car files store vectors and inertia in Godot axes (X right, Y up, -Z forward). Inertia order in the file is pitch, yaw, roll. `GodotInertiaToUnrealAxes` reorders it.
- Slip angle in code: `atan2(LateralSpeed, max(1.8, |ForwardSpeed|))`, positive when the contact slides to the wheel's right. The lateral force is negative for a positive slip angle. The force always opposes the sliding.
- Wheel inclination in the tyre law: positive when the wheel top leans to the wheel's right. Negative camber on the left wheel is a positive inclination.
- Visual steer angle has the opposite sign to the sim steer angle. `SimCruiseVehicle.cpp:545` negates it. Do not "fix" this.
- SAE J670 and ISO 8855 use different axis signs (SAE Z down). Always state the frame when you quote a sign.

## 2. Tyre models

### 2.1 TMeasy (Rill, Hirschberg)
- The Miata files and the 1965 Mustang use `"tyre_model": "tmeasy"`. The Corvette (`meridian_z.json`) uses the legacy law.
- TMeasy describes each direction with five numbers at nominal load Fz,N and at 2·Fz,N: initial stiffness dF0, peak force FM at slip sM, sliding force FS from slip sS.
- Rill's slip (Hirschberg, Rill, Weinfurter 2002, eq. 9-10): sx = (vx − rD·Ω)/(rD·|Ω|), sy = vy/(rD·|Ω|). Later versions add a small speed vN to the denominator.
- Rill combines slip with normalising factors: s = sqrt((sx/ŝx)² + (sy/ŝy)²). The force acts along the normalised slip direction.
- SimCruise uses the road speed as the denominator (floor 1.5 m/s for x, 1.8 m/s for y), and uses no ŝ normalisation (Chrono style). This is acceptable when sM,x ≈ sM,y (0.139 against 0.149 here). Record it as a known deviation.
- Load interpolation: forces are quadratic in Fz through 0, Fz,N and 2·Fz,N. Clamp Fz/Fz,N at 2.5, or the quadratic turns over (`SimCruiseTireModel.cpp:105`).
- Rule: the curve must reach its peak. Initial slope ≥ 2·FM/sM (`SimCruiseTireModel.cpp:132`).
- Typical passenger-car numbers (Chrono "GuessPassCar70" set, BSD-3): dF0,y ≈ 16·Fz per unit slip, sM,y ≈ 0.15, FM,y ≈ 1.0·Fz, FS/FM ≈ 0.83.

### 2.2 Magic Formula (Pacejka)
- Fy = D·sin(C·atan(B·α − E·(B·α − atan(B·α)))). D = μ·Fz. BCD = cornering stiffness.
- Cornering stiffness of a road tyre: about 12-25 per rad times Fz (unverified range; check against tyre data when you have it).
- Peak slip angle: about 4-8° for street radials, 3-5° for race slicks (Milliken RCVD ch. 2; values approximate).
- Use MF numbers only to cross-check TMeasy curves. Do not import MF coefficients from a commercial tyre file without a licence.

### 2.3 Brush model
- Use it to reason about pneumatic trail. Trail at zero slip is about 1/6 of the contact length (brush theory), and it falls to zero as the patch slides.
- Aligning torque peaks at about 2-4° slip and falls to near zero at peak grip. The steering goes light before the front tyres let go. This is the most important force-feedback cue.
- SimCruise computes trail with TMeasy's lever (`TmeasyPneumaticTrailM`) for every car, also for legacy-law cars.

### 2.4 Relaxation length
- A tyre builds side force over a travel distance σ (relaxation length). First-order law: dα/ds = (α_target − α)/σ.
- SimCruise integrates this law exactly over the step (exponential) and returns the interval mean (`SimCruiseTireModel.cpp:30-60`). This is unconditionally stable. Keep it.
- σ for passenger tyres is about 0.3-0.7 m at nominal load (Pacejka ch. 7; range unverified). σ grows with load (exponent 0.5 in code).
- Pacejka: σ falls as the tyre nears saturation (σ ≈ C_Fα-dependent). SimCruise keeps σ constant over slip. At the limit the car responds a little slowly.
- At walking pace the code blends σ to zero (0.8-3.5 m/s). Check that parking and creeping do not jitter.

### 2.5 Combined slip
- A tyre has one friction budget. Braking or drive force reduces side force.
- Check: at a fixed slip angle, side force must fall as slip ratio rises. At a locked wheel the force must point opposite the contact sliding velocity.
- SimCruise TMeasy path: Fx and Fy are both secant·slip on one combined slip, so the force is collinear with the slip vector (Rill eq. 17).

### 2.6 Camber thrust
- Camber stiffness of a radial car tyre is small compared with cornering stiffness (Pacejka ch. 4; ratio unverified). File value `camber_thrust` 0.48·Fz per rad gives 0.008·Fz per degree.
- Caster adds camber as the wheel steers: outer wheel gains negative camber. Code line `SimCruiseVehicleSim.cpp:532` does this with the correct sign.

## 3. Load transfer and chassis

- Steady lateral load transfer per axle: ΔFz = (m·ay/t)·[ (h − hRC)·(K_axle/K_total) + hRC_axle·(m_axle/m) ] (Milliken RCVD ch. 18; Gillespie ch. 6).
- SimCruise applies all tyre forces at the contact point on the ground. The roll centre is therefore at ground level and all transfer is elastic (springs and bars). Real double-wishbone cars have roll centres above the ground (NB values unverified). Consequence: more roll and a slower load-transfer build-up in a transient.
- Longitudinal transfer: ΔFz = m·ax·h/L. No anti-dive or anti-squat geometry exists in the model. Pitch under braking is therefore larger than real.
- Anti-roll bars: wheel rate from torsion plus bending of the arms, times 0.82 for bushes (`SimCruiseSuspensionModel.cpp:75-106`).
- Wheel rate = coil rate × motion ratio² (`SimCruiseSuspensionModel.cpp:72`). Ride frequency f = (1/2π)·sqrt(k_wheel/m_sprung_corner). Street cars: 1.0-1.5 Hz. Stiff street coilovers: 1.6-2.0 Hz.
- Wheels are massless. `unsprung_corner_kg` only scales critical damping. No wheel-hop mode (about 10-15 Hz on real cars) exists. Road profile rate enters the damper directly.

## 4. Handling metrics (SAE J266, SAE J670, ISO 4138)

- Understeer gradient K: K = dδ/day − L/R-term. ISO 4138 constant-radius method: K = d(δ_road)/d(ay) on a fixed circle. Units deg/g.
- Typical road cars: about 2.6 deg/g (European), 4.4 deg/g (US) (Crolla course notes, quoted in `VehicleBenchTests.cpp:122-125`; secondary source). A sporty roadster is at the low end, about 1-3 deg/g (unverified).
- Yaw-rate gain: r/δ = (V/L)/(1 + K·V²/(L·g)) with K in rad/g.
- Characteristic speed (understeer): vch = sqrt(L·g/K) with K in rad/g.
- Sideslip gradient: dβ/day. A road car at 0.5 g: β about 0.5-1.5°.
- ISO 7401 step steer: measure yaw-rate response time (to 90 %), peak response time, overshoot. Road cars: response time about 0.15-0.30 s at 80 km/h (unverified range).
- ISO 3888-1 double lane change: pass or fail with cone gates. ISO 3888-2: obstacle avoidance.
- ISO 9816: power-off in a turn. ISO 7975: braking in a turn.

## 5. Integration and substeps

- Chaos runs at 360 Hz (`Config/DefaultEngine.ini:90`). The vehicle uses one wheel substep per physics step (`SimCruiseVehicleMovementComponent.cpp:286`).
- Explicit coupling stability rule: for a spring-like coupling k between inertias, k·Δt·(1/I1 + 1/I2) < 2. Aim below 1 for good damping.
- Wheel spin and contact force are solved implicitly (`SolveTmeasySubstep`). This is stable for a spinning or locked wheel. Keep it.
- The automatic clutch caps its coupling with the rule above (`SimCruiseVehicleSim.cpp:725-728`). The manual clutch does not (`SimCruiseVehicleSim.cpp:736-739`).
- Relaxation uses an exact exponential. Suspension spring and damper are explicit at 360 Hz. Check: bump stop 800 000 N/m² gives k ≈ 48 kN/m at 30 mm. This is safe.
- Determinism: `SimCruise.Vehicle.Determinism` must stay green after any change.
- Convergence: `SimCruise.Vehicle.Convergence.*` compares 360 Hz with a finer rate. A change must keep 0-60, 70-0 and skidpad inside one fifth of each band.

## 6. How to judge SimCruise

Files to read first:
- `Source/SimCruise/Private/Vehicle/SimCruiseTireModel.cpp` (tyre).
- `Source/SimCruise/Private/Vehicle/SimCruiseVehicleSim.cpp` (wheel loop, steering, brakes, clutch, forces).
- `Source/SimCruise/Private/Vehicle/SimCruiseSuspensionModel.cpp`, `SimCruisePowertrainModel.cpp`.
- `Source/SimCruise/Private/Tests/VehicleBenches.cpp`, `VehicleBenchTests.cpp`.
- `docs/cars.md` (bench method and targets).

Checks:
1. Sign check: put a positive lateral speed on one wheel. The lateral force must be negative.
2. Static check: `StaticLoad.<car>` carries m·g within 0.2 % at the file's front fraction.
3. Steady state: `LiftOff.<car>` gives K between the car's target band. The current band is 0-8 deg/g, which is too wide to catch a wrong car.
4. Transient: run a step steer (ISO 7401) at 80 km/h, 0.4 g. Record yaw response time and overshoot. No test exists yet.
5. Braking stability: brake at full pedal with 0.3 g of lateral acceleration. Yaw rate must not rise more than 20 % in 1 s. No test exists yet (the lift-off bench brakes only to 0.15 g).
6. Stability: for any explicit coupling, compute k·Δt·Σ(1/I). Flag any value above 1.
7. Combined slip: hold 4° slip angle and sweep slip ratio. Fy must fall monotonically past the peak.
8. Aligning torque: rack torque must peak before lateral force peaks.

Thresholds:
- Bench bands: 0-60 ± 0.15 s, 70-0 ± 5 ft, skidpad ± 0.02 g (`VehicleBenchTests.cpp:16-18`).
- Spin: sideslip past 8° in a lift-off case is a fail.

## 6a. Worked numbers for the player car (nb2001_aaron)

Use these as quick sanity values. They come from the car file and the formulas above.

| Quantity | Value | How |
|---|---|---|
| Tyre radius 205/50R15 | 0.293 m | 0.1905 + 0.205·0.5 |
| Nominal corner load | 2557 N | file `reference_corner_load_n` |
| Front wheel rate | 31.7 N/mm | 392 lb/in · 175.13 · 0.68² |
| Rear wheel rate | 29.1 N/mm | 280 lb/in · 175.13 · 0.77² |
| Front ride frequency | 1.77 Hz | sprung corner 256 kg |
| Rear ride frequency | 1.75 Hz | sprung corner 240 kg |
| Lateral cornering stiffness | 18.5·Fz per unit slip | file `tmeasy_stiffness_y` |
| Peak lateral slip | tan α ≈ 0.149 (8.5°) | default `TmeasyPeakSlipY` |
| Relaxation time at 30 m/s | 12 ms | 0.35 m / 30 m/s |
| Yaw radius of gyration | 1.10 m | sqrt(1308/1083) |
| Long. transfer per g | 0.177 of weight | h/L = 0.40/2.266 |
| Ideal front brake share at 1 g | 0.69 | 0.515 + 0.177 |
| File front brake share | 0.64 | 1150/(1150+650) |
| Manual clutch k·Δt·Σ(1/I), 1st gear, free wheels | 2.3 | 10·(1/360)·(1/0.18 + 13.48²·0.895/2.1) |
| Same, reverse | 3.3 | ratio 3.758·4.3 |
| Same, 2nd gear | 0.94 | ratio 1.888·4.3 |

Interpretation:
- Values above 2 in the clutch rows are outside the explicit stability limit. They matter only when the tyres do not hold the wheels (wheels light, in the air or spinning at speed).
- Ride frequencies of 1.75 Hz are normal for street coilovers.
- The 0.05 gap between ideal and file brake share is the brake-balance problem in the review.

## 6b. Test recipes (for a bench author)

- Step steer (ISO 7401): 80 km/h, ramp the rack in 0.15 s to the angle that gives 0.4 g steady. Record yaw rate r(t). Report t90 (time to 90 % of steady r), peak time, overshoot %, lateral-acceleration lag.
- Braking in a turn (ISO 7975): 100 m circle at 0.5 g steady. Freeze the wheel. Apply full pedal in 0.2 s. Report yaw-rate ratio at 1 s, peak sideslip, path deviation. A rear-first lock shows as yaw-rate ratio above 1.5 and sideslip growing.
- Understeer gradient (ISO 4138 constant radius): 40 m and 100 m radius, ay 0.1 to 0.6 g in 0.1 g steps, steady 3 s each. Fit δ against ay in the 0.1-0.4 g linear range.
- Wheel-hop check (needs unsprung mass): drive over a 20 mm step at 15 m/s. Report the dominant frequency of the wheel load.

## 7. Common failure modes

- Slip angle with |vx| floor too low: jitter at parking speed. Too high: dead steering at walking pace.
- Force applied at the wrong point (centre of mass instead of contact): no roll or pitch moment.
- Torque converted with 100 instead of 100²: car yaws 100 times too little.
- Inertia axis order swapped (Godot pitch/yaw/roll): car rotates wrongly in yaw.
- Explicit stiff coupling (clutch, diff lock, bump stop): limit-cycle chatter at 180 Hz. Shows as buzzing torque in telemetry and sound.
- Straight-line benches: they never test yaw stability. A rear-biased brake passes 70-0 and still spins the car in a corner.
- Tuning grip to hit one number: it hides a wrong model. Compare shape (curves), not only peaks.

## 8. References

- H. B. Pacejka, *Tire and Vehicle Dynamics*, 3rd ed., Elsevier/SAE R-418, 2012. https://shop.elsevier.com/books/tire-and-vehicle-dynamics/pacejka/978-0-08-097016-5 (book; buy or borrow; do not copy).
- W. Hirschberg, G. Rill, H. Weinfurter, "User-appropriate tyre-modelling for vehicle dynamics in standard and limit situations", *Vehicle System Dynamics* 38(2), 2002, 103-125. Author copy: https://graz.elsevierpure.com/files/1925147/tmeasy_prt.pdf
- Project Chrono, `ChTMeasyTire` reference (BSD-3 code): https://api.projectchrono.org/classchrono_1_1vehicle_1_1_ch_t_measy_tire.html
- W. F. Milliken, D. L. Milliken, *Race Car Vehicle Dynamics*, SAE R-146, 1995 (book).
- T. D. Gillespie, *Fundamentals of Vehicle Dynamics*, SAE R-114, 1992 (book).
- G. Rill, *Road Vehicle Dynamics: Fundamentals and Modeling*, CRC Press, 2011 (book).
- SAE J266 Steady-state directional control test procedures: https://saemobilus.sae.org/standards/j266_202504-steady-state-directional-control-test-procedures-passenger-cars-light-trucks (paywalled; cite clause numbers only).
- SAE J670 Vehicle dynamics terminology (paywalled; terms only).
- ISO 4138 Steady-state circular driving: https://iteh.es/catalog/standards/iso/d98b7944-45f2-477d-80f3-cba545998a68/iso-4138-2021
- ISO 7401 Lateral transient response: https://iteh.es/catalog/standards/iso/a1f9fdfd-8c82-4209-8d6a-f5dc0717bf00/iso-7401-2011
- ISO 3888-1 Double lane change; ISO 7975 Braking in a turn; ISO 9816 Power-off in a turn (ISO catalogue; paywalled).
- Self-aligning torque overview: https://en.wikipedia.org/wiki/Self_aligning_torque (secondary).

Licence notes: ISO and SAE documents are paid. Quote test names and metrics only. Chrono code is BSD-3; keep its notice if code is ported. Pacejka, Milliken and Gillespie are copyrighted books; use formulas, not text.

## What I now know (onboarding, 2026-11-14)

**How my area works today.**
- Contact: each wheel casts 3 rays, at the centre and at ±0.4 × section width (`Private/Vehicle/SimCruiseVehicleSim.cpp:468-474`). A ray hit counts only if normal·up > 0.33. The highest ray carries the wheel. The surface grip is the mean of the rays that hit (`:478-511`). A hit with normal·up < 0.25 gives no contact (`:518`). The corner load is capped at 10 × the static corner share (`:533-534`).
- Ground query: `LineTraceSingleByChannel` on the tile's complex collision. `ASimCruiseWorldTile::SurfaceGripAtFace` gives each face a dry grip and a wet loss (`SimCruiseVehicleMovementComponent.cpp:319-340`). Grip = DryGrip × (1 − WetLoss × wetness).
- There are only three surface kinds (`Private/World/SimCruiseWorldTile.cpp:96-141`, from Wong table 1.3): asphalt and concrete 1.0 dry, 0.70 wet; gravel 0.7 dry, 0.63 wet; terrain (verge, lawn, soil) 0.8 dry, 0.65 wet. Buildings, props and bridge actors get the asphalt default. Snow, ice, sand and mud do not exist.
- Gravel flag: the helper passes only `p.surface==='unpaved'|'paved'` (`helper/atlas.mjs:130`). Then `bUnpaved` (`SimCruiseRoadNetwork.cpp:497`) sets vertex colour R=1 (`SimCruiseTileBuilder.cpp:747`), and the tile reads R>0.5 as gravel (`SimCruiseWorldTile.cpp:225-234`). A road with no surface tag becomes asphalt.
- Wetness: `FSimCruiseWeather::StepRoadWetness` fills with a 180 s time constant in live rain and dries with 900 s. It uses `bRaining` only (`SimCruiseWeather.cpp:91-96`). The pawn copies it to the physics state every tick (`SimCruiseVehicle.cpp:445`).
- Tyre: all three cars use `"tyre_model": "tmeasy"` (nb2001_aaron.json:44, meridian_z.json:63, mustang65.json:61). `GetTmeasyCurves` clamps the load at 2.5 × Fz,N. The surface factor G multiplies the peak force, the slide force, the peak slip and the slide slip. It does not change the initial stiffness (`SimCruiseTireModel.cpp:105-120`).
- Slip floors: Fx law 1.5 m/s (`SimCruiseTireModel.cpp:235`), ABS slip 5 m/s (`:232`), slip angle 1.8 m/s (`:39`). Relaxation is 0.35 m (NB) × load^exp, faded to 0 between 0.8 and 3.5 m/s, and integrated exactly (`:43-59`). A locked wheel's force is `Ke·(−V)` (`:261`).
- Load transfer: anti-roll bars (`SimCruiseVehicleSim.cpp:584-591`) and roll-centre geometric transfer from the last substep's side forces (`:597-611`). Camber thrust uses the inclination minus the road lean (`:662, 673`).
- Road geometry under the tyre: `helper/road-grade.mjs` fits a bench (Gaussian, `SIGMA=16` m, `:75`) only for classes motorway…minor (`:16`) that are not loose (`:114`). Superelevation is AASHTO-style and at most 6 %. The crown is a constant 2 % (`Public/World/SimCruiseRoadNetwork.h:156`) and does not come from data. Unpaved, `service` and `track` roads lie on the terrain grid (about 2.2 m, `SimCruiseTileBuilder.cpp:18`).
- `RoadProfileM` and `RoadProfileRateMps` (`SimCruiseVehicleSim.h:73`) are read in the sim, but no game code sets them. The road has no roughness except the mesh.
- Tests: no test in `VehicleBenches.cpp`, `VehicleBenchTests.cpp` or `VehicleModelTests.cpp` uses SurfaceGrip ≠ 1 or a grade. All benches run on a flat, dry plane.

**Does it work at a random US place?**
- General: the ray contact, the TMeasy tyre, the wetness from live weather and the per-face grip lookup work on any streamed tile. There is no rule for one road or one region. Bronx and Seattle (estimate): kerbs carry on the shoulder rays. 20 % grades are well inside the 0.33 normal limit (cos 11.3° = 0.98).
- Data-dependent: gravel exists only where OSM has `surface=unpaved` (or a value that OpenMapTiles maps to it). Casselton ND, Delta UT and Edinburg TX section and farm roads often have no tag (estimate). There the car drives gravel at asphalt grip (1.0 instead of 0.7), on a paved bench, with the asphalt look.
- Climate: Anchorage AK, Rawlins WY and Casselton ND in winter. Live snow (`bSnowing`) and `TemperatureC` do not reach grip. The car drives on a snowy road at dry grip (estimate, from code paths).
- Poor data: the unpaved lanes of Welch WV and Jasper AR (tracks, service roads) lie on the raw DEM grid. The helper's own comment says that point-sampling a grid makes "every grid edge a slope step" (`road-grade.mjs:59-60`). Expect jolts (estimate). Hilo HI: rain wetness works if the live weather says rain.
- Test tools: the keen pre-flight robot uses the dry skidpad grip on every surface (`Gameplay/SimCruisePreflight.cpp:563-566`). On wet or gravel roads it drives at about 100 % of the available grip. Its steering gains also assume the NB (handoff 2026-11-13).

**Main gaps against "realistic everywhere".**
1. P1: Snow and ice have no grip effect. `SimCruiseWeather.cpp:91-96` and `SimCruiseVehicleMovementComponent.cpp:334-339` ignore `bSnowing` and the temperature. Packed snow μ is about 0.2 and ice about 0.1 (Wong table 1.3, unverified from memory).
2. P1: Unpaved is detected only from an explicit tag. An untagged rural road or `track` is asphalt (`helper/atlas.mjs:130`, `road-grade.mjs:114`). This is wrong grip, wrong look and wrong geometry at farm-grid places.
3. P1: The loose-surface tyre shape is wrong. G = 0.7 also shrinks the peak slip, 0.149 → 0.104 (about 8.5° → 6.0°), and keeps the full stiffness (`SimCruiseTireModel.cpp:112-120`). Gravel then feels like wet asphalt: sharp and snappy. Real loose surfaces have lower stiffness, a larger peak slip and little drop after the peak (unverified; check Rill and Chrono).
4. P1: No bench covers wet, gravel, a split-μ road or a grade (`Private/Tests/VehicleBenches.cpp`). A rear-biased or ABS fault on low μ stays hidden.
5. P2: The car can creep on a grade with the brake held. The locked-wheel force is viscous, `Ke·V` with `Ke = 18.4·Fz/1.5` ≈ 31 kN·s/m per wheel (`SimCruiseTireModel.cpp:243, 261`). The computed creep on a 20 % grade is 1.0 m/min with 4 wheels braked and 2.0 m/min on a rear parking brake (computed; no run).
6. P2: Look and physics can disagree. `-SimCruiseLiveWet` wets only the material (`Live/SimCruiseLiveWorldSubsystem.cpp:510`), not the tyres (`SimCruiseVehicle.cpp:445`).

**My first three actions.**
1. Surface and grade benches in `Private/Tests/VehicleBenches.cpp` and `VehicleBenchTests.cpp`: 70-0, skidpad and ISO 7975 brake-in-turn at grip 0.7 (wet, gravel) and on split μ, plus a 60 s hold on a 20 % grade. Done when the tests run green for all 3 cars, the low-μ stop scales about as 1/μ, there is no spin, and grade drift is < 0.05 m in 60 s.
2. Snow and ice to grip: map `bSnowing`, temperature and wetness to a surface state in `SimCruiseVehicleMovementComponent.cpp:334-339`, with Wong's or another sourced μ. Make `-SimCruiseLiveWet` (and a snow override) also reach the tyres. Done when a unit test passes and `scripts\preflight.ps1 -Roads us-anchorage-ak,us-rawlins-wy -Cars nb2001_aaron,meridian_z,mustang65` with forced snow logs a surface grip of about 0.25 and the car stays on the road.
3. Unpaved inference with gis-data-scientist, in `helper/atlas.mjs:130` and `road-grade.mjs:114`: `track` and untagged low-class rural roads get a prior from the place graph (region, road class). The helper logs the unpaved share per tile. Done when `scripts\preflight.ps1 -Roads us-casselton-nd,us-delta-ut,random` shows telemetry surface grip 0.7 on section roads, gravel in the contact sheets, and no jolt reasons on those roads.

**Docs that are wrong or old.**
- `docs/unreal-architecture.md:76` says the world stream "needs to supply" wheel grip and that `RoadProfileM` is "in place". Grip now comes from `SurfaceGripAtFace`. No code sets `RoadProfileM`.
- `docs/unreal-architecture.md:489` lists "rain and snow (road wetness)". Snow does not change wetness or grip (`SimCruiseWeather.cpp:91-96`).
- `docs/unreal-architecture.md:11` describes the slip-ratio curve with a friction ellipse, and `docs/cars.md:143` names TMeasy for "the 2001 roadster" only. All three cars now run TMeasy.
- The comment at `helper/road-grade.mjs:66` says sigma 10 m. The code says `SIGMA=16` (`:75`).

Claim labels: everything above with a file:line is "read in code". The creep figures, the 0.104 peak slip and the wet grip values (0.70, 0.63, 0.65) are "computed". The judgments about each place, the untagged-road share and the loose-surface tyre behaviour are "estimate" or "unverified". I made no test run and changed no file.

Main files:
- /home/claude/simcruise/Source/SimCruise/Private/World/SimCruiseWorldTile.cpp
- /home/claude/simcruise/Source/SimCruise/Private/Vehicle/SimCruiseVehicleMovementComponent.cpp
- /home/claude/simcruise/Source/SimCruise/Private/Vehicle/SimCruiseVehicleSim.cpp
- /home/claude/simcruise/Source/SimCruise/Private/Vehicle/SimCruiseTireModel.cpp
- /home/claude/simcruise/Source/SimCruise/Private/Environment/SimCruiseWeather.cpp
- /home/claude/simcruise/helper/atlas.mjs
- /home/claude/simcruise/helper/road-grade.mjs
