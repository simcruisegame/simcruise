# 9. Grip you can feel and hear: slip-driven Xbox rumble and trigger rumble, tyre scrub/squeal, ABS pulse, 40 ms audio, per-engine voices (slip-haptics-audio)
category feel · impact 5 · 4 d · depends: none

## Rationale
On a monitor with a pad, the grip limit has to come through audio and haptics, because the vestibular cue is missing. In fixed-base sims drivers take curves faster and accept more lateral g than on the road. Today the limit is silent, there is no rumble call anywhere, audio lags about 150 ms, and the C8 and Mustang V8s fire at half their real frequency through the inline-four voice.

## Code claims (proposer)
Nothing calls Input.start_joy_vibration (grep). RoadNoise.render_road (RoadNoise.gd:154) takes only speed, grip, bump and contact. Powertrain already has slip_ratio[i], peak_slip[i], rolling_slip, tc_cut and abs_release[i] (Powertrain.gd:66-69,159-163,421-446), and nothing outside Powertrain reads them. Drive.gd:250 and RoadNoise.gd:99 use AudioStreamGenerator buffer_length 0.15 and fill every available frame. InlineFourSound.gd:62 uses target_hz = rpm/30, and :15 LIMITER_RPM 6950 is a constant. Drive.gd:550-555 builds InlineFourSound for every combustion car.

## Design (proposer)
1) New game/Haptics.gd, called from Drive._physics_process at 60 Hz with a 0.05 s duration.
- Per-wheel normalised slip s_i = sqrt((kappa/kappa*_i)^2 + (alpha/alpha*_i)^2), using the tyre's own peaks (peak_slip now, the tyre core later).
- Weak motor = clamp((max_front_s - 0.8)/0.4, 0, 1)*0.6 + scrub texture, giving front grip and understeer.
- Strong motor = rear or driven-wheel slip excess, plus kerb and impact from suspension vertical acceleration (high-passed above 8 Hz), plus ABS pulse at the abs_release oscillation (10-15 Hz envelope).
- Rack-force drop: when rack_force falls more than 25 % from its peak while ay rises, add a short weak-motor fade, the 'going light' cue.
- Preferences: rumble 0-1, trigger rumble on/off.
2) Trigger rumble (impulse triggers):
- A small GDExtension, game/addons/sdl_triggers, calling SDL_RumbleGamepadTriggers (zlib licence; Godot 4.5+ already links SDL3 for joypads). Windows.Gaming.Input is the fallback.
- LT = front braking slip excess and ABS. RT = driven-wheel spin excess and TC cut.
- Kept behind an interface so Godot PR #114642 can replace it.
3) Tyre audio, a scrub layer on the Road bus.
- Amplitude smoothstep(0.6, 1.0, s)*Fz/Fz0.
- Band-passed noise crossfading for s > 1 into a resonant SVF tone (Q 8) at 700 Hz + 25 Hz per m/s of sliding speed. Broadband skid for s > 1.2.
- Panned by wheel, paved surfaces only.
4) Audio latency: buffer 0.05 s. Push only enough to keep 40 ms queued (target = RATE*0.04 - queued). Fall back to 80 ms when starvation is detected.
5) Engine voice from data: CarSpec cylinders and firing order. f = rpm*cyl/120, crossplane V8 half-orders for the Mustang 289 and the C8 LT2. Limiter = fuel_cut_rpm*0.99, POP_RPM = 0.45*redline. The Miata output stays bit-identical.

## Gate
RoadNoiseCheck:
- Scrub RMS below -40 dBFS at s = 0.5 and at least -22 dBFS at s = 1.1.
- Spectral peak between 600 and 1600 Hz when sliding.
- Master peak under 1.0, with the level budget at RoadNoise.gd:11-17 re-proved.
Other gates:
- EngineSoundDrive: FFT fundamental within 3 % of rpm*cyl/120 at 3000 rpm for each kept car. NB output bit-identical. Step latency under 50 ms, with 0 starvation frames over 60 s at Ultra.
- A headless HapticsCheck on CarTrack traces: weak motor at most 0.05 at 0.5 g steady state and at least 0.5 in a slide, strong motor pulsing during the ABS stop.
- No physics change, so all calibration bands are untouched.

## Files
game/Haptics.gd
game/addons/sdl_triggers
game/RoadNoise.gd
game/InlineFourSound.gd
game/Drive.gd
game/CarSpec.gd
game/DrivingPreferences.gd
game/checks/RoadNoiseCheck.gd
game/checks/EngineSoundDrive.gd
docs/controls.md

