# Game developer: study notes

Role: senior Unreal Engine 5.8 C++ engineer for SimCruise.
Scope: Chaos physics and the custom vehicle sim, tick order, threads, GC,
input, automation tests, packaging, performance on the 28 W GPU.
Style: ASD-STE100 Simplified Technical English. One instruction per sentence.
Status of facts: "[verified]" = read in a primary source on 2026-11-12.
"[unverified]" = from memory or a secondary source. Check it before you rely on it.

## 1. How SimCruise drives the car (read this first)

- The car is a custom sim, not Chaos Vehicles. `FSimCruiseVehicleSim` computes
  all wheel forces. `USimCruiseVehicleMovementComponent` runs it in a Chaos
  sim callback (`OnPreSimulate_Internal`) on the physics thread.
- Physics ticks async at a fixed 1/360 s (`Config/DefaultEngine.ini`,
  `bTickPhysicsAsync=True`, `AsyncFixedTimeStepSize=0.0027777778`).
- The game thread writes controls under one mutex. The physics thread reads
  them on its next step. Telemetry goes back under the same mutex.
- The physics step casts four suspension rays with `UWorld::LineTraceSingleByChannel`
  from the physics thread. This reads the game thread's scene-query structure.
- Reset (`ResetVehicle`) is a pending flag. The physics thread applies it on its
  next step. The game-thread actor transform changes only when results return.
- The world uses a floating origin. `ASimCruiseWorldStreamer::Rebase` moves tiles.
  `ASimCruiseGameMode::HandleRebased` moves the car with `TeleportPhysics`.
- Key files:
  - `Source/SimCruise/Private/Vehicle/SimCruiseVehicleMovementComponent.cpp`
  - `Source/SimCruise/Private/Vehicle/SimCruiseVehicleSim.cpp`
  - `Source/SimCruise/Private/Vehicle/SimCruiseVehicle.cpp`
  - `Source/SimCruise/Private/Gameplay/SimCruisePlayerController.cpp`
  - `Source/SimCruise/Private/Gameplay/SimCruiseDriverShaping.cpp`
  - `Source/SimCruise/Private/Gameplay/SimCruiseGameMode.cpp`
  - `Source/SimCruise/Private/Gameplay/SimCruisePlayerCameraManager.cpp`
  - `Source/SimCruise/Private/Gameplay/SimCruiseCameraModels.cpp`
  - `Source/SimCruise/Private/Gameplay/SimCruiseInputConfig.cpp`
  - `docs/unreal-architecture.md` (section "Vehicle physics core")

## 2. Rules: fixed step, async physics and frame rate

1. Run vehicle physics at a fixed step. A variable step changes the result.
   Source: Fiedler, "Fix Your Timestep!" [verified, page exists].
2. Keep the step at 1/360 s. The project proved convergence from 240 to 720 Hz
   (`SimCruise.Vehicle.Convergence.<car>`). Do not lower it without that test.
3. Know `MaxPhysicsDeltaTime`. Epic: "If this is smaller than the tick of the
   engine, physics will move artificially slow in order to increase stability."
   [verified, Epic "Physics Settings" page].
4. The default `MaxPhysicsDeltaTime` is 1/30 s (`UPhysicsSettings` constructor)
   [unverified secondary source: ue544cvarwiki]. SimCruise does not override it.
5. With async physics, a game frame longer than `MaxPhysicsDeltaTime` loses
   physics time. The car moves in slow motion during that frame. An Epic
   engineer called the discarded time "a surprise" [verified, forum thread
   "Async physics slows down at low game thread fps"].
6. Rule for SimCruise: a frame of 34 ms on the 28 W GPU must not lose time.
   Set `MaxPhysicsDeltaTime` to at least 0.1 s. Measure the result.
7. Measure sim time against wall time. Use `phys_steps` in the drive-test CSV.
   Expected steps per frame = `frame_dt * 360`. The error must stay under 1 %.
8. Controls from the game thread reach the physics thread one step late or
   more. Anything that must repeat step for step must run on the physics
   thread (`SetPhysicsThreadDriver`).
9. Do not differentiate physics velocity over the game frame time. The number
   of physics steps per frame varies (for example 5, 6 or 7 at 60 fps).
   Use the physics sim time (`Telemetry.SimTimeS`) as the divisor.
10. Never read or write a particle's game-thread API from the physics thread.
    Use `GetPhysicsThreadAPI()` inside the callback only.

