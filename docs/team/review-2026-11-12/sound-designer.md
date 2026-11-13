# Sound designer review, 2026-11-12

Reviewer: sound-designer (see docs/team/sound-designer.md).
Scope: `Source/SimCruise/Private/Audio/*`, audio glue in `SimCruiseVehicle.cpp` and `SimCruiseTraffic.cpp`, `Content/Vehicles/CarPresentation.json`, car data, audio tests.
Method: code reading and arithmetic. No audio was rendered or heard in this session. Every "hear" statement below is a prediction from the code.

## Summary

The procedural voices are well built. The firing frequencies are correct (inline four rpm/30, V8 rpm/15, with the cross-plane bank model). Tests cover determinism, headroom, cabin filtering and tyre onset. The main bug: the rev limiter and the pitch clamp are constants tuned for one car, so the limiter is wrong for all three main cars. The main gap: there is no mix control and no loudness measurement.

## Findings

### 1. Audio rev limiter and pitch clamp are constants, not the car's fuel cut  (P1)
Evidence: `Public/Audio/SimCruiseInlineFourSynth.h:25` `LimiterRpm = 6950.0`; `Private/Audio/SimCruiseInlineFourSynth.cpp:76` `FMath::Clamp(Rpm, 0.0, 7200.0)`; `SimCruiseInlineFourSynth.cpp:96` `bLimiting = Rpm > LimiterRpm && ...`. Car data: `data/cars/nb2001_aaron.json:104` fuel cut 7250, `:110` max_omega 775 rad/s (7400 rpm); `meridian_z.json:105` fuel cut 6600; `mustang65.json:112` fuel cut 5000.
Problem:
- Miata: from 6950 to 7250 rpm the synth stutters as if on the limiter while the engine still makes full power. Above 7200 rpm the pitch stops rising. The auto upshift is at 6900 rpm (`nb2001_aaron.json:123`), so overshoot during a full-throttle shift can trigger a false stutter (estimate; not observed).
- Corvette C8 and Mustang: the engine cuts fuel at 6600 and 5000 rpm, but the synth never reaches 6950. The player hears no limiter at all. The real cue "you are on the limiter, shift" is lost for two of the three main cars.
Fix: add `LimiterRpm` and `MaxRpm` to `FSimCruiseInlineFourSynth::FInputs` (or a configure call). Fill them from `FSimCruiseCarDefinition::FuelCutSpeedRpm` and `max_omega` in `ASimCruiseVehicle::UpdateEngineSound`. Better: send a `bFuelCut` flag from the powertrain, so sound and physics cut together.
Verify: new test `SimCruise.Audio.LimiterFollowsCar`: render at fuel cut + 100 rpm with pedal 1 for each of the three cars; expect the quarter-cycle level ratio > 2 (as in `SimCruise.Audio.InlineFour`). Render the Miata at 7100 rpm; expect no stutter. Render at 7350 rpm; expect the firing peak at 245 Hz +-2 %.

### 2. No player volume controls and no mix bus  (P1)
Evidence: only a sound on/off (`SimCruiseInputConfig.cpp:57` `ToggleSound` on M; `SimCruisePlayerController.cpp:478-483` atlas settings list "Sound · On/Off"). Levels are fixed: `SimCruiseVehicle.cpp:443` engine `SetVolumeMultiplier(0.25f)` (Godot -12 dB); `SimCruiseRoadNoiseSynth.h:39-40` road -8 dB, wind -30 dB. No Sound Class or Submix is set on the components.
Problem: the player cannot lower the engine against tyres and wind, or set an overall level. A long drive at a fixed mix is tiring for some players. Accessibility guidance asks for separate volume controls (GAG / XAG; exact item not re-verified). Without a submix there is no place for a master limiter or a loudness meter.
Fix: route both synth components through one Sound Class tree (Master, Engine, Road and wind, Traffic) and one Submix. Expose Master, Engine and Road volumes in the atlas settings, remembered in `preferences.json`. This is a small settings addition; schedule it for the next phase if it counts as a feature.
Verify: unit test that each setting changes the class volume; capture test that the settings persist across a restart.

