# Cinematographer: study notes

Role: camera, lens, light and colour expert for SimCruise.
Question this role answers: does the picture look and move like a real car seen through a real lens, by a real driver?
Style: ASD-STE100 Simplified Technical English. Short sentences. Active voice.
Status of facts: each fact has a source. "(unverified)" marks a fact that we did not check in the primary source.

## 1. Core idea

The camera is the player's body in the car.
In the seat, the camera must move like a head, not like a rigid mount and not like a drone.
Outside, the camera must frame the car and show its motion (slip, roll, dive) without fighting the player.
The image must expose like a camera that a person trusts: the road readable, the sky held, night dark but legible.

## 2. Rules and numbers

### 2.1 Lens and field of view

- Vertical FOV v and focal length f on full frame (24 mm sensor height): f = 12 mm / tan(v / 2).
  45 deg vertical ≈ 29 mm. 50 deg ≈ 26 mm. 67 deg ≈ 18 mm.
- Unreal takes a horizontal FOV. Horizontal h = 2 atan(tan(v/2) × aspect).
  45 deg vertical at 16:9 = 72.7 deg horizontal. (Checked by test `SimCruise.Gameplay.Camera.HeadModel`.)
- "True to screen" FOV: v = 2 atan(H / 2D). A 15.6 inch 16:9 screen (H = 19.4 cm) at D = 60 cm gives 18.4 deg vertical.
- Rule: a true-to-screen FOV is correct for scale but too narrow for situational awareness on a small screen. Simulators use a wider geometric FOV. Diels and Parkes found speed is perceived better at GFOV/FOV ≈ 1.22:1 [C6]. SimCruise "natural" is about 2.4:1 on the laptop. Treat the effect as untested.
- Rule: keep the horizon in the upper half of the frame in the seat. Show some bonnet or dash: it gives scale and a motion reference.
- Rule: do not change FOV automatically with speed in the seat. A real eye does not zoom. A small chase-camera FOV change (≤ 5 deg) with speed is a known game convention. It is optional.

### 2.2 Driver head motion

- Rule: the head is not welded to the chassis. Neck muscles and the vestibulo-ocular reflex stabilise gaze. The horizon is partly held.
- SimCruise model (`SimCruiseCameraModels.h`): spring 9 rad/s critically damped; 3 cm/g lateral, 2 cm/g longitudinal; brake pitch 1.5 deg/g; gaze lead 4 deg; 70 % of body roll shown.
- Rule: compute felt acceleration over the real time between the velocity samples. Do not divide a physics-step velocity change by a render-frame time.
- Rule: vibration has a spectrum. Body bounce is about 1-2 Hz. Wheel hop is about 10-15 Hz. Engine shake follows rpm. White noise sampled once per rendered frame has no physical spectrum. Its amplitude also changes with frame rate. (Ride frequencies: Gillespie, Fundamentals of Vehicle Dynamics, SAE 1992, unverified page.)
- Rule: pitch and roll of the view in the 0.2-0.4 Hz band cause motion sickness. Keep them low. (domain-ideas F-section "Two ship gates", ISO 2631-1 band, unverified.)
- Rule: gaze leads into a bend toward the tangent point, about 1-2 s ahead (Land and Lee 1994, Nature 369:742, unverified in primary).

### 2.3 Chase and scenic cameras

- Rule: position may lag. Rotation must also be smoothed, or the car shakes the whole frame. A camera whose rotation is welded to the car yaw transmits every steering twitch and bump. [C3]
- Rule: lag the camera's offset, not its world position. A world-space lag falls behind by v/rate at speed. (SimCruise already does this.)
- Rule: show slip. Swing the camera part of the way toward the velocity vector so a drift reads on screen. SimCruise uses 0.4 × beta.
- Rule: camera collision must not pop. Use a sphere sweep. Ignore thin obstacles (poles, trunks, small props). Pull in fast but ease out slowly. Or fade the obstacle instead of moving the camera. [C3]
- Rule: never put the camera inside the ground or a wall. Never let it cross the car.
- Typical chase numbers from SimCruise (ported from Godot): 5.8 m behind, 1.94 m up, look 3.9 m ahead at 0.7 m, 50 deg vertical.

