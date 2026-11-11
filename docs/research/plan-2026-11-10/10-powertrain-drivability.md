# 10. Powertrain that behaves like the real one: throttle-aware shift map, displacement-based engine braking, air-path lag, coast-biasing Torsen, Coulomb clutch, DCT shift torque (powertrain-drivability)
category physics · impact 4 · 5 d · depends: dyn-verification-suite

## Rationale
Every car in auto cruises at high rpm, because the shift logic never reads the pedal. Engine braking is the same 20 N·m at 5000 rpm for a 1.8 L NB and a 6.2 L C8, which is 2.5-9 times too weak. The NB's Torsen goes open on lift, though the real one biases in coast and that shapes lift-off and trail-brake balance. The clutch is a viscous damper that always slips about 100 rpm.

## Code claims (proposer)
Powertrain.gd:329-333 shift on rpm and road-speed omega only. CarSpec.gd:105-106 give engine friction 10 + 0.002·rpm N·m for every car, applied at Powertrain.gd:381-383. Powertrain.gd:413 multiplies overrun torque by eta. CarSuspension.gd:62-70 return an even split whenever drive is false. Powertrain.gd:392-394 is the viscous clutch (12 N·m·s/rad). :217-221 hold torque flat below the first curve point. :405 gives a torque×0.12 shift with the clutch dumping at capacity (C8 about 35 % over max). docs/roadmap.md:50 lists throttle-aware shifts as open.

## Design (proposer)
1) Shift map: up(thr) = lerp(eco_upshift_rpm, upshift_rpm, thr^1.5), with eco default 2.2×idle. Down = the next-lower gear's up line minus 30 % hysteresis.
- Kickdown when pedal rate > 3/s or thr > 0.9: choose the lowest gear with rpm < 0.9·upshift_rpm.
- Downshift while braking at 1.2·downshift_rpm. WOT keeps today's upshift_rpm.
2) Engine braking:
- displacement_l per car (NB 1.84, Mustang 4.74, C8 6.16).
- T_drag = (FMEP(N) + 0.8 bar·(1-thr))·Vd/(4π), with FMEP = 0.97 + 0.15(N/1000) + 0.05(N/1000)² bar (Heywood form). The existing fields remain as overrides.
- Overrun axle torque = T·ratio/η.
- The curve is treated as net WOT, so full-throttle output is unchanged.
3) Torque curve: an implicit (0.6·idle, 0) point at the bottom so stalls are real.
4) Air path: manifold first-order tau 0.08 s on the throttle-to-torque target, for NA, carb and DBW alike. The turbo boost state is kept generic but unused on the kept roster.
5) Differential types per axle (open, torsen, clutch, viscous):
- torsen: bias in drive, coast_bias in overrun (NB both about 2.0-2.5, sourced or estimate).
- clutch: preload + ramp_drive/ramp_coast. The C8 eLSD is approximated as clutch.
- The coast branch is used whenever |axle torque| > 0.
6) Clutch as a Coulomb stick-slip: locked when the torque needed to keep both sides at one speed is ≤ capacity, slipping at capacity·sign(slip) otherwise.
7) C8 DCT shift: cap clutch_load at pre-shift torque·r_new/r_old during the inertia phase, with spark cut absorbing flywheel energy. Downshift blip drives engine_omega to target before re-engagement.

## Gate
New ShiftSchedule check, PI speed driver per car at 25/45/65 mph road load:
- NB about 2900 rpm at 65 mph in top.
- C8 and Mustang compared against published N/V.
- Throttle step 0.2→1.0 at 50 mph downshifts within 0.5 s.
- No gear hunting on a 6 % grade.
Other gates:
- New EngineBraking check: closed-throttle deceleration in 2nd at 4000 rpm (NB target about 0.10-0.15 g).
- ClutchLock check: slip 0 rpm at a steady 50 mph.
- ShiftQuality check: C8 peak/mean drive torque across the shift ≤ 1.1.
- DifferentialCheck coast split case.
- CarTrack 0-60 in band (WOT unchanged), 70-0 in band. LiftOff, SBend, CombinedSlip and TractionControlCheck pass.
- InlineFourSound shows no clicks (EngineSoundDrive).

