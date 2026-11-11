# 4. First-class Xbox feel: state-aware countersteer allowance, caster self-centring, pedal shaping, per-car steering ratio, 1:1 wheel mode (pad-steering-pedals)
category feel · impact 5 · 3 d · depends: none

## Rationale
The owner plays on an Xbox pad. The speed-only rack cap also caps countersteer, so a slide past about 8 deg cannot be caught. Keyboard pedals are 0-to-1 steps, which lock the Mustang's brakes and dump full torque in one substep. The visual wheel uses one 14.5 ratio for every car. Real cars catch slides because the wheel self-centres toward the front-axle velocity, and none of that exists today.

## Code claims (proposer)
Powertrain.gd:231-238: available_steering depends only on |speed|, with limit = atan(L*0.95g/v^2) + 5 deg, which is 8.0 deg at 20 m/s on the NB. Powertrain.gd:282,294 apply the same cap to countersteer. Powertrain.gd:246-251 is the thumbstick shaping, :264 the rate limits 2.75/4.4. Drive.gd:666 passes the trigger and key strengths raw. Powertrain.gd:274-278 have no pedal slew. Drive.gd:104 STEERING_RATIO=14.5 is shared by every car. Powertrain.gd:233-235: Direct mode scales the rack by 1/(1+0.044v). The clutch is a digital action (Drive.gd:246, Powertrain.gd:280-281).

## Design (proposer)
1) Countersteer allowance in available_steering(speed, assisted, state):
- Front-axle velocity angle beta_f = atan2(v_lat + r*a_f, v_long).
- When sign(steer_input) != sign(yaw_rate) or |beta_f| > 2 deg, cap_counter = clamp(|beta_f| + surplus, cap, PARKING_RACK_RAD), applied only in the opposing direction.
- Rack rate while countersteering up to 40 deg/s at the rack (about 600 deg/s at the wheel).
2) Caster self-centring for pad and keys when the input is released: the rack returns toward beta_f, the zero-aligning-moment angle, not toward 0.
- The rate is proportional to rack_force_n from the tyre core, capped at 30 deg/s. Until the tyre core lands, a plain kinematic return toward beta_f.
- This is what lets a pad player hold a drift without micro-managing the stick. It is a driver-side filter; yaw inertia and grip are never touched.
3) Pedals:
- Keys: rise 4/s throttle and 6/s brake, release 8/s. Applied in Drive.gd before vehicle.drive(), for digital events only.
- Triggers stay linear, with an optional gamma preference (1.0-2.0, default 1.0) and a 0.04 deadzone.
- In manual mode, an optional analog clutch on LT with brake moved to LB. A two-stage digital release: fast to the bite point, then 0.8/s through the friction zone.
4) CarSpec steering_ratio and lock_to_lock_turns.
- C8: 15.7:1, 2.5 turns (GM).
- Mustang 65: manual box 19.9:1, 4.6 turns, or the power option 16:1, 3.75 turns.
- NB: from the factory service manual, flagged unverified until sourced.
- Used by the Drive.gd:694 visual wheel and the Cockpit.gd wheel.
5) A new 'wheel' input profile: rack = wheel_deg/steering_ratio with no cap and no slew. The bench Direct map stays for the checks.
6) DrivingPreferences entries: steering_assist (assisted/direct), pad_speed_sensitivity, trigger_gamma.

## Gate
New checks/SlideCatch.gd:
- NB and C8 at 20 m/s. Lift-off in a 0.85 g corner, or a 0.4 s rear surface_grip drop to 0.6, then full opposite stick.
- Gate: recovers from 15 deg of beta at 45 mph (yaw rate under 5 deg/s within 3 s). Before the change, expect a spin.
KeyboardTap gains pedal cases:
- A 0.3 s brake tap at 45 mph on the Mustang gives peak front slip under 0.3.
- The existing heading-under-30 and roll-under-10 gates hold.
Other gates:
- PadSteer and LiftOff pass.
- CarTrack bands are unchanged, because the benches use the Direct map and scripted pedals.
- A synthetic wheel axis at 90 deg gives exactly 90/ratio deg of rack at 5, 15 and 30 m/s.

## Files
game/Powertrain.gd
game/Drive.gd
game/CarSpec.gd
game/DrivingPreferences.gd
game/Cockpit.gd
game/cars/nb1999.json
game/cars/mustang65.json
game/cars/meridian_z.json
game/checks/SlideCatch.gd
game/checks/KeyboardTap.gd
docs/controls.md

## Skeptic verdict
claims_hold false · already_done false · keep false

### Wrong claims
- Mustang 65 steering numbers contradict the repo's own sourced data. game/cars/mustang65.json:159 records '21.7:1 manual steering, 3.73 turns'. The proposal's '19.9:1, 4.6 turns' would replace sourced data with unsourced data. 4.6 turns at 19.9:1 also implies about 41.6 deg of rack against parking_rack_deg 33.
- 'The clutch is a digital action' is only half true. The input is digital: Shift and LB (Drive.gd:234,246). But Powertrain.gd:281 already slews clutch_pedal at 14/s in and 2.8/s out, so a staged release builds on an existing ramp. It is not new.
- Design item 3 says 'triggers ... a 0.04 deadzone', but Drive.gd:239 already sets a 0.04 deadzone on both triggers.
- 'The visual wheel uses one 14.5 ratio' is incomplete. Drive.gd:104 STEERING_RATIO is one copy. Cockpit.gd:166 has a separate hard-coded literal (car.steering * 14.5) and uses rack angle, not wheel_heading, so there are two sources to unify.
- The 'tyre core' and rack_force_n do not exist anywhere in game/. caster_deg is used only for camber gain at Powertrain.gd:315. Caster self-centring therefore depends on unbuilt work, yet depends_on is []. The 'kinematic fallback' is the whole feature.
- Minor: the cited lines are accurate (available_steering 231-237, cap 282/294, thumbstick 246-251, rates 264, Drive.gd:666/104). The NB 8.0 deg at 20 m/s checks out: atan(2.266*0.95*9.81/400)+5 = 8.02 deg. The core gap is real: steering = steering_input*cap is symmetric, so countersteer is capped. No aligning or self-centring model exists, and the pedals have no slew. docs/roadmap.md:46 already lists 'ramp the pedals', which was never done.

