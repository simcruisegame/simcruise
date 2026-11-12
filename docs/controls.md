# Playing SimCruise

The Unreal client starts in the 2001 roadster in Ocean Drive's lane, Miami Beach, in a morning sun (the bundled start: no network needed). The keys and buttons are one table in `Source/SimCruise/Private/Gameplay/SimCruiseInputConfig.cpp`, checked by the `SimCruise.Gameplay.Input` tests.

## Controls

| Action | Keyboard | Xbox controller |
|---|---|---|
| Accelerate | W or Up | Right trigger |
| Brake, then reverse once stopped (below about 1 mph; a trigger pressed too lightly to brake coasts) | S or Down | Left trigger |
| Steer | A / D or Left / Right | Left stick |
| Full brake (service brake) | Space | — |
| Parking brake, rear wheels | H (hold) | A (hold) |
| Automatic / manual gearbox | T | B |
| Clutch (manual mode) | Shift (hold) | LB (hold) |
| Shift up / shift down (manual mode) | E / Q | RB / X |
| Restart a stalled engine | I, with the clutch held | Right-stick click, with the clutch held |
| Camera: driver's seat, chase | C | Y |
| Hide or show the driving HUD | F2 | Left-stick click |
| Frame time and stats | F3 | — |
| Driver lens (natural, true to screen, wide) | F6 | — |
| Head lamps on / off | L | — |
| Recover to the nearest road | R | View |
| Sound on / off (engine, tyres, road, wind) | M | — |
| Road atlas, find a place | Tab, / | Menu, X in the atlas |

The engine sound is on at the start.

**Go somewhere else.** Press / for "Where to?": type a street, a town, a landmark or `latitude, longitude` and press Enter, or pick a preset, or press Surprise me (Y on the pad) for a random US road. Press Tab (Menu) for the road atlas: move the map with the left stick, the mouse or the arrow keys, zoom with the D-pad, the wheel or +/−, click to pin a spot, and press Enter (A) or Drive here to go to the pin or the map centre. In the atlas, X finds a place, Y recentres, RB changes the car and LB the gearbox; Esc, Tab or B goes back to the drive. The game pauses while either is open.

**Automatic gearbox.** It changes up early with a light pedal and holds each gear longer the deeper you press, kicks down when you floor it, never changes up while you brake and holds its gear through a corner.

**Manual gearbox.** Press T (B) to take over the five-speed. Hold the clutch (Shift / LB), tap E (RB) for first, add throttle and let the clutch out. Shifts need the clutch; the gearbox refuses a gear that would over-rev the engine, and a stalled engine restarts with I (right-stick click) while the clutch is held.

**Steering on a pad.** Steering and pedal shaping (speed-sensitive range, filtering) shape your input but never the physics: the rack takes the shaped angle as given. Holding the stick toward a slide opens extra opposite lock (up to the slide angle plus 5 degrees) for catching it. The pad rumbles from tyre slip, the ABS pulse and road texture (`simcruise.Rumble.Strength` in the console); with Windows GameInput the trigger motors carry wheelspin (RT) and locking or ABS (LT).

**The right trigger is a pedal curve.** A trigger has about 1 cm of travel, a real accelerator about 6 cm, so a linear trigger made every light press a large throttle opening. The pedal now follows the trigger to the power 1.8: half the trigger is 29 % throttle, three quarters is 60 %, the full trigger is still full throttle. In a tight turn in 1st or 2nd gear, full throttle spins the inside rear wheel and steps the tail out, as it does in the real car; part throttle pulls the car round. `simcruise.TriggerGamma` sets the curve (1 = linear, up to 3). The brake trigger stays linear.

**Time of day.** A drive starts at 10:00 local solar time at the place. `-SimCruiseRealTime` follows your computer's clock instead and `-SimCruiseUtc=2026-11-12T01:30:00Z` fixes a moment; more launch options are in [unreal-building.md](unreal-building.md#drive-it).