### 2.4 Shutter and motion blur

- Rule: the film standard is a 180 deg shutter: exposure time = half the frame time. 1/48 s at 24 fps. (ASC Manual, unverified edition.)
- Rule: in Unreal, Motion Blur Amount 0.5 ≈ 180 deg shutter (unverified mapping; Epic docs call amount "strength").
- Rule: Unreal `MotionBlurTargetFPS` makes blur independent of the actual frame rate. A value of 0 makes blur depend on the real frame rate. [C2]
- Rule: in the seat, too much blur hides the road texture and kills the speed cue. Use a short shutter for play (about 1/120-1/250 s look). Use a longer shutter only for replays and photo mode.
- Rule: per-object blur of the player's own car in chase view must stay near zero; the camera follows the car.

### 2.5 Exposure

- Reflected-light meter: EV100 = log2(L × 100 / K), K = 12.5. Grey card L = 0.18 E / π.
- "Sunny 16": clear noon is EV100 about 15. A moonlit scene is about EV100 -3 to -4. Lit city streets are about EV100 3-6 (unverified range).
- Unreal auto exposure, histogram method [C1]:
  - Speed Up and Speed Down are in f-stops per second.
  - With "Extend default luminance range", Min/Max are EV100.
  - Low and High percent: Epic suggests start ranges of 70-80 and 80-95.
  - Exposure compensation: +1 doubles the brightness.
  - Local exposure: highlight and shadow contrast typically 0.6 to 1. Epic says set it up whenever Lumen GI is on.
- Rule: real eyes adapt to light fast (seconds) and to dark slowly (minutes). A tunnel exit should brighten fast; a tunnel entry should darken slower. Do not copy the full dark-adaptation time: it would be unplayable.
- Rule: clamp the auto exposure to a band around a physical reference. Then the sky cannot drag the street into darkness. (SimCruise does this.)

### 2.6 Tone map and colour

- Unreal's filmic tonemapper is ACES-based. It has Slope, Toe, Shoulder, Black Clip and White Clip controls. [C4]
- Rule: hold the sky. Do not let the sky clip to white in daylight. Use local exposure and the shoulder.
- Rule: keep saturation neutral by day. Reduce saturation at night (mesopic vision). SimCruise fades it to a third by EV -2.
- Rule: day-for-night convention in film: the sky stays slightly lighter than the land, so silhouettes read. Pure black skies read as a void. (ASC practice; unverified source text.)
- Rule: lit windows and signs must not clip to flat white. Vary intensity and colour temperature (2700 K to 6500 K).

### 2.7 Time of day light

- SimCruise computes sun and moon positions and IES daylight illuminance (`SimCruiseDaylightModel.cpp`, `SimCruiseSolarPosition.cpp`).
- Rule: low sun gives the best shapes. Golden hour is sun below about 6 deg elevation.
- Rule: blue hour is sun 4-8 deg below the horizon. Sky and artificial light balance there.
- Rule: shadow colour is blue from the sky. Shade must not go grey or black by day.

### 2.8 Composition for captures and replays

- Rule of thirds for the car in chase and scenic shots.
- Low camera for speed (bumper height, asphalt in frame). High camera for place.
- A long lens (70-135 mm look) compresses the street and shows the car in its place.
- Hard-mounted car cameras pass vibration through; that reads as speed (Ford v Ferrari, ASC article, domain-ideas F19).

## 3. How to judge SimCruise

### 3.1 Files to read first

