# 1. Dynamics verification suite: all kept cars, ISO 4138 gradients, ISO 7401 step steer, tick convergence, working harness (dyn-verification-suite)
category verification · impact 5 · 3.5 d · depends: none

## Rationale
Calibration bands are gated for the NB only. The tests that make a Miata feel different from a C8 (understeer and roll gradients, yaw response time) do not exist. Every later physics change needs these numbers as its regression net and as the realism yardstick. It is cheap and analysis-only.

## Code claims (proposer)
check.sh:63-67 runs CarTrack with no args, and CarTrack.gd:33-34 then defaults to queue=["nb1999"]. CarTrack.gd:73-85 already runs the ISO 4138 ramp steer but keeps only ramp_peak. SteerTransient.gd:4-6, KeyboardTap.gd:40 and SBend.gd:31 are NB-only and report no T_r or overshoot. check.sh:65 lists DriftHold and BumpDrive, which do not exist and are counted as skips. check.sh:22-24 has only a macOS Godot fallback, so on Windows every log in build/check-logs is empty. RearDamperCheck.gd:3 and DifferentialCheck.gd:2 test dead code in NB1999Sport.gd instead of CarSuspension. SplitGrip.gd:29-32 always exits 0. project.godot [physics] sets no physics_ticks_per_second.

## Design (proposer)
1) check.sh:
- Add a Windows fallback (Godot_v4.7.x-stable_win64_console.exe on PATH or under GODOT_BIN). Fail loudly when no binary is found.
- Make a missing listed check a FAIL. Drop DriftHold and BumpDrive.
- Add per-check args so CarTrack runs `-- --all`, limited to the kept roster (nb1999, mustang65, meridian_z) once the generated cars are removed.

2) CarTrack ramp, extra logging: rack (car.steering), road-wheel angle, roll (basis.x.y), beta and ay.
- Linear-fit over 0.1-0.4 g: K_us = d(delta_rw)/d(ay) - (L/R)/g in deg/g, roll gradient in deg/g, beta gradient.
- Gate the roll gradient against the analytic m_s*g*h_ra/(K_phi_f+K_phi_r) from CarSuspension.wheel_rate and roll_bar_rate, within ±15 %.
- Body-class plausibility bands. NB: K_us 1-3 deg/g, roll 4-6 deg/g. C8: K_us 0.5-2.5, roll 2-4. Mustang 65 on bias-era tyres: K_us 3-6, roll 5-8.

3) New checks/StepSteer.gd, for every kept car with steering_assist=false:
- 80 km/h, 300 deg/s handwheel-equivalent ramp to 0.4 g steady state.
- Report T_r (50 % steer to 90 % yaw rate), T_peak, overshoot %, steady r/delta and the ay lag.
- Add a 0.2-2 Hz chirp at 0.2 g and fit the yaw-gain magnitude and phase at 1 Hz.
- Plausibility bands: T_r 0.10-0.20 s for NB and C8, 0.20-0.35 s for the Mustang. Overshoot 5-30 %.

4) Convergence mode in CarTrack and StepSteer:
- --hz=N sets Engine.physics_ticks_per_second and scales the substep count to keep 360 Hz force.
- Print 60/120/240 tables.
- Fail when 0-60 moves more than 0.05 s, 70-0 more than 2 ft, skidpad more than 0.01 g, or T_r more than 10 %.

5) RideFrequency check: 30 mm heave and pitch release on a pad. Zero-crossing frequency and log-decrement zeta, compared with sqrt(k_ride/m_s)/2pi (±10 %).

6) Point RearDamperCheck and DifferentialCheck at CarSuspension.new(CAR_SPEC.load_id(id)). Give SplitGrip an analytic gate: (1+TBR)*mu_low*Fz_low ±15 %, yaw under 2 deg.

7) Move band widths into CarSpec as target_bands, the single source of truth. Track.gd and CarTrack exit non-zero by themselves, and Track.gd's skidpad speed step drops to 0.1 m/s.

## Gate
The suite itself is the gate. Harness: `sh scripts/check.sh` on Windows prints a non-empty table for 3 cars. A deliberate sign flip in CarSuspension.dyno_load now fails RearDamperCheck. A 0.05 g grip loss fails both Track and CarTrack. The new metrics are recorded as baselines in CHANGELOG. They are report-only for one release, then become gates. Today's calibration (CarTrack bands, LiftOff, KeyboardTap, PadSteer) does not change, because nothing in the physics changes.

## Files
scripts/check.sh
game/checks/CarTrack.gd
game/checks/Track.gd
game/checks/StepSteer.gd
game/checks/RideFrequency.gd
game/checks/RearDamperCheck.gd
game/checks/DifferentialCheck.gd
game/checks/SplitGrip.gd
game/CarSpec.gd
CONTRIBUTING.md
docs/technical-overview.md

## Skeptic verdict
claims_hold false · already_done false · keep true