## Files
game/Powertrain.gd
game/CarSuspension.gd
game/CarSpec.gd
game/cars/nb1999.json
game/cars/mustang65.json
game/cars/meridian_z.json
game/checks/ShiftSchedule.gd
game/checks/EngineBraking.gd
game/checks/DifferentialCheck.gd
docs/cars.md

## Skeptic verdict
claims_hold true · already_done false · keep true

### Wrong claims
- Scope: the user asked about an AI engine for traffic, extras and detail generation. This proposal is powertrain physics and does not answer that question. It is valid only as a separate physics backlog item.
- 'The clutch always slips about 100 rpm' is wrong. Slip is clutch_load/clutch_stiffness, so it scales with torque. It is about 30 rpm for the NB at cruise, about 125 rpm for the NB at WOT, and about 500 rpm for the C8 (meridian_z) at 600+ N·m. It is not a constant 100 rpm. The cap at Powertrain.gd:393 (coupling ≤ 1/(ds·(1/I_e+reflected))) is already a one-substep deadbeat limit, so a 'Coulomb lock' is mostly raising clutch_stiffness to that limit, not a new mechanism.
- 'Stalls are real' only with a zero-torque point at the bottom of the curve is misleading. Manual stalls already exist (stall_omega=35, a governor up to 100 N·m below governor_rpm, stalled flag at Powertrain.gd:401,407). The flat torque below 800 rpm (crank_torque clamps the lerp) is real but minor.
- The gate 'NB about 2900 rpm at 65 mph in top' is physically wrong. 29.06 m/s / R≈0.29 m × 0.814 × 4.30 ≈ 3,370 rpm in 5th. Top-gear rpm is also fixed by gearing, so it does not test the shift map at all.
- 'CarTrack 0-60 in band (WOT unchanged)' is not assured. Removing viscous slip moves the engine's operating point (up to about 500 rpm on the C8). Capping the C8 DCT clutch_load during shifts removes the flywheel dump that helps it accelerate today. An air-path lag also delays launch. All three change 0-60, and the C8 most likely needs recalibration.
- 'DifferentialCheck coast split case' as an added case: the existing game/checks/DifferentialCheck.gd currently fails if (not driving and split.x!=split.y). It enforces open-in-coast, so it must be rewritten, not extended. It also calls NB1999Sport.differential_torque (game/NB1999Sport.gd:15, DRIVE_BIAS_RATIO=2.0), a second copy of the diff that must change too.