## 3. Rules: tick groups and order

1. Tick groups run in this order: `TG_PrePhysics`, `TG_StartPhysics`,
   `TG_DuringPhysics`, `TG_EndPhysics`, `TG_PostPhysics`, `TG_PostUpdateWork`
   [verified, Epic "Actor Ticking"].
2. Write controls in `TG_PrePhysics`. The player controller does this in `PlayerTick`.
3. Read physics results in `TG_PostPhysics` or later. The pawn ticks in `TG_PostPhysics`.
4. Use `AddTickPrerequisiteActor` when one actor must tick after another.
   Remove the prerequisite when the link ends (`OnUnPossess`).
5. The player controller ticks while the game is paused. Other actors do not,
   unless `bTickEvenWhenPaused` is true [unverified engine default for APlayerController].
6. Physics does not step while the world is paused. Telemetry is then frozen.
   Do not use telemetry to confirm a change made during a pause.

## 4. Rules: threads and locks

1. Hold one lock order: vehicle mutex, then physics scene lock.
2. Keep work under the vehicle mutex short. The physics step holds it across
   four ray casts. Each game-thread setter also takes it.
3. Batch game-thread writes into one call per frame where possible
   (`SetDriverControls` already does this for the shell).
4. A pending command (reset, shift, manual) holds coordinates or state from the
   moment it was queued. A rebase or a car change between queue and apply
   makes it stale. Shift stored world positions on rebase, or clear them.
5. Do not cast or dereference UObjects on the physics thread unless the object
   is kept alive and the data is locked. `ASimCruiseWorldTile::SurfaceGripAtFace`
   is correct: it copies a shared pointer under a lock.

## 5. Rules: GC and UPROPERTY

1. Every UObject pointer member of a UObject must be a `UPROPERTY()`. Use `TObjectPtr<>`.
   Source: Epic "Object Pointers in Unreal Engine" [verified page exists].
2. Use `TWeakObjectPtr` when the holder must not keep the object alive.
3. Use `TStrongObjectPtr` for a UObject held by a non-UObject (static caches).
4. Do not store UObject pointers in physics-thread state. Store data copies.
5. Current status: a grep found no bare UObject pointer members outside tests.

## 6. Rules: `WITH_EDITOR` and packaged builds

1. Code inside `#if WITH_EDITOR` does not exist in a packaged game.
2. SimCruise builds some materials from code in the editor and loads a saved
   copy in a packaged game (`SimCruiseLowBeamPattern.cpp`, `SimCruiseCarClock.cpp`).
   The saved copy can go stale. Re-save it with the commandlet after each change.
3. Loose JSON (`data/cars`, `Content/Vehicle/Calibration`) is copied by
   `scripts/package.ps1`. Epic's way is `DirectoriesToAlwaysStageAsNonUFS`.
4. Test the packaged Shipping build, not only the editor. `scripts/playtest.ps1`
   runs `-SimCruiseShellSelfTest` on the packaged game.
5. `SIMCRUISE_CALIBRATION_DIR` works in development builds only.

## 7. Rules: input (Enhanced Input)

1. SimCruise builds its actions and mapping context in code
   (`USimCruiseInputConfig::Create`). The table is `Bindings()`.
2. Dead zones: triggers 0.04, stick X 0.12, axial
   (`UInputModifierDeadZone`) [verified API exists].
3. Analog values are read with `GetActionValue` each frame in `PlayerTick`.
4. Discrete actions bind to `ETriggerEvent::Started`. One press = one event.
5. A pad that disconnects must not leave a held throttle. Pause the game on
   disconnect. Xbox guidance asks for remapping and sensitivity options
   (XAG 107) [verified].
6. Never reset steering state while the car is still moving at speed, unless
   the car is also reset. A reset rack in a corner makes the car run wide.

## 8. Rules: automation tests

1. Use `IMPLEMENT_SIMPLE_AUTOMATION_TEST` for unit tests and latent commands
   for multi-frame tests [verified, Epic "Write C++ Tests"].
2. Run all tests with `scripts/check.ps1` (`-Filter SimCruise.Vehicle` for a subset).
3. Bench worlds tick at exactly 1/60 s (`VehicleBenchWorld.cpp:120`). They cannot
   find frame-rate bugs. Add tests that tick at 1/15, 1/30 and 1/50 s and with jitter.
