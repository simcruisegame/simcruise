# 3. Match the real test procedures before recalibrating: C&D 1-ft rollout, brake application ramp, plateau decel (test-procedure-fidelity)
category verification · impact 4 · 1.5 d · depends: dyn-verification-suite

## Rationale
Several calibrations have a systematic error built in. Cars tuned to the C&D 0-60 figure, which has the 1-ft rollout subtracted, are timed from rest, so their launches come out 0.2-0.3 s too quick. 70-0 uses an instant brake step, so the locked-tyre mu is fitted about 4.6 % low. Recalibrating the tyre on biased targets would build the error into the tyre itself.

## Code claims (proposer)
CarTrack.gd:54-58 and Track.gd:131-135 start the clock at pedal-down with no rollout. meridian_z.json calibration_open already notes C&D omits a 1 ft rollout, about 0.3 s. CarTrack.gd:59-62 applies brake = 1.0 at the 70 mph crossing. Powertrain.gd:276/417 apply the full torque in the same substep. nb1999.json sources pin locked-tyre grip at 0.963 from the 173 ft stop.

## Design (proposer)
1) CarSpec target_rollout_ft per source: 1.0 for C&D and MotorTrend, 0 for the 1965 Car Life. CarTrack and Track start the timer once the car has travelled that distance, and report both the from-rest and the rollout times.
2) Brake application in the procedure, not as a fake lag:
- The CarTrack pedal ramps 0 to 1 over 0.15 s, which is typical tester foot plus hydraulic build.
- Powertrain gains a physical first-order brake pressure state, tau = brake_rise_s (default 0.08 s), between pedal and torque. It applies to every input path.
- Report the stopping distance and the mean deceleration from 60 to 10 mph separately, so the tyre number and the procedure number can be told apart.
3) Re-fit only the parameters each change actually affects: launch_rpm and clutch for 0-60, and lambda_mu_x / static_mu for 70-0.
4) Update the CONTRIBUTING.md baselines and band text from CarSpec.

## Gate
The rollout-minus-rest delta per car is printed (expected 0.2-0.35 s). The NB plateau deceleration rises toward about 0.99 g while the 173 ft distance stays in band. After the re-fit, CarTrack is in band for all kept cars. LockedBrakeTurn (22-32 m), AbsReverse, CombinedSlip (within the 3 % allowed gain) and TractionControlCheck all pass.

## Files
game/checks/CarTrack.gd
game/checks/Track.gd
game/CarSpec.gd
game/Powertrain.gd
game/cars/nb1999.json
game/cars/mustang65.json
game/cars/meridian_z.json
CONTRIBUTING.md

## Skeptic verdict
claims_hold true · already_done false · keep true

### Wrong claims
- Track.gd:131-135 does not exist: game/checks/Track.gd is 63 lines. The equivalent clock start and 0-60 interpolation are at Track.gd:31-37 (tick==90 starts t=0, then a60 is interpolated at 26.8224 m/s, and brake=1 at the 31.2928 m/s crossing at 37/40). The CarTrack.gd line references are about right: 54 starts the clock, 58 is a60, 59 is the 70 mph crossing, 62 is brake 1.0.
- 'lambda_mu_x' is not a parameter in this codebase. CarSpec only has static_mu (CarSpec.gd:45), which is shared by longitudinal and lateral grip, and lateral_grip_scale (CarSpec.gd:46). Powertrain.gd:345-346 multiplies lateral force by mu*LATERAL_GRIP_SCALE, so raising static_mu also raises skidpad grip and traction-limited launches unless lateral_grip_scale is re-fit at the same time.
- '1.0 for C&D and MotorTrend': no car in game/cars cites MotorTrend. Six cars cite Car and Driver (nb1999, kestrel_s, tora_rs, bison_xl, meridian_z, volta), and mustang65 cites Car Life 1965.
- 'launches come out 0.2-0.3 s too quick': the bench is too quick overall by the rollout amount. Nothing shows the launch phase is where the error sits. CarTrack times from pedal-down in automatic, and the error could equally be in shifts, driveline efficiency, drag or engine curve.
- The 4.6 % locked-mu figure matches the 0.15 s pedal ramp alone, which loses about 0.075 s of full deceleration at 31.3 m/s (52.7 m to 50.4 m). The design also adds a 0.08 s first-order pressure lag in Powertrain. Together they lose about 0.155 s, about 4.9 m or 16 ft, so the implied mu change is about 10 % and the plateau would move toward about 1.04 g, not 'about 0.99 g'. The rationale and the gate contradict the design.
- The proposal implies the gap is unknown. It is already documented, though not implemented: docs/cars.md:21 and :29 say the bench is 'timed from rest without the magazines' 1 ft rollout', and meridian_z.json:180 carries it as a calibration_open item.