## Skeptic verdict
claims_hold true · already_done false · keep true

### Wrong claims
- 'Nothing outside Powertrain reads them' is only true of runtime code. Headless checks do read these fields: checks/CombinedSlip.gd:109-110 reads abs_release, checks/TractionControlCheck.gd:71-76 reads tc_cut and grip_use, and checks/LiftOff.gd:145 and checks/RoadDrive.gd:223 read rolling_slip. No runtime file reads them: not Drive, Hud, Cockpit or RoadNoise.
- The design uses 'rack_force', but no such variable exists anywhere: grep finds no rack_force, aligning or steer_torque in Drive.gd, SportWheel.gd, Powertrain.gd or CarSuspension.gd. The 'going light' cue has nothing to read and would need a new aligning-torque estimate, so it cannot be done 'with no physics change' as drop-in code.
- 'Using the tyre's own peaks (peak_slip now)' covers longitudinal slip only. Powertrain.gd:445 peak_slip is a slip-ratio peak. The lateral law (Powertrain.gd:346) is tanh(alpha*slip_stiffness) with a falloff starting at slip_falloff_from (0.18 rad on the NB), and there is no alpha* field. The lateral term of s_i needs a definition (for example atanh(0.95)/slip_stiffness, or slip_falloff_from) per car, and every threshold in the gates (0.8, 1.0, 1.1, 1.2) depends on that choice.
- Wrong-frequency engines are not limited to the C8 and the Mustang. bison_xl.json describes a 325 hp / 400 lb-ft 2018+ Ford, which is the 2.7 EcoBoost V6, so it should fire at rpm/20 but plays at rpm/30. The Mustang's fuel_cut_rpm of 5000 also means the hard-coded LIMITER_RPM of 6950 never fires for it. Kestrel (Honda I4) and Tora (Subaru flat-4) are correct at rpm/30.
- CarSpec has no cylinders or firing-order fields (grep finds none in CarSpec.gd or cars/*.json), so 'Engine voice from data: CarSpec cylinders and firing order' means adding new spec fields to all 7 JSONs, not reading existing data.
- Minor: Drive.gd:550-555 is toggle_sound(), where the voice is built on every sound toggle. It does build InlineFourSound for every car, but only plays it for non-electric cars, so the claim is essentially right.
- Verified correct: there is no Input.start_joy_vibration call. render_road(frames, speed, grip, bump, contact) is at RoadNoise.gd:154. Drive.gd:250 and RoadNoise.gd:99 both use buffer_length .15. Drive.gd:770-773 and RoadNoise.gd:141-146 push every available frame. InlineFourSound.gd:62 is rpm/30 and :15 is LIMITER_RPM 6950 (POP_RPM 3000 is also constant).

### Design problems
- Scope: the user's actual question was about the AI engine for traffic, extras and procedural detail generation. This proposal (haptics and audio feel) does not answer it and should not be presented as the response to that request. At most it is a separate backlog item.
- The trigger-rumble GDExtension is the weakest part. Godot links SDL3 statically and does not export its symbols, so an extension cannot get Godot's SDL_Gamepad handle. It would need its own SDL3 DLL and would open the same pad a second time, and with the HIDAPI backend that can conflict with Godot's instance. XInput has no trigger motors at all, so it only works through WGI or HIDAPI. The repo has no addons and no native build toolchain (no .gdextension or godot-cpp). Godot PR #114642 could not be verified. Building, signing and shipping this per platform in a 4-day item is unrealistic. Drop it or make it a separate spike.
- Audio pushes happen in Drive._process and RoadNoise._process at render rate, next to heavy streaming work (stream(), building_stream.tick, street_detail.tick). Any frame over about 40 ms, such as tile loads or 'Loading the road ahead', would starve a 40 ms queue. The adaptive fallback helps, but the gate should use AudioStreamGeneratorPlayback.get_skips() and has to run windowed, because RoadNoise disables playback headless (RoadNoise.gd:95). Better options are filling from physics ticks or a worker thread, or 60-80 ms as the target with 40 ms only when frame time allows. Also, 'queued' is not exposed: it has to be computed as capacity minus get_frames_available(), with capacity measured once (the ring buffer is rounded to a power of two).
- ABS haptic pulse: abs_release is an integrator (Powertrain.gd:427), and whether it really oscillates at 10-15 Hz is emergent and unverified. Sending 0.05 s vibration commands at 60 Hz through ERM motors with 30-50 ms spin-up smears a 12 Hz envelope. Synthesise the pulse from 'ABS active' (abs_release<0.95 on any braked wheel) at a fixed rate of about 8 Hz instead of relaying the physics signal.
- Kerb/impact: RoadNoise.update_roughness already high-passes suspension travel rate into a 0..1 roughness. Reuse it (or the same source) instead of a second vertical-acceleration filter that could disagree with the audio rumble.
- grip_use[i] (Powertrain.gd:161,452) already gives the longitudinal friction-circle use per wheel, and the combined budget Vector2(fx, fy/LATERAL_GRIP_SCALE).length()/fxmax is computed at :450. Expose that combined utilisation (and whether it was clipped) as the slip signal rather than re-deriving s_i from kappa and alpha outside Powertrain. It is cheaper, consistent with what the tyre actually did, and covers lateral slip without inventing alpha*.
- EngineSoundDrive.gd builds the default Powertrain (NB) only, so a per-car FFT gate means adding spec loading to that bench. A crossplane-V8 half-order also needs per-bank modulation of the single evenly spaced wavetable. Keep the NB path literally untouched (branch on cylinders==4 and inline) so 'bit-identical' holds.
- The rumble and trigger preferences need entries in DrivingPreferences.DEFAULTS and a HUD/settings control. Docs (controls.md, roadmap.md line 76 already lists 'squeal' as planned, CHANGELOG, README) must be updated per the owner's keep-docs-updated rule. The files list omits roadmap, CHANGELOG and README.
- Gate realism: no physics files change, so CarTrack, LiftOff and the other calibration gates are unaffected as long as Haptics.gd only reads state. That part holds. HapticsCheck needs Haptics as a pure function of car state (no Input calls in the computation) to run headless.

### Corrected design (USE THIS where it differs)
Split into three items, none of which touch physics, so CarTrack, LiftOff and the other calibration gates stay green.
(A) Pad rumble, about 1.5 days:
- Powertrain exposes per-wheel combined grip utilisation from the existing budget at Powertrain.gd:450, as u_i = budget/fxmax before clipping and clipped_i = budget > fxmax. This is read-only bookkeeping: it adds no new force.
- New game/Haptics.gd computes pure functions of car state, which HapticsCheck can test headless:
  - weak = smoothstep(0.85, 1.05, max front u) for understeer, plus a 0.15 scrub texture.
  - strong = smoothstep(0.9, 1.1, max driven/rear u), plus the existing RoadNoise roughness for kerbs, plus a synthesised 8 Hz ABS pulse whenever any braked wheel has abs_release < 0.95.
- Drive._physics_process calls Input.start_joy_vibration(device, weak*pref, strong*pref, 0.1) only when the values change by more than 0.03, and stops on pause or close.
- Add a rumble 0-1 preference.
- Drop the 'going light' rack cue until an aligning-torque estimate exists.
- Drop the trigger-rumble GDExtension; revisit if Godot exposes trigger motors natively.
(B) Tyre scrub/squeal in RoadNoise, about 1 day:
- Pass per-wheel u and Fz into render_road.
- Scrub amplitude = smoothstep(0.75, 1.0, u)*Fz/Fz0. Above u = 1, crossfade into an SVF tone at Q 8, 700 Hz + 25 Hz per m/s of sliding speed.
- Paved surfaces only. Mix it before the existing tanh, so the 0.90 Road ceiling and the RoadNoise.gd:11-17 budget still hold.
- RoadNoiseCheck gates as proposed.
(C) Engine voice and latency, about 1.5 days:
- Add cylinders and layout (i4, flat4, v6, v8_crossplane) to CarSpec and all JSONs, with the NB defaulting to i4.
- f = rpm*cyl/120. Use a half-order bank modulation for crossplane V8s (Mustang 289, C8 LT2) and the correct V6 frequency for the Bison's 2.7 EcoBoost.
- LIMITER_RPM = fuel_cut_rpm - 90 and POP_RPM scaled per car. The NB code path stays branch-identical.
- Extend EngineSoundDrive to load each spec and do an FFT check.
- Latency: keep about 60 ms queued (capacity measured once, minus get_frames_available()), adapt upward when get_skips() increases, and verify with a windowed run (headless has no mixer) at Ultra: 0 skips over 60 s while driving through tile loads.
- Update controls.md, roadmap.md, CHANGELOG and README.
Not an answer to the user's AI-for-traffic/extras/detail question; track it separately.
