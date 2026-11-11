# 8. Semi-implicit chassis coupling inside the substeps, plus a measured tick-rate decision (60 vs 120 Hz) (substep-chassis-coupling)
category physics · impact 4 · 2.5 d · depends: dyn-verification-suite, tyre-mf-combined-core

## Rationale
Jolt integrates the chassis once per 60 Hz tick, and all 6 GDScript substeps compute slip against the same frozen chassis velocity. A stiffer, sharper-peaked real tyre makes this explicit coupling a source of rate-dependent yaw error. Commercial sims run tyres at 333-400 Hz against a chassis state that is updated within the step.

## Code claims (proposer)
Powertrain.gd:357 is the 6-substep loop (ds = dt/6). Rows [5] and [6] (chassis velocities) are frozen for the tick, and :473-474 send one apply_force per wheel per tick. Lateral and suspension loads are evaluated once at :309, :324 and :335-349. project.godot [physics] has no physics_ticks_per_second (60 Hz default), and docs/technical-overview.md:13 says fixed 60 Hz. Physics interpolation is not enabled in project.godot.

## Design (proposer)
1) Inside the substep loop, keep the local predicted chassis state v_k, w_k (world linear and angular velocity at the COM).
- Evaluate each contact's patch velocity as v_k + w_k x r_i, and the slip from that.
- Integrate v_{k+1} = v_k + (ΣF_i/m + g_tangent)*ds and w_{k+1} = w_k + I_world^-1*(Σ r_i x F_i)*ds.
- After the loop, apply the substep-mean force and torque to Jolt with apply_central_force and apply_torque, so Jolt's own integration lands near v_6.
2) Suspension load is re-evaluated per substep from the predicted body velocity at the mount for the damper term. The spring length stays at the tick raycast plus v_n*t.
3) Run the dyn-verification-suite convergence tables at 60/6, 120/3 and 240/2 (force rate held at 360 Hz).
- If any metric differs by more than 2 % between 60 and 120 after the coupling, set physics_ticks_per_second = 120 with 3 substeps, enable physics_interpolation, and set physics_jitter_fix to 0.
- Check that QualityBench physics_ms at 120 Hz with 32 traffic cars stays under 2 ms.

## Gate
The convergence table shows |delta| under 2 % for skidpad g, StepSteer T_r and overshoot, and the LiftOff yaw ratio, between production rate and twice it. CarTrack stays in band (re-fit lambda_mu only if needed). RideCheck, TractionControlCheck (no sustained oscillation) and AbsReverse pass. Checks that count ticks (RideCheck tick==120/330) are converted to seconds. QualityBench p95 frame time increases by at most 0.3 ms at Ultra.

## Files
game/Powertrain.gd
game/project.godot
game/checks/RideCheck.gd
docs/technical-overview.md

## Skeptic verdict
claims_hold false · already_done false · keep true

### Wrong claims
- "All 6 GDScript substeps compute slip against the same frozen chassis velocity" is only half right. The lateral slip and lateral force are not computed in the substeps at all. contact_slip() and side[i] run once per tick, before the loop (Powertrain.gd:335-349), and the substeps only reuse row[8]+=fy/6. Only the longitudinal slip (kappa from row[5]) and the combined-slip budget are re-evaluated per substep. So the proposal's 'force rate held at 360 Hz' for the 60/6 vs 120/3 vs 240/2 comparison is false for the lateral (yaw-relevant) force: today it updates at the tick rate (60 Hz). Changing the tick rate changes the lateral update rate too.
- The rationale ignores the integrator that is already there. contact_slip() (Powertrain.gd:173-191) is a relaxation-length first-order lag on lateral slip, with the load-scaled length set by spec.relaxation_m. It returns the interval-mean slip over the tick, with a comment saying this is what stops the lateral impulse running ahead in time. That is already a semi-implicit and exact-exponential treatment of the lateral DOF. The proposal treats the coupling as plain explicit and does not say how per-substep slip would interact with this filter or replace it.
- The longitudinal force is already solved implicitly against wheel inertia (Powertrain.gd fx=long_stiff*(spin*R-v)/(1+long_stiff*R*R*ds/WHEEL_I)), so the stiff part of the explicit coupling is already handled on that axis.
- Line citation: the per-wheel apply_force is at :474 only (:473 is the loop header). This is minor.
- The gate's 're-fit lambda_mu' refers to a parameter that does not exist in the codebase. It belongs to the unbuilt tyre-mf-combined-core dependency. The real knobs are static_mu, lateral_grip_scale, slip_stiffness and relaxation_m in CarSpec.
- The gate says 'Checks that count ticks (RideCheck tick==120/330) are converted to seconds', which understates the scope. At least 13 checks count physics ticks or frames: AbsReverse, CarTrack, CombinedSlip, EngineSoundDrive, KeyboardTap, LockedBrakeTurn, PadSteer, RideCheck, RoadDrive, SplitGrip, SteerTransient, Track and TractionControlCheck. docs/technical-overview.md:23 states that Track calibration runs 'at a fixed 60 Hz'.

