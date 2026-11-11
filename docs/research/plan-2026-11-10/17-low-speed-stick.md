# 17. Real standstill: carcass-deflection tyre with a stick state, so cars hold on grades and stop jittering (low-speed-stick)
category physics · impact 3 · 2 d · depends: tyre-mf-combined-core, substep-chassis-coupling

## Rationale
Below 0.8 m/s the contact is a lateral damper with c·dt/m ≈ 1.04 at 60 Hz, so the lateral velocity changes sign every tick and the car creeps about 1 m/min on a 10 % cross slope. Real-road driving is full of stops, hill holds and parking, so this matters more for SimCruise than for a racing sim.

## Code claims (proposer)
Powertrain.gd:174 is target = atan2(vy, max(1.8,|vx|)). Powertrain.gd:179 makes the relaxation length smoothstep(0.8,3.5,speed), so it is 0 at low speed. The comment at :175 claims static friction, but no stick state exists. Powertrain.gd:445 Fx = Fz·12·Δv/1.5 below 1.5 m/s acts as a damper. Powertrain.gd:453-461 blends locked braked wheels below 1.5 m/s.

## Design (proposer)
1) Per-wheel carcass deflections (u_x, u_y), integrated implicitly in the substeps.
- du/dt = v_slip - (|vx|/σ)·u for sliding consistency at speed.
- F = k·u + d·du/dt, with k_x = Cx, k_y = Cy from the tyre block (TNO 381.9 / 157.6 kN/m, scaled).
- d sized from the implicit limit: d·ds/m_corner < 1.
2) Cap |F| at mu·Fz. When the cap binds the tyre slides, and u is clamped (Coulomb).
3) Blend from the deflection model below 2 m/s to the steady-state Tyre.gd above 4 m/s with a sine step, as in Chrono's 1-3 m/s blend. The same σ keeps them consistent.
4) The parking brake and P lock hold through this state.

## Gate
New ParkGrade check: each kept car on 10 % and 20 % longitudinal and cross grades with the parking brake or P. Displacement under 1 cm in 60 s. 0 lateral-velocity sign changes per second at standstill. Creep check at 1 km/h shows no oscillation. TurningCircle (within 0.20 m), AbsReverse, LaunchCheck, LockedBrakeTurn and CarTrack 0-60 launch remain in band.

## Files
game/Tyre.gd
game/Powertrain.gd
game/checks/ParkGrade.gd
docs/technical-overview.md

## Skeptic verdict
claims_hold false · already_done false · keep true

