# Playing SimCruise

The Unreal client starts in the 2001 roadster in Ocean Drive's lane, Miami Beach, in a morning sun (the bundled start: no network needed). The keys and buttons are one table in `Source/SimCruise/Private/Gameplay/SimCruiseInputConfig.cpp`, checked by the `SimCruise.Gameplay.Input` tests.

## Controls

| Action | Keyboard | Xbox controller |
|---|---|---|
| Accelerate | W or Up | Right trigger |
| Brake, then reverse once stopped (below about 1 mph; a trigger pressed too lightly to brake coasts) | S or Down | Left trigger |
| Steer | A / D or Left / Right | Left stick |
| Full brake (service brake) | Space | — |
| Parking brake, rear wheels | H (hold) | B (hold) |
| Automatic / manual gearbox | T | Right-stick click |
| Clutch (manual mode) | Shift (hold) | A (hold) |
| Shift up / shift down (manual mode) | E / Q | RB / LB |
| Restart a stalled engine | I, with the clutch held | D-pad left, with the clutch held |
| Camera: driver's seat, chase | C | Y |
| Hide or show the driving HUD | F2 | X |
| Frame time and stats | F3 | — |
| Driver lens: natural (the default), true FOV, wide | F6 | Left-stick click |
| Head lamps on / off | L | — |
| Recover to the nearest road | R | View |
| Sound on / off (engine, tyres, road, wind) | M | — |
| Road atlas, find a place | Tab, / | Menu, X in the atlas |
| Tune the car (standing still) | F4 | D-pad Right |

**Tuning.** Stop the car and press D-pad Right (F4). Up/Down (D-pad or stick) chooses a row, Left/Right changes it, X resets the row, Y pressed twice puts the whole car back as it left the factory, A (Enter) saves the setup for this car and applies it, and B (Esc, F4) cancels. The rows are front and rear spring rates (lb/in), damping (% of stock), ride height (mm), front and rear anti-roll bars (% of stock, 0 disconnects one), engine torque (%), final drive and steering ratio (a lower ratio is quicker: the stick turns the wheels further and faster). Each stays inside a safe range around the real car's. Under the rows the menu shows the body roll in degrees per g of cornering, so you see what springs and bars do before you drive. The setup is saved per car and comes back next time you drive that car.

The engine sound is on at the start.

**Go somewhere else.** Press / for "Where to?": type a street, a town, a landmark or `latitude, longitude` and press Enter, or pick a preset, or press Surprise me (Y on the pad) for a random US road. Press Tab (Menu) for the road atlas: move the map with the left stick, the mouse or the arrow keys, zoom with the D-pad, the wheel or +/−, click to pin a spot, and press Enter (A) or Drive here to go to the pin or the map centre. In the atlas, X finds a place, Y recentres, RB changes the car and LB the gearbox, and the buttons in its top bar set the sound, the HUD level, head motion (100, 50 or 0 %) and rumble strength (100, 60, 30 or 0 %), all remembered; Esc, Tab or B goes back to the drive. The game pauses while either is open.

**Automatic gearbox.** It changes up early with a light pedal and holds each gear longer the deeper you press, kicks down when you floor it, never changes up while you brake and holds its gear through a corner.

**Manual gearbox.** Press T (right-stick click) to take over the gearbox. Hold the clutch (Shift / A), tap E (RB) for first, add throttle and let the clutch out. Shifts need the clutch; the gearbox refuses a gear that would over-rev the engine, and a stalled engine restarts with I (D-pad left) while the clutch is held. On the pad the right thumb holds the clutch and the bumpers change gear like paddles (RB up, LB down), so you can brake with LT, hold the clutch and change down at the same time.