### 3. No loudness target or measurement of the full mix  (P1)
Evidence: tests check RMS and peak per layer only (`SimCruiseRoadNoiseTests.cpp:101` "asphalt at 30 m/s near -16 dBFS"; `SimCruiseAudioTests.cpp:77` peak <= 0.94). Nothing measures the summed engine + road + wind + traffic.
Problem: perceived loudness (LUFS) and true peak of the real mix are unknown. Full throttle plus squeal on rough road could exceed full scale after the mixer sums three sources (estimate: engine 0.94 x 0.25 + road/wind up to 0.40 = 0.64 peak, so likely safe, but not proven with traffic voices added).
Fix: add an offline render of a scripted 10-minute drive (DriveTest telemetry) through both synths at their mix gains. Measure integrated loudness, LRA and true peak.
Verify: target -24 LUFS +-2 LU integrated, true peak < -1 dBTP (EBU R128 gives -23 LUFS / -1 dBTP for broadcast; -24 for console games is unverified).

### 4. Hard clip at the engine output  (P2)
Evidence: `SimCruiseInlineFourSynth.cpp:166` `FMath::Clamp(OutputLow * Gain, -.94, .94)`. The road and wind use `tanh` soft limiting (`SimCruiseRoadNoiseSynth.cpp`, `RenderRoad` and `RenderWind`).
Problem: if pops, boom and full load stack, the engine clips hard and adds harsh odd harmonics. The test only checks that the peak is <= 0.94, so it cannot see clipping.
Fix: use the same `0.94 * tanh(x / 0.94)` limiter as the road layer.
Verify: test that renders a worst case (limiter + overrun pops + cabin boom) and counts samples at exactly +-0.94; expect zero.

### 5. Traffic voices: one voice type, no Doppler, no air absorption  (P2)
Evidence: `SimCruiseTraffic.cpp:692-711`: every non-V8 traffic car uses the inline-four voice; only the attenuation falloff is set (`NaturalSound`, 3 m inner radius); no pitch shift from closing speed; no LPF by distance. Car data includes V6 trucks and SUVs (`f150raptor18`, `grandcherokee13`, `odyssey18`).
Problem: a passing V6 truck sounds like a small four (wrong firing frequency: rpm/30 instead of rpm/20). A car passing at 25 m/s gives no pitch fall (expected about 14 % across the pass: c/(c-25) vs c/(c+25)). Speed feels lower.
Fix: add a V6 even-fire layout (three pulses per revolution) to the presentation profile. Add a pitch ratio input to the synth, set from the radial velocity each frame. Enable the attenuation LPF (air absorption) on the traffic voice.
Verify: test of the Doppler ratio function; render test of a V6 voice (firing peak at rpm/20).

### 6. Smoothing coefficients applied once per buffer  (P2)
Evidence: `SimCruiseRoadNoiseSynth.cpp:90` `HissCutoff += (CutoffTarget - HissCutoff) * .3;` and `:195` the same for `WindCutoff`, once per `Render` call.
Problem: the glide time depends on the audio buffer size (256 vs 1024 frames gives a 4x change). On a slow PC with large buffers, the hiss colour lags speed changes.
Fix: use a time constant: `alpha = 1 - exp(-Frames / (SampleRate * tau))` with tau about 50 ms.
Verify: test that two renders with 256-frame and 1024-frame buffers over 1 s end at the same cutoff within 1 %.

### 7. Sample rate 22.05 kHz removes all content above 11 kHz  (P2)
Evidence: `SimCruiseInlineFourSynth.h:24` and `SimCruiseRoadNoiseSynth.h:38` `SampleRate = 22050`.
Problem: the exterior pickup, wind and skid hiss lose their top octave. The cabin filter (2.4 kHz) hides this in the driver's seat, which is the default view. The chase view sounds dull (prediction).
Fix: none needed this phase. Next phase: render at the device rate (48 kHz) after a CPU check; the DSP is cheap.
Verify: profile the audio render thread at 48 kHz on the 28 W target; compare spectra above 11 kHz in the chase view.

