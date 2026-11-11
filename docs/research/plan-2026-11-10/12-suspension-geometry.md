# 12. Suspension geometry that matters: roll centres, anti-dive/squat, camber effect on grip, bump/roll/compliance steer, live rear axle for the Mustang (suspension-geometry)
category physics · impact 4 · 4 d · depends: tyre-mf-combined-core

## Rationale
Every tyre force acts at ground level, so roll centres sit at ground and anti-geometry is zero. That makes all load transfer elastic: the NB sits 14 mm into its front bump rubbers on every hard stop, and front/rear roll-centre balance cannot exist. Camber only adds a small thrust and never changes peak mu, so docs/cars.md records geometry moving results by at most 0.11. The 1965 Mustang's leaf-spring live axle is modelled as two independent corners.

## Code claims (proposer)
Powertrain.gd:474 applies Fx and Fy at contact point p. CarSpec has no roll-centre or anti field (grep). Powertrain.gd:313-315 computes camber, used only in :348 camber thrust, and :345 mu has no camber term. Powertrain.gd:312 toe is static. mustang65.json has rear camber_per_m 0 and rear bar 0, with independent rear springs at :302-324. Audit math: the NB roll gradient is 4.8 deg/g and 0.95 g braking compresses each front 68 mm against a 54 mm bump gap.

## Design (proposer)
1) Per-axle roll_centre_height_m and anti_dive and anti_squat fractions (defaults 0 reproduce today exactly).
- Lateral force is applied at p + n·h_rc(axle) instead of p, which gives geometric transfer and jacking automatically.
- Fx is applied at p + n·(anti%·h_cg·share).
- NB values from hardpoint data or measurement, otherwise an estimate of front about 0.05 m, rear about 0.10 m, flagged as such.
2) Camber factor on peak mu: mu·(1 - c_gamma·(gamma_eff - gamma_opt)²), with gamma_eff = wheel camber + road lean (already computed). Radials use c_gamma about 0.6/rad² and gamma_opt about -1.5 deg (estimate).
3) bump_steer_deg_per_m and roll_steer per axle, from (static_length - len). Rear compliance toe-in deg per kN·Fy with a one-tick lag.
4) rear_suspension 'live_axle' (Mustang 65):
- Roll centre at leaf-eye height, about 0.35 m (estimate from the 1965 shop manual).
- Both wheels share camber equal to axle roll relative to the road, from the travel difference over the track.
- Roll steer about 0.05 deg/deg of understeer from leaf wind-up (estimate).
5) A new RollPitchCheck: a steady 40 m circle at 0.3/0.5/0.7 g, 0.8 g braking and a WOT launch. Reports roll and pitch deg/g, LLTD, and bump-stop contact time.

## Gate
With defaults 0, every check is bit-identical (this is verified first). After real values:
- RollPitchCheck: NB roll 4-6 deg/g, pitch under 3 deg/g in braking, bump-stop contact under 30 % of a 0.95 g stop. Mustang roll 5-8 deg/g.
- K_us and StepSteer stay within the body-class bands.
- CarTrack re-fit using lambda_mu only.
- LiftOff, SBend, KeyboardTap and SteerTransient pass.

## Files
game/Powertrain.gd
game/CarSuspension.gd
game/CarSpec.gd
game/cars/nb1999.json
game/cars/mustang65.json
game/cars/meridian_z.json
game/checks/RollPitchCheck.gd
docs/cars.md

## Skeptic verdict
claims_hold true · already_done false · keep true

### Wrong claims
- 'Camber is used only in :348' is true for physics only. Drive.gd:692 also reads wheel_camber to draw the wheels, so a live-axle camber change would move the visuals too.
- The '0.11' figure comes from the CHANGELOG line 12 / docs/cars.md:41 LiftOff experiments: brake bias, diff, toe, rear camber gain, load sensitivity, inertia and CG height changed the 0.7 g yaw-rate ratio by at most 0.11. It covers those specific experiments, not geometry in general, and it does not show that camber cannot matter.
- Treating the NB's front bump-rubber contact on a 0.95 g stop as a defect is not established. The arithmetic is right: 1043 kg x 0.95 g x 0.435/2.266 = 933 N per front wheel, over a 13.6 kN/m wheel rate (168 lb/in x 0.68^2) = 68.6 mm, against the 54 mm gap. But docs/cars.md:43 sources the 51 mm gap from owners, and stock NB bump rubbers are long progressive foam that works as part of the spring. Brushing them on a hard stop may be realistic. The 30 % gate has no source.
- The 4.8 deg/g roll gradient checks out when the full mass is used (front springs 13.6k + front bar about 20.5k + rear springs 14.0k + rear bar about 5.1k = 53.2 kN.m/rad). So the proposed gate 'NB roll 4-6 deg/g' already passes today. Adding roll centres of 0.05/0.10 m cuts it to roughly 4.0 deg/g, which is borderline-failing. The gate does not tell the change apart from today's code.
- 'CarTrack re-fit using lambda_mu only': no lambda_mu exists. The fit knob is CarSpec.static_mu, so this gate depends entirely on the unbuilt tyre-mf-combined-core.

