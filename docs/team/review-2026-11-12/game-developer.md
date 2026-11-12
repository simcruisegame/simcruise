# Review 2026-11-12: game developer

Reviewer: `game-developer` (Unreal 5.8 C++). Method: code reading of the
current tree (no build, no run on the target PC). Each finding gives file:line
evidence. "Hypothesis" marks a claim that a test must still prove.

## Findings (ranked)

### 1. P1: The car runs in slow motion when a frame takes longer than 33 ms
- Evidence: `Config/DefaultEngine.ini:85-90` sets `bTickPhysicsAsync` and the
  1/360 s step but not `MaxPhysicsDeltaTime`. The engine default is 1/30 s
  (unverified secondary source). Epic: "If this is smaller than the tick of the
  engine, physics will move artificially slow" (Physics Settings page). An Epic
  engineer confirms that async physics drops the extra time (forum thread
  "Async physics slows down at low game thread fps").
- Impact: the target laptop runs at exactly 30 fps on battery
  (`docs/research/real-world-detail.md:17`). Every frame over 33.3 ms (tile
  installs, shader compiles, a busy junction) loses physics time. At 15 fps the
  car moves at half speed. Speed, engine sound and the road disagree.
- Fix: set `MaxPhysicsDeltaTime=0.1` (or higher) under `[/Script/Engine.PhysicsSettings]`.
  Check whether `MaxSubstepDeltaTime` also clamps in async mode (Epic reply).
- Verify: `-SimCruiseDriveTest -ExecCmds="t.MaxFPS 15"`. In the CSV, the sum of
  `phys_steps` deltas must equal the sum of `frame_dt * 360` within 1 %.
  Add an automation test that ticks a world at 1/15 s and checks `PhysicsSteps`.

### 2. P1: Recover snaps the camera to the old place, then the camera swoops
- Evidence: `SimCruisePlayerController.cpp:604-615` calls `RecoverVehicle` and
  then `Camera->SnapNextFrame()`. `SimCruiseGameMode.cpp:690` only queues the
  reset. `SimCruiseVehicleMovementComponent.cpp:594-599` sets a pending flag;
  the physics thread applies it at `:210-218`. The camera reads
  `Pawn->GetActorTransform()` (`SimCruisePlayerCameraManager.cpp:68`) on the
  next frame, before the new pose returns from the physics thread. The snap
  lands on the old pose. `Chase.Update` (`:108`) then smooths toward the new pose,
  up to `RecoverSearchRadiusM` away.
- Status: from code reading; not seen on screen yet.
- Fix: snap when the reset is done. Count resets in telemetry (a reset serial),
  and call `SnapNextFrame` when the serial changes.
- Verify: chase view, drive 60 m off the road, press View. Record video and
  the drive log. The camera must not travel between the two places.

### 3. P2: Head motion and chase camera differentiate velocity over the game frame
- Evidence: `SimCruiseCameraModels.cpp:82-94` computes acceleration as
  `(WorldVelocityMps - PreviousVelocity) / FrameS` with the game frame time.
  The velocity comes from the last physics step (`SimCruisePlayerCameraManager.cpp:69`).
  With async physics the number of 1/360 s steps per frame varies (for example
  5-7 at 60 fps), so the felt acceleration jumps by about 15 % frame to frame.
  `SimCruise.Gameplay.Camera.HeadModelFrameRate` feeds a perfect velocity and
  cannot see this.
- Fix: divide by the physics time between the two telemetry samples
  (`FSimCruiseVehicleTelemetry::SimTimeS`). Pass it through `FSimCruiseDriveTelemetry`.
- Verify: steady 0.5 g circle at 45 fps; log head offset. Peak-to-peak noise
  must be under 10 % of the mean. Add the jitter case to the test.

### 4. P2: Closing the atlas at speed resets the steering rack to zero
- Evidence: `SimCruisePlayerController.cpp:580` calls `Shaper.Reset()` in
  `CloseOverlays`. `SimCruiseDriverShaping.cpp:97-106` sets `RackRad = 0`. The
  car keeps its speed and yaw rate through the pause. On resume the rack restarts
  from straight and ramps back at the pad apply rate.
- Impact: open the atlas in a corner, close it, the car runs wide for a moment.
- Fix: keep the rack on close (reset only the key pedal ramps), or reset to the
  telemetry rack angle.
- Verify: 50 km/h circle, open and close the atlas, log `shaper_rack_deg`. No
  dip to 0.

### 5. P2: The gearbox toggle in the atlas reads frozen telemetry
- Evidence: `SimCruisePlayerController.cpp:375` computes `!Telemetry.bManualTransmission`.
  `:378` reads telemetry back "now", but physics does not step while the atlas
  pauses the game. `AtlasView` shows `Telemetry.bManualTransmission` (`:475`).
