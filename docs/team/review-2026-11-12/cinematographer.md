# Cinematographer review, 2026-11-12

Reviewer: `cinematographer` (camera, lens, light, colour).
Scope: driver/chase/scenic cameras, head model, FOV, motion blur, exposure, tone map, night light, HUD over the image.
Inputs: repo at origin/main (2026-11-12), captures in the project files folder.
Phase: bugs and driving experience. Proposals for the next phase are in the last section.

## Findings (ranked)

### 1. P1 | Head model and g-meter differentiate a stepped physics velocity over the render frame time
Evidence: `Source/SimCruise/Private/Gameplay/SimCruiseCameraModels.cpp:93` `Raw = (WorldVelocityMps - PreviousVelocity) / FrameS` and `:245` (g-meter, same). The velocity is `Telemetry.BodyVelocityMps`, which `SimCruiseVehicleMovementComponent.cpp:608-611` copies from the latest physics step (no interpolation). Physics runs async at 360 Hz (`Config/DefaultEngine.ini:89-90`). The pawn transform the camera uses is the game-thread transform (`SimCruisePlayerCameraManager.cpp:68`).
What the player sees: the number of physics steps per frame varies (5, 6 or 7 at 60 fps; 2 or 3 at 144 fps). The velocity change then covers 14-19 ms while `FrameS` says 16.7 ms. Under 0.8 g braking one step more or less is ±0.022 m/s, which reads as ±1.3 m/s² (±0.13 g) of fake acceleration. At 144 fps the error is about ±20 % of the real value every frame. This drives head offset (2-3 cm/g), gaze lead and brake dip, and makes the g-meter dot jitter. The 0.06 s filter hides part of it. The tests (`SimCruise.Gameplay.Camera.HeadModelFrameRate`) feed exact synthetic velocities, so they cannot see it.
Fix: copy `SimTimeS` (already in `FSimCruiseVehicleTelemetry`, `SimCruiseVehicleMovementComponent.cpp:119`) into `FSimCruiseDriveTelemetry` in `SimCruiseVehicle.cpp:279-281`; divide Δv by Δ`SimTimeS`; when Δ`SimTimeS` is 0, hold the last value.
Verify: new test: velocity integrates at 1/360 s; frames take 5/6/7 random steps; 0.8 g braking. Head offset noise (std) < 1 mm, g-meter noise < 0.02 g. Then log `Felt` in game at 60 and 144 fps.
Confidence: data path verified in code; size of the noise computed, not measured.

### 2. P2 | Chase camera rotation is welded to the car's yaw while its position lags
Evidence: `SimCruiseCameraModels.cpp:194-197` builds the frame from the current body heading; `:214` the look-at uses that frame with no smoothing; `:226` only the offset is smoothed (rate 10/s). `SimCruisePlayerCameraManager.cpp:133` aims at that look-at.
What the player sees: every yaw twitch (steering corrections, bumps, kerb strikes, ABS) turns the whole picture at once. The car stays nailed to the centre and the world shakes. This is the "rigid rotation" camera mistake (Nesky, GDC 2014).
Fix: smooth the heading (or the look-at) with a critically damped spring, time constant about 0.15-0.3 s, in the same yaw-only frame, so the swing and the heading share one lag.
Verify: slalom at 50 km/h in chase view; log camera yaw rate and body yaw rate; camera yaw rate amplitude above 2 Hz must be at least 50 % lower than body yaw rate.
Confidence: verified in code; feel inferred.

### 3. P2 | Chase camera snaps in front of solid roadside objects
Evidence: `SimCruisePlayerCameraManager.cpp:122-129`: a line trace on `ECC_Camera` from the roof pivot to the camera; on hit the camera jumps to the hit point at once (`OverrideLocation`). Palms are `BlockAll` (`SimCruiseTrees.cpp:392`), parked cars are `BlockAll` boxes (`SimCruiseWorldTile.cpp:490-496`). At low speed the camera sits 1.92 m to the right (`SimCruiseCameraModels.cpp:209`), over the kerb lane.
What the player sees: when a palm trunk, pole or parked car passes between the car and the camera, the camera jumps forward to it in one frame and then eases back in 0.1 s. Thin objects make it flicker.
Fix: sphere sweep (radius about 15-20 cm); ignore thin or small actors (trunks, poles, parked cars) for the camera channel; pull in over 2-3 frames and ease out over about 0.5 s.
Verify: chase view at 15 km/h along Ocean Drive's parked row and palm lawn; log camera-to-pivot distance; count frame-to-frame changes > 0.5 m. Goal: zero.
Confidence: code verified; the frequency of hits is speculative (not run).