### Wrong claims
- 'On Windows every log in build/check-logs is empty' is out of date. The empty logs (all with .code 0) are from Nov 9, which is before commit ab1ef31 (Nov 10). That commit switched check.sh to GNU timeout, which Git Bash has at /usr/bin/timeout. It also made check.sh:109 turn an empty log into exit 127 and FAIL, and the CHANGELOG records the fix. GODOT_BIN already works on Windows and CONTRIBUTING.md:23 documents it. What is really missing is a win64 auto-discovery fallback and one clear 'no Godot found' abort. Today a missing binary just produces about 37 FAIL rows that each say 'exit 127'.
- 'SplitGrip.gd:29-32 always exits 0' is not exact. Line 26 does quit(1) when a wheel loses contact. It does have no grip or yaw gate and prints no PASS/FAIL line, so the table detail reads 'exit 0'.
- 'SBend.gd:31 is NB-only' is wrong. SBend.gd:32-36 accepts --car= (and --speed=). Only its default is nb1999, and only the default runs in check.sh. SteerTransient.gd and KeyboardTap.gd:40 really are NB-only: they call Powertrain.new() with no use_spec, so they get the default spec. KeyboardTap also runs Assisted steering by default, so it is not a chassis-dynamics test.
- Design step 7 says CarTrack should be made to 'exit non-zero by itself'. It already does: CarTrack.gd:99 calls quit(0 if failed==0 else 1). Only Track.gd needs this, because it exits 0 and check.sh:115-125 applies the band with awk.
- Line references are off. DriftHold and BumpDrive are listed at check.sh:63, not :65. CarTrack is invoked by the generic loop at check.sh:103, not at 63-67. The substance is right: neither file exists, and check.sh:96-97 counts them as 'skip'.
- Design step 4's premise, 'scale the substep count to keep 360 Hz force', misreads Powertrain. Chassis forces are applied once per physics tick: apply_force at Powertrain.gd:474 sits after the substep loop, and fx/6 and fy/6 are averaged into net[i]. Suspension loads (Powertrain.gd:309, 324) are also computed once per tick, before the loop. The six substeps (`for step in 6`, Powertrain.gd:357) integrate only wheel spin, clutch, diff and TC. Changing the count also means editing Powertrain.gd, which is not in the file list. At 240 Hz the scaled count would be 1.5, which is not an integer.
- Design step 2's analytic roll gate, m_s*g*h_ra/(K_phi_f+K_phi_r), does not fit this model. There is no roll-centre field and no separate unsprung body. Lateral and vertical forces are applied at the ground contact point (Powertrain.gd:474), so the roll axis is the ground and the rolling mass is the whole rigid body. In this code the gate becomes m*g*cg_height/(sum over axles of (wheel_rate*t^2/2 + roll_bar_rate*t^2)), using Powertrain.gd:324's bar law. That checks the code's own algebra, so it is a regression gate, not a realism check.
- The K_us formula 'd(delta_rw)/d(ay) - (L/R)/g' is dimensionally sloppy. In a constant-speed ramp the Ackermann term is L*ay/V^2, so the gradient to subtract is L*g/V^2 rad per g.
- Design step 3 says 'handwheel-equivalent 300 deg/s' and 'every kept car with steering_assist=false'. Neither exists in the spec. CarSpec has no steering_ratio field, only parking_rack_deg. steering_assist is a runtime flag on Powertrain (Powertrain.gd:24, :205), not a car property, and every bench car is Direct unless the check changes it. The step input has to be written as a road-wheel or rack rate. The Direct-map cap also depends on speed (Powertrain.gd:233-235), so the step amplitude has to be computed from the rack, not from the input value.
- 'Today's calibration does not change' is not guaranteed. Track.gd:57 steps the skidpad speed by 0.4 m/s. Dropping that to 0.1 m/s changes which laps are run and which best_lat is reported, so the gated 0.885 g Track number can move inside or outside 0.875-0.895. The step-4 Powertrain substep edit is also a physics-file change.

