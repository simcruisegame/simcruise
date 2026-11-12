# UX and accessibility: study notes

Role: controls and human factors for a gamepad driving game. Scope: stick dead zones and response curves, speed-sensitive steering, trigger mapping, rumble as information, HUD readability, menus, Xbox Accessibility Guidelines (XAG), Game Accessibility Guidelines (GAG), motion comfort, colour-blind safe UI.
Written 2026-11-12. Style: ASD-STE100 Simplified Technical English.
Marks: **[V]** = verified in a source on 2026-11-12. **[U]** = not verified; treat as an estimate. **[C]** = read from SimCruise code.

## 1. Principles

1. The Xbox controller is the main input. Every driving and menu function must work from the pad alone.
2. Input shaping changes what the thumb asks for. It never changes the physics. (SimCruise rule, docs/controls.md) **[C]**
3. A setting that changes comfort or reach (dead zone, curve, rumble, FOV, head motion, text size) must be adjustable in game, from the pad.
4. Information must reach the player by at least two channels (for example colour and shape, or sound and rumble).
5. Accidental input must not change state that the player cannot see at once.

## 2. Thumbstick

### 2.1 Dead zone

- Microsoft XInput defines default dead zones: left stick 7849, right stick 8689 (of 32767), trigger threshold 30 (of 255). **[V]** (Getting Started With XInput)
- 7849 / 32767 = 0.24 of travel (radial). 30 / 255 = 0.12 of trigger travel.
- Microsoft recommends a circular (radial) dead zone and a rescale of the remaining range to 0..1. A non-linear transform may follow. **[V]**
- These defaults are conservative. Modern pads often need less. Worn pads can need more. So the dead zone must be adjustable. **[U]** for the share of worn pads.
- SimCruise: stick X axial dead zone 0.12, triggers 0.04, both rescaled (Enhanced Input `UInputModifierDeadZone`, Axial). **[C]** (`SimCruiseInputConfig.cpp:12-13`; test `SimCruise.Gameplay.Input.DeadZonesAndDirections`)

Rules:
- Rescale after the dead zone. A step from 0 to the dead-zone value is a bug.
- Give the player a dead-zone setting from 0 to 0.30 for the steering stick, and 0 to 0.15 for each trigger.
- Show a live stick readout next to the setting, so the player can see drift.

### 2.2 Response curve and speed-sensitive steering

- A thumbstick has about 1-2 cm of travel. A steering wheel has about 900 degrees. So the stick must map to a speed-dependent range. **[U]** (travel numbers are typical)
- Good practice: the stick centre gives fine lane corrections at speed. The stick end gives the grip limit (not more) at speed and full lock when parking.
- SimCruise: `ThumbstickCommand` = s x (c + (1 - c) s^2), with c = Ackermann angle for 0.89 g / available angle (clamped 0.16..1). The available angle = Ackermann angle for 0.95 g + 5 degrees slip surplus, blended into the parking lock. **[C]** (`SimCruiseDriverShaping.cpp:30-66`)
- Worked example, Miata at 30 m/s: available 6.3 degrees, c = 0.20. Half stick gives about 1.27 degrees (about 0.9 g steady state). A lane change (0.1-0.2 g) needs a stick of about 0.2 after the dead zone. **[C]** (computed from code and car data)
- Slew limits: pad apply 2.75 x available range per s, return 4.4 x per s, countersteer up to 40 deg/s. **[C]**

Rules:
- At any speed, 50 % stick should not exceed the steady grip limit. 100 % stick may reach the limit plus a small slip surplus.
- Keep a sensitivity setting with at least +-50 % of the default. XAG 107 asks for this range for each analog input. **[V]**
- Keep the stick release returning to zero. Do not add hidden self-centring (SimCruise rule). **[C]**
- Test with a slalom and a lane change at 50, 80 and 110 km/h. Count corrections per second and peak overshoot.

### 2.3 Triggers

- A trigger has about 1 cm of travel. A pedal has about 6 cm. **[U]** (as stated in docs/controls.md)
- SimCruise: throttle = trigger^1.8 (cvar `simcruise.TriggerGamma`, 1..3). Brake is linear. **[C]**
- Rule: expose the throttle curve and the brake curve as settings. Default stays as now.
- Rule: a resting trigger must give exactly 0 pedal. Check worn triggers that rest at 3-8 %.

## 3. Button mapping

- XAG 107: provide in-game remapping of all controls. Ideally any action can go to any input. Update hints and diagrams after remapping. **[V]**
- XAG 107: avoid simultaneous button presses. Avoid long holds. Offer toggles or auto alternatives. **[V]**
- Stick clicks (L3, R3) are easy to press by accident under load, especially on the steering stick. **[U]** (common design knowledge, not a cited study)