### Design problems
- Off-topic for the relayed user request. The user asked about the AI engine for traffic, extras and detail generation. This is a pad steering and pedal feel proposal, so it does not answer that question, and the user request wins where they conflict.
- The KeyboardTap pedal gate cannot test the change. KeyboardTap.gd calls car.drive() directly (line 98) and never goes through Drive.gd, but the proposal puts the key slew in Drive.gd before vehicle.drive(). It also builds the default NB via setup(), so the Mustang needs use_spec as LiftOff.gd:81 does.
- The pedal gate is likely meaningless or failing. At a 6/s brake rise, a 0.3 s tap reaches full brake after 0.17 s. The Mustang has no ABS (abs_enabled defaults to false at CarSpec.gd:76), so it still locks, and peak front slip under 0.3 is improbable. A slew delays a lock but does not prevent one. Full brake locking a no-ABS 1965 car is realistic anyway.
- PadSteer.gd frame 68 requires |vehicle.steering| < 1e-4 after the stick is released. A rack that returns toward beta_f instead of 0 breaks that gate unless the return is speed-gated. The car is near standstill there, where atan2(v_lat + r*a, v_long) is noisy, and it flips sign in reverse.
- An asymmetric cap breaks the fraction representation. steering = steering_input*cap means a held full opposite stick makes the rack track |beta_f| every tick. When the allowance closes, the rack jumps with no slew. The rack angle needs to be a state slewed in radians, not a fraction of a cap that moves.
- The trigger sign(steer_input) != sign(yaw_rate) is fragile. In a pendulum or tank-slapper the yaw rate reverses while beta is still large. The allowance should be keyed on the direction of beta_f alone, which is general and has no state machine.
- 'Before the change, expect a spin' in SlideCatch is unverified. The scenario (a synthetic 0.4 s rear grip drop to 0.6) is a special case. The owner prefers general tests in random places.
- lock_to_lock_turns duplicates steering_ratio and parking_rack_deg and can disagree with them. C8: 2.5 turns * 180 / 15.7 = 28.7 deg, but meridian_z parking_rack_deg is 34. Derive the turns from the ratio and the rack lock instead of storing both.
- The analog clutch on LT with brake moved to LB takes the slot the clutch already uses (LB). It also puts a no-ABS car's brake on a digital button, which is worse for realism.
- The 'wheel' profile has no device path. Direct mode is chosen only by --direct-steering (Powertrain.gd:205), and no axis or range detection or calibration exists. 'No cap, no slew' is a separate input feature, not a tweak.
- Self-centring on release that steers the rack to beta_f is an automatic countersteer aid. Realism-first suggests making it an Assisted-only option with a gate showing it never moves the rack while the input is held.

### Corrected design (USE THIS where it differs)
If pursued at all, run it separately from the AI-engine question, in two small parts.

(A) Countersteer allowance, Assisted only:
- Keep a rack-angle state, rack_rad, slewed in rad/s instead of steering_input*cap.
- Compute beta_f = atan2(v_lat + r*a_f, max(v_long, 3)) only when v_long > 5 m/s; otherwise beta_f = 0.
- The cap toward sign(beta_f) is max(cap, min(|beta_f| + LIMIT_SLIP_SURPLUS, PARKING_RACK)). The cap the other way stays cap.
- Countersteer slew is up to 40 deg/s; normal slew is unchanged.
- No self-centring to beta_f in v1. Release still returns to 0, so the PadSteer exact-center gate holds.
- Gate, new SlideCatch: NB and C8 at 20 m/s, with lift-off at the LiftOff 0.85 g level and a scripted full opposite stick. Report before and after numbers, and keep it only if 'before' actually spins.
- Benches stay Direct, so CarTrack, LiftOff and SBend are unaffected.

(B) Pedals:
- Add a static Powertrain.key_pedal(current, pressed, dt). Rise is about 2.5/s for brake and 3/s for throttle, so a 0.3 s tap is about 0.75; release is 8/s.
- Drive.gd calls it only for key events, and KeyboardTap calls the same function, so the gate tests real code.
- Gate: Mustang (use_spec) brake tap at 45 mph reports peak slip ratio, and LockedBrakeTurn and CombinedSlip stay unchanged.

Steering ratio:
- Add CarSpec steering_ratio from sourced data only (Mustang 21.7 per mustang65.json; others flagged unverified).
- Feed it to both Drive.gd:694 and Cockpit.gd:166; derive the turns from the ratio and the rack lock.

Defer: the wheel profile, analog clutch, trigger gamma and caster self-centring until an aligning-torque model exists.

Update docs/controls.md and roadmap line 46.