### Design problems
- Off-topic for the relayed user request, which is about an AI engine for traffic, extras and detail generation.
- Keyboard throttle is binary (Drive.gd:666 Input.get_action_strength on W/Up, and keyboard is the primary hint). So thr^1.5 gives only two lines: WOT while holding, eco on lift. 'Pedal rate > 3/s or thr > 0.9' fires kickdown on every keypress. Tapping to cruise would hunt: kickdown, then eco upshift on release, with a 0.15-0.45 s torque gap for each non-DCT shift (Mustang shift_seconds 0.45). Shift decisions need a filtered or ramped pedal, plus a minimum dwell time between shifts.
- The downshift rule 'next-lower gear's up line minus 30 %' compares rpm across different ratios. Where the gear step exceeds 1/0.7 = 1.43 it hunts. NB 1st to 2nd is 1.66: at thr 0.3 the down line is about 1,860 rpm, and after the downshift rpm is about 3,090, which is above the about 2,660 up line, so it upshifts at once. Hysteresis must be checked in post-shift rpm (rpm·r_low/r_cur < up(thr)·(1-h)).
- Lift-off upshift: at thr=0 the eco up line (2.2×idle ≈ 1,870 rpm) sits below almost any corner rpm. Every mid-corner lift therefore triggers an upshift, with a non-DCT clutch_load=0 gap, during exactly the LiftOff/SBend manoeuvres. The design needs a coast or lateral-g gear hold. Without it LiftOff results change for reasons other than chassis behaviour.
- Engine-braking interaction with the linear throttle map: torque=(crank+friction)·throttle and drag=friction (Powertrain.gd:382-383). Raising friction 2.5-9× moves the zero-torque pedal to about 17-20 % and adds part-throttle deadband on pad triggers. Worse, traction control works by scaling throttle (throttle=pedal·(1-tc_cut), line 376), so a 0.9 TC cut would now produce strong engine braking on a slipping driven axle. TractionControlCheck/SplitGrip behaviour would change. The TC cut must scale indicated (crank) torque, like a spark cut, not the throttle that feeds pumping and friction.
- The air-path lag (tau 0.08 s) must sit on the pedal before TC. If it sits on the final throttle it slows TC response.
- Coulomb lock: the reflected inertia used for the lock torque ignores the tyre-to-ground coupling, and that is exactly why the code comment at Powertrain.gd:386-391 records sign-alternating chatter in low gears at the grip limit. A hard stick state needs the substep-implicit combined engine+wheel+tyre stiffness, or it will chatter. The Bison XL is 'automatic' and should keep a torque-converter-style slip law, not a Coulomb clutch. Launch-rpm logic (line 401) and clutch_saturated/TC gating also depend on slip.
- Stronger RWD overrun torque raises rear longitudinal slip on lift (the NB already spun at 0.5 g before the reverse fix). Coast-locking the Torsen pulls the other way. The net LiftOff effect is unknown and must be measured before claiming that LiftOff passes. The CHANGELOG notes that diff experiments moved the 0.7 g response by ≤0.11.
- The AWD centre diff (Powertrain.gd:498) and front diffs are also gated on drive. The coast design must cover them, not only CarSuspension.differential_torque.
- C8 DCT: capping clutch_load at the pre-shift torque·r_new/r_old removes the current about 33 % spike (850 N·m capacity vs about 637 peak). It also removes acceleration the C8's 0-60 calibration currently includes, so a CarTrack re-tune is required.
- This is too many independent changes for one 5-day item. The shift map, engine braking, diffs, clutch and DCT each move CarTrack/LiftOff, so failures cannot be attributed. Split them and land each with its own gate.

### Corrected design (USE THIS where it differs)
Only if the owner wants powertrain work. It does not answer the AI traffic/extras/detail question. Split it into separately gated steps.
(a) Engine braking. Per-car displacement and Heywood FMEP plus pumping drag. Overrun axle torque = clutch_load·ratio/η when clutch_load<0. Keep WOT net = curve. Change TC so it scales crank torque (spark cut), not throttle. Gate: an EngineBraking check (NB 2nd at 4000 rpm, about 0.12-0.15 g), CarTrack 70-0 and LiftOff measured, not assumed.
(b) Shift map. Decide on a slewed pedal (keyboard ramp about 2-3/s for shift purposes) with a minimum gear dwell (about 1.5 s) and a coast/lateral-g hold (no upshift on lift above about 0.3 g or within about 2 s of a lift). Hysteresis is evaluated in post-shift rpm. Kickdown fires on the filtered pedal > 0.9. Gate: ShiftSchedule asserting top gear at a steady 65 mph (NB about 3,370 rpm, from N/V), no hunting on a 6 % grade or under keyboard tap-cruise, and kickdown within 0.5 s.
(c) Coast diff bias. Applies to Torsen and clutch LSDs and the AWD centre diff. Rewrite the DifferentialCheck coast assertion and NB1999Sport.differential_torque. Gate: LiftOff/SBend.
(d) Clutch lock. Raise coupling to the deadbeat limit including a tyre-stiffness term, for manual and DCT only. Keep converter slip on the automatic. Gate: ClutchLock and LaunchCheck.
(e) DCT torque handover with spark cut. Re-calibrate C8 0-60 afterwards.
Drop the zero-torque curve point; stalls already exist. The air-path lag is optional and goes on the pedal before TC.
