# 16. Unsprung wheel DOF with tyre vertical stiffness and an enveloping multi-ray contact: wheel hop, smooth unloading, curbs (wheel-dof-contact)
category physics · impact 4 · 5 d · depends: substep-chassis-coupling

## Rationale
Wheels are massless points with a rigid tyre. At the droop limit the load jumps by 1.3 kN (49 % of static) in one tick. Curbs and steps load the spring instantly, and the damper never sees wheel velocity. There is no 12-16 Hz wheel-hop mode and no dynamic load variation on bumpy roads, which is the main reason real bumpy mid-corner grip drops. On a road-trip sim over random US roads, this is ride and grip realism.

## Code claims (proposer)
Powertrain.gd:303-309 does one intersect_ray per corner, with load from CarSuspension.contact_load. unsprung_corner_kg is used only at CarSuspension.gd:54 (critical-damping reference). Powertrain.gd:306 clamps len to [min,max]: at REST the NB spring still pushes 1301 N, and one more mm of lift drops it to 0. The damper velocity is body velocity minus road profile rate (Drive.gd:676, clamped ±0.6 m/s). CarSuspension.gd:52,58 cap every car at 14200 N.

## Design (proposer)
1) A per-corner wheel state (z, zdot) integrated inside the 6-substep loop (ω·dt about 0.25 at 14.6 Hz, semi-implicit Euler).
- Suspension force = spring + ARB + damper(v_mount - zdot) + bump rubber, acting between body and wheel.
- Tyre force = k_t·max(0, pen) + c_t·pen_dot, clamped ≥ 0, between wheel and road.
- Body gets the substep-mean suspension force.
2) Spec tyre_vertical_rate_n_m defaults from size, e.g. Cz about 200 kN/m (TNO 205/60R15 Cz 209.7). The NB 195/50R15 gives about 190 kN/m. c_t gives about 5 % critical.
3) Contact: 5 rays across the patch length (±0.08 m and ±0.16 m fore-aft along the wheel heading), taking the max height for enveloping and a normal low-passed over 15 ms. Or a ShapeCast3D sphere of radius R once per tick, with the plane interpolated through the substeps.
4) The droop limit becomes a wheel-to-body stop. Solid bump stop of 3 MN/m beyond the rubber, substepped. max_contact_load removed.
5) Rolling radius changes with deflection, feeding the tyre R.

## Gate
RideCheck: peak and RMS tyre-load variation reported; body vertical acceleration on the 45 mm bump unchanged within ±10 %.
New checks:
- WheelHopCheck at 20 m/s over a 2 cm × 0.5 m sine washboard: tyre-load transmissibility peaks at 12-16 Hz, and body acceleration PSD falls above 8 Hz.
- CrestCheck: max per-substep |dFz| under 200 N (today about 1.3 kN).
Calibration and regressions:
- CarTrack in band (re-fit lambda_mu only if needed). RideFrequency within ±10 % of analytic with the tyre in series.
- LiftOff, SBend, RoadNoiseCheck and RearDamperCheck pass.
- QualityBench physics_ms within +0.3 ms.

## Files
game/Powertrain.gd
game/CarSuspension.gd
game/CarSpec.gd
game/checks/WheelHopCheck.gd
game/checks/CrestCheck.gd
game/checks/RideCheck.gd
docs/technical-overview.md

## Skeptic verdict
claims_hold true · already_done false · keep true

### Wrong claims
- 'CarSuspension.gd:52,58 cap every car at 14200 N' is wrong. 14200 is only the CarSpec.gd:160 default and the hard-coded value in NB1999Sport.gd:84,90. The car JSONs override it: bison_xl 28414, kestrel_s 18135, meridian_z 22519, mustang65 13000. Removing max_contact_load_n therefore also means migrating the JSONs and NB1999Sport.
- The road-profile clamp is at Drive.gd:679, not 676. The substance is right: road_profile_rate = clamp((bump - prev)/dt, -0.6, 0.6), and the damper sees v.dot(n) - road_profile_rate (Powertrain.gd:309).
- Calling bumpy mid-corner grip loss 'the main' ride/grip gap overstates it for normal play. RoadRoughness.gd is off by default ('--rough-roads' only; its comment says real roads must drive smooth), and its shortest octave is 0.9 m. On default roads the vertical input comes only from the collision mesh, so the washboard and wheel-hop benefits mostly show on curbs, mesh steps and crests.
- The '6-substep loop' (Powertrain.gd:357) is a driveline/brake/TC loop. Contact rays, contact_load, ARB (line 324) and the lateral tyre force (lines 340-349) are all computed once per tick before it. Body forces go to Jolt once per tick through apply_force at line 474. So 'integrated inside the 6-substep loop' has no per-substep geometry, mount velocity or lateral force to work with yet, which is why it depends on substep-chassis-coupling.
- The gate cites RideCheck 'body vertical acceleration on the 45 mm bump' and a 'RideFrequency' check. RideCheck.gd measures only static L/R load, peak L-R load difference and roll. It reports no vertical acceleration, and there is no RideFrequency check in game/checks.