- Impact: press LB in the atlas: the label does not change. Press it again: the
  code sets the same value again. The saved preference is right; the screen is wrong.
- Fix: keep the requested state on the controller (or read `bManualRequested`
  from the vehicle) and show that.
- Verify: open atlas, press LB twice, label must flip twice; close; check the HUD.

### 6. P2: A pending reset keeps pre-rebase coordinates
- Evidence: `SimCruiseVehicleMovementComponent.cpp:33` stores `PendingResetTransform`
  in world space. `SimCruiseGameMode.cpp:636-652` (`HandleRebased`) moves the
  pawn but not that transform.
- Impact: Recover in the same frame as a rebase puts the car one tile (about
  550 m) away from the road, maybe in the air. Rare.
- Fix: add `ShiftPendingReset(Offset)` under the mutex, called from `HandleRebased`.
- Verify: unit test: queue a reset, rebase, step; car is at the shifted place.

### 7. P2 (hypothesis): One physics step without ground at each rebase
- Evidence: suspension rays run on the physics thread against the game-thread
  scene query structure (`SimCruiseVehicleMovementComponent.cpp:262-284`,
  `docs/unreal-architecture.md:78`). `ASimCruiseWorldStreamer::Rebase` moves tile
  actors (`SimCruiseWorldStreamer.cpp:350`) and the car moves separately
  (`SimCruiseGameMode.cpp:645`). Steps in flight between these two moves can
  cast from the old car place against moved tiles.
- Verify first: drive at 130 km/h across 5 rebases with `-SimCruiseDriveLog`.
  Look for rows with `contacts` < 4 or a jump in `susp_cm` at the rebase.
- Fix if proven: freeze the vehicle step for the rebase frame, or rebase on the
  physics thread through the sim callback.

### 8. P2: The fallback car is hard-coded to `nb1999`
- Evidence: `SimCruiseGameMode.cpp:576-577` loads `nb1999` when the chosen car
  fails. `Public/Gameplay/SimCruiseGameMode.h:91` also defaults `StartCarId` to
  `nb1999`; only `Config/DefaultGame.ini:11` sets `nb2001_aaron`.
- Fix: fall back to `StartCarId`; set the C++ default to `nb2001_aaron`.
- Verify: `-SimCruiseCar=nosuchcar` starts in Aaron's NB.

### 9. P2: The vehicle mutex is held across four ray casts
- Evidence: `SimCruiseVehicleMovementComponent.cpp:162` locks for the whole step,
  including `LineTraceSingleByChannel` at `:269`. The game thread locks the same
  mutex several times per frame (controls, telemetry for HUD, camera, pawn, rumble).
- Impact: game-thread stalls up to one step (21-34 µs) per lock. Small now; it grows
  with traffic cars that use the same component.
- Fix: copy controls in and telemetry out under the lock; run the step outside it.
- Verify: Unreal Insights, `FScopeLock` wait time on the game thread < 0.1 ms per frame.

## Next phase proposals (ranked)

| # | Goal | Why it matters for the vision | Scope | Acceptance test | Risk | Size |
|---|---|---|---|---|---|---|
| 1 | Frame-rate-proof driving | "Behaves exactly like the real car" must hold at 30 fps on the 28 W GPU | Physics time budget, sim-time camera maths, a jittered-tick bench (1/15-1/60 s with noise) for every car | CarTrack and robot results within 2 % at 15, 30, 60 fps; sim time = wall time within 1 % | Higher `MaxPhysicsDeltaTime` lets a long hitch run many steps at once | M |
| 2 | Physics-thread input timeline | A 30 fps stair-step on the rack feels coarse at 360 Hz | Send time-stamped controls; interpolate rack and pedals on the physics thread | Rack angle continuous at 360 Hz in the CSV at 30 fps; robot results unchanged at 60 fps | Adds latency if done wrong | M |
| 3 | Replay of a recorded drive | Every playtest bug becomes a repeatable test | Record controls per physics step; replay through `SetPhysicsThreadDriver` | A recorded 60 s drive replays bit for bit (`Determinism` style) | Streaming world must also repeat | L |
| 4 | Packaged-build test on the target PC | Shipping-only failures are invisible in the editor | Gauntlet or `playtest.ps1` runs 10 min of robot driving in Shipping | No Error lines; frame-time and memory limits met | Needs the PC and a free GPU | M |
| 5 | Game-thread and GPU budget | Lumen, VSM and mirror captures compete with physics on a 28 W GPU | Per-system budget table and Insights captures | 95th-percentile frame < 33 ms at the chosen preset | Budget can force visual cuts | M |