### Design problems
- The proposal does not match the user's request. The relayed request asks about an AI engine for traffic, extras and generation of detail. This proposal is a vehicle-dynamics verification suite and does none of that, so it should not be presented as the answer to that request.
- Convergence mode cannot run on the current checks as they are. They count time in ticks: CarTrack.gd:49 (60*600) and :54 (tick==90); Track.gd:27 (18000); SplitGrip 75/165; SteerTransient 90/100 and its frame constants. All of these have to become seconds before --hz=N means anything. Godot's max_physics_steps_per_frame (default 8) together with --fixed-fps 60 also has to be checked at 240 Hz.
- The convergence fail thresholds (0.05 s, 2 ft, 0.01 g, 10 % T_r) will probably trip on the first run. Several elements are tuned per tick at 60 Hz: steering move_toward, explicit dampers and roll bars evaluated once per tick, and the Jolt contact step. Keep convergence report-only until the first numbers exist. Otherwise the suite starts red, and that pushes toward 60 Hz-specific fixes, the special-casing the owner wants to avoid.
- 'Limited to the kept roster (nb1999, mustang65, meridian_z) once the generated cars are removed' assumes a removal decision that docs/roadmap.md and docs/cars.md do not record. depends_on is []. Use CAR_SPEC.available(), or an explicit list in check.sh, without assuming deletions.
- Running CarTrack --all, StepSteer with chirp, and convergence at 60/120/240 Hz in one Godot run each risks the 600 s cap in run_godot (check.sh:103). Split them by car or by tick rate.
- The body-class bands (NB roll 4-6 deg/g, Mustang K_us 3-6, and so on) and the T_r bands have no sources. The owner requires realism backed by published numbers, so either cite instrumented data for each band (for example the docs/cars.md style) or label the bands as estimates.
- RideFrequency compares against sqrt(k/m)/2pi per axle. That only holds when the pitch dynamic index is near 1, which is about 0.94 for the NB and unknown for the others. The NB's rear damper is a nonlinear dyno table, so a log-decrement zeta is only an approximation. RideCheck.gd already exists, so extend it rather than adding a separate check.
- RearDamperCheck already hardcodes REAR_RATIO 0.77 and NB1999Sport constants. To repoint it, read lever_ratio from CarSpec.load_id('nb1999') and keep the same published dyno readings. Note that CarSuspension.differential_torque takes an extra bias argument, and NB1999Sport stays preloaded but unused in Powertrain.gd:5. Either delete that preload or document it.
- Design step 7, moving bands into CarSpec target_bands, touches CarSpec.apply and every car JSON. That is broader than 'analysis-only'. Keeping the bands in CarTrack, with per-car overrides, has the same effect with less churn.

### Corrected design (USE THIS where it differs)
1) check.sh:
- Look for a Windows Godot binary when GODOT_BIN is unset and godot is not on PATH: Godot_v4.7.2-stable_win64_console.exe on PATH. If none is found, print one clear error and stop before the import step.
- Treat a listed check that has no file as FAIL, and remove DriftHold and BumpDrive from the list.
- Add per-check arguments. Run CarTrack with '-- --all' and split it by car if the 600 s cap gets close.
- Have Track.gd quit non-zero on its own; CarTrack already does.

2) CarTrack ramp, report-only at first. Log the rack, mean road-wheel angle, roll (basis.x.y), beta and ay, then do a linear fit over 0.1-0.4 g:
- K_us = d(delta)/d(ay) - L*g/V^2, in deg/g.
- Roll gradient and beta gradient.
- An analytic roll check for this model: m*g*cg_height / sum over axles (wheel_rate*t^2/2 + roll_bar_rate*t^2), within ±10 %. Label it a code-consistency gate.
- Body-class bands only where a cited instrumented figure exists; otherwise print them as estimates.

3) New StepSteer.gd. Any car via --car or --all, with steering_assist forced false:
- Command a rack-rate step, for example 20 deg/s of road-wheel angle, to the angle that gives 0.4 g. Report T_r (50 % rack to 90 % yaw rate), T_peak, overshoot, steady r/delta and the ay lag.
- Add the chirp later as a separate case.
- Report only for one release.

4) Convergence:
- First convert every check's tick counters to seconds.
- Add --hz=N, which sets Engine.physics_ticks_per_second. Keep 6 wheel substeps per tick, so Powertrain.gd does not change, or make the count an integer var that defaults to 6 and stays byte-identical at 60 Hz.
- Print the 60, 120 and 240 Hz tables, report-only. Do not gate on them until the baseline is known.

5) Extend RideCheck.gd with a heave release:
- Frequency from zero crossings and zeta from log decrement.
- Compare with sqrt(wheel_rate/corner_mass)/2pi, flagged approximate.

6) Point RearDamperCheck and DifferentialCheck at CarSuspension.new(CarSpec.load_id('nb1999')), reading lever_ratio and bias from the spec. Remove the unused SPORT preload, or keep NB1999Sport as a frozen reference and say so.

7) Give SplitGrip a PASS/FAIL line and a gate:
- An analytic drive-force check, (1+TBR)*low-wheel force ±15 %, with the measured low-wheel force.
- Record yaw at first and turn it into a gate after the baseline is known.

8) Leave the Track.gd skidpad step at 0.4 m/s, or, if it changes, re-baseline the Track band in the same change and record it in the CHANGELOG.

Files: scripts/check.sh, game/checks/{CarTrack, Track, StepSteer, RideCheck, RearDamperCheck, DifferentialCheck, SplitGrip, SteerTransient}.gd, optionally game/Powertrain.gd (substep var, no-op at 60 Hz), CONTRIBUTING.md, docs/technical-overview.md, CHANGELOG.md.

Gate: CarTrack, LiftOff, KeyboardTap, PadSteer and Track produce unchanged numbers at 60 Hz. Comparing the git-stash runs before and after must give identical RESULT lines.