### Design problems
- Off-topic for the relayed user request. The user asked about 'the AI engine we discussed to assist with traffic, extras, generation of detail'. This proposal is about calibrating the physics test procedure and does nothing for AI traffic, extras or detail generation. It should not be presented as an answer to that question.
- Brake lag is counted twice. The 0.15 s bench ramp is described as 'tester foot plus hydraulic build', and a separate Powertrain tau of 0.08 s then adds hydraulic build again. Use one model. Either keep a physical pressure lag in Powertrain with a short tester-foot ramp in the bench (about 0.05 s), or a bench-only ramp with no Powertrain lag. Then derive the expected mu shift from the actual combined delay.
- mustang65's 264 ft target is not a measured stopping distance. Its sources say it is derived from Car Life's measured mean deceleration of 20 ft/s2 (v^2/2a = 263.5 ft), a plateau figure with no application delay. With the new ramp the Mustang stop gets 8-16 ft longer and leaves its ±5 ft band. The design sets the per-source procedure only for rollout. Braking needs one too, for example target_brake_basis 'distance' or 'plateau_decel', or a comparison against the reported 60-10 mean deceleration.
- Re-fitting static_mu alone breaks the skidpad gate. Lateral force uses mu*lateral_grip_scale, so a 5-10 % static_mu rise moves NB skidpad from about 0.884 toward 0.93-0.97 g, outside the ±0.02 band. It also shifts the tyre-limited launches (kestrel_s, meridian_z per docs/cars.md:29) and LockedBrakeTurn (22-32 m). lateral_grip_scale has to be re-fit with it, and 0-60 re-checked afterwards.
- Re-fitting only launch_rpm and clutch to absorb the rollout puts a residual error into an unrelated knob. With rollout applied, NB would read about 7.7 s against 7.97, so the car must get slower, and detuning the launch hides wherever the real excess is. That goes against the owner's 'realism first, general methods' rule. Use Car and Driver's rolling-start 5-60 and the quarter-mile trap speed, which do not depend on the launch, to find whether the excess is in the launch or in the powertrain before choosing what to re-fit.
- A Powertrain brake lag 'on every input path' also changes keyboard and pad braking, auto-hold release at standstill (service_brake logic at Powertrain.gd:269-278) and AbsReverse. ABS is downstream of brake_amount (Powertrain.gd:417-428), so it is unaffected only if the lag is applied to brake_amount before the per-wheel ABS multiplier. The parking brake (line 430) should bypass the lag. KeyboardTap does not test braking, so it is not a relevant gate. LiftOff does not use the brakes and is unaffected.
- The doc list is incomplete. The 'from rest without the 1 ft rollout' wording lives in docs/cars.md:21/:29 and in the CarTrack.gd header (lines 8-15, band text), and the change must also go in CHANGELOG. CONTRIBUTING.md holds only the Track.gd expected line (7.95 s / 173.0 ft), not band text.
- Adding rollout makes the time shorter, so the change only helps meridian_z (3.55 against 2.8). Every other car currently passing within ±0.15 s will fail until re-fit, so the gate must allow per-car calibration_open notes during the transition or land with the re-fit in the same change.
- depends_on 'dyn-verification-suite' could not be checked against this codebase. No such check exists in game/checks.

### Corrected design (USE THIS where it differs)
1) Rollout, measurement only, done first. Add CarSpec target_rollout_ft, default 0, set to 1.0 in the six Car and Driver cars, and 0 for mustang65 (Car Life 1965, from rest). In CarTrack.gd (lines 55-59) and Track.gd (lines 33-36), record the time at which distance travelled since pedal-down passes rollout_ft. Report a60_rest, a60_rollout and the delta, and gate on a60_rollout. Also print 5-60 mph and the quarter-mile time and trap speed where sources give them, to separate the launch from the powertrain. Do not re-fit yet. List each car's residual first.
2) Braking, one model of the delay. Option A, preferred for realism in play: a first-order brake pressure state in Powertrain.drive applied to brake_amount (tau about 0.06-0.08 s, CarSpec brake_rise_s), placed before the per-wheel ABS multiplier at Powertrain.gd:417 and bypassed for the parking brake. The bench keeps a step or a short 0.05 s foot ramp. Option B: a bench-only ramp of about 0.15 s with no Powertrain change. Both report the stopping distance and the mean deceleration from 60 to 10 mph separately. Add CarSpec target_brake_basis ('distance' for the Car and Driver cars, 'plateau_decel' for mustang65, whose 264 ft comes from a 20 ft/s2 mean). The plateau case is gated on mean deceleration, or on the stop with no application delay.
3) Re-fit only after diagnosis. Change static_mu together with lateral_grip_scale so skidpad stays within ±0.02 g, and re-run tyre-limited 0-60. Change launch_rpm and clutch only if 5-60 and the trap speed show the residual is in the launch. Otherwise look for the cause in the sourced powertrain (shift time, driveline efficiency, drag) and record it as calibration_open rather than detuning the launch.
4) Docs. Update the CarTrack.gd header, docs/cars.md:21/:29 (rollout and brake basis wording, before and after numbers), the Track.gd expected line in CONTRIBUTING.md, the meridian_z calibration_open text (rollout no longer an open cause), and CHANGELOG.
Gates: print the rollout delta per car; the CarTrack table on the rollout time; LockedBrakeTurn 22-32 m; AbsReverse; CombinedSlip within 3 %; TractionControlCheck; skidpad unchanged within ±0.02 g after any static_mu change; LiftOff and SBend unchanged.
This is a physics-verification item and does not address the AI-assist engine (traffic, extras, detail generation) the user asked about. It should be filed separately from that answer.