4. A test must fail on the bug. Write the failing test first.
5. Use Gauntlet for packaged-build tests on the target PC [verified page exists].

## 9. Performance on a 28 W GPU (numbers)

- The physics step costs 21-34 µs (doc measurement). 360 steps = about 1 % of a core.
- On battery the laptop runs at exactly 30 fps (NVIDIA Battery Boost)
  (`docs/research/real-world-detail.md`). That is the `MaxPhysicsDeltaTime` edge.
- The mirrors render a 768x320 scene capture every third frame. Count it in GPU budget.
- Target: no frame over 50 ms in a 10-minute drive. Use `stat unit` and Unreal Insights.

## 10. How to judge SimCruise (checks)

| Check | Method | Pass |
|---|---|---|
| Physics time is real time | Drive-test CSV: sum of `phys_steps` vs sum of `frame_dt * 360`, at 60, 30, 15 fps | error < 1 % |
| Fixed-step result | `SimCruise.Vehicle.Convergence.*`, `Determinism` | pass |
| Body ownership | `SimCruise.Vehicle.Chaos.BodyOwnership` | pass, `MassPropertyRestores` = 0 |
| Reset | Recover at 100 km/h in chase view, record | no camera swoop, car at rest |
| Rebase | Drive at 120 km/h across a rebase, log contacts | 4 contacts every step |
| Input | `SimCruise.Gameplay.Input.*` | pass |
| Packaged | `scripts/playtest.ps1` smoke | no Error lines |

## 11. Common failure modes

1. Slow motion at low frame rate (`MaxPhysicsDeltaTime` clamp).
2. Camera or head jitter from velocity differentiated over the frame time.
3. Stale pending commands (reset transform) across a rebase.
4. Physics-thread reads of game-thread data while the game thread moves it.
5. A test that passes only because the bench ticks at a perfect 60 Hz.
6. Editor-only code paths that hide a packaged-build failure.
7. State that survives a reset (traction-control integrator, ABS release).
8. A UI toggle that reads physics telemetry while the game is paused.

## 12. References

- Epic, Physics Settings: https://dev.epicgames.com/documentation/unreal-engine/physics-settings-in-the-unreal-engine-project-settings
- Epic, Physics Sub-Stepping: https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-sub-stepping-in-unreal-engine
- Epic, ISimCallbackObject API: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Chaos/ISimCallbackObject
- Epic, FPhysicsSolverBase API: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Chaos/FPhysicsSolverBase
- Epic forum, "Async physics slows down at low game thread fps" (Epic staff reply): https://forums.unrealengine.com/t/async-physics-slows-down-at-low-game-thread-fps/2731575
- MaxPhysicsDeltaTime default (secondary): https://indxzero.github.io/ue544cvarwiki/articles/maxphysicsdeltatime
- Epic, Actor Ticking: https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-ticking-in-unreal-engine
- Epic, ETickingGroup: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/ETickingGroup
- Epic, Object Pointers: https://dev.epicgames.com/documentation/en-us/unreal-engine/object-pointers-in-unreal-engine
- Epic, Incremental GC: https://dev.epicgames.com/documentation/unreal-engine/incremental-garbage-collection-in-unreal-engine
- Epic, Enhanced Input: https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine
- Epic, UInputModifierDeadZone: https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/EnhancedInput/UInputModifierDeadZone
- Epic, Write C++ Tests: https://dev.epicgames.com/documentation/en-us/unreal-engine/write-cplusplus-tests-in-unreal-engine
- Epic, Automation Test Framework: https://dev.epicgames.com/documentation/unreal-engine/automation-test-framework-in-unreal-engine
- Epic, Gauntlet: https://dev.epicgames.com/documentation/en-us/unreal-engine/gauntlet-automation-framework-in-unreal-engine
- Epic, UProjectPackagingSettings: https://dev.epicgames.com/documentation/unreal-engine/API/Developer/DeveloperToolSettings/UProjectPackagingSettings
- Epic, Chaos Vehicles API (for comparison): https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ChaosVehicles/UChaosVehicleMovementComponent
- Fiedler, G., "Fix Your Timestep!": https://gafferongames.com/post/fix_your_timestep/
- Microsoft, Xbox Accessibility Guideline 107 (Input): https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/107

Licences: Epic docs and forum posts are read for reference only. Do not copy
their code. Gaffer on Games and Microsoft Learn are read for reference only.
