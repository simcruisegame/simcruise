# Unreal client architecture

How the Unreal Engine 5.8 client is put together, one section per area. The Godot client under `game/` remains the behavioural and calibration reference while the port is under way; each section names the Godot files it ports. Building and running are in [unreal-building.md](unreal-building.md).

## Driving shell

`Source/SimCruise/{Public,Private}/Gameplay/` — the drivable loop around whatever simulates the car: input, steering and pedal shaping, cameras, HUD and pad rumble. It never applies a force; the vehicle model and every calibration band belong to the vehicle code.

| Class | Role | Ports |
|---|---|---|
| `ISimCruiseVehicleControl` (`SimCruiseVehicleControl.h`) | The seam to the vehicle: 7 calls, SI units, body frame | Powertrain's public state |
| `USimCruiseInputConfig` | Enhanced Input actions and mapping context built in code | `Drive.gd` input map, `docs/controls.md` |
| `FSimCruiseDriverShaper` (`SimCruiseDriverShaping.h`) | Thumb, hand and foot: rack angle and pedal positions | `Powertrain.gd` steering map, plan 04 |
| `ASimCruisePlayerController` | Reads actions, shapes, commands the vehicle, rumbles the pad | `Drive.gd` `_physics_process`, `_unhandled_input` |
| `ASimCruisePlayerCameraManager`, `FSimCruiseHeadModel`, `FSimCruiseChaseFrame` | Driver, chase and scenic views | `Cockpit.gd` `head_view`, `ViewLens.gd`, `Drive.gd` cameras, plan 15 |
| `ASimCruiseHUD` | Canvas HUD, Off / Glance / Full | `Hud.gd` |
| `FSimCruiseHapticsModel` (`SimCruiseHaptics.h`) | Rumble from tyre slip, ABS and road texture | `feel/slip-haptics-audio:game/Haptics.gd` |
| `ASimCruiseGameMode` | Spawns the vehicle pawn (from config) with the shell classes | `Drive.tscn` |
| `ASimCruisePlaceholderVehicle` | Stand-in single-track car until the vehicle pawn lands | none (not a SimCruise model) |

### The vehicle seam

A vehicle pawn implements `ISimCruiseVehicleControl` and is named in `Config/DefaultGame.ini`:

```ini
[/Script/SimCruise.SimCruiseGameMode]
VehiclePawnClass=/Script/SimCruise.YourVehiclePawn
```

Without it the game mode spawns `ASimCruisePlaceholderVehicle`. Conventions: Unreal body axes (+X forward, +Y right, +Z up), SI units, yaw rate positive turning right, rack angle = mean front road-wheel angle in radians, positive right; wheels 0 FL, 1 FR, 2 RL, 3 RR (Godot `MOUNTS` order); the driver's eye in pawn-local centimetres (`SimCruiseGodotToLocalCm` converts a car file's `driver_eye`).

- `GetVehicleSetup()` — wheelbase, CG-to-front-axle, parking rack lock, assist lateral g, limit slip surplus, driver eye and sight, idle/upshift/redline rpm, gear count, published skidpad g and the braking g of the 70-0 mph distance.
- `GetVehicleTelemetry()` — body velocity and angular velocity, rpm, gear, manual/stalled/electric/service-brake flags, the rack angle applied, engine load, road roughness, and per wheel: contact, driven, ABS acting, load, slip ratio and its peak, relaxed slip angle and its 98 %-plateau angle (`SimCruiseHaptics::PlateauSlipAngleRad`), suspension travel, surface grip. Data a vehicle does not report keeps its default and simply produces no cue.
- `ApplyDriverCommand()` once per frame before physics (the controller is a tick prerequisite of the pawn while it possesses it): throttle, brake, clutch, parking brake, full service brake (Space), rack angle. In automatic, the vehicle keeps Godot's "brake, then reverse once stopped" pedal semantics.
- Threading: every call is made on the game thread. A vehicle that steps on the physics thread (Chaos async physics or substep callbacks) hands the command over itself, for example as async input, and answers `GetVehicleTelemetry()` from its last published game-thread copy, never by reading physics-thread state in place. Telemetry is read before the pawn ticks, so the shell shapes on state one frame old.
- `RequestGearChange(±1)`, `SetManualTransmission()`, `RequestEngineStart()`, `ResetVehicle(transform)`.

### Input

`USimCruiseInputConfig::Bindings()` is the single table behind the mapping context, the `SimCruise.Gameplay.Input.*` tests and this list. Analog channels are separate actions from their keys because they are shaped differently.