- `Source/SimCruise/Private/Gameplay/SimCruisePlayerCameraManager.cpp` (views, collision, FOV).
- `Source/SimCruise/Private/Gameplay/SimCruiseCameraModels.cpp` and `Public/Gameplay/SimCruiseCameraModels.h` (head model, chase frame, g-meter).
- `Source/SimCruise/Private/Environment/SimCruiseEnvironment.cpp` (`ApplyExposure`, fog, sun, moon).
- `Source/SimCruise/Public/Environment/SimCruiseEnvironment.h` (exposure band and comments).
- `Source/SimCruise/Private/Environment/SimCruiseDaylightModel.cpp` (reference EV, `DisplayEV100`).
- `Source/SimCruise/Private/Environment/SimCruiseGraphics.cpp` (presets, screen percentage, TSR).
- `Config/DefaultEngine.ini` (auto exposure, local exposure, motion blur, pre-exposure).
- `Source/SimCruise/Private/Vehicle/SimCruiseVehicleMovementComponent.cpp` (`CopyTelemetry`: what the camera reads).
- `Source/SimCruise/Private/Gameplay/SimCruiseHUD.cpp` (what is drawn over the image).
- `docs/unreal-architecture.md` (light, sky, rendering; drive capture).
- `docs/research/domain-ideas.md` (section 5 camera and cinema).

### 3.2 Checks

1. FOV: compute the vertical FOV for each lens and view. Compare with the screen. Is the bonnet or dash in frame? Is the horizon in the upper half?
2. Head model: is acceleration computed from real sample times? Does it behave the same at 30, 60, 90 and 144 fps with real physics data, not only with synthetic data?
3. Vibration: does its amplitude and spectrum stay the same across frame rates?
4. Chase: is rotation smoothed? Does collision pop? Does the frame show slip?
5. Motion blur: is it set explicitly? Is it frame-rate independent? Is it short in the seat?
6. Exposure: is the band physical? Are Speed Up/Down sensible? Does the sky hold at noon? Does the road read at night?
7. Tone map and colour: are highlights clipped (sky, windows, signs)? Is shade blue, not grey?
8. Docs vs code: do numbers in docs match the code?
9. Captures: inspect PNG/JPG captures. Look for clipping, crushed blacks, noise, banding, repeated patterns.

### 3.3 Test methods

- Automation tests: `SimCruise.Gameplay.Camera.HeadModel`, `SimCruise.Gameplay.Camera.ChaseShowsSlip`, `SimCruise.Gameplay.Camera.HeadModelFrameRate` in `Source/SimCruise/Private/Gameplay/Tests/SimCruiseGameplayTests.cpp`.
- Add a "quantised physics" test: velocity changes only every 1/360 s; render frames take 5, 6 or 7 steps. Head offset noise must stay under 1 mm. G-meter noise under 0.02 g.
- Drive capture: `-SimCruiseDriveCapture` saves cockpit, chase and top shots, parked and moving.
- Exposure report: `SimCruise.Env.Report` prints reference EV and the band.
- Look development: `SimCruise.Env.Look ExposureCompensation <v>`, `OpenStops`, `CloseStops`, `Saturation`.
- Histogram check: in a capture, count pixels at 255 in the sky. Goal under 1 % at noon.
- Blind A/B: game capture vs a licensed photo of the same street and time. Score d'.
- Comfort: log camera pitch and roll; FFT; energy in 0.2-0.4 Hz must be low.

## 4. Common failure modes

1. Differentiating a stepped physics value per rendered frame. The result is aliasing noise.
2. Random shake per rendered frame. The shake changes with frame rate.
3. Camera rotation welded to the car. Every twitch shakes the world.
4. Instant collision pull-in on thin objects. The camera jumps past poles and trees.
5. Engine default motion blur. It changes with frame rate and preset.
6. Auto exposure with no band. The sky darkens the street.
7. Night with a pure black sky and clipped white windows.
8. Docs that keep old numbers after a look change.
9. Local exposure set below Epic's suggested range. Midtones go flat (risk, not a rule).

## 5. Severity guide

- P0: the camera blocks driving (inside geometry, black screen, wrong view, sickness-level motion).
- P1: a visible camera or image fault in normal driving, every drive (jitter, pops, clipping in the main view).
- P2: a fault in rare views or only at night; a look improvement; a doc mismatch.

## 6. References

