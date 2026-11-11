# Unreal client architecture

How the Unreal client is put together, one section per area. The Godot client under `game/` stays the behavioural and calibration reference until the Unreal client reaches parity; see the [rewrite design](superpowers/specs/2026-11-10-simcruise-unreal-rewrite-design.md) and the [migration plan](superpowers/plans/2026-11-10-simcruise-unreal-migration.md).

## Vehicle physics core

Source: `Source/SimCruise/{Public,Private}/Vehicle/`, tests in `Source/SimCruise/Private/Tests/Vehicle*`.

### The pieces

| Piece | What it is |
|---|---|
| `FSimCruiseCarDefinition`, `FSimCruiseCarDefinitionLoader` | The car file (`game/cars/<id>.json`, canonical) in C++, every field unit-labelled and validated, sources kept. An Unreal calibration overlay (`Content/Vehicle/Calibration/<id>.json`) may replace estimated fields; each overlay states its reason in `_note`, and the bench logs what it changed. |
| `FSimCruiseVehicleSim` (`SimCruiseVehicleSim.h`) | The vehicle model, independent of any engine object: a faithful port of `game/Powertrain.gd` and `game/CarSuspension.gd`. Ray contacts, springs with installed preload, dampers (ratio with a knee, or a shaft-speed dyno table), anti-roll bars, bump stops, camber gain and caster, Ackermann steering with toe, relaxed slip angles with load-dependent relaxation length, the saturating slip-angle curve with camber thrust, the slip-ratio tyre curve with combined slip through a friction ellipse and load sensitivity, wheel inertia solved together with the contact force, locked-wheel sliding, engine torque curve with friction, idle governor and fuel cut, clutch (or converter, or dual clutch) with launch control, automatic (with the 0.45 m/s reverse inhibitor) and manual gearboxes with stalling, torque-sensing differentials (front, rear, centre), service and parking brakes, ABS and traction control on the cars that had them, aerodynamic drag and rolling resistance, plus Godot's steering maps (assisted keys, thumbstick shaping, direct). |
| `SimCruiseDynamics::*` (`SimCruiseVehicleDynamics.h`) | The per-stage formulas the model is made of (suspension load, bar and spring rates, relaxed slip angle, friction coefficient, lateral force, one longitudinal substep, engine curve, differential split), unit-tested on their own. |
| `SimCruiseUnits` (`SimCruiseUnits.h`) | The only place SI meets Unreal: m to cm, N to kg·cm/s², N·m and kg·m² to their cm² forms, degrees, road-test units, and Godot's axes (X right, Y up, −Z forward) to Unreal's (X forward, Y right, Z up). |
| `USimCruiseVehicleMovementComponent` | Puts the model on Chaos. Configures the root body from the car (mass, centre of mass, principal inertia in Unreal axes, Godot's damping, inertia conditioning off, never sleeps) and registers a Chaos sim callback that runs the model on the physics thread before every fixed step. Holds the controls, gearbox commands and telemetry behind one lock. |
| `ASimCruiseVehicle` | The pawn: a simulating box (the car file's collision box, centred `collision_box_y` above the Godot car origin) plus the movement component, with the driving-shell interface. |
| `FSimCruiseVehicleTelemetry` | What HUD, rumble, audio and presentation read: speed, rpm, gear text, manual/reverse/stall/shift state, brake lamps, steering input and rack angle, clutch, throttle after traction control, ABS and TC activity, sound observers (Powertrain.gd's `sound_*`), stranded timer, and per wheel: contact, load, slip ratio and its peak, slip angle, grip use and combined grip use, angular speed and accumulated spin angle, suspension length, steer angle, inclination, tyre forces, surface grip, ABS pressure, contact point. |

### Where the forces are computed

Chaos ticks asynchronously at a fixed step (`Config/DefaultEngine.ini`: `bTickPhysicsAsync=True`, `AsyncFixedTimeStepSize=1/360 s`). Before every step, `FSimCruiseVehicleSimCallback::OnPreSimulate_Internal` (a `Chaos::TSimCallbackObject`) runs on the physics thread:

1. read the particle (`FRigidBodyHandle_Internal`: position, rotation, centre-of-mass velocity, angular velocity, centre of mass) and convert to SI;
2. apply pending gearbox commands and the latest controls; run a bench driver if one is installed;
3. cast the four suspension rays (`UWorld::LineTraceSingleByChannel` on `GroundTraceChannel`, the owner ignored; the same physics-thread query Epic's Chaos Vehicles make);
4. step the model over the interval, splitting it into wheel/driveline substeps no longer than `MaxWheelSubstepSeconds` (1/360 s);
5. add the summed force and its torque about the centre of mass to the particle (`AddForce`, `AddTorque`), converted to Unreal units;
6. publish telemetry.

`SimCruise.Vehicle.Chaos.ForceIntegration` proves the boundary: a known SI force and torque applied this way for 360 steps gives exactly `F·t/m` and `τ·t/I` (within float precision, 1.4e-6 of 0.55 m/s), one callback per step, every step the fixed interval. `SimCruise.Vehicle.Determinism` runs the same launch twice and compares 22 state values bit for bit (all equal). That holds for a single-threaded solver with the controls applied by a physics-thread driver (`SetPhysicsThreadDriver`), as the benches do. Controls set from the game thread are picked up by whichever physics step runs next, so with Chaos on its own threads the step on which a pedal change lands can differ by up to a game frame between runs; replays and anything that must reproduce step for step drive the car from the physics thread.

### Why 360 Hz

Godot evaluated contacts, loads and slip angles once per 60 Hz tick and integrated wheels and driveline in six 1/360 s substeps. In Unreal everything runs at 360 Hz. `SimCruise.Vehicle.Convergence.<car>` runs CarTrack at five rates:

| Rate (body / wheels) | NB 0–60 · 70–0 · skidpad | Mustang | Corvette |
|---|---|---|---|
| 60 / 360 Hz (Godot's scheme) | 7.99 s · 172.9 ft · 0.884 g | 9.01 s · 265.2 ft · 0.699 g | 3.47 s · 151.7 ft · 1.025 g |
| 120 / 360 Hz | 7.99 · 172.4 · 0.884 | 8.94 · 264.0 · 0.698 | 3.47 · 150.7 · 1.024 |
| 240 / 480 Hz | 7.98 · 172.0 · 0.884 | 8.95 · 263.7 · 0.698 | 3.46 · 150.0 · 1.023 |
| **360 / 360 Hz (shipping)** | **7.97 · 171.9 · 0.884** | **8.95 · 263.6 · 0.698** | **3.46 · 150.1 · 1.024** |
| 720 / 720 Hz | 7.97 · 171.8 · 0.884 | 8.94 · 263.4 · 0.698 | 3.46 · 149.6 · 1.021 |

(Mustang with its Unreal brake calibration; see `docs/cars.md`.) From 360 to 720 Hz no result moves by more than a fifth of its calibration band (the test's gate), so 360 Hz is converged; 240 Hz would nearly do, but 360 Hz keeps the wheel substep the cars were calibrated with and leaves margin for curbs and the Corvette's stiff springs. The vehicle's own physics-thread work measured 21–34 µs a step on the target i9-10885H (four traces included), about 8–11 ms of CPU per simulated second per car, around 1 % of one core. A whole bench (engine tick included, single-threaded) runs 15–20 times faster than real time.

Two Godot behaviours turned out to depend on the step and were fixed in the port rather than copied:

- The clutch caps its coupling at the torque that closes the slip gap in one substep. In first and second gear that cap binds below `clutch_stiffness`, so it is part of each car's calibrated launch; it is now evaluated at the calibration substep (1/360 s) instead of the actual one, which keeps launches identical at every rate (0–60 at 720 Hz moved 0.03 s before).
- Godot's 60 Hz contact step let the Corvette's left and right loads swap every tick during its traction-controlled launch (a numerical roll oscillation that cost traction); at 120 Hz and above it is gone and the car reaches 60 mph in 3.46 s instead of Godot's 3.55 s (target 2.8 s, a stated open item).

The port also found a bug in the first Unreal helper: the anti-roll bar rate returned 0 for any bar whose second moment of area is under `UE_DOUBLE_SMALL_NUMBER` (1e-8 m⁴), which is every bar under about 21 mm, including the NB's 11 mm rear bar. That shifted the roll stiffness forward and cost the NB 0.022 g on the skidpad; `SimCruise.Vehicle.Model.SuspensionPreload` now checks both bars against CarSuspension.gd's formula.

### Who owns the body

The movement component owns the root body's Chaos particle and mass properties:

- When the body's physics state is recreated (collision switched off and on, a re-register), the old particle is queued for deletion. The component listens to `OnComponentPhysicsStateChanged`: on *destroyed* it stops stepping the old particle at once, on *created* it configures the new one. Before this, the physics thread kept the old pointer and crashed on its next step (use after free); `SimCruise.Vehicle.Chaos.BodyOwnership` recreates the body mid-drive.
- Anything that makes the game thread recompute the body's mass properties (welding an attached collider into the body, rescaling it, a mass override or physical-material change) would swap in a uniform box's mass, centre and inertia. The physics step compares the particle with the car's values every step and puts them back, logs a warning the first time, and counts it in `MassPropertyRestores` (telemetry; should stay 0). The game-thread copy of the particle keeps the recomputed values until the car is set again, so read mass from the car definition, not from the body. Presentation meshes attached to the pawn should have collision off: a welded collider also changes the body's collision shape, which the restore does not undo.
- Lock order is vehicle lock, then physics scene lock: the physics step holds the vehicle lock across its four scene queries. Never call the vehicle's setters while holding a physics scene write lock.

### Interface for the driving shell

On `ASimCruiseVehicle` (and the movement component), all game-thread calls:

- `SetThrottle(0..1)`, `SetBrake(0..1)` (in automatic, below 0.45 m/s the brake pedal selects reverse and drives, as in Godot), `SetSteer(-1..1, bFromThumbstick)` (+ right; a thumbstick gets Godot's centre shaping and rates), `SetHandbrake(bool)` (rear parking brake), `SetClutch(0..1)`, `ShiftUp()`, `ShiftDown()`, `ToggleManual()`; on the component also `SetFullBrake`, `SetManualTransmission`, `RequestEngineStart`, `SetSteeringAssist` (assisted for keys and sticks, off for a real wheel), `ResetVehicle`.
- `ResetAt(GroundLocation, YawDeg)` puts the car on its wheels, stopped (Powertrain.gd `restart`).
- `GetVehicleTelemetry()` / `GetTelemetry()` return the last physics step (fields above). Manual gear refusals arrive as `LastGearMessage` ("Hold the clutch to change gear", "Stop before changing direction", "Speed is too high for that gear").
- `SetPhysicsThreadDriver(TFunction)` runs code on the physics thread before every step with the model, body and controls (benches, replays, autopilots).

Controls set on the game thread reach the physics thread on its next step; telemetry is at most one game frame old. `SimCruise.Vehicle.Interface` drives a car through this interface only, with Chaos on its own task-graph threads as in the game: launch and upshift, steering right (rack, road wheels, load transfer and yaw all to the right), braking to a stop without ever selecting reverse, manual neutral/clutch/first gear and a stall-free clutch release, the handbrake holding, and a reset.

### Not yet done

- Surface grip comes from `FSimCruiseGroundHit::SurfaceGrip` (1.0 on the bench); the world stream needs to supply Godot's per-tile wheel grip and road roughness (`RoadProfileM`, `RoadProfileRateMps` are in place).
- Contacts are rays as in Godot; a wheel-radius sphere cast for curbs and potholes, and an aligning moment for force feedback, are next.
- The ground query reads the game thread's scene-query structure from the physics thread, as Chaos Vehicles do; streamed road collision must therefore be in place before the car reaches it (the world stream's job).
- Packaging: the car files (`game/cars/`) and calibration overlays (`Content/Vehicle/Calibration/*.json`) are loose JSON, which a cooked build does not stage by default; a packaged build needs them added to `DirectoriesToAlwaysStageAsNonUFS` (or moved into assets), or the Mustang loses its overlay. `SIMCRUISE_CALIBRATION_DIR` is read in development builds only.