| Action | Keyboard | Xbox | Value |
|---|---|---|---|
| ThrottlePad / ThrottleKey | W, ↑ | RT (0.04 dead zone) | trigger linear; key ramps 3/s up, 8/s down |
| BrakePad / BrakeKey | S, ↓ | LT (0.04) | trigger linear; key ramps 2.5/s up, 8/s down |
| SteerPad / SteerKey | A/D, ←/→ (A and D cancel) | left stick X (0.12, rescaled) | see steering |
| FullBrake | Space | — | held |
| ParkingBrake | H | A | held |
| Clutch | Shift | LB | 14/s in, 2.8/s out |
| ToggleTransmission | T | B | |
| ShiftUp / ShiftDown | E / Q | RB / X | |
| StartEngine | I | right-stick click | |
| CycleCamera | C | Y | driver → scenic → chase |
| CycleHud | F2 | left-stick click | Off → Glance → Full |
| ToggleStats | F3 | — | `stat fps`, `stat unit` |
| CycleLens | F6 | — | natural 45° → true to screen → wide 67° |
| ToggleSound / ToggleLights | M / L | — | flags until audio and lamps are ported |
| ToggleAtlas / FindPlace | Tab / `/` | Menu | message until the atlas and search are ported |
| Recover | R | View | upright at rest on the ground below; road-aware recovery comes with the roads |

`Config/DefaultInput.ini` sets the legacy `Gamepad_LeftX` dead zone to 0 so Enhanced Input's 0.12 is the only one.

### Steering and pedals: shaping that never fakes physics

`FSimCruiseDriverShaper` decides which rack angle a thumb or a key asks for and how fast a hand can move the wheel; the car then does what its tyres do. It ports `Powertrain.gd` with the audited corrections of `docs/research/plan-2026-11-10/04-pad-steering-pedals.md`:

- **Range** (`AvailableSteeringRad`): the Ackermann angle for the assist lateral g plus the limit slip surplus, smooth-min blended into the parking lock (NB: 8.02° at 20 m/s, 36.6° parked).
- **Countersteer allowance**: toward the front axle's velocity angle β_f = atan2(v_y + r·a, v_x) the range opens to |β_f| + surplus (never past the lock) and a hand may move the rack that way at up to 40°/s. Keyed on β_f alone (no yaw-rate state machine); zero below 5 m/s and in reverse. A 15° slide at 45 mph can be met with 20° of opposite lock; steering into the slide keeps the normal cap. Plan 04's gate (keep it only if a slide that spins with the capped rack is caught with it) has **not** been met: on the placeholder car a gentle catch never needs the allowance, a snap to full opposite lock uses it and overcorrects into a spin that the capped rack avoids, and a 0.3 s parking-brake flick at 60 mph spins either way (`SimCruise.Gameplay.Loop.PlaceholderConventionsAndSlideCatch` and the capture's slide trace). The allowance is closer to a real wheel, which has no speed cap, so it stays; the gate has to be run on the real vehicle.
- **Rack state in radians**, slewed in rad/s. When the allowance closes the rack unwinds at the return rate; it never jumps.
- **Stick map** (`ThumbstickCommand`): the stick's centre goes to lane corrections, the ends reach the full range; pad rates 2.75 and 4.4 ranges/s. Releasing returns to exactly zero (no self-centring aid).
- **Keys**: a hand's rate, 2.75 locks/s parked easing to 12°/s at highway speed (a half-second tap at 60 mph gives 6°); release unwinds first at 1.6× (at least 24°/s).
- **Pedals**: triggers linear past their dead zone; keys ramp like a foot (a 0.3 s brake tap reaches 0.75, so a no-ABS car is not locked by a tap).

### Cameras

`ASimCruisePlayerCameraManager::UpdateViewTarget` builds the view from the pawn and its telemetry:

- **Driver** (default): `FSimCruiseHeadModel`, a port of `Cockpit.gd head_view`. A critically damped 9 rad/s spring moves the eye against the felt specific force (differentiated over the real frame time, so the lean is the same at 30 and 360 fps) (3 cm/g lateral, 2 cm/g longitudinal, clamped), braking dips the gaze 1.5°/g, the gaze leads up to 4° into a corner from 14 m/s (normalised by the car's own range, not the NB's 0.10 rad), suspension travel rate adds millimetre vibration, and the horizon keeps 70 % of the body roll. `simcruise.Camera.HeadMotion` (0..1) scales all but the horizon. The lens is a vertical field of view converted to Unreal's horizontal one for the window's aspect: natural 45° (72.7° across 16:9), true to screen (`simcruise.Lens.ScreenHeightCm`, `ViewDistanceCm`), wide 67°.
- **Chase**: Drive.gd's offsets (5.8 m back, 1.94 m up, 1.92 m right easing to 0.35 m) in a yaw-only heading frame swung toward the direction of travel by 0.4 β (β = atan2(v_y, |v_x|), clamped ±35°, faded in from 2 to 8 m/s, no swing in reverse, held in a spin, 0.3 s lag), so a slide reads on screen. The position lag acts on the offset from the car, so the camera no longer trails further behind as speed rises. 50° vertical. A ground trace keeps it 55 cm up (probing from only 2.5 m above the camera, the rule of Drive.gd `surface()`, so an overpass deck never lifts the camera onto it) and a wall trace pulls it in front of buildings.
- **Scenic**: 7.2 m back, 4.9 m right, 3.45 m up, same frame and lag.

![Chase view cornering at 59 mph on the placeholder car: 1.7° of rack, 4.6° of body slip, the camera swung 1.8° with the travel](images/unreal/shell-chase-cornering.jpg)

![Chase view in a parking-brake slide at 46 mph, 34° of body slip with 11° of opposite lock: the camera has swung 7.8° with the direction of travel, so the car is seen at its slip angle](images/unreal/shell-chase-sliding.jpg)

### HUD

`ASimCruiseHUD` draws on the canvas in code (engine Roboto duplicated per typeface, so no UMG asset). **Glance** (default): a translucent tag bottom left with speed, gear, rpm and shift bar, and a g-g meter whose ellipse is the car's published skidpad g sideways and its 70-0 mph braking g fore and aft, with a two-second trace; it fades to 40 % while cruising and wakes on a 3 mph change or a message. **Full**: an opaque strip with the same plus the car, gearbox, view and controller-aware hints. **Off**: prompts only. Messages show at every level.

![Driver view under acceleration with the Glance HUD](images/unreal/shell-driver-accelerating.jpg)

### Rumble

`FSimCruiseHapticsModel` ports the slip-haptics branch: normalised slip s = √((κ/κ*)² + (α/α*)²) per tyre, felt from s 0.75 (65 % at the plateau end 1.15, full at 2.5). The high-frequency (right) motor carries front-tyre scrub, road texture, loose surface and a faint engine buzz; the low-frequency (left) motor carries the rear tyres and driven-wheel slip, an 8 Hz ABS pulse and kerbs. New for Unreal: the impulse triggers carry the pedal's own story, right trigger = driven-wheel spin under throttle, left = a locking wheel or the ABS pulse. Unreal's channels reach the pad differently per backend, so `SimCruiseHaptics::ToChannels` maps them: XInput (the default plugin) drives the left motor from max(LeftLarge, RightLarge) and the right from max(LeftSmall, RightSmall) and has no trigger motors; GameInput maps LeftLarge, RightLarge to the body motors and the Small channels to the triggers. The controller plays each channel as a dynamic force-feedback effect and stops them when paused, unpossessed or ended. `simcruise.Rumble.Strength` (0..1) scales it. Coasting straight on a smooth road it is silent. Slip angles fade in as felt scrub between 0.5 and 2 m/s (a tyre scrubs at about v·sin α), so a car stopped sideways after a slide is quiet; wheelspin from standstill is still felt.

### Placeholder vehicle

`ASimCruisePlaceholderVehicle` exists only so the shell can be driven before the vehicle pawn lands. Spawning, reset and recover probe for the ground from 3 m above the car first (a reset under an overpass stays on the road below) and only then from high above. It is a planar single-track model (relaxed slip angles, the car file's lateral law and friction, a friction ellipse, static load transfer, the engine curve, gearbox, brakes, engine braking) riding the ground on four line traces, drawn with engine basic shapes, using `nb1999`. It is never used for calibration and makes no claim about the real car.

### Checks

- `SimCruise.Gameplay.*` automation tests (13): bindings against `docs/controls.md`, dead zones and directions, the Godot range numbers, exact centring, key rate at 60 mph, countersteer allowance and its slewed closing, pedal ramps, rumble (plateau angle 10.6° on the NB, monotonic feel, silence when coasting and when stopped, which motor for understeer, wheelspin and ABS, both backends), head model offsets, brake dip and 70 % horizon, the same lean and g-meter reading from 30 to 360 fps, chase swing = 0.4 β, HUD levels, and the closed loop of shaper and placeholder car (stick right yaws right; the allowance opens past the cap in a slide; four catch variants logged).
- `-SimCruiseShellSelfTest` (or the `SimCruiseShellSelfTest 1` console command) drives the real Enhanced Input stack in the game like Godot's `checks/PadSteer.gd` and prints `SIMCRUISE SHELL SELF-TEST PASSED|FAILED`; with quit it exits 0 on a pass and 1 on a failure.
- `-SimCruiseShellCapture` drives a scripted lap (accelerate, corner, a 0.3 s parking-brake flick at 60 mph answered by aiming the wheels along the front axle's travel, brake) and saves six screenshots to `Saved/Screenshots/`, with a 10 Hz trace of the slide in the log. On the placeholder the flick spins the car, so the last two shots show it stopping after the spin.

### Not yet verified

Rumble and trigger levels have not been felt on a physical Xbox pad; the captures prove the levels the model asks for, not how they feel. The shell has only driven the placeholder car on the engine's OpenWorld template map. The countersteer allowance has not passed plan 04's slide-catch gate (see above). The self-test's failure exit code follows the engine source (a forced exit with the code) and has not been triggered.
