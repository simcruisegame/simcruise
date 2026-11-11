# Playing SimCruise

The game opens in the cockpit on a random road from a pool of 36 verified US drives (`game/data/start-pool.json`). If that road cannot load, for example offline, it tries three more and then starts in the bundled offline neighbourhood (Ocean Drive, Miami Beach). Press `/` (or click **Find a place**) and type a place name, a street with its town, or `latitude, longitude`; pick a result to load the nearest mapped road, or press **Surprise me** for another random road. Places worth trying: `Monument Valley · US 163, UT`, `Capitol Avenue · Cheyenne, WY`, `Princeton Road · Lancaster County, NE` (gravel), `Clay Center · Charleston, WV`, or the coordinates `40.808017, -96.675328` for a Nebraska street whose roofs are measured from public lidar as you drive. A new area needs a few seconds to stream; the horizon terrain can take longer on a first visit.

## Controls

| Action | Keyboard | Xbox controller |
|---|---|---|
| Accelerate | W or ↑ | Right trigger |
| Brake, then reverse once stopped (below about 1 mph; a trigger pressed too lightly to brake coasts) | S or ↓ | Left trigger |
| Steer | A / D or ← / → | Left stick |
| Full brake (service brake) | Space | — |
| Parking brake, rear wheels | H (hold) | A (hold) |
| Automatic ↔ manual gearbox | T | B |
| Clutch (manual mode) | Shift (hold) | LB (hold) |
| Shift up / shift down (manual mode) | E / Q | RB / X |
| Restart a stalled engine | I, with the clutch held | Right-stick click, with the clutch held |
| Camera: chase, driver, scenic | C | Y |
| Hide or show the driving HUD | F2 | Left-stick click |
| Frame rate, active tiles, map requests | F3 | — |
| Graphics preset (Auto, Low, Medium, High, Ultra) | F4 | — |
| Driver lens (natural 45°, true to screen, wide 67°) | F6 | — |
| Engine sound on / off | M | — (HUD button) |
| Head lamps on / off (1999 roadster) | L | — |
| Road atlas open / close | Tab (Esc also closes) | Menu |
| Atlas: zoom, recenter | + / −, mouse wheel, drag to pan, click to pin | D-pad up / down, A |
| Minimap zoom | Mouse wheel over the minimap, its + / − buttons | D-pad up / down |
| Find a place | / | — (HUD button; typing needs a keyboard) |
| Recover to the nearest road | R | View |

**Manual gearbox.** Press T (B) to take over the five-speed. Hold the clutch (Shift / LB), tap E (RB) for first, add throttle and let the clutch out. Shifts need the clutch; the gearbox refuses a gear that would over-rev the engine or change direction while rolling. If you stall, hold the clutch and press I (right-stick click). Automatic mode is an assist on the same simulated gearbox, not a different car. Your transmission choice is remembered on this Mac.

**Controller rumble.** With an Xbox controller you feel the tyres: the right (light, high-pitched) motor buzzes as the front tyres approach and pass their grip limit (understeer, a locking front wheel), over broken pavement, on gravel or grass, and faintly with the engine under load; the left (heavy) motor rumbles as the rear tyres slide or the driven wheels spin, thumps over kerbs and potholes, and pulses at 8 Hz while ABS works on the cars that have it. Each tyre's rumble follows its slip measured against that tyre's own peak, so it starts at about 90 % of the grip (from about 0.87 g in the 1999 roadster, whose skidpad limit is 0.885 g) and grows into a slide; coasting or cruising straight on a smooth road it is off. You hear the same signal: past the limit the tyres scrub, then squeal (a 700-1,600 Hz tone that rises with sliding speed), on pavement only. **Rumble · On/Off** and the strength slider sit beside the other settings in the road atlas (Tab); both are remembered. Rumble stops while the atlas or search is open, while a road loads, and while the game window is in the background.

*Trigger rumble.* The Xbox One and Series controllers also have a motor in each trigger. Godot 4.7.2 drives only the two body motors (`Input.start_joy_vibration`); the trigger motors are reachable only through Windows.Gaming.Input (`GamepadVibration.LeftTrigger`/`RightTrigger`) or SDL3 (`SDL_RumbleGamepadTriggers`, zlib licence), neither of which GDScript can call. A GDExtension would have to load its own SDL3 or WinRT runtime beside Godot's built-in SDL (which does not export its symbols) and open the pad a second time, so it is not shipped: this build machine has no C++ toolchain to build or test one, and a second owner of the controller can fight Godot's own input. The plan if Godot exposes trigger motors natively, or an add-on proves itself: the left trigger carries front braking slip past the peak and the ABS pulse, the right trigger driven-wheel spin and traction-control cuts, both from the same `Haptics.gd` slip values.

**Road atlas.** Tab opens a full-screen OpenStreetMap-based map. Drag to pan, scroll to zoom, click to pin a spot and load its nearest road. Its buttons switch **Transmission**, **Street photos** (colour hints from licensed KartaView thumbnails near the road), **Imagery** (recent Sentinel-2 colour, with the capture date), **Weather** (nearest METAR observation drives clouds, visibility and fog) and **USDA ground cover**. **Source photo ↗** opens the credited photograph. **Compare Street View ↗** opens Google Maps in your browser at the pinned or current position so you can compare. **Source photo ↗** and **Compare Street View ↗** are the only two things in the game that open your browser, and both fire only when you click them.