### Design problems
- The core mechanism is physically wrong in this model. Tyre Fz is the spring + ARB load (CarSuspension.contact_load + roll_bar_rate, row[2]), and every force goes to one rigid body. Moving Fy to p + n*h_rc only shrinks the roll moment on the body. Springs then carry m*ay*(h_cg-h_rc)/t, and that is all the tyre model ever sees. The geometric share of load transfer disappears instead of being added. The result is the same as lowering the CG: the outside tyre gets less load, load sensitivity costs less grip, and the skidpad rises.
- It does not give 'jacking automatically'. A horizontal force at a different height adds a moment and no vertical force. Jacking needs an explicit vertical link force on each wheel.
- Per-axle roll-centre balance does not appear either. The total moment reduction, Fy_f*h_rc_f + Fy_r*h_rc_r, is shared between axles by roll stiffness, not given to each axle's own tyres. So front/rear roll-centre height cannot tune LLTD the way it does in a real car.
- Anti-dive/squat by raising the Fx application point has the same flaw. Front tyre Fz in braking would fall below the physical m*ax*h/L, which lengthens 70-0, shifts ABS peak-slip behaviour and changes the 0-60 launch traction. Also, 'anti%*h_cg*share' is not a standard or well-defined formula.
- The camber-on-mu sign is wrong. wheel_camber[i] = -sign_side*camber is signed in the body frame, but (gamma - gamma_opt)^2 with gamma_opt = -1.5 deg needs camber signed against each tyre's lateral force. Otherwise the inside and outside wheels get opposite benefit, and the result flips with turn direction.
- road_lean (body roll relative to the road) is added to camber for every axle. For a live axle the wheels stay roughly square to the road, so body roll must be left out there. The proposal says this for the Mustang but not as a general rule in the camber term.
- The live-axle design leaves out the leaf-spring base. The Mustang's rear roll stiffness should come from the spring spacing (about 1.0-1.1 m), not the 1.42 m track that wheel_rate_n_m implies today. It also leaves out the heavy shared unsprung axle (and wheel hop or axle wind-up under launch), which affects the 0-60 and launch gates.
- Feeding a geometric Fz that depends on Fy creates a loop: Fy depends on Fz. That needs the previous tick's Fy/Fx (row[8], net[]) or one extra iteration, with a clamp. The proposal names a one-tick lag only for compliance steer.
- Several constants are guesses with no source. c_gamma 0.6/rad^2 and gamma_opt -1.5 deg are radial guesses, and the Mustang is on 6.50-14 bias-ply tyres. The NB roll-centre heights, the pitch gate under 3 deg/g (today about 3.7 deg/g on springs alone), the bump gate under 30 % and Mustang roll steer 0.05 are all unsourced. That conflicts with the owner's 'drive like the real car' sourcing standard.
- The gate list leaves out PadSteer, SplitGrip, CombinedSlip, RideCheck, RearDamperCheck, Track.gd and RoadDrive bump_stop counts. All of them read Fz, so all would move.

### Corrected design (USE THIS where it differs)
Keep every force applied at p, which is the correct point for a body with no unsprung masses. Add geometry as explicit per-wheel vertical link forces.

1) Force-based roll centre and anti-geometry. For each wheel i:
   Fz_geo_i = Fy_i(prev) * h_rc[axle] / (t_axle/2) + Fx_i(prev) * tan_anti[axle] (separate dive and squat/lift values by force sign).
   Fx and Fy are last tick's values from net[]/row[8], or one fixed-point iteration. Add Fz_geo_i to row[2] after the ARB term and clamp at 0 and max_contact_load_n. That one value then feeds the tyre model (mu, Fx/Fy limits) and the body's normal force. Springs carry only the elastic share, so roll and pitch drop by the right amount. Total transfer stays m*a*h/t. Each axle's geometric transfer goes to its own tyres, so LLTD responds to roll-centre heights. Net jacking comes out as sum(Fz_geo) != 0. With defaults of 0 the added term is exactly 0.0, so everything stays bit-identical. Verify that first with the full check suite.

2) Camber on peak mu. Take gamma relative to the road, signed against each tyre's Fy direction: gamma_rel = (wheel_camber[i] + k_body_roll[axle]*road_lean) * -sign(Fy_i). Set k_body_roll to 1 for independent suspension and 0 for a live axle. Then mu *= 1 - c_gamma*(gamma_rel - gamma_opt)^2, with c_gamma defaulting to 0. Source c_gamma and gamma_opt per tyre type (radial or bias) from published Fy-vs-camber data, or flag them as estimates in sources/estimates. Fold this into tyre-mf-combined-core rather than stacking another ad hoc factor.

3) Bump steer and roll steer per axle from (static_length - len) and from the side-to-side travel difference. Rear compliance toe in deg/kN from the lagged Fy. All default 0.

4) rear_suspension = 'live_axle':
   - camber from the travel difference over the track; k_body_roll = 0
   - roll stiffness from spring_base_m (leaf spacing) instead of the track
   - roll centre via (1) at the leaf-eye height
   - roll steer from axle roll
   - optional shared axle unsprung mass later
   Re-run the Mustang's CarTrack 0-60 (launch squat) as well as the skidpad.

5) RollPitchCheck. Report roll deg/g (body and per-axle elastic vs geometric), pitch deg/g in braking and launch, LLTD, jacking height and bump-stop time.
   - Gates should be relative to today's numbers plus sourced targets wherever any exist (e.g. published NB roll gradients). Do not use bands the current code already meets.
   - Keep CarTrack within its existing tolerances by refitting static_mu (lambda_mu only after the MF core lands).
   - Keep LiftOff, SBend, KeyboardTap, SteerTransient, PadSteer, SplitGrip, CombinedSlip, TractionControlCheck, AbsReverse, RideCheck, RearDamperCheck and Track passing, and RoadDrive bump_stop ticks on Dragon/NY-218 not increasing.
   - Update docs/cars.md, technical-overview, roadmap and CHANGELOG.
