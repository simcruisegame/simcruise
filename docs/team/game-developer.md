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

## What I now know (onboarding, 2026-11-14)

**How my area works today.**
- Physics runs async at a fixed 1/360 s. `MaxPhysicsDeltaTime=0.1` is now set (`Config/DefaultEngine.ini:93-97`), so a 30 fps frame does not lose sim time.
- `StepVehicleOnPhysicsThread` (`Vehicle/SimCruiseVehicleMovementComponent.cpp:167`) holds the vehicle mutex for the whole step. It puts back the car's mass properties if they changed (190-215) and applies a queued reset (217-226). A contact guard limits speed gain to 3 g, spin gain to 40 rad/s², and spin to 12 rad/s (235-271). Four suspension rays call `World->LineTraceSingleByChannel` from the physics thread (326). Grip comes from `ASimCruiseWorldTile::SurfaceGripAtFace` through `Cast<ASimCruiseWorldTile>(Hit.GetActor())` (335).
- Lifetime: `BeginPlay` registers the sim callback (505). `EndPlay` unregisters it (550). A physics-state destroy clears the proxy (517-523).
- `ASimCruiseWorldStreamer::TickStreaming` runs in `TG_PrePhysics` (`World/SimCruiseWorldStreamer.cpp:29,483`). The order in each frame is:
  - It rebases when the focus is more than 2000 m from the origin (494; header :84).
  - It predicts where the car will be in 5 s (501).
  - It evicts and requests tiles.
  - It installs 1 tile per frame, with a 6 ms step budget (523-532). An aerial photo uploads in a frame of its own (535-548).
  - It trims unused aerials only on frames that install a tile (581-586).
- `FSimCruiseTileManager` (`World/SimCruiseTileManager.cpp`):
  - The ring is 3x3 (`RingRadius=1`, header :78). At most 25 tiles stay resident (header :59). At most 2 tiles build at once (`MaxInFlight=2`).
  - Two edge neighbours never build together (62-67). The score is 0.4 × the gap now + 0.6 × the gap in 5 s (53).
  - A generation and serial number drop stale results (97-103). A failed tile waits 15 s before a retry (116; header :32).
- `Launch` (`Streamer.cpp:222`) reads the bundled tile first (297). Otherwise it calls `RequestTile` with a 90 s timeout (314; `AtlasClient.h:113`). The future continuation sends the parse and build to the thread pool (318-331). One worker fetches each aerial photo with the `aerial` command (60 s timeout). The other workers sleep-poll every 50 ms until it is done (253-279).
- `FSimCruiseAtlasClient` (`Atlas/SimCruiseAtlasClient.cpp`):
  - It starts one helper process per job on its own thread. `MaxConcurrent` defaults to 2, clamped to 1-8 (119, 197).
  - It polls every 5 ms. A cancel or a timeout kills the process tree (267-293).
  - The answer is the last non-empty stdout line, parsed as JSON (316-341).
  - It looks for the helper in this order: `SIMCRUISE_MAP_HELPER`, `tools/atlas.exe`, the folder of the game binary, then Bun (92-113).
- Floating origin: `TileSizeM = 40075016/65536 × cos(start lat)` is set once per drive (`World/SimCruiseGeo.cpp:23-25,46`). `RebaseTo` moves by whole tiles (82-87). These listen to `OnRebased`:
  - GameMode moves the car with `TeleportPhysics` and snaps the camera (`Gameplay/SimCruiseGameMode.cpp:690-705`).
  - Traffic and LiveSky also listen.
  - Trees, guard rails and regional terrain do not listen. They poll the frame each tick.
- `USimCruiseWorldSubsystem::OnWorldBeginPlay` (`World/SimCruiseWorldSubsystem.cpp:23-49`):
  - A packaged game starts the drive by default. The editor needs `-SimCruiseWorld`.
  - It reads `-SimCruiseStart=lat,lon`. If the string does not parse, it falls back to Ocean Drive and does not log it.
- Live feeds (`Live/SimCruiseLiveWorldSubsystem.cpp`): the slow feeds read again after the car moves `MoveRefetchKm` (5 km per the doc) (331). The request budget is 3 in flight and 600 an hour (577).
- AI detail (`Live/SimCruiseNeuralDetail.cpp`):
  - The engine is a heap singleton that is never freed (81). One worker thread takes the newest photo first, and the queue has no limit (313-345).
  - On the GPU it rests between windows, to use about 35 % (`Duty 0.35`, 228). It keeps photos at 2x (`OutputScale=2`, header :80).
  - The texture swap walks every `UMaterialInstanceDynamic` on the game thread (`LiveWorldSubsystem.cpp:160-168`).
  - The cache in `Saved/NeuralDetail` has no eviction (289-295).

**Does it work at a random US place?**
- General: the tile math, rebase, thread model and physics are plain Web Mercator with no region in them. They work at any lat/lon. Only the 24 Miami tiles are bundled, so every other place needs the helper and the network.
- Hard-wired: sky glow is scaled to Miami Beach's 300 lamps (`Streamer.cpp:388-399`). Street lights are a fixed 1800 cd at 3000 K (413-441). A bad start string, or a start where the tiles fail, falls back to Ocean Drive (`WorldSubsystem.cpp:36-37`; `GameMode.cpp:541-545`).
- us-rawlins-wy (I-80, about 120 km/h): a tile is 456 m, so the car crosses one in about 13.7 s. The tile straight ahead builds alone, because its two side tiles are edge neighbours. On an empty cache, the helper fetch plus the 1-9 s build (`docs/unreal-architecture.md:290`) must finish in under 14 s (estimate: often too slow). us-casselton-nd: 418 m tiles, about 14 s at 105 km/h (estimate).
- us-welch-wv, us-hilo-hi, us-stonington-me (poor data or slow sources): a failed tile is a hole for at least 15 s. Nothing catches the car there:
  - The regional terrain has `NoCollision` (`SimCruiseRegionalTerrain.cpp:262`).
  - Only the start waits for tiles (`GameMode.cpp:534`).
  - So the car falls through (estimate, from the code).