### Design problems
- Off-topic for the relayed user request. The user asked about 'the AI engine we discussed to assist with traffic, extras, generation of detail'. This proposal is chassis physics and does not address that request at all.
- Verified gaps: one intersect_ray per corner (Powertrain.gd:303-304). Length clamped to [min_travel_m, REST=max_travel_m] (line 306). unsprung_corner_kg used only at CarSuspension.gd:54. No tyre vertical compliance or unsprung DOF anywhere (TireStep.gd is a lateral sideslip rig). The droop discontinuity is real: NB front corner 1043*0.515/2*g = 2634 N, minus 0.098 m * 13604 N/m = 1301 N at REST (49 %), then 0 N once the ray (length REST+R) misses.
- The droop and curb discontinuities do not need a wheel DOF. A quasi-static tyre spring in series (load = min(suspension_force, k_t*max(0, REST+R - hit_dist)), continuous at the droop stop) removes the 1.3 kN jump in about 1 day, without new integration state, and keeps the NB1999Sport-equivalence contract stated in the CarSuspension.gd header.
- WheelHopCheck as specified cannot pass meaningfully. A 0.5 m washboard at 20 m/s is 40 Hz forcing, above the 30 Hz Nyquist of the per-tick road sampling in Drive.gd:670-680 (aliases to about 20 Hz). A single-frequency input cannot show a 12-16 Hz transmissibility peak. A 2 cm amplitude at 40 Hz needs about 5 m/s profile velocity, which the ±0.6 m/s clamp truncates. Use a swept sine or broadband profile sampled per substep, at amplitudes the wheel can follow.
- 'Max height of 5 rays across ±0.16 m' is not enveloping. It ignores tyre curvature, so the tyre climbs a curb early and over-reports small steps. Either use h_eff = max_x(h(x) - (R - sqrt(R^2 - x^2))) or a sphere/cylinder shape cast. Also, 5 rays per corner is 20 queries per tick against today's 4; QualityBench physics_ms within +0.3 ms is plausible but unmeasured.
- Dynamic load gives no grip effect unless the tyre forces move into the substeps too. Fz, the load-sensitive mu and the lateral force are evaluated once per tick (Powertrain.gd:340-349), so a 15 Hz wheel-hop load is sampled about 4 times per cycle and the mean grip loss over the nonlinearity is aliased and lost. The lateral/contact_slip path must run per substep or average F(Fz) across the substeps.
- Series tyre compliance lowers static ride height by about Fz/k_t = 2634/190000, roughly 14 mm. CoM height, camber (camber_per_m) and bump_gap engagement all move, which shifts CarTrack skidpad, LiftOff and SBend. Mounts or REST must be re-referenced so static ride height is unchanged, and this needs saying explicitly in the design.
- Feeding a deflection-dependent radius into R is risky and physically doubtful. R is a global used for gear/speed (Powertrain.gd:331), wheel omega, slip ratio, ABS/TC and speedometer. A radial tyre's effective rolling radius is nearly load-insensitive (about R0 - δ/3). This would disturb 0-60/70-0 calibration and TC/ABS for little gain, so drop it.
- A 3 MN/m solid stop on 23 kg unsprung gives ω·ds of about 1.0 at 360 Hz substeps. Semi-implicit Euler stays stable, but the 70 kg bison_xl corner is fine while lighter corners with the progressive bump_stiffness (800000*x^2) on top need a check. Prefer a velocity-level stop (inelastic clamp) over a stiff spring.
- Breaks the CarSuspension.gd == NB1999Sport.gd equivalence that RearDamperCheck relies on (it calls NB1999Sport.contact_load statically). The plan must say whether NB1999Sport gets the same model or the check is retargeted.
- connected_wheels[i].position.y feeds Cockpit.gd (lines 203 and 229), RoadNoise.gd:143 and the drawn wheels (Drive.gd:691). Switching them to the wheel DOF z adds 12-16 Hz content to camera and sound, so RoadNoiseCheck and HeadCameraCheck gates need review, not just pass/fail.

### Corrected design (USE THIS where it differs)
Stage it, and gate each stage on its own.

Stage A (about 1.5 days, no dependency): continuous contact with tyre compliance.
(1) Add spec tyre_vertical_rate_n_m (default from size, about 190-210 kN/m) to CarSpec and the JSONs.
(2) Cast the ray to REST+R+margin, so the ray still finds the road just past the droop limit.
(3) Per corner, pen = REST + R - hit_dist + road_profile. Tyre load = k_t*max(0, pen) in series with the suspension: solve the quasi-static balance, clamping at the droop stop. Load then goes to 0 continuously as the body lifts, and a curb step is softened by the tyre rate instead of loading the spring instantly.
(4) Re-reference so static ride height (and therefore CoM height, camber, bump_gap engagement) is unchanged.
(5) Keep max_contact_load_n for now. Mirror the change in NB1999Sport.gd, or retarget RearDamperCheck.
Gates: new CrestCheck (largest per-tick |dFz| at the droop limit under 200 N, today about 1.3 kN). CarTrack, LiftOff, SBend, RideCheck and RearDamperCheck unchanged within band.

Stage B (after substep-chassis-coupling lands): unsprung DOF.
(1) Per-corner (z, zdot) integrated per substep with the mount velocity from the coupled chassis, the damper on (v_mount - zdot), and the ARB on wheel positions.
(2) Droop and bump stops as velocity-level constraints rather than a 3 MN/m spring.
(3) Fz, mu(Fz) and the lateral and contact_slip forces evaluated per substep, so the load variation actually changes grip.
(4) Contact as a sphere/cylinder shape cast, or the circle-envelope h_eff = max_x(h(x) - (R - sqrt(R^2 - x^2))), not max-of-rays. Sample the road profile per substep.
(5) Leave R constant.
Gates: WheelHopCheck using a swept-sine or ISO 8608 broadband profile sampled per substep, with the wheel-load transmissibility peak at 12-16 Hz. Add body vertical acceleration reporting to RideCheck. QualityBench physics_ms within +0.3 ms. RoadNoiseCheck and HeadCameraCheck reviewed for the added wheel-hop content in connected_wheels.

Priority note: roughness is off by default, so Stage B's payoff is mainly curbs, crests and --rough-roads. Stage A delivers most of the smoothness win.