- [C1] Epic Games. Auto Exposure in Unreal Engine. https://dev.epicgames.com/documentation/en-us/unreal-engine/auto-exposure-in-unreal-engine . Checked: speed in f-stops per second; Low 70-80, High 80-95 start ranges; local exposure contrast 0.6-1.
- [C2] Epic Games. FPostProcessSettings::MotionBlurTargetFPS. https://docs.unrealengine.com/4.26/en-US/API/Runtime/Engine/Engine/FPostProcessSettings/MotionBlurTargetFPS/index.html . Checked: "A value of zero makes the motion blur dependent on the actual frame rate."
- [C3] Nesky, J. (2014). 50 Game Camera Mistakes. GDC 2014. https://gdcvault.com/play/1020460/50-Camera . (Talk; summary at https://gamedeveloper.com/design/video-50-common-game-camera-mistakes----and-how-to-fix-them . Not re-watched here.)
- [C4] Epic Games. Color Grading and the Filmic Tonemapper. https://dev.epicgames.com/documentation/en-us/unreal-engine/color-grading-and-the-filmic-tonemapper-in-unreal-engine . (Not fetched in this session.)
- [C5] GDC 2018. Vehicle Feel Masterclass: Balancing Arcade Accessibility with Simulation Depth. https://gdcvault.com/play/1025295/ . (Not watched here.)
- [C6] Diels, C., Parkes, A. M. (2010). Geometric field of view manipulations affect perceived speed in driving simulators. Advances in Transportation Studies XXII:53-64. https://pureportal.coventry.ac.uk/en/publications/geometric-field-of-view-manipulations-affect-perceived-speed-in-d/ . Checked abstract.
- [C7] American Society of Cinematographers. American Cinematographer Manual (10th/11th ed.). ASC Press. (Not read here. Standard reference for shutter angle and exposure.)
- [C8] ASC. Ford v Ferrari: Lap of Honor. https://theasc.com/articles/ford-v-ferrari-lap-of-honor (link from domain-ideas F19).
- [C9] Land, M. F., Lee, D. N. (1994). Where we look when we steer. Nature 369:742-744. (Not fetched.)

Licences: all sources are cited, not copied. Epic documentation is read under the Epic website terms. No text or figures are reproduced beyond short quotes.

## What I now know (onboarding, 2026-11-14)

This is a desk study of the code. I could not run Unreal here. Judgments without a run are marked (estimate).

**How my area works today.**
- `ASimCruisePlayerCameraManager::UpdateViewTarget` (`Source/SimCruise/Private/Gameplay/SimCruisePlayerCameraManager.cpp:92`) has five views: Driver, Hood, Near chase, Far chase, Scenic. A car with no cabin (no steering wheel found) shows Chase instead of Driver (:143-144).
- Lens (`SimCruiseCameraModels.cpp:19-51`, `SimCruiseCameraModels.h:31-35`): natural 45° vertical (72.7° across 16:9), wide 67°, chase 50°. True FOV is 2·atan(H/2D) from `simcruise.Lens.ScreenInches` 15.6 and `ViewDistanceCm` 60, clamped 10-90°. Every view and every lens widens by up to 6 % at 36 m/s (:154). This includes true FOV and a seat FOV that the player set.
- Head model (`SimCruiseCameraModels.cpp:118-200`):
  - It is a critically damped spring at 9 rad/s, 3 cm/g lateral and 2 cm/g longitudinal (clamped ±3/±2 cm).
  - Brake pitch is 1.5°/g. Gaze lead is 4°. The horizon shows 70 % of body roll.
  - Acceleration now uses the physics sample time `SimTimeS` (:83-102), not the frame time.
  - Vibration is 2.2 mm of RNG noise per rendered frame, filtered at 30/s (:179-181). Its spectrum still changes with the frame rate.
- Chase (`SimCruiseCameraModels.cpp:223-280`):
  - Frame: yaw only, swung 0.4·β (±35°, faded in from 2 to 8 m/s).
  - Offsets: near -5.8/+1.92→0.35/+1.94 m; far -10.5/+0.9/+3.3 m; scenic -7.2/+4.9/+3.45 m.
  - The offset lags at rate 10/s (τ 0.1 s). The look-at uses the car heading with no lag (:238-264), so the camera turns at once with every yaw twitch.