- us-anchorage-ak: tiles are 294 m, so the tile changes and rebases come more often. The scale error grows fastest here (tan 61° = 1.8).
- us-bronx-ny: dense tiles move build time toward the 9 s end. Install steps keep the 6 ms budget. GPU memory for aerials is about 4 × 21 MB at 2x (estimate).

**Main gaps against "realistic everywhere".**
1. P0: A missing or failed tile ahead has no safety net. The car drives onto ground with no collision and falls. `SimCruiseWorldStreamer.cpp:532-559`, `SimCruiseTileManager.cpp:114-117`, `SimCruiseRegionalTerrain.cpp:262`. Unverified: the KillZ check may then destroy the pawn.
2. P1: Streaming may not keep up at highway speed.
   - When a tile drops out of the ring, its helper job is not cancelled. `TakeEvictions` (`SimCruiseTileManager.cpp:78-95`) never touches `InFlight`, so a stale job holds 1 of the 2 slots for up to 90 s.
   - The edge-neighbour rule makes the tiles ahead build one after another.
3. P1: The tile scale is fixed at the start latitude (`SimCruiseGeo.cpp:46`). The error is tan(lat) × Δlat. 500 km north from 42° is about 7 % too large. 100 km at Anchorage is about 2.9 %. Road lengths and grades come out wrong (estimate).
4. P1 (hypothesis, open since review 2026-11-12): at each rebase the tiles move at once (`Streamer.cpp:351`). The car's teleport reaches the physics thread later (`GameMode.cpp:699`). Steps already running then cast rays at moved tiles (`MovementComponent.cpp:326`): no contact for 1-12 steps every 2 km.
5. P2: Three smaller faults.
   - A reset queued in the same frame as a rebase is not moved with the rebase (`MovementComponent.cpp:33,219`; `HandleRebased` does not shift it). It is still open from the review.
   - The physics thread reads the tile actor and its `SurfaceLock` (`SimCruiseWorldTile.cpp:106`). `StartAt` destroys all tiles while the car may still stand on them (`Streamer.cpp:69-71`). This could be a use-after-free (hypothesis).
   - The `Saved/NeuralDetail` disk cache grows without limit on long drives.

**My first three actions.**
1. Missing-ground guard.
   - What: if the tile under the car, or the next tile on its path, is not `IsCollisionReady`, hold the car at its last road pose and show the loading line. Log `GROUND-MISSING`.
   - Where: `Gameplay/SimCruiseGameMode.cpp`, with a test flag `-SimCruiseHelperFailAfter=<s>`.
   - Done when: `scripts\preflight.ps1 -Roads us-welch-wv,us-hilo-hi -Cars nb2001_aaron -Seconds 120`, with the flag at 30 s, never puts the car more than 2 m below the road. Also, `SimCruise.World.MissingTile` fails before the fix.
2. Cancel stale tile jobs and measure lead time.
   - What: keep the atlas job for each ticket. Cancel it when its tile leaves the ring. Log the lead time (distance to the tile edge ÷ speed) when each tile becomes ready.
   - Where: `World/SimCruiseWorldStreamer.cpp` and `World/SimCruiseTileManager.cpp`.
   - Done when: with an empty `SIMCRUISE_MAP_CACHE`, `scripts\preflight.ps1 -Roads us-rawlins-wy,us-casselton-nd -Cars meridian_z -Seconds 300` shows 0 frames on a tile that is not ready.
3. Prove or close the rebase contact loss, and fix the pending reset.
   - What: log `WheelContactCount` for each step around the "Floating origin moved" log line. Add `ShiftPendingReset(Offset)` under the mutex, called from `HandleRebased`.
   - Done when: `scripts\preflight.ps1 -Roads random -RandomCount 3 -Seconds 180` shows 4 contacts on every step and no jolt over 8 m/s² at a rebase. The new unit test `SimCruise.Vehicle.RebasePendingReset` passes.

**Docs that are wrong or old.**
- `docs/unreal-architecture.md:286-303` ("Known gaps") says there are no trees, furniture, signals, traffic, bridges or surface friction. The code has all of them, for example `SurfaceGripAtFace` (`SimCruiseWorldTile.cpp:96`) and `SimCruiseTraffic`.
- `docs/unreal-architecture.md:513` says "the streamer waits for" the AI detail. The code does not wait: `EnhanceAerial` queues the photo and returns false (`SimCruiseNeuralDetail.cpp:313-325`; `Streamer.cpp:284`).
- `docs/team/ownership.md` rule 5 points to `docs/team/test-places.md`, which does not exist. The list is `data/test-places.json`. The "about 550 m" tile size in `how-the-game-works.md` is true only at Miami's latitude; it is 294-576 m across the test places.
- `docs/unreal-building.md:268-270` describes the pre-flight as the 12 hero-road runs only. It leaves out `-Roads us-*|random|pool` (`scripts/preflight.ps1:21-30`).

Proposed line for `docs/team/game-developer.md`: "Tile size is fixed at the start latitude (`SimCruiseGeo.cpp:46`); scale error = tan(lat) × Δlat, about 7 % after 500 km north at 42° [verified in code]."

Note: I did not use any outside web sources. Every claim above comes from the repo files cited. The pawn-destroy-at-KillZ point is unverified engine behaviour.