Rules:
- Do not put a state-changing action on L3 while the left stick steers.
- Manual gearbox: do not require the same finger for brake and clutch. Offer an auto-clutch for the pad.
- Every action that has a keyboard key must have a pad route (direct button, a radial, or a menu entry).

## 4. Haptics as information

- Rumble must carry a meaning. In a driving game: front slip (understeer), rear slip (oversteer), wheelspin, lock-up, ABS, kerbs, surface. **[C]** (SimCruise design, `SimCruiseHaptics.h`)
- The two body motors differ: left = low frequency (heavy), right = high frequency (light). **[V]** (XInput motor convention, UE XInput mapping as quoted in `SimCruiseHaptics.h`)
- Impulse trigger motors exist on Xbox One and Series pads. Windows exposes them through GameInput / Windows.Gaming.Input, not classic XInput. **[U]**
- Rumble must be adjustable from 0 to 100 %. SimCruise steps 100/60/30/0 %. **[C]**
- Rules:
  - Coasting straight on smooth asphalt must give zero rumble.
  - A cue must start before the limit (warning), not only after it (alarm).
  - Do not put a continuous engine buzz on a motor at a level that masks the slip cue. Keep it below 10 %.
  - Tell the truth in docs. If a motor path is off, the docs must say so.

## 5. HUD readability

- XAG 101: minimum body text height at 1080p is 26 px for console/TV and 18 px for PC. Players should scale text up to 200 %. **[V]**
- XAG 102: contrast at least 4.5:1 for standard text and elements, 3:1 for large text (console large text starts at 52 px at 1080p), 7:1 in a high-contrast mode. Measure against the lowest-contrast part of a non-solid background. **[V]**
- XAG 102 suggests a solid backing plate with player-adjustable opacity, text colour options and outlines. **[V]**
- SimCruise targets a TV with an Xbox pad. So use the console figure (26 px) for driving text that the player must read.

Contrast formula (WCAG relative luminance, linear RGB): L = 0.2126 R + 0.7152 G + 0.0722 B. Ratio = (L1 + 0.05) / (L2 + 0.05). `FLinearColor` values are already linear.

Rules:
- Speed and gear must be readable in one glance (under about 0.5 s) at 3 m from a 55 inch TV.
- Do not fade speed below a readable contrast while the car moves.
- Use a backing plate of at least 70 % opacity behind small text over the 3D scene.
- Check text pixel height in a 1080p screenshot. The cap height of the smallest driving text must be at least about 18 px (26 px body). **[U]** for the cap-height to body ratio.

## 6. Colour-blind safe UI

- Do not carry essential information by colour alone. **[V]** (Game Accessibility Guidelines: "Ensure no essential information is conveyed by a fixed colour alone")
- About 8 % of men and 0.5 % of women have a colour vision deficiency. **[U]**
- Red vs amber vs green is the hardest set for protan and deutan players.
- Rules:
  - Add a second channel to each colour state: text ("SHIFT"), a shape, a flash, or a position.
  - Test each HUD screenshot with a deuteranopia and protanopia simulation.

## 7. Motion comfort

- XAG 117: avoid or offer to turn off camera shake, head bob and motion blur. Offer adjustable FOV. Offer first and third person views. **[V]**
- GAG: allow FOV adjustment in 3D games. **[V]** (guideline title)
- Visually induced motion sickness is worst at about 0.2-0.4 Hz oscillation. **[U]** (cited in docs/research/domain-ideas.md F9; not re-read)
- SimCruise: head motion 100/50/0 %, lens Natural (45 deg vertical) / Screen / Wide (67 deg), chase view. **[C]**
- Rules:
  - Every comfort setting must be reachable from the pad.
  - Do not add camera motion at 0.2-0.4 Hz that the player did not cause.
  - Remember comfort settings between sessions (SimCruise does this in `preferences.json`). **[C]**

## 8. Menus

- The pad must reach every button. Focus must be visible. B must go back. **[U]** (common platform practice; XAG 112 covers UI navigation, not re-read)
- Hint lines must name the pad buttons when the pad is in use and keys when keys are in use. SimCruise does this in the Full HUD. **[C]**
- Pause the car while a menu is open. SimCruise does this. **[C]**

## 9. How to judge SimCruise

