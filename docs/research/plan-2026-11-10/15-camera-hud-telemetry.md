# 15. Head physics, instruments and teaching telemetry for every kept car; chase camera that shows slip; g-g ring at the published skidpad g (camera-hud-telemetry)
category feel · impact 3 · 3 d · depends: pad-steering-pedals

## Rationale
Generated cars are being removed, but the downloaded C8 and Mustang still take the generic path: a fixed eye, no head spring, and no speed readout in driver view at the default HUD. The chase camera is welded to body yaw, so a slide never reads on screen. Without vestibular cues, head motion and a g-g display with the real skidpad ring are what let a monitor player learn the limit, and they are also a realism check the player can see.

## Code claims (proposer)
Drive.gd:719 sets cockpit.set_view(view==1 and not generic_car), and Drive.gd:730-734 gives the fixed generic eye with no Cockpit.head_view. Hud.gd:207 hides tag_speed in view 1. Hud.gd:96 hard-codes '1999 Mazda MX-5 Miata'. Drive.gd:743-753 places the chase camera at a body-fixed point with lerp. Cockpit.gd:197-244 is the head model (9 rad/s, 3 cm/g lat, 2 cm/g long, 70 % horizon). tc_cut and abs_release exist (Powertrain.gd:350,421) but are not displayed. The HUD rpm updates every 0.16 s (Drive.gd:762-766).

## Design (proposer)
1) Cockpit.head_view parameterised by spec.driver_eye and sight, run for every car. The cabin mesh is optional.
2) The speed tag is shown in view 1 when the car has no gauge rig, plus a minimal rpm, gear and shift-light strip. The car label comes from spec.name.
3) Chase yaw = slerp(body_yaw, velocity_yaw, 0.4·smoothstep(2,8 m/s)) with a 0.3 s first-order lag, centred above walking speed. The driver-view gaze adds a beta_f term clamped to 8 deg and scaled by head_motion.
4) TC and ABS lamps in Glance when tc_cut > 0.05 or any abs_release < 0.95.
5) An F7 telemetry strip:
- throttle, brake and clutch bars, steer against cap
- four slip bars s_i coloured green < 0.8, amber up to 1, red > 1
- a g-g trace with a 2 s tail and a ring at the car's target_skidpad_g
- a 60 Hz CSV recorder to user://telemetry with the fields the checks log, readable by the lab/vehicle tools
6) FOV calculator in settings: GFOV = 1.1-1.2× the geometric FOV by default (Diels & Parkes). The true value is shown, and ViewLens 'screen' mode is kept.

## Gate
HeadCameraCheck --car=meridian_z and --car=mustang65: non-zero head offset under a 0.8 g brake, horizon roll at 70 % of body roll, and chase yaw minus body yaw about 0.4·beta in a scripted 15 deg drift. HudLevelCheck: a speed readout visible at Glance in view 1 for every kept car, and the carinfo text equals spec.name. The TC/ABS lamp rises during the TractionControlCheck and AbsReverse interventions and falls within 0.3 s. The telemetry CSV of a CarTrack run reproduces its 0-60 within 1 tick. Camera only, so the physics bands are untouched.

## Files
game/Cockpit.gd
game/Drive.gd
game/Hud.gd
game/ViewLens.gd
game/Telemetry.gd
game/checks/HeadCameraCheck.gd
game/checks/HudLevelCheck.gd
docs/controls.md

## Skeptic verdict
claims_hold true · already_done false · keep false