### 4. P2 | Cockpit vibration is white noise per rendered frame
Evidence: `SimCruiseCameraModels.cpp:136-137`: a new random vector each frame, filtered with `1 - exp(-30 Dt)`.
What the player sees: the shake has no physical spectrum, and its size depends on frame rate. Steady RMS of a one-pole filter on white noise is σ·sqrt(α/(2-α)): 0.68σ at 30 fps, 0.49σ at 60, 0.35σ at 120. So the cabin shakes twice as hard at 30 fps as at 120 fps. At 30 m/s on a rough road the dash 0.6 m away moves about 2-3 px per frame at 1080p (computed).
Fix: generate the noise at a fixed rate on sim time and band-limit it (for example a second-order filter around 10-15 Hz for wheel hop, plus the existing roughness gain), or drive it from the suspension travel rate that the model already computes (`:125-134`).
Verify: extend `SimCruise.Gameplay.Camera.HeadModelFrameRate`: vibration RMS at 30, 60, 144 fps within 10 % of each other.
Confidence: verified in code; RMS figures computed.

### 5. P2 | Motion blur is left at engine defaults
Evidence: `Config/DefaultEngine.ini:36` turns motion blur on. `SimCruiseEnvironment.cpp:416-463` (`ApplyExposure`) sets exposure, flare, vignette, bloom and fringe, but no `MotionBlurAmount`, `MotionBlurMax` or `MotionBlurTargetFPS`.
What the player sees: blur strength follows engine defaults and may change with the preset's frame rate (Epic: TargetFPS 0 means blur depends on the real frame rate). In the seat, too much blur smears the road texture and weakens the speed cue. The turntable already overrides blur (`SimCruiseTurntable.cpp:244-245`), so the look was considered there but not in driving.
Fix: set the three values explicitly in the environment's post-process component: amount 0.5 (180° shutter), max about 3 %, `MotionBlurTargetFPS` 60. Decide per view later (next phase).
Verify: drive capture at 50 mph at High and Low presets; blur length of lane marks must match within 20 %.
Confidence: verified that no override exists; engine default values not checked for 5.8.

### 6. P2 | Night: black sky and black road outside the light pools
Evidence: captures buildings-night-breakwater.jpg, buildings-night-edison-cafe.jpg: sky and unlit road at RGB ≈ 0. Only the player's low beams feed the exposure reference (`SimCruiseVehicle.cpp:216-220`); street lights do not, as the architecture notes admit (`docs/unreal-architecture.md:473`). Sky glow is at most 0.06 cd/m² (`SimCruiseWorldStreamer.cpp:398`), far below the night band (`SimCruiseDaylightModel.cpp:111-116`).
What the player sees: the city has no sky; roof lines vanish; the road between lamps is a void. A dark-adapted driver in Miami Beach sees an orange-grey sky and the road.
Fix: add the lamp pool's illuminance at the car to `SetArtificialIlluminanceLux` (it is what a driver adapts to); lift the night sky floor a little (day-for-night convention) so silhouettes read.
Verify: night capture: sky mean ≥ 3 code values above the roofs; road between lamps above 0 in the histogram; compare with a licensed night photo of Ocean Drive.
Confidence: verified in code and captures.

### 7. P2 | Lit windows and some signs clip to flat white at night
Evidence: captures above; window emissive in `SimCruiseWorldResources.cpp:284-285`.
What the player sees: no detail or colour in lit rooms; bloom at 0.25 makes them glow uniformly. See magician finding 6 for the variation fix.
Fix: lower the emissive peak so lit rooms land near 80-90 % of display white at the night exposure; vary colour temperature.
Verify: < 1 % of window pixels at 255 in a night capture.
Confidence: verified in captures.

### 8. P2 | Exposure documentation does not match the code; local exposure below Epic's typical range
Evidence: `docs/unreal-architecture.md:410` says +0.7 compensation and local exposure 0.8/0.8. Code: `SimCruiseEnvironment.h:172` +0.2; `Config/DefaultEngine.ini:34-35` highlight 0.55, shadow 0.95. `docs/unreal-architecture.md:696` has the new values. Epic gives 0.6-1 as the typical contrast range.
What the player sees: nothing directly. Risk: the next person tunes from the wrong numbers. Highlight contrast 0.55 is a strong compression; check midtones for flatness.
Fix: update line 410; keep 0.55 only if the A/B against the reference photo still wins.
Verify: doc review; A/B noon capture at 0.55 and 0.65 against the Panoramax reference.
Confidence: verified.