- Chase collision (`SimCruisePlayerCameraManager.cpp:198-217`):
  - A ground line trace starts 2.5 m above the camera and keeps it 55 cm above the ground. Nothing smooths this clamp.
  - A wall line trace runs on ECC_Camera from 1.12 m above the car to the camera. On a hit it puts the camera 22 cm in front of the hit, at once (`OverrideLocation`, `SimCruiseCameraModels.h:153`). It moves back out at the same 0.1 s rate.
  - There is no sphere sweep. Nothing is filtered out as a thin obstacle.
- Every BlockAll object blocks that trace: tree trunk capsules (`World/SimCruiseTrees.cpp:454,801`), palms (`SimCruiseWorldTile.cpp:339`), parked-car boxes (:589), traffic boxes (`SimCruiseTraffic.cpp:148`), guard rails, and the tile mesh (`SimCruiseWorldTile.cpp:149`).
- Exposure (`Environment/SimCruiseEnvironment.cpp:423-482`):
  - Histogram metering over 10-90 %. Speed up 2 stops/s, speed down 1 stop/s.
  - The band is `DisplayEV100(ref)` -3 to -0.5 EV (`SimCruiseEnvironment.h:156,163`).
  - Bias is 0.2 + (-0.3 × day), so -0.1 by day and +0.2 at night.
  - Motion blur: amount 0.15, TargetFPS 0, max 1.0. Saturation falls to 0.35 at night.
  - `DisplayEV100` has a knee at EV 10 and a slope of 0.55 below it (`SimCruiseDaylightModel.cpp:111-116`).
- The reference EV is an 18 % card under the open sky. Its inputs are sun elevation, cloud cover, moon lux and one artificial term (:404).
  - The only artificial source is the player's low beams, `NightAdaptationLux` (`Vehicle/SimCruiseVehicle.cpp:267`).
  - Street lights, tree canopy and the walls of a street canyon do not change the reference.
- Local exposure in `Config/DefaultEngine.ini:34-35` is highlight 0.55 and shadow 0.95. No code overrides it.
- Drive capture (`SimCruiseDriveCapture.cpp:213-300`):
  - Shots are taken at fixed clock times: cockpit 2.0 s, chase 4.6 s, top (60°, 35 m) 7.3 s, then moving shots from 15.0 to 17.15 s at 45 % throttle.
  - Hood and far chase are shot only 0.45-0.5 s after the view changes.
  - The SHOT line logs speed, gear, rpm, wheel contacts and `Describe()`. That text gives the band, not the exposure the camera actually used.
- View check (`SimCruiseViewCheck.cpp:147-208, 117-143`):
  - It measures luma SD, edges and hue bins only in the lower 60 % of the frame (`Y0 = 0.4H`).
  - It has no test for clipped sky, crushed black or mean exposure.

**Does it work at a random US place?**
- General: the lens, the head model, the exposure reference (sun and cloud from the place and time) and the snap on teleport do not depend on the road. They run the same at us-anchorage-ak (low winter sun) and us-hilo-hi (estimate).
- us-jasper-ar, us-seattle-wa, us-hilo-hi (trees and palms beside the road): each trunk or palm between the car and the chase camera pulls the camera in at once. The camera then moves back out in about 0.1 s, so the frame pumps in bends (estimate, from the code above).
- us-bronx-ny: a following SUV or truck taller than the trace height (about 1.5-1.9 m at that point) and parked cars put the camera 22 cm from their box. Scenic sits 4.9 m to the right and enters the parked-car lane and the walls on narrow side streets (estimate).
- us-hazard-ky and us-welch-wv cut slopes: the wall trace hits the terrain and pulls the camera in. A hit point is not checked again for the 55 cm ground rule, so the camera can sit close to the slope face (inferred).
- us-seattle-wa grades: on a 26 % grade the ground behind is about 1.5 m higher, so the near chase drops to about 0.44 m and the clamp lifts it. The clamp follows each DEM triangle with no smoothing (estimate).
- Tunnels: the helper drops tunnel roads (`helper/atlas.mjs:115`, `p.brunnel==='tunnel'` returns). No tunnel exists in the world, so tunnel exposure is never exercised. Under dense canopy (us-jasper-ar) the band opens at most 3 EV. A road 4-6 EV under open sky stays 1-3 EV dark (estimate).

