# Playing SimCruise

The game opens in the cockpit on a random road from a pool of 36 verified US drives (`game/data/start-pool.json`). If that road cannot load, for example offline, it tries three more and then starts in the bundled offline neighbourhood (Ocean Drive, Miami Beach). Press `/` (or click **Find a place**) and type a place name, a street with its town, or `latitude, longitude`; pick a result to load the nearest mapped road, or press **Surprise me** for another random road. Places worth trying: `Monument Valley · US 163, UT`, `Capitol Avenue · Cheyenne, WY`, `Princeton Road · Lancaster County, NE` (gravel), `Clay Center · Charleston, WV`, or the coordinates `40.808017, -96.675328` for a Nebraska street whose roofs are measured from public lidar as you drive. A new area needs a few seconds to stream; the horizon terrain can take longer on a first visit.

## Controls

| Action | Keyboard | Xbox controller |
|---|---|---|
| Accelerate | W or ↑ | Right trigger |
| Brake, then reverse once stopped (below about 1 mph; a trigger pressed too lightly to brake coasts) | S or ↓ | Left trigger |
| Steer | A / D or ← / → | Left stick |
| Full brake (service brake) | Space | — |
| Parking brake, rear wheels (holds the car still, also on a 10 % grade) | H (hold) | A (hold) |
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

**Road atlas.** Tab opens a full-screen OpenStreetMap-based map. Drag to pan, scroll to zoom, click to pin a spot and load its nearest road. Its buttons switch **Car** (the 1999 roadster, the Corvette C8 and the 1965 Mustang in turn; the drive reopens at the same place), **Transmission**, **Street photos** (colour hints from licensed KartaView thumbnails near the road), **Imagery** (recent Sentinel-2 colour, with the capture date), **Weather** (nearest METAR observation drives clouds, visibility and fog) and **USDA ground cover**. **Source photo ↗** opens the credited photograph. **Compare Street View ↗** opens Google Maps in your browser at the pinned or current position so you can compare. **Source photo ↗** and **Compare Street View ↗** are the only two things in the game that open your browser, and both fire only when you click them.