Files to read first:
- `Source/SimCruise/Private/Gameplay/SimCruiseInputConfig.cpp` (bindings, dead zones).
- `Source/SimCruise/Private/Gameplay/SimCruiseDriverShaping.cpp` and its header (stick, trigger, rack slew).
- `Source/SimCruise/Private/Gameplay/SimCruiseHaptics.cpp` and its header; `SimCruisePlayerController.cpp` (`UpdateRumble`, `ChangeSetting`, `AtlasView`).
- `Source/SimCruise/Private/Gameplay/SimCruiseHUD.cpp` and its header (Glance, Full, minimap, messages).
- `Source/SimCruise/Private/Gameplay/SimCruiseAtlasUI.cpp` (road atlas, place search, settings buttons).
- `Source/SimCruise/Private/Gameplay/SimCruisePreferences.cpp` (remembered settings).
- `docs/controls.md`.
- Tests: `Private/Gameplay/Tests/SimCruiseGameplayTests.cpp` (`SimCruise.Gameplay.Input.*`, `Steering.*`, `Haptics.*`, `Hud.*`).

Checks:
1. Bindings: no state change on L3 or R3 during driving except harmless ones. Each keyboard-only action has a pad route.
2. Dead zone: rescaled; adjustable; resting stick and trigger give exactly 0.
3. Curve: at 30 m/s, 50 % stick gives at most the steady grip limit. Test with `ThumbstickCommand`.
4. Simultaneous presses: list each action that needs two inputs at once. Each one needs an alternative.
5. Text: smallest driving text >= 26 px body at 1080p. Measure in a capture (`-SimCruiseShellCapture`).
6. Contrast: >= 4.5:1 against the brightest sky and road in Miami at noon.
7. Colour: each colour state has a second cue.
8. Rumble: zero when coasting straight on smooth asphalt (test `SimCruise.Gameplay.Haptics.SlipRumble`). Each motor path that docs describe is live.
9. Comfort: FOV, head motion, rumble, sound reachable from the pad.
10. Menus: every button of the atlas reachable and operable by pad only.

## 10. Common failure modes

1. Dead zone without rescale (jump at the edge).
2. A fixed dead zone that is too small for a worn pad (drift steers the car).
3. A state toggle on the steering stick click.
4. Clutch and brake on the same finger.
5. Thin, small, translucent text that fades while moving.
6. Colour-only shift lights.
7. Keyboard-only settings (lens, lights, stats) in a pad-first game.
8. Docs that describe a feature path that the code turns off.
9. Comfort settings only in console variables.
10. Menu buttons that the pad cannot focus.

## 11. References

1. Xbox Accessibility Guideline 101: Text display. https://devdocs.xbox.com/build/game-principles/accessibility/xag-deep-dives/xag-101-text-display.md **[V]**
2. Xbox Accessibility Guideline 102: Contrast. https://devdocs.xbox.com/build/game-principles/accessibility/xag-deep-dives/xag-102-contrast.md **[V]**
3. Xbox Accessibility Guideline 107: Input. https://devdocs.xbox.com/build/game-principles/accessibility/xag-deep-dives/xag-107-input.md **[V]**
4. Xbox Accessibility Guideline 117: Visual distractions and motion settings. https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/117 **[V]**
5. Xbox Accessibility Guidelines index. https://learn.microsoft.com/en-us/gaming/accessibility/guidelines **[U]** (index not read)
6. Microsoft, Getting Started With XInput (dead zones). https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput **[V]**
7. Game Accessibility Guidelines: Ensure no essential information is conveyed by a fixed colour alone. https://gameaccessibilityguidelines.com/ensure-no-essential-information-is-conveyed-by-a-fixed-colour-alone/ **[V]** (title)
8. Game Accessibility Guidelines: If the game uses field of view, allow a means for it to be adjusted. https://gameaccessibilityguidelines.com/if-the-game-uses-field-of-view-3d-engine-only-allow-a-means-for-it-to-be-adjusted/ **[V]** (title)
9. Game Accessibility Guidelines: Avoid or provide option to disable any difference between controller movement and camera movement. https://gameaccessibilityguidelines.com/avoid-or-provide-option-to-disable-any-difference-between-controller-movement-and-camera-movement/ **[V]** (title)
10. "7 building blocks for better controls", Game Developer. https://www.gamedeveloper.com/game-platforms/7-building-blocks-for-better-controls **[U]** (not read)
11. Loughborough University, frequency characteristics of visually induced motion sickness. https://repository.lboro.ac.uk/articles/Frequency_characteristics_of_visually_induced_motion_sickness/9346865/files/16955915.pdf **[U]** (from docs/research/domain-ideas.md)

No peer-reviewed paper on gamepad steering response curves was found in this session. Treat the curve rules in section 2.2 as engineering practice, to be confirmed by a player test. **[U]**

Licence notes: XAG and Microsoft docs are public reference (Microsoft terms). GAG is free to use as guidance. Do not copy text blocks into the game.