### 9. P2 | HUD names the chosen view, not the shown view
Evidence: `Source/SimCruise/Private/Gameplay/SimCruiseHUD.cpp:366` uses `GetView()`. The camera has `GetShownView()` for cars without a cabin (`SimCruisePlayerCameraManager.h:31`).
What the player sees: in a car with no cabin the full HUD says "Driver view" while the chase view is shown. None of the three focus cars is affected today.
Fix: use `GetShownView()`.
Verify: select the CR-V; full HUD must say "Chase view".
Confidence: verified.

### Note (not a finding): lens numbers
Natural lens 45° vertical = 72.7° horizontal at 16:9, about a 29 mm full-frame lens (`SimCruiseCameraModels.h:28`). On the 15.6 inch laptop at 60 cm the screen subtends 18.4° vertical, so GFOV/FOV ≈ 2.4:1. Diels and Parkes (2010) tested only 0.83-1.33 and found 1.22 best for speed judgement. The effect at 2.4 is unknown. See proposal N2.

## Not checked

- Lumen noise on neon-lit facades at night (visible speckle in buildings-night-breakwater.jpg); cause not traced.
- Tunnel and overpass exposure transitions (Speed Up 2, Speed Down 1 stops/s look plausible).
- Mirror capture image quality.
- Any run of the game: all items are from code, tests and captures.

## Next phase proposals (ranked)

These are proposals for the phase after the driving release. Do not start them now.

### N1. Camera feel pass with measured targets
- Goal: the seat camera and chase camera feel like a real head and a real tracking car, at any frame rate.
- Why: "feel exactly like the real car" is felt first through the camera. Jitter or a welded camera makes good physics feel cheap.
- Scope: fix-forward of findings 1-4 as one designed system: sim-time differentiation, band-limited vibration from suspension data, smoothed chase heading, swept collision with ease-out; add a comfort filter that keeps view pitch/roll energy low in 0.2-0.4 Hz.
- Acceptance test: automated tests at 30/60/144 fps with stepped physics data (noise thresholds above); blind A/B with 6 drivers, current vs new, 10 min each; new preferred by ≥ 5 of 6; no sickness reports.
- Risk: over-smoothing hides the car's real motion (roll, dive) that the driver needs.
- Size: M.

### N2. Lens and speed calibration
- Goal: the driver judges speed and distance correctly on the real 15.6 inch screen.
- Why: the natural lens is 2.4x the screen angle; speed perception at that ratio is untested.
- Scope: run the Diels and Parkes speed-production protocol with the three lenses; then pick the default lens and, if needed, a "seat" preset (eye distance, FOV) per car. Shared with magician N2.
- Acceptance test: mean speed error < 5 % with the chosen default.
- Risk: the best lens for speed may hide too much of the road on tight streets.
- Size: S.

### N3. Night look: day-for-night grade and real street light exposure
- Goal: night drives read like a night photo of the real street.
- Why: cruising real roads at night is a core pleasure; today the sky and road are a void.
- Scope: street light lux into exposure, sky floor lift, window and sign emissive range, Lumen night noise check, night saturation curve review.
- Acceptance test: blind "photo or game" d' at night falls by 30 % against licensed night photos; histogram rules above pass.
- Risk: Lumen cost at night with many emissives on the 28 W GPU.
- Size: M.

### N4. Explicit shutter per view, and a replay/photo shutter
- Goal: short shutter for play, film shutter for replays and screenshots.
- Why: blur sells speed in a replay but hides the road while driving.
- Scope: per-view motion blur settings (seat, chase, scenic); a 1/50 s look for replays when a replay exists.
- Acceptance test: lane-mark blur length within target per view at all presets.
- Risk: low; small GPU cost.
- Size: S.

## References

- Epic Games, Auto Exposure: https://dev.epicgames.com/documentation/en-us/unreal-engine/auto-exposure-in-unreal-engine
- Epic Games, MotionBlurTargetFPS: https://docs.unrealengine.com/4.26/en-US/API/Runtime/Engine/Engine/FPostProcessSettings/MotionBlurTargetFPS/index.html
- Epic Games, ISM SetCullDistances: https://docs.unrealengine.com/4.26/en-US/API/Runtime/Engine/Components/UInstancedStaticMeshComponent/SetCullDistances/index.html
- Nesky, 50 Game Camera Mistakes, GDC 2014: https://gdcvault.com/play/1020460/50-Camera
- Diels and Parkes 2010: https://pureportal.coventry.ac.uk/en/publications/geometric-field-of-view-manipulations-affect-perceived-speed-in-d/
