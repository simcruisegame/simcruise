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