**New pad layout in 2.0.0.** The clutch moved from LB to A, the parking brake from A to B, shift down from X to LB, the gearbox switch from B to the right-stick click, the engine restart from the right-stick click to D-pad left and the HUD from the left-stick click to X; the left-stick click does nothing (since 2.0.1: it changed the lens when pressed by accident while steering; the lens is F6 or the atlas's View setting). The reason: the clutch on LB used the same finger as the brake trigger, and hard steering pressed the left stick by accident.

**True field of view.** The driver's seat starts on the natural lens (45° tall, 74° across a 16:9 screen). F6 or the left-stick click changes it to the true field of view and to wide. With the true field of view the game shows the world at the angle your screen really covers at your eye, as if the screen were a window: a 15.6" 16:9 laptop at 60 cm spans 32° across (2·atan(width / 2·distance)). Set your screen's diagonal and your eye's distance on the road atlas (Tab: Screen, Eye to screen), or with `simcruise.Lens.ScreenInches` and `simcruise.Lens.ViewDistanceCm`. View · Wide (67° tall) shows more of the cabin for tight streets. The eye sits at each car's real driver's head position; the chase view keeps its wider angle.

**Night.** Your eye adapts to the road your low beams light (about 0.5 cd/m², what real lamp-lit asphalt measures), and the beams reach 250 m.

**A USB steering wheel.** Plug in a racing wheel with a Windows GameInput driver (Logitech G920/G923, Thrustmaster and Fanatec wheels with Xbox support) before starting the game; the HUD names it. The rim turns the car's front wheels 1:1 at the car's real steering ratio (Miata 15:1, Mustang 21.7:1, Corvette 15.7:1) over the wheel's own rotation, with none of the pad's speed shaping. Throttle, brake and clutch pedals map directly, the paddles (or a sequential shifter) change gear, and the handbrake lever holds the rear wheels. Force feedback is the tyres' aligning torque through the steering ratio, as the real column carries it: heavy as the front tyres build grip, going light as they pass their peak and slide. A light centring spring and the column's damping sit under it. `simcruise.Wheel.Force 0..1` (default 0.6) sets the strength, `simcruise.Wheel.MaxTorqueNm` (default 2.5) sets your wheel's peak torque, and `simcruise.Wheel.ForceSign -1` flips a wheel that pushes the wrong way. Not yet tested on a physical wheel; wheels with only a DirectInput driver (the PlayStation G29) are not read yet.

**Steering on a pad.** Steering and pedal shaping (speed-sensitive range, filtering) shape your input but never the physics: the rack takes the shaped angle as given. Holding the stick toward a slide opens extra opposite lock (up to the slide angle plus 5 degrees) for catching it. The pad rumbles from tyre slip, the ABS pulse and road texture (`simcruise.Rumble.Strength` in the console); with Windows GameInput the trigger motors carry wheelspin (RT) and locking or ABS (LT).

**The right trigger is a pedal curve.** A trigger has about 1 cm of travel, a real accelerator about 6 cm, so a linear trigger made every light press a large throttle opening. The pedal now follows the trigger to the power 1.8: half the trigger is 29 % throttle, three quarters is 60 %, the full trigger is still full throttle. In a tight turn in 1st or 2nd gear, full throttle spins the inside rear wheel and steps the tail out, as it does in the real car; part throttle pulls the car round. `simcruise.TriggerGamma` sets the curve (1 = linear, up to 3). The brake trigger stays linear.

**Your own stick and trigger settings.** The road atlas has a Controls row, and each choice is saved: *Stick steering speed* (60-150 % of the researched hand speed, `simcruise.Pad.SteerSpeed`), *Stick response* (fine centre, the default, where the stick's middle is for lane corrections; between; or linear, `simcruise.Pad.SteerLinearity`), *Stick dead zone* (12 %, the fixed one, up to 25 %; the rest of the travel still reaches full lock, `simcruise.Pad.SteerDeadZone` is the extra) and *Throttle trigger* (linear 1.0, gentle 1.4, progressive 1.8, the default, or very progressive 2.4). On a pad, D-pad Left/Right picks a setting (it lights up), A changes it and B lets go. The car's own steering ratio is in the tuning menu.

**Time of day.** A drive starts at 10:00 local solar time at the place. `-SimCruiseRealTime` follows your computer's clock instead and `-SimCruiseUtc=2026-11-12T01:30:00Z` fixes a moment; more launch options are in [unreal-building.md](unreal-building.md#drive-it).