### Design problems
- Step 1's 'apply substep-mean force and torque with apply_central_force/apply_torque' brings nothing new. The current code already applies per-wheel forces averaged over the substeps (net[i]+=fx/6, row[8]+=fy/6) at the contact offset, and that equals a central force plus torque. The only real change is evaluating patch velocity from a predicted v_k, w_k. The predicted state also leaves out terms Jolt will apply: aero drag and rolling resistance (applied after the loop with dt), the collision box's contacts, angular_damp (0.02) and the gyroscopic term. So 'Jolt lands near v_6' needs those terms in the predictor, or the predictor and Jolt drift apart over each tick.
- To get any lateral benefit, the lateral force and contact_slip have to move inside the substep loop, using ds and a rolling_slip update in each substep. That changes the interval-mean formula the code relies on, and it touches the load-sensitive mu, camber thrust and slip_falloff evaluation at :340-348 for every car. The design does not mention this. Without it, the predicted v_k changes only kappa, not yaw.
- Re-evaluating the damper load per substep from predicted body velocity also needs the ARB term (:324) and the road_profile_rate input re-evaluated per substep. Otherwise the load transfer is inconsistent within the tick. The spring term 'tick raycast + v_n*t' also extrapolates over the clamp to min_travel/REST.
- Moving to 120 Hz is not just a project setting. 'for step in 6' and the sound accumulators (/(6.*180.)) are hard-coded, so the substep count needs parameterizing. Drive._physics_process work per tick doubles: surface grip lookup per wheel, RoadRoughness IRI and height per wheel, tile checks. road_profile_rate=(bump-prev)/dt changes the sampled roughness spectrum, which affects ride feel and RideCheck. Many tick-based checks and the 'fixed 60 Hz' calibration docs need rework, not just RideCheck.
- QualityBench 'physics_ms with 32 traffic cars' is a weak cost gate. Traffic is kinematic IDM in TrafficDirector/AmbientLife, not Jolt bodies, so the 120 Hz cost lands in Drive._physics_process and the player car script, which physics_ms partly misses. Measure drive_process_ms plus physics_ms.
- Physics interpolation is half-prepared already: Drive.gd:725 reads get_global_transform_interpolated(), Cockpit sets cameras to PHYSICS_INTERPOLATION_MODE_OFF, and Powertrain.restart calls reset_physics_interpolation. But enabling it adds one tick of visual latency and needs the teleport, rebase and freeze paths (Drive tile wait, recover, rebase) to call reset_physics_interpolation. The design does not mention this.
- The motivation depends on a stiffer, sharper-peaked tyre (tyre-mf-combined-core) and a convergence suite (dyn-verification-suite), and neither exists. With the current tanh tyre plus relaxation filter there is no measured rate-dependent yaw error, so the 2.5-day effort and the impact of 4 are speculative until the convergence table is run first.

### Corrected design (USE THIS where it differs)
Do the measurement first and the coupling only if the measurement shows a need.

(0) Parameterize the substep count: replace the literal 6 and the /6 averaging and sound scaling with SUBSTEPS. Add a check-only override of physics_ticks_per_second through a command-line argument, so the production project stays at 60 Hz.

(1) Run the convergence table now, on the current tyre. Use 60/6, 120/3 and 240/2, plus 60/12 to separate the tick rate from the substep rate. Measure skidpad g, StepSteer yaw-rate rise time and overshoot, and the LiftOff yaw ratio for every car. Note that the lateral force currently updates at the tick rate, so 60/6 vs 60/12 isolates the longitudinal and TC/ABS effect, and 60 vs 120 isolates the lateral effect. If every delta is under 2 %, stop and record the result in technical-overview.md.

(2) If the lateral delta is over 2 %, move contact_slip and the lateral force into the substep loop. Call contact_slip with ds and update rolling_slip in each substep, so the exact-exponential relaxation keeps holding. Evaluate patch velocity from a predicted v_k, w_k. The predictor integrates ΣF, the tyre torques, gravity, aero drag and rolling resistance, using I_world from spec.inertia and the body basis. Keep the existing per-wheel apply_force of substep-mean forces, which already equals the force plus torque. Leave the suspension and ARB loads per tick unless RideCheck or the roughness drive shows substep sensitivity.

(3) Change the production rate to 120 Hz only if, after (2), 60 vs 120 still differs by more than 2 %. When doing so:
- Enable physics_interpolation and call reset_physics_interpolation on the restart, recover, rebase and tile-freeze paths.
- Convert all 13 tick-counting checks to seconds (AbsReverse, CarTrack, CombinedSlip, EngineSoundDrive, KeyboardTap, LockedBrakeTurn, PadSteer, RideCheck, RoadDrive, SplitGrip, SteerTransient, Track, TractionControlCheck).
- Retune the road_profile_rate clamp and roughness sampling, then re-verify RideCheck at both rates.
- Gate the cost on drive_process_ms plus physics_ms, not physics_ms alone.
- Update docs/technical-overview.md (lines 13 and 23), the CHANGELOG and the CarTrack numbers.

Re-fit the existing CarSpec grip knobs (static_mu, lateral_grip_scale, slip_stiffness, relaxation_m) only if CarTrack leaves its band. There is no lambda_mu parameter.
