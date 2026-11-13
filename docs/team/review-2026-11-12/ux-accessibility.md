# UX and accessibility review, 2026-11-12

Reviewer: ux-accessibility (see docs/team/ux-accessibility.md).
Scope: `SimCruiseInputConfig.cpp`, `SimCruiseDriverShaping.cpp`, `SimCruiseHaptics.cpp`, `SimCruiseHUD.cpp`, `SimCruiseAtlasUI.cpp`, `SimCruisePreferences.cpp`, the related parts of `SimCruisePlayerController.cpp`, `docs/controls.md`, gameplay tests.
Method: code reading and arithmetic. The game was not run in this session. Pixel and contrast numbers are estimates; the arithmetic is shown.

## Summary

The driving input is careful work. Dead zones rescale (test `SimCruise.Gameplay.Input.DeadZonesAndDirections`). The stick curve is speed-aware and keeps half stick near the grip limit at highway speed (Miata at 30 m/s: half stick about 1.27 deg of steer, about 0.9 g). The throttle curve is documented. Rumble is quiet when coasting. Head motion and rumble strength are remembered settings. The problems are in the layer around the driving: a risky button choice, a manual gearbox that needs brake and clutch on one finger, HUD text that is small and low in contrast, and settings that the pad may not reach.

## Findings