**Main gaps against "realistic everywhere".**
1. P1: each floating-origin rebase (every 2 km, `World/SimCruiseWorldStreamer.h:84`) calls `SnapNextFrame` (`Gameplay/SimCruiseGameMode.cpp:690-706`). The jump test fires too (`SimCruisePlayerCameraManager.cpp:119`).
   - Driver view: `Head.Reset()` (:159-162) zeroes head offset, gaze lead (up to 4°) and brake pitch in one frame.
   - Chase view: swing and offset snap.
   - The player sees a view jump every 2 km, at every place. The jump is verified in code. Its size on screen is an estimate.
2. P1: the chase collision is a line trace that pulls in at once and eases out fast, and thin props block it. `SimCruisePlayerCameraManager.cpp:210-216`, plus the BlockAll props listed above.
3. P2: the chase look-at follows the car heading with no lag. `SimCruiseCameraModels.cpp:238-264`.
4. P2: the exposure reference is open sky only. Canopy, canyon streets and street lights do not change the band. `SimCruiseEnvironment.cpp:404,437-440`. Tunnels are not built at all (`helper/atlas.mjs:115`).
5. P2: the capture tools do not measure exposure. The view check ignores the top 40 % of the frame. SHOT lines do not log the exposure actually used. `SimCruiseViewCheck.cpp:154`, `SimCruiseDriveCapture.cpp:112`.
6. P2: vibration is per-frame RNG, so its spectrum changes with the frame rate. `SimCruiseCameraModels.cpp:180-181`.

**My first three actions.**
1. Rebase without a camera pop.
   - What: add `ASimCruisePlayerCameraManager::ShiftOrigin(Offset)`, which does `LastBodyLocation += Offset`. Call it from `ASimCruiseGameMode::HandleRebased` instead of `SnapNextFrame`.
   - Done when (test): a new automation test rebases mid-corner. The head offset changes < 1 mm, gaze < 0.05°, and the chase offset < 1 cm across the rebase.
   - Done when (run): `scripts\preflight.ps1 -Roads random -Seconds 120` shows at least one "Floating origin moved" line, with no frame step at that time in the video.
2. Chase collision without pops.
   - What: in `SimCruisePlayerCameraManager.cpp:212`, use a 20 cm sphere sweep. Make trunks, palms, traffic and parked boxes ignore ECC_Camera. Keep the instant pull-in and ease out with a τ of about 0.6 s. Check the ground rule again after a wall hit.
   - Done when: preflight on `us-jasper-ar`, `us-bronx-ny` and `us-hazard-ky` in chase and scenic logs the camera-to-pivot distance. It never changes by more than 0.5 m in one frame except on snaps.
3. Exposure numbers in the captures.
   - What: in `SimCruiseViewCheck.cpp` `MeasureImage`, add the % of top-40 % pixels at 255 and the % of bottom-60 % pixels at luma ≤ 5. Log the exposure actually used in the `Shoot` SHOT line.
   - Done when: `scripts\preflight.ps1 -Roads random` plus `us-seattle-wa` and `us-jasper-ar` at 12:00 show sky clip < 1 % and road crush < 5 %.

**Docs that are wrong or old.**
- `docs/unreal-architecture.md` "Light in physical units" says "+0.7 compensation" and "Local exposure 0.8 / 0.8". The code bias is 0.2 + (-0.3 × day) (`SimCruiseEnvironment.cpp:443`, `.h:172,181`). The ini local exposure is 0.55 / 0.95 (`DefaultEngine.ini:34-35`).
- `docs/unreal-architecture.md` "Cameras" says the head model is "differentiated over the real frame time". The code uses physics sample time (`SimCruiseCameraModels.cpp:83-102`). The section also does not list the Hood and Far chase views.
- `docs/controls.md` says natural is "74° across a 16:9 screen". The code gives 72.7° (`SimCruiseCameraModels.cpp:47-51`), and 77° only with the full speed widening. The "true FOV" text says "as if the screen were a window", but the code widens it by up to 6 % with speed (`SimCruisePlayerCameraManager.cpp:174`).
- `docs/team/cinematographer.md` §2.1 says not to change the seat FOV with speed. The code does (:154). The rule or the code must change.