### Wrong claims
- The proposal lists game/checks/HeadCameraCheck.gd and game/checks/HudLevelCheck.gd as new files, but both already exist. HeadCameraCheck already checks the Cockpit head model with a FakeCar: 3 cm/g lateral, 2 cm/g longitudinal, the caps, the brake dip, the 4 deg gaze lead, 70 % horizon follow and the head_motion=0 weld. HudLevelCheck already loads Drive.tscn and checks Glance, Full and Off, plus the eye position at rest. These have to be extended, not created. The '0.8 g brake gives a non-zero offset' and '70 % horizon' parts of the gate are already tested for the NB. Only the per-car part (--car) is new, and HeadCameraCheck does not load a CarSpec today.
- Several line numbers have drifted. The set_view call is at Drive.gd:724, not 719. The chase camera is at Drive.gd:741-747, not 743-753. tc_cut is assigned at Powertrain.gd:351 and 375 and declared at 66. abs_release is declared at 162 and updated at 427-429, not 421. The substance of each claim is still correct.
- 'Readable by the lab/vehicle tools' has nothing behind it. lab/vehicle holds README.md, engine-sound/, locked-braking/, pad-corrections/ and vehicle_plugin_bench.gd. There is no CSV reader, and the checks do not share a common logged field set: each prints its own ad hoc line. 'The fields the checks log' therefore has to be defined from scratch.
- The proposal says the telemetry CSV of a CarTrack run should reproduce its 0-60. That cannot work if the recorder lives in Drive.gd. CarTrack (and Track.gd) build a bare Powertrain with load('res://Powertrain.gd').new() and never instantiate Drive.tscn. The recorder has to be a node that attaches to any Powertrain and samples in _physics_process, which runs at the default 60 Hz since project.godot does not set physics_ticks_per_second.
- The design asks for an FOV calculator as if none exists, but ViewLens.gd already computes the geometric screen FOV from the OS DPI and the view_cm preference ('screen' lens, cycled with F6). Only the 1.1-1.2x Diels & Parkes GFOV multiplier and a settings readout would be new.
- The design mentions 'spec.driver_eye and sight', but CarSpec has driver_eye and no sight field. The generic path hard-codes the look target as eye + (0,-0.32,-12), a pitch of about -1.5 deg. The NB's SIGHT-EYE gives about -7.1 deg. So 'sight' is a new field, and the two pitches differ.

### Design problems
- OFF-TOPIC for the relayed user request. The user asked about 'the AI engine we discussed to assist with traffic, extras, generation of detail'. This proposal is camera, HUD and telemetry work. It does not touch traffic AI (AmbientLife.gd), pedestrians or extras, or procedural or AI detail generation (StreetDetail, BuildingStream, CarBody). Whatever its merit, it does not answer the question the user asked.
- Chase yaw = slerp(body_yaw, velocity_yaw, ...) breaks when reversing. Above 2 m/s backwards, velocity_yaw is body_yaw+pi, so the camera would swing about 72 deg (0.4*180 deg) toward the front of the car. The blend has to use the body slip angle beta = atan2(v_lat, |v_long|), clamped to about ±35 deg, with the sign of forward speed respected. The same applies near standstill and during spins: above about 90 deg of beta the yaw has to be held or blended back, not slerped.
- The existing chase code builds goal and target from the full body transform, so pitch and roll pass through as well. The new yaw frame must be yaw-only (a heading basis) and must still feed the goal.y surface clamp and the collision ray retraction, which use 'body' and the pivot. The existing lerp(1-exp(-10dt)) position lag stacks with the proposed 0.3 s lag. The gate value '≈0.4·beta' has to state which lag it measures at steady state.
- The driver gaze would add a beta_f term clamped to 8 deg on top of the existing 4 deg GAZE_LEAD_DEG, for up to 12 deg combined. Cockpit.head_view also normalises steering by the NB's 0.10 rad (car.steering/.10), which is not right for the C8's or the slow-ratio Mustang's rack. Both need a combined clamp and per-car normalisation, for example by spec max steer.
- head_view uses the Miata constants EYE and SIGHT, and set_view(false) resets camera.fov to 62. Running head_view for generic cars needs EYE and SIGHT moved to instance fields set from the spec. The generic camera.fov=cockpit.driver_fov line must also be kept, or the C8 and Mustang lose the F6 lens.
- The HUD refresh runs only every 0.16 s (Drive.gd timer>.16). A shift light or rpm strip driven from there will lag by up to 160 ms and look sluggish at redline. The strip needs per-frame updates outside that timer.
- The proposal's 'four slip bars s_i' are undefined, and Powertrain exposes nothing suitable. grip_use is longitudinal only (fx/(fz*mu), after the budget clamp), so it can never exceed 1 or show lateral use. There is slip_ratio and peak_slip but no stored per-wheel slip angle or peak angle. A combined s_i = |(kappa/kappa_peak, alpha/alpha_peak)| needs the slip angle (computed locally at Powertrain.gd:341) and a peak angle to be stored per wheel. That is a read-only addition to the physics file, but it is still a Powertrain edit.
- A g-g ring at target_skidpad_g is lateral only. Braking capability, for example from the target 70-0 distance (a = v^2/2d), is different. A circle at skidpad g mislabels the longitudinal limit. Use an ellipse, or two marked axes.
- In 'tc_cut > 0.05 or abs_release < 0.95', abs_release is reset to 1 every substep when ABS is not active. Read once per rendered frame, short interventions alias. The lamp should latch a max over the physics ticks since the last frame, which also makes the 0.3 s fall-time gate meaningful.
- The dependency on 'pad-steering-pedals' is not justified for most of this. Only the 'steer against cap' bar could need it. The head and chase camera and the HUD fixes have no such dependency.