### 1. HUD toggle on the steering stick click (L3)  (P1)
Evidence: `SimCruiseInputConfig.cpp:54` `{TEXT("CycleHud"), EKeys::Gamepad_LeftThumbstick}`; `SimCruiseHUD.cpp:72-76` `CycleLevel` shows the message for 1.5 s only.
Problem: the left stick steers. In a hard corner or a slide catch the thumb pushes the stick to the gate and often clicks it. Each click cycles Glance -> Full -> Off. The HUD can vanish mid-corner, and the choice is saved to `preferences.json` (`SimCruisePlayerController.cpp:424`). XAG 107 asks to avoid accidental and demanding inputs (https://devdocs.xbox.com/build/game-principles/accessibility/xag-deep-dives/xag-107-input.md).
Fix: move `CycleHud` to D-pad Left (D-pad Up/Down already zoom the minimap). Leave L3 unbound while driving.
Verify: extend `SimCruise.Gameplay.Input.BindingsMatchControlsDoc` to assert that no action is bound to `Gamepad_LeftThumbstick`; update `docs/controls.md:19` and the hint at `SimCruiseHUD.cpp:371`.

### 2. Manual gearbox needs brake and clutch on the same finger  (P1)
Evidence: `SimCruiseInputConfig.cpp:29` brake on LT, `:44` clutch on LB (hold), `:47-48` shift down on X; gear change refused without the clutch (`VehicleModelTests.cpp:288` "Hold the clutch to change gear").
Problem: a downshift while braking into a corner needs LT and LB held by the left index finger at the same time, plus X with the right thumb. Most players cannot hold both shoulder inputs with one finger. XAG 107: avoid simultaneous presses; offer an alternative (link above). This makes the manual mode, the most "driver's" mode, hard on the main input device.
Fix (smallest): when the clutch is not held and the input is the pad, let a shift request run an automatic clutch dip (press, shift, release over `shift_seconds`). Keep the held clutch for wheel users and for players who want it. If this counts as a feature, schedule it as NP1.
Verify: new test: pad manual mode, brake 0.6, `RequestGearChange(-1)` with no clutch input gives the lower gear and no stall at 15 m/s; existing "no gear without the clutch" test kept for the wheel path.

### 3. Glance HUD: small text and low contrast over a bright scene  (P1)
Evidence: `SimCruiseHUD.cpp:320-342` Glance panel alpha 0.38 (`:329`), text at 10 pt for rpm, "MPH" and ABS (`:335`, `:337`, `:340`), gear 13 pt (`:336`); `SimCruiseHUD.h:24` `GlanceFaded = 0.4f` multiplies text and panel while moving; `SimCruiseHUD.cpp:159` scale = screen height / 900; `:208` base font 24 pt.
Estimate (unverified pixel mapping: a 24 pt Roboto legacy font renders about 32 px per em at 96 DPI):
- Size: 10 pt -> 10/24 x 32 = 13 px at 900 p, 16 px at 1080p. XAG 101 asks for at least 26 px at 1080p on a TV (https://devdocs.xbox.com/build/game-principles/accessibility/xag-deep-dives/xag-101-text-display.md). Speed (32 pt, about 51 px) passes; rpm, gear, MPH, ABS, place-card note (9 pt, `:203`), messages (12 pt, `:388`) fail.
- Contrast (bright sky, assumed linear luminance 0.7): awake, panel 0.38 -> background L = 0.44; Paper text L = 0.945 -> ratio 2.0:1; Dim rpm text L = 0.65 -> 1.4:1. Faded while moving (alpha 0.4) -> about 1.2:1. XAG 102 asks for 4.5:1, measured against the worst part of the background (https://devdocs.xbox.com/build/game-principles/accessibility/xag-deep-dives/xag-102-contrast.md).
Problem: on a sunny road (the default start is Miami Beach, morning sun) gear and rpm are hard to read at TV distance, and the speed fades to 40 % whenever the car moves.
Fix: raise the minimum driving text to about 20 pt in this scale (26 px at 1080p); make the panel at least 0.75 opaque behind text; fade only the g-meter and minimap, never speed and gear below alpha 0.85; add a text-size setting later (NP3).
Verify: capture at 1080p with `-SimCruiseShellCapture` at the Miami start at noon; measure the gear glyph height (>= 26 px body) and contrast (>= 4.5:1) at the brightest pixel behind the panel. Add a unit test that `GlanceFaded` applies only to the non-essential items.

### 4. Atlas settings may be unreachable with the pad  (P1, needs an in-game check)
Evidence: the overlay focuses the atlas root (`SimCruisePlayerController.cpp:538` `ShowOverlay(AtlasPanel..., AtlasPanel)`); `SimCruiseAtlasUI.cpp:940-961` handles B, Menu, D-pad Up/Down, A, X, Y, RB, LB and keyboard arrows, and the comment says it keeps "the d-pad's sides from moving focus to the buttons"; the pad hint (`SimCruiseAtlasUI.cpp:593`) names no way to reach Sound, HUD, Head motion or Rumble. `docs/controls.md:29` says these buttons set the settings.
Problem: if the pad cannot move focus to the settings row, a pad-only player cannot turn off head motion or rumble, or turn sound off (M is keyboard only, `SimCruiseInputConfig.cpp:57`). Head motion is a motion-comfort setting (XAG 117, https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/117). Whether Slate navigation from D-pad Left/Right reaches the buttons could not be confirmed from code (the handler checks keyboard `EKeys::Left/Right`, not `Gamepad_DPad_Left/Right`).
Fix: give the settings direct pad routes in the atlas (for example D-pad Left/Right moves a visible focus along the settings row, A activates) and name them in the hint line.
Verify: shell self-test that opens the atlas with simulated pad input only and changes each of the four settings; check `preferences.json` after.

### 5. Trigger rumble is documented but can never run  (P2)
Evidence: `SimCruisePlayerController.cpp:71` `RumbleBackend = ESimCruiseRumbleBackend::XInput;` (always); `SimCruiseHaptics.cpp:67-73` the GameInput branch that feeds the trigger motors is unreachable; `SimCruiseHaptics.cpp:140-148` computes `RightTrigger` / `LeftTrigger` every frame for nothing. `docs/controls.md:37` says "with Windows GameInput the trigger motors carry wheelspin (RT) and locking or ABS (LT)".
Problem: the docs promise a pedal cue that no player gets. The ABS cue still reaches the body motor, so no information is lost, but the doc is wrong.
Fix: this phase: change the doc sentence to "not active yet". Next phase: enable GameInput for pads behind a setting and test on hardware.
Verify: doc review; a test that asserts the backend choice and the doc agree.

### 6. Pad users cannot reach lens (FOV), lights or stats  (P2)
Evidence: `SimCruiseInputConfig.cpp:55-58` `ToggleStats` F3, `CycleLens` F6, `ToggleSound` M, `ToggleLights` L have no pad binding; the atlas settings list has four items only (`SimCruisePlayerController.cpp:478-483`), no lens.
Problem: the field of view is a key motion-comfort setting (XAG 117; GAG "allow FOV to be adjusted"). Lights follow the sun (`SimCruisePlayerController.cpp:630`), so lights are minor.
Fix: add "Lens" as a fifth atlas setting (same `ChangeSetting` ladder pattern). This reuses existing code.
Verify: atlas pad self-test changes the lens; `preferences.json` holds `lens`.

### 7. Shift light uses colour alone  (P2)
Evidence: `SimCruiseHUD.cpp:260` the rpm bar fill is Paper, then Amber near the upshift point, then Signal red near the redline; nothing else changes.
Problem: amber (1.0, 0.66, 0.15) and red (1.0, 0.24, 0.18) are hard to tell apart for protan and deutan players, and at 4-6 px bar height (`:342`, `:361`) the colour patch is tiny. GAG: no essential information by colour alone (https://gameaccessibilityguidelines.com/ensure-no-essential-information-is-conveyed-by-a-fixed-colour-alone/).
Fix: add a second cue near the redline: the bar blinks at 4 Hz or a "SHIFT" label appears. Note: the engine limiter sound is the natural second cue, but it is broken for the C8 and the Mustang (sound-designer finding 1).
Verify: screenshot through a deuteranopia filter; HUD rules test for the blink state.

### 8. Dead zones and curves are fixed or console-only  (P2)
Evidence: `SimCruiseInputConfig.cpp:12-13` stick 0.12, triggers 0.04, constants; `SimCruiseDriverShaping.cpp:7-11` trigger gamma is a console variable only; no steering sensitivity setting.
Problem: a worn pad that rests at 0.15 on X steers the car by itself; a player with limited thumb range cannot raise sensitivity. XAG 107 asks for in-game sensitivity of each analog input with at least +-50 % range. Microsoft's own XInput default left-stick dead zone is 0.24 radial (https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput), so 0.12 is on the small side for old pads.
Fix: next phase (NP2). This phase: none; keep the defaults.
Verify: see NP2.

### 9. Steering reads only the stick's X axis  (P2, low confidence)
Evidence: `SimCruiseInputConfig.cpp:32` `SteerPad` on `Gamepad_LeftX` with an Axial dead zone (`:126`).
Problem: a thumb that rolls along the round gate gives less steer on the diagonals (at 45 degrees, X = 0.71). Some players steer this way. Unverified how common this is.
Fix: none this phase. Next phase: test an optional "angle steering" mode in the player study (NP2).
Verify: player test with a lane-change course.

### 10. Message toasts are short and small  (P2)
Evidence: `SimCruiseHUD.cpp:388` message text 12 pt (about 19 px at 1080p by the estimate in finding 3); gear refusals show for 2 s (`SimCruisePlayerController.cpp:386-394`).
Problem: "Hold the clutch to change gear" appears for 2 s in small text at the top while the eyes are on the road. XAG 101 size rule applies.
Fix: raise to the minimum text size; show refusals for at least 3 s.
Verify: capture measurement as in finding 3.

## Open questions

- Pixel size of a 24 pt UE legacy font on the Canvas at 1080p (assumed 32 px per em). Measure in a capture.
- Slate gamepad navigation in `SSimCruiseRoadAtlas` (finding 4) needs an in-game check.
- No peer-reviewed study of gamepad steering curves was found; the curve checks are engineering practice.

## Next phase proposals

Ranked. None of this work has started.

### NP1. Pad-friendly manual gearbox (auto-clutch assist)  (size S)
- Goal: a pad player drives the manual five-speed without holding the clutch.
- Why: "the ultimate driver's experience" on the main input device; the Miata is a manual car.
- Scope: an "Auto clutch" option (default on for the pad, off for wheels); the shift request dips the clutch over `shift_seconds`; launch from rest uses the existing launch logic.
- Acceptance test: automation tests for up- and downshift under braking with no clutch input; owner play test: heel-and-toe-free downshift from 3rd to 2nd into a corner without stall in 10 of 10 tries.
- Risk: purists may want the full clutch; keep it as an option.

### NP2. Controls settings page: dead zone, sensitivity, curves, remap  (size M)
- Goal: XAG 107 compliance for the pad: in-game remapping of all actions, per-input sensitivity (+-50 %), stick and trigger dead zones with a live readout.
- Scope: one atlas page; data in `preferences.json`; the binding table stays the single source and gains a user override layer; hints follow the remap.
- Acceptance test: every action can be moved to any pad button; dead-zone readout shows raw and shaped values; tests prove that resting inputs give exactly 0 at every dead-zone value; settings survive a restart.
- Risk: hint text and docs must follow remaps; conflict handling.

### NP3. Readable HUD at TV distance  (size M)
- Goal: XAG 101 and 102 compliance for all driving text.
- Scope: minimum 26 px text at 1080p, a text-size setting to 200 %, a backing-plate opacity setting, a high-contrast option (7:1), a non-colour shift cue, km/h option for roads outside the US.
- Acceptance test: capture-based checks at noon and at night for three places; measured sizes and contrast pass; a colour-blind simulation pass.
- Risk: screen space on small displays; keep the Glance idea.

### NP4. Comfort and settings from the pad  (size S)
- Goal: every comfort setting (lens/FOV with a slider, head motion, rumble, master and channel volumes, camera) reachable and visible from the pad.
- Acceptance test: pad-only self-test reaches and changes each setting; XAG 117 checklist items pass.
- Risk: low.

### NP5. Hardware haptics pass  (size M)
- Goal: impulse-trigger cues (wheelspin on RT, lock-up/ABS on LT) on real Xbox pads through GameInput, and a measured check that each rumble cue starts before the grip limit.
- Acceptance test: on hardware, a blindfold-style test (HUD off): the owner names front slip, rear slip, wheelspin and ABS from the pad alone in 8 of 10 trials.
- Risk: GameInput driver behaviour on Windows PCs; needs hardware.
