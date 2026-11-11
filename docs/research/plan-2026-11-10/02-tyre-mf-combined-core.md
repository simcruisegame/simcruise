# 2. Data-backed tyre: Magic Formula/TMeasy pure + combined slip, saturating load stiffness, per-construction defaults, aligning torque (tyre-mf-combined-core)
category physics · impact 5 · 6 d · depends: dyn-verification-suite, test-procedure-fidelity

## Rationale
The lateral curve peaks at 14.2 deg and reaches only 0.64 of peak at 4 deg. Its cornering stiffness is about 11.5/rad, roughly half that of a real radial, so the steering assist adds 5 deg of surplus to compensate. Every car has the same curve shape (a bias-ply 1965 tyre equals a 305/30ZR20). Combined slip is a post-hoc ellipse clip. There is no Mz, so the steering carries no tyre information. This is the single largest gap between the sim and 'drives like the real car'.

## Code claims (proposer)
Powertrain.gd:346 has Fy=-Fz*mu*Lscale*tanh(10.8*alpha)*(1-0.13*smoothstep(.18,.65,|alpha|)). Powertrain.gd:345 has mu(Fz) linear in load, so C_alpha stays linear up to 5.8*Fref. Powertrain.gd:335-348 computes the lateral force once per tick, before the 6-substep loop at :357. Powertrain.gd:447-448 is the ellipse clip of (fx, fy/Lscale). Powertrain.gd:437 versus :445 use different kappa denominators (max(|vx|,5) against max(1.5,|vx|)). CarSpec.gd:53-58 shares slip_stiffness, longitudinal_stiffness 12 and relaxation_m 0.35 across cars. CarSpec.gd:85 has LIMIT_SLIP_SURPLUS 5 deg. grep finds no aligning, pneumatic or trail term in game/*.gd outside checks.

## Design (proposer)
1) New game/Tyre.gd: a pure, stateless function plus a per-wheel state struct, so it can be unit-tested headless.
- Lateral, MF 6.x structure: Fy0 = D*sin(C*atan(B*a - E*(B*a - atan(B*a)))).
  - D = Fz*mu_y(Fz)*surface_grip*axle_grip.
  - mu_y = PDY1*(1 + PDY2*dfz)*lambda_mu, with dfz = (Fz - Fz0)/Fz0.
  - K_alpha = PKY1*Fz0*sin(PKY4*atan(Fz/(PKY2*Fz0)))*lambda_K*(1 - PKY3*|gamma|), giving saturating stiffness.
  - B = K_alpha/(C*D). A lower surface mu therefore gives an earlier and lower peak.
- Longitudinal: the same family with PKX1 (Kx/Fz 18-22), PCX1 about 1.55-1.65, and a sliding/peak ratio of 0.72-0.80.
- Baseline shape: the TNO 205/60R15 MF6.2 set (PCY1 1.333, PEY1 -0.82, PKY1 -15.3, PKY2 1.70, PDY2 -0.064, PKX1 21.7). Check its provenance first. If it cannot be redistributed, use Chrono's BSD-3 PAC2002 files or the Chrono TMeasy GuessPassCar70Par pattern (dFy0 15.98 pn, Fym 1.001 pn at s_ym 0.149, Fys 0.834 pn).

2) Per-car tyre block in the JSON: construction (bias, touring, summer_uhp), size, Fz0, and optional overrides. CarSpec derives the defaults from construction and aspect ratio:
- Bias 6.95-14 (Mustang 65): K_alpha/Fz about 9/rad, peak about 11 deg, sigma_alpha 0.7 m.
- NB 195/50R15 summer: about 17/rad, peak 7 deg, sigma 0.35 m.
- C8 PS4S 245/35ZR19 front, 305/30ZR20 rear: about 22/rad, peak 5.5 deg, sigma 0.28 m.
- Each value is written to the JSON with an 'estimate' source.

3) Combined slip, TMeasy similarity:
- s_n = sqrt((kappa/kappa*)^2 + (tan(alpha)/alpha*)^2), F = F_curve(s_n).
- Components follow the normalised slip direction: Fx = F*(kappa/kappa*)/s_n, Fy = F*(tan(alpha)/alpha*)/s_n*ellipse_axis.
- Evaluated inside the substep loop with that substep's kappa. The implicit wheel solve at :446 linearises dF/dkappa around the current kappa.
- One kappa denominator: max(|vx|, R*|omega|, 1.0).

4) Keep the exact-exponential relaxation, with sigma = K_alpha/C_carcass_y so that it depends on load.

5) Aligning torque per front tyre:
- t(alpha) = t0*max(0, 1 - |alpha|/alpha_t)^2 with alpha_t about 0.55*alpha*. t0 = QDZ1*R0 is about 28 mm for the 205/60R15, scaled by width.
- Mz = -t*Fy, plus a mechanical trail of R*sin(caster) times Fy.
- Exposed as rack_force_n = Σ(Mz)/arm. It is a signal only in this proposal and is not applied to the body.

6) Re-calibrate only lambda_mu per car and axle against the CarTrack skidpad and 70-0 bands. The curve shape is never hand-tuned.

7) After the re-calibration, cut LIMIT_SLIP_SURPLUS_DEG from 5 to about 2 deg, or derive it per car as alpha* minus the Ackermann term.

## Gate
TireStep extension:
- Sweep alpha 1-20 deg at 0.5, 1 and 2 Fz0. Gates: alpha_peak 6-8 deg for NB and C8, 10-12 deg for the Mustang. Share of peak at 4 deg at least 0.85 for the radials. K_alpha(2Fz0)/K_alpha(Fz0) at most 1.6.
- Mz peak at 0.4-0.6 of alpha_peak.
- Fy/Fy0 at kappa = 0.05, alpha = 4 deg in 0.80-0.92.
Calibration and transients:
- CarTrack 0-60/70-0/skidpad bands stay in band for all kept cars.
- LiftOff (no spin at 0.5 g or below), SBend, KeyboardTap (under 30 deg heading), PadSteer, CombinedSlip, TractionControlCheck and AbsReverse all pass.
- The new K_us and T_r stay within the dyn-verification-suite body-class bands, and the convergence table stays under 2 %.

## Files
game/Tyre.gd
game/Powertrain.gd
game/CarSpec.gd
game/cars/nb1999.json
game/cars/mustang65.json
game/cars/meridian_z.json
game/checks/TireStep.gd
game/checks/CombinedSlip.gd
docs/cars.md
docs/technical-overview.md

## Skeptic verdict
claims_hold false · already_done false · keep true

### Wrong claims
- The line numbers have drifted. The ellipse clip is at Powertrain.gd:450-451, not 447-448. The two kappa denominators are at :439 (max(|vx|,5)) and :448 (max(1.5,|vx|)), not :437/:445. :441 has a third one, the TC slip (max(|vx|,1.5)). relaxation_m is at CarSpec.gd:59, outside the cited 53-58.
- 'C_alpha stays linear up to 5.8*Fref' is wrong. mu(Fz) is linear in load (:345), but C_alpha = 10.8*Fz*mu(Fz)*Lscale, which is quadratic and degressive. Unclamped, it peaks at 5.76*Fref. For the NB the mu_min 0.7 clamp takes over at about 3.9*Fref, and C_alpha grows linearly again from there. C(2Fref)/C(Fref) is 1.81. The underlying point stands: there is no real saturation across the useful load range.
- 'Peak at 14.2 deg' is numerically right: max of tanh(10.8a)*(1-.13*smoothstep(.18,.65,a)) is 0.983 at 0.248 rad. It is misleading, though. tanh has no peak, and the curve is already at 97.5% of max by 10.3 deg and 95% by about 8.9 deg, so the '14.2 deg' peak is an artifact of the falloff window. The share at 4 deg is about 0.65, which the proposal states correctly.
- Item 4 implies relaxation does not depend on load. It already does: Powertrain.gd:179 scales SIDE_RELAXATION_M by (Fz/Fref)^relaxation_load_exponent (0.5, CarSpec.gd:75).
- The Mustang tyre is not 6.95-14. mustang65.json records '6.50-14 bias-ply (Car Life test car)'.
- The claims that do hold: Fy is computed once per tick before the 6-substep loop at :357; the ellipse clip is post hoc; there is no Mz, aligning, trail or rack-force term anywhere in game/*.gd; LIMIT_SLIP_SURPLUS is 5 deg (CarSpec:85); cornering stiffness is about 11.5/rad (10.8 * 0.963 * 1.105); and slip_stiffness, falloff, longitudinal_stiffness and relaxation_m are set only in nb1999.json (to the defaults), so all 7 cars share the curve shape and only mu, lateral scale and axle_grip differ.

### Design problems
- The design contradicts its own gates on aligning torque. With t(a) = t0*(1-|a|/a_t)^2 and Fy roughly linear early, Mz ∝ a*(1-a/a_t)^2 peaks at a_t/3. With a_t = 0.55*alpha*, that is about 0.18*alpha*, so the gate 'Mz peak at 0.4-0.6 of alpha_peak' fails by construction. Use the TMeasy trail shape instead (linear-to-zero with an S-shaped tail, or MF's Dt*cos(Ct*atan(Bt*a...))) and set the crossover so that the Mz peak lands at about 0.4-0.5 alpha*.
- The baseline shape and the stated per-construction peaks are inconsistent. With PCY1 1.333 and PEY1 -0.82, the MF peak sits where B*a ≈ 1.8, so alpha* ≈ 1.8*C*mu_eff/(K/Fz). NB (mu_eff ≈ 0.963*1.105 = 1.06, K/Fz 17) gives about 8.5 deg, not 7, and fails the 6-8 gate. C8 (mu_eff ≈ 1.17, 22/rad) gives about 7.3 deg, not 5.5. Mustang (0.856, 9/rad) gives about 13 deg, not 11. The C8 target of 5.5 deg also violates the gate's own 6-8 band. Hitting the targets needs E or K tuned per construction, which contradicts 'curve shape never hand-tuned'.
- The combined slip is a hybrid that is not fully specified. It puts pure MF curves into a TMeasy similarity norm and adds an 'ellipse_axis' fudge. TMeasy also needs the combined curve parameters (dF0, FM, sM, FS) blended by slip direction, and the proposal does not define that. Pick one model: either full TMeasy (pure plus combined) or MF with Gxa/Gyk weighting functions.
- CombinedSlip.gd fails any non-ABS car (NB, Mustang) whose combined grip moves more than 3% from the MAIN constants, and its header says such a change needs a published measurement. A new combined model will move the NB and Mustang envelopes. The gate 'CombinedSlip passes' is therefore unlikely without rebaselining MAIN, which the check's own policy forbids without data.
- The kappa unification max(|vx|, R*omega, 1.0) changes three things at once. It lowers the implicit-solve floor from 1.5 to 1.0 m/s, which raises low-speed stiffness 1.5x in the solve that keeps standstill and launch stable. It changes ABS target tracking, which uses kappa with a floor of 5. It changes TC's slip ratio, which uses a floor of 1.5. TractionControlCheck, AbsReverse, LaunchCheck and the CarTrack 0-60 times are all sensitive to this. Unify kappa for the force law only, keep each controller's own measurement floor, and keep the 1.5 floor in the implicit solve unless a standstill or creep check passes.
- A nonlinear curve evaluated per substep, with an implicit solve 'linearised around current kappa', is only stable if dF/dkappa is evaluated at the end-of-substep kappa. Past the peak dF/dkappa goes negative, so the implicit denominator (1 + k*R^2*ds/I) can approach zero or flip sign. Clamp the linearised stiffness at a minimum of zero, or use the secant to the curve.
- Moving Fy into the substep loop adds little. The slip angle comes from body velocity, which only changes per Jolt tick, and the existing clip already re-runs per substep with that substep's fx. The real gain is the coupling through kappa only.
- LiftOff, KeyboardTap and SBend risk. A stiffer tyre (17-22/rad against 11.5) that peaks earlier and falls to a sliding level of 0.80-0.83 (against the current 13% fall-off past 10 deg) makes the rear saturate sooner and lose more once past the peak. The gates 'no spin at <=0.5 g' and 'heading under 30 deg' may fail before LIMIT_SLIP_SURPLUS is retuned. Expect the assist cap and the yaw-transient checks to need retuning together with the tyre, not after it.
- Camber is double-counted. The design adds PKY3 camber reduction of K_alpha, but the existing camber_thrust term (:348) still adds lateral force from camber plus road lean. Decide which model owns camber.
- Mz has no consumer. There is no force-feedback path in the code (no ffb, rack_force or self-align anywhere), and input is keyboard, pad or wheel through the Input map. Exposing rack_force_n as a signal only has zero drive-feel impact until it feeds steering self-centering or the assist cap, or until FFB exists. Either defer item 5 or give it a concrete consumer.
- Calibration mapping: 'only lambda_mu per car and axle' must stay two handles, lambda_mu_x for 70-0 (today static_mu) and lambda_mu_y for the skidpad (today lateral_grip_scale). A single lambda cannot hit both CarTrack bands.
- Provenance: the TNO 205/60R15 MF6.2 .tir set ships with commercial tools (MATLAB, Adams), so redistribution is doubtful. Use Chrono's BSD-3 TMeasy or PAC files from the start. The 'estimate' K/Fz values per construction need cited sources (Pacejka's book, or published Calspan/TIRF data) to meet the owner's realism-first rule.
- Dependencies 'dyn-verification-suite' and 'test-procedure-fidelity' do not exist in the repo. They are other proposals, so the K_us/T_r band gate cannot be evaluated yet.
- Scope: the relayed user request asks about an AI engine for traffic, extras and generating detail. This tyre proposal does not address that request at all, so it should not be presented as the answer to it.

### Corrected design (USE THIS where it differs)
Phase A, as a pure, headless-tested Tyre.gd:
- Adopt full TMeasy, after Rill / Chrono BSD-3 (dF0, sM, FM, sS, FS per direction, with load interpolation between Fz0 and 2*Fz0). Use its own combined-slip blending. Do not mix MF and TMeasy.
- Per-construction parameter sets live in CarSpec, are written to JSON with sources, and keep two grip scalars, mu_x and mu_y, mapped from today's static_mu and lateral_grip_scale.
- Set the per-construction alpha* targets from the chosen parameter set, then gate on them. Use bands: radial 6-9 deg, bias 10-13 deg.
- Pneumatic trail uses TMeasy's n(s) form, with the Mz peak gated at 0.35-0.55 alpha*.

Phase B, integration in Powertrain.gd:
- Keep the per-tick relaxation (already load-scaled at :179).
- Evaluate the combined force per substep.
- The implicit wheel solve uses the secant stiffness, clamped at 0 or more, and keeps the 1.5 m/s floor.
- Unify kappa only for the force law, and leave ABS's and TC's own slip measures as they are.
- Remove camber_thrust only if TMeasy or the chosen model carries camber; otherwise keep it and leave out PKY3.

Phase C, recalibration:
- Fit mu_x to 70-0 and mu_y to the skidpad per car and axle.
- Then retune limit_slip_surplus per car as alpha* minus a margin, together with KeyboardTap, SBend and LiftOff.
- CombinedSlip's MAIN constants are rebaselined only with a cited combined-slip measurement. Otherwise accept the non-ABS cars at no more than 3% from today's figures.

Phase D:
- Mz drives a concrete consumer, either self-centering of the assisted rack (return rate scaled by rack force) or FFB when that exists. Otherwise defer it.

Gates:
- TireStep sweep: radial alpha* inside its band; share at 4 deg of at least 0.85; K(2Fz0)/K(Fz0) at most 1.6; Fy/Fy0 at kappa 0.05 and alpha 4 deg in 0.80-0.92.
- All CarTrack bands, LiftOff, SBend, KeyboardTap, PadSteer, TractionControlCheck, AbsReverse and LaunchCheck pass.
- The CombinedSlip policy is unchanged.