### Wrong claims
- The line numbers are off by 1 to 3. atan2(transverse, maxf(1.8, speed)) is at Powertrain.gd:175, not 174. The 'static friction' comment is at :176-177, the smoothstep(.8,3.5,speed) relaxation length is at :180, and the longitudinal law is at :448-449, not :445. The substance of each claim is correct.
- The claim that 'Fx = Fz·12·Δv/1.5 acts as a damper' is only partly true. Lines 448-449 solve the force implicitly against wheel inertia: fx = long_stiff·(spin·R − vx)/(1 + long_stiff·R²·ds/I). It acts as a damper on chassis velocity only when the brake holds the wheel at spin = 0 (line 431 move_toward, and line 468 holds it at zero). An unbraked wheel just rolls.
- The locked-wheel blend is backwards. At :456-466 lock_weight is multiplied by smoothstep(1.5, 3.0, |v|), so the blend is zero below 1.5 m/s and full above 3 m/s. It is not 'below 1.5 m/s'.
- 'The lateral velocity changes sign every tick' is true only for a transient on flat ground. With c·dt/m = 1.04 the update is v(n+1) = −0.04·v(n) + g_s·dt, so on a cross slope v settles to a constant positive creep, g_s·m/c, with no sign changes. The real defect is the creep, not jitter. A gate of '0 sign changes per second' would probably pass today and proves nothing.
- The design refers to 'the steady-state Tyre.gd' and puts game/Tyre.gd in its files list, but game/Tyre.gd does not exist. The lateral tyre law is inline at Powertrain.gd:345-348.
- 'The parking brake and P lock hold through this state': no P (Park) lock exists. The selector only covers forward and reverse (in_reverse, selected_gear -1/0/n). The parking brake is momentary: Input.is_action_pressed('parking_brake') at :430 acts only on the rear wheels and only while H is held, so nothing latches.
- There are no TNO Cx/Cy carcass stiffnesses (381.9 / 157.6 kN/m) anywhere in CarSpec.gd or game/cars/*.json, so 'k_x = Cx, k_y = Cy from the tyre block' has no data to read.

### Design problems
- The gap is real and its numbers check out. Below 0.8 m/s the lateral force is −fz·mu·1.105·tanh(10.8·atan2(vy,1.8)), so c/m_corner = g·mu·1.105·10.8/1.8 ≈ 62.6 /s for the NB, and 1.04 per 60 Hz tick (scripts/check.sh runs at 60). Steady cross-slope creep is about 1043·0.98/(4·16.3 kN·s/m) ≈ 0.016 m/s, roughly 0.9 m/min, less a small rolling-resistance cap. Holding a 10 % grade on the parking brake creeps about 1.6 m/min, and on the foot brake about 0.75 m/min. CarTurntable.gd:56 already presses the parking brake 'so it cannot creep' and then waits for |v| < 0.03 m/s, which shows the problem is known and worked around. Measure a baseline before building.
- The stiffness consistency is wrong. The current small-angle cornering stiffness is C_α = fz·mu·LATERAL_GRIP_SCALE·slip_stiffness (about 29 kN/rad per NB corner). With relaxation_m = 0.35 that requires k_y = C_α/σ ≈ 84 kN/m. The TNO 157.6 kN/m would give k·σ ≈ 55 kN/rad, almost 2x the calibrated tyre, so 'the same σ keeps them consistent' fails. k must be derived from the existing tyre law and σ, not imported.
- The damper criterion uses the wrong timestep. side[i] is computed once before the 6 substeps and the chassis force goes through apply_force once per Jolt tick (:474). Chassis velocity (row[5], row[6]) is frozen across substeps, so integrating u 'implicitly in the substeps' does not make the chassis coupling implicit. Stability is set by the tick dt: d·dt/m_corner should be kept to about 0.5, and k·dt²/m_corner well under 1. The ds-based criterion d·ds/m < 1 would allow a damper 6x too stiff and reproduce the overshoot.
- TurningCircle, in the default suite, runs at SPEEDS = [1.0, 1.8] m/s with a 0.20 m gate. That is squarely inside the proposed below-2 m/s deflection regime, and it currently depends on the atan2 1.8 m/s floor. It is the highest-risk gate, not just a gate to keep in band.
- Low-speed handling already has overlapping speed regimes: the 0.8-3.5 relaxation smoothstep, the 1.5 m/s floors for long_stiff and TC slip, the ABS cutoff above 1.5 m/s and the 1.5-3.0 lock blend. A separate 2-4 m/s sine blend to 'Tyre.gd' adds another, against the owner's general-methods-not-special-cases rule. du/dt = v − |vx|·u/σ already is the first-order relaxation the code uses at speed, with u = σ·tanα. One state can replace rolling_slip at all speeds with no blend.
- The dependencies are overstated. Nothing here needs tyre-mf-combined-core. It can be done inline in Powertrain.gd on the existing lateral law and budget ellipse. substep-chassis-coupling is a nice-to-have, not a prerequisite, if the damper is sized on the tick dt.
- The ParkGrade gate tests a 'P' that does not exist. Either add a latching parking brake or a Park gear as a separate item, or gate on the held parking brake, as CarTurntable does with Input.action_press, and on the foot brake.
- u_x on driven and unbraked wheels has to act on slip velocity (ωR − vx) and couple to wheel inertia: about 12 kg equivalent at 381.9 kN/m is about 180 rad/s, which needs the implicit substep solve. Otherwise the change risks LaunchCheck and the CarTrack 0-60 start. Applying the stick state only to the chassis-damper part (braked or held wheels, and lateral) lowers that risk.
- Scope note for the orchestrator: the relayed user request asks about the AI engine for traffic, extras and detail generation, not low-speed tyre physics. CHANGELOG.md already documents an optional local Ollama --traffic-llm persona feed in TrafficDirector.gd. This proposal does not answer that request.

### Corrected design (USE THIS where it differs)
Do it inline in Powertrain.gd, without a new Tyre.gd and without depending on tyre-mf-combined-core.

1) Baseline first. Add checks/ParkGrade.gd and measure the current creep for each kept car on 10 % and 20 % longitudinal and cross grades. Run it held three ways: on the foot brake, on the held parking brake (Input.action_press, as in CarTurntable) and with no brake on the cross slope. The expected order is 0.75-1.6 m/min.

2) Lateral: replace the rolling_slip lag with one per-wheel bristle deflection u_y, valid at all speeds with no low-speed cutoff:
- du_y/dt = v_y − (|v_x|/σ)·u_y, integrated exactly over the tick with an exp factor, as contact_slip does now.
- σ = relaxation_m·load_scale. Drop the smoothstep(.8,3.5) and the 1.8 m/s floor.
- k_y = C_α/σ, with C_α = fz·mu·LATERAL_GRIP_SCALE·slip_stiffness from the existing law, so k_y·σ equals today's cornering stiffness.
- Force: F_y = −fz·mu·LGS·tanh(k_y·u_y/(fz·mu·LGS))·falloff − d_y·v_y·w(v). The damper d_y is sized on the Jolt tick, with d_y·dt/m_corner ≤ 0.5 and w fading out above about 2 m/s. Check k_y·dt²/m_corner < 1 for every car.
- At speed u_y/σ = tanα, so steady cornering and the relaxation dynamics are unchanged by construction. That protects SteerTransient, SBend, KeyboardTap and LiftOff.

3) Longitudinal stick only where the wheel is held: the brake holds spin at 0 (line 468 path), or a future P. Integrate u_x on the contact-patch slip velocity (ωR − v_x) with k_x = fz·longitudinal_stiffness/σ_x and the same tick-dt damper rule. Leave the rolling and driven-wheel implicit solve (:448-449) as it is.

4) Coulomb: pass the spring force through the existing combined budget ellipse (fxmax, LATERAL_GRIP_SCALE). When the ellipse binds, clamp u back to the boundary so it slides. Release the hold when |F_x·R| exceeds the brake torque.

5) Reset u in restart() and whenever a wheel loses contact, as rolling_slip is now.

6) Treat Park and a latching parking brake as a separate small item.

Gates:
- ParkGrade: under 1 cm in 60 s on 10 % and 20 % longitudinal and cross grades with foot brake or held parking brake. Report the creep rate rather than sign changes.
- A 1 km/h creep run with no growing oscillation.
- TurningCircle at 1.0 and 1.8 m/s within 0.20 m, the highest risk.
- LockedBrakeTurn, AbsReverse, LaunchCheck, KeyboardTap, SteerTransient, SBend, LiftOff and CarTrack (0-60, 70-0, skidpad, all kept cars) in band.

Document it in technical-overview.md and CHANGELOG.md. Effort: about 2 days.