### Corrected design (USE THIS where it differs)
Do not schedule this under the user's request: it does not answer the question about an AI engine for traffic, extras or detail generation. If it is kept as a separate feel item, it can be split into small parts, all camera and HUD only, with CarTrack, LiftOff and SBend left untouched.

(a) Per-car head:
- Turn Cockpit.EYE and SIGHT into instance fields. Set them from spec.driver_eye and a new optional spec.driver_sight. The default sight is eye + (0,-0.32,-12), so today's generic pitch is kept.
- Call head_view whenever view==1. The cabin mesh is optional, and camera.fov=driver_fov is kept for every car.
- Normalise the gaze lead by spec max steer instead of the NB's 0.10 rad.
- Extend the existing HeadCameraCheck to load CarSpec by --car. Run it for nb1999, meridian_z and mustang65 against the existing constants.

(b) HUD:
- carinfo label = spec.name.
- Show tag_speed in view 1 when cockpit.cabin is not visible.
- Add a per-frame gear, rpm and shift-light strip, kept out of the 0.16 s timer.
- Extend the existing HudLevelCheck with --car runs.

(c) Chase camera:
- Yaw-only heading frame.
- Offset by k·clamp(beta,±35°)·smoothstep(2,8,|v_forward|), where beta = atan2(v_lat,|v_long|), so reversing does not flip the camera.
- Hold the yaw when |beta| > 90°.
- Use one first-order lag (0.3 s) on the yaw, keeping the existing position lerp, the surface clamp and the ray retraction.
- Test this as a pure function in a new ChaseFrame.gd with a scripted beta input.

(d) TC/ABS lamps:
- Powertrain latches max(tc_cut) and min(abs_release) over the ticks since the HUD last read them. This is a read-only accessor.
- Lamp on above the thresholds, with a 0.3 s hold.

(e) F7 telemetry (F7 is free; F2, F3, F4 and F6 are taken):
- Telemetry.gd is a node that attaches to any Powertrain and records per _physics_process tick (60 Hz): t, speed, throttle, brake, clutch, steering, gear, rpm, ax, ay, slip_ratio[4], slip_angle[4], tc_cut, abs_release[4].
- Recording needs Powertrain to store the per-wheel slip angle, read-only.
- The CSV goes to user://telemetry.
- Gate: attach it inside CarTrack and confirm its 0-60 matches CarTrack's own number to 1 tick. Also confirm that CarTrack, LiftOff and SBend outputs are byte-identical with and without the recorder.
- The g-g plot gets an ellipse: lateral = target_skidpad_g, longitudinal = v²/2d from the 70-0 target.
- Slip bars use the normalised combined slip.

(f) Lens: only add the optional GFOV multiplier and an FOV readout to the existing ViewLens 'screen' mode. No new calculator.
