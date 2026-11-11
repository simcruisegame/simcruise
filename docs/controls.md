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
| Road atlas, find a place | Tab, / | Menu, — |

The engine sound is on at the start. The atlas and place search answer with a message until they are ported from the Godot client.

**Automatic gearbox.** It changes up early with a light pedal and holds each gear longer the deeper you press, kicks down when you floor it, never changes up while you brake and holds its gear through a corner.

**Manual gearbox.** Press T (B) to take over the five-speed. Hold the clutch (Shift / LB), tap E (RB) for first, add throttle and let the clutch out. Shifts need the clutch; the gearbox refuses a gear that would over-rev the engine, and a stalled engine restarts with I (right-stick click) while the clutch is held.

**Steering on a pad.** Steering and pedal shaping (speed-sensitive range, filtering) shape your input but never the physics: the rack takes the shaped angle as given. Holding the stick toward a slide opens extra opposite lock (up to the slide angle plus 5 degrees) for catching it. The pad rumbles from tyre slip, the ABS pulse and road texture (`simcruise.Rumble.Strength` in the console); with Windows GameInput the trigger motors carry wheelspin (RT) and locking or ABS (LT).

**Time of day.** A drive starts at 10:00 local solar time at the place. `-SimCruiseRealTime` follows your computer's clock instead and `-SimCruiseUtc=2026-11-12T01:30:00Z` fixes a moment; more launch options are in [unreal-building.md](unreal-building.md#drive-it).