### 8. Overrun pop threshold is one constant for all cars  (P2)
Evidence: `SimCruiseInlineFourSynth.h:27` `PopRpm = 3000.0`; `SimCruiseInlineFourSynth.cpp:99`.
Problem: a 1965 Mustang 289 with a carburettor and a 5000 rpm cut behaves differently from a modern C8; pops above 3000 rpm are an estimate for all (header says "estimates, not BP-4W microphone measurements").
Fix: move pop rpm and pop chance to `engine_sound` in `CarPresentation.json` with a source note per car.
Verify: data load test that the field is read; listening test against owner-approved reference clips.

### 9. Engine voice is not tied to the camera position in the chase view  (P2)
Evidence: `SimCruiseEngineSoundComponent.cpp:11` `bAllowSpatialization = false` for the player car; the exterior pickup is only a brighter filter (`SimCruiseInlineFourSynth.cpp:91` `Filter = In.bCabin ? .20 : .52`).
Problem: in the chase view the car turns but the engine stays centred. This is a design choice from the Godot build (header comment), so it is not a bug. It removes a small spatial cue.
Fix: next phase only; optional spatialized exhaust emitter at the tail pipes in the chase view.
Verify: listening test in the chase view while turning.

## Open questions

- Real squeal frequency for the NB's tyres (700 Hz + 25 Hz per m/s in code) has no cited source.
- Cabin boom frequency (about 120 Hz in the header) for the NB with the soft top up: no measurement found.
- Exact console game loudness target: -24 LUFS (Sony ASWG-R001) is from memory, not re-read.

## Next phase proposals

Ranked. None of this work has started.

### NP1. Recorded reference set and match test for the three main cars  (size M)
- Goal: each main car's engine matches a reference recording within agreed limits.
- Why: the vision says the car must sound exactly like the real car. Today the envelopes are estimates.
- Scope: the owner records the Miata NB (cabin and tail pipe) at idle, 2000/4000/6000 rpm at light and full load, overrun, limiter. Use licensed recordings for the C8 and Mustang only with a written licence. Build an offline compare tool: order levels (2, 4, 6...) and spectral centroid vs rpm and load.
- Acceptance test: for each car, the levels of the first 6 engine orders lie within 3 dB (median) of the reference at 6 operating points; a blind A/B listening test with the owner rates "same car" in at least 4 of 6 pairs.
- Risk: licence of third-party recordings; mic placement differences.

### NP2. Mix, volume controls and loudness gate  (size S)
- Goal: a measured, controllable mix.
- Scope: Sound Classes and one Submix; Master, Engine, Road+Wind, Traffic volumes in the atlas; offline R128 measurement in CI.
- Acceptance test: integrated loudness of the standard drive -24 +-2 LUFS; true peak < -1 dBTP; each volume step changes the measured level by its stated dB.
- Risk: low.

### NP3. Physically informed exhaust and intake (pipe resonances)  (size L)
- Goal: replace static harmonic tables with a pulse source through exhaust and intake waveguides (Baldan et al. 2015 style), keeping the current API.
- Why: pipe resonances and load-dependent pulse shape give the "real car" character; a Miata with an aftermarket exhaust could then be modelled from pipe lengths.
- Scope: DSP class, per-car pipe data in `CarPresentation.json`, tests for orders and resonances.
- Acceptance test: NP1 match test passes at least as well as the current voice; CPU under 0.5 ms per buffer on the 28 W target.
- Risk: tuning time; CPU.

### NP4. Pass-by realism for traffic  (size M)
- Goal: correct voice type per traffic car, Doppler, distance low-pass, and guard-rail and tunnel reflections from OSM (docs/research/domain-ideas.md idea 12).
- Acceptance test: V6 firing peak at rpm/20; Doppler pitch fall 13-15 % for a 25 m/s pass; tunnel adds measurable early reflections.
- Risk: CPU with many voices; voice limit needed.

### NP5. Surface-driven tyre noise  (size M)
- Goal: tyre noise level and spectrum from the road surface type (asphalt, chip seal, tined concrete) per road segment.
- Acceptance test: tined concrete +5 dB near 1 kHz vs asphalt at the same speed (FHWA OBSI figure, to verify).
- Risk: OSM `surface` tags are sparse.
