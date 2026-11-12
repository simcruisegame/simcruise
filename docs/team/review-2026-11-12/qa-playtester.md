# Review 2026-11-12: QA playtester

Reviewer: `qa-playtester`. Method: code and test reading of the current tree.
No item was reproduced on the target PC in this review. Each item says
"from code reading" and gives the steps to reproduce it.

## Findings (ranked)

### 1. P1: No test catches frame-rate-dependent driving
- Evidence: bench worlds tick at exactly 1/60 s (`Source/SimCruise/Private/Tests/VehicleBenchWorld.cpp:120-122`).
  `scripts/drive-robot.ps1:25` makes 15 fps optional (`-LowFps`) and judges only
  slip and yaw, not physics time. No check compares `phys_steps` with wall time.
  The game developer's finding 1 (slow motion below 30 fps) passes every current test.
- Repro (from code reading): 1. Launch with `-SimCruiseDriveTest -ExecCmds="t.MaxFPS 15"`.
  2. Open `Saved/DriveTest/drive-test.csv`. 3. Sum `frame_dt*360` and the
  `phys_steps` increase. Expected: equal. Actual (predicted): steps about half.
- Fix: add a physics-time check to `drive-robot.ps1` (fail if error > 1 %); make
  `-LowFps` part of the default run; add a bench test that ticks at 1/15 s and 1/30 s with jitter.
- Verify: the new check fails on today's config and passes after the config fix.

### 2. P1: The drive robot does not drive the Corvette or the Mustang
- Evidence: `scripts/drive-robot.ps1:23` defaults to `nb2001`, `nb2001_aaron`.
  The C8 (`meridian_z`) and 1965 Mustang (`mustang65`) are two of the three cars that matter.
- Fix: default `-Cars` to `nb2001_aaron, meridian_z, mustang65`. Set per-car
  "stable manoeuvre" lists (a 1965 Mustang on bias-era geometry may slide sooner;
  ask the vehicle-dynamics expert for its limits).
- Verify: `history.csv` has rows for all three cars after one default run.

### 3. P1: Recover is not tested end to end; the camera likely swoops
- Evidence: `SimCruise.Drive.ShellControl` resets through the vehicle only
  (`Private/Tests/DriveIntegrationTests.cpp:172-180`). Nothing drives
  `ASimCruiseGameMode::RecoverVehicle` (`SimCruiseGameMode.cpp:654-695`) with the
  camera. The camera snaps before the physics reset lands (game developer finding 2).
- Repro: 1. Chase view (Y). 2. Drive 50 m off the road onto grass. 3. Press View.
  Expected: the view cuts to the car on the road. Actual (predicted): the camera
  flies from the grass to the road.
- Fix: snap on a reset serial from telemetry. Add a world test: recover from
  60 m off-road; car at rest on the road within 0.5 s; camera distance to car < 8 m every frame.

### 4. P2: The car button cycles through ten cars, not the three that matter
- Evidence: `SimCruiseGameMode.cpp:401-409` cycles `GetAvailableCarIds()`
  (`SimCruiseGameInstance.cpp:34-40`): every file in `data/cars` in alphabetical
  order (CR-V, F-150, Grand Cherokee, C8, Model 3, Mustang, NB 1999, NB 2001,
  Aaron's NB, Odyssey). From Aaron's NB to the C8 takes five presses through
  vans and SUVs. Each press is saved as the remembered car (`:409`).
- Fix: a priority list (Aaron's NB, C8, Mustang) first, or only those three in
  this phase. This is a curation change, not a new feature.
- Verify: from Aaron's NB, RB in the atlas reaches the C8 in one press and the Mustang in two.

### 5. P2: Pad disconnect is not handled
- Evidence: no handler for controller connection changes in `Private/Gameplay`
  (grep for `ConnectionChange`, `InputDeviceConnection`: only the wheel logs a
  disconnect, `SimCruiseWheelDevice.cpp:105`). The game does not pause.
- Repro: 1. Drive at 80 km/h with RT held. 2. Switch off the pad. Expected: the game
  pauses and shows "Controller disconnected". Actual (unknown): the car may keep the
  last throttle or coast with no warning. Test on the PC.
- Fix: pause and open a message on disconnect; zero the shaper on reconnect.
- Verify: the repro, 5 of 5 tries.

### 6. P2: Easy accidental presses change the drive
- Evidence: `SimCruiseInputConfig.cpp:54` maps left-stick click to the HUD level
  (steering hard often clicks the stick). `:63` maps View to Recover with no hold
  or confirm: one press teleports the car. `:41` maps B to the gearbox toggle;
  `SimCruisePlayerController.cpp:379` saves it as a preference at once.
- Fix: require a short hold (about 0.5 s) for Recover and the gearbox toggle in
  driving, or move HUD off the stick click. Note XAG 107 asks to avoid forced holds,
  so make the hold an option and keep remapping on the next-phase list.
- Verify: 30 minutes of pad-only driving with an event log; zero unintended actions.

### 7. P2: The atlas gearbox button shows a stale label
- Evidence: `SimCruisePlayerController.cpp:375-378` and `:475` read telemetry
  that cannot change while the atlas pauses physics (game developer finding 5).
- Repro: 1. Menu (atlas). 2. LB. Expected: label "Manual". Actual (predicted):
  label stays "Automatic". 3. LB again: still "Automatic"; the car ends up manual.
- Verify: label flips on each press; the HUD agrees after closing.

### 8. P2: The rebase test does not use a physics car
- Evidence: `SimCruise.World.Rebase` (`Private/World/Tests/SimCruiseWorldTests.cpp:574-624`)
  moves a plain `FVector` "car". No test drives an `ASimCruiseVehicle` across a
  rebase at speed (game developer finding 7 is a hypothesis about that moment).
- Fix: a test that drives a vehicle at 35 m/s across two rebases on bundled tiles.
  Pass: four contacts every step; speed change < 0.5 m/s across the rebase.

### 9. P2: No soak test for the player car
- Evidence: only `SimCruise.World.TrafficSoak` exists (`Private/Tests/TrafficSoakTests.cpp`).
  `scripts/playtest.ps1` runs a 10-minute self-test (`-SmokeMinutes 10`).
- Fix: a 2-hour robot drive (loop of start-road phases plus "Surprise me" every
  10 minutes) that logs memory, hitch count and physics time.
- Pass: memory growth < 5 % after 10 minutes; no hitch > 100 ms after the first minute.

### 10. P2: Packaged materials can drift from the code
- Evidence: the editor builds the low-beam and clock materials from code; a
  packaged game loads saved copies (`SimCruiseLowBeamPattern.cpp:65-68`,
  `SimCruiseCarClock.cpp:135-138`). No check compares the two.
- Fix: a commandlet check in `check.ps1` that fails when the saved asset's shader
  code differs from the code string.
- Verify: change the code string; the check fails until the asset is re-saved.

## Next phase proposals (ranked)

| # | Goal | Why it matters for the vision | Scope | Acceptance test | Risk | Size |
|---|---|---|---|---|---|---|
| 1 | Nightly driving regression | Keeps the three cars feeling right while work continues | `check.ps1` + robot for 3 cars at 60 and 15 fps + physics-time check + diff of `history.csv` vs the last release | One report per night; any change > 2 deg slip or > 10 % yaw needs a reason | Needs the PC free at night | S |
| 2 | Player-car soak and hitch test | Long relaxed drives are the core use | 2 h robot drive with travel, memory, hitch and physics-time logs | Thresholds in finding 9 met on battery and mains | Long runs block the PC | M |
| 3 | Controller-only certification | The Xbox pad is the main input | Checklist from XAG 107: disconnect pause, remap, stick and trigger sensitivity, no forced holds, menus by pad | 30 min pad-only session passes the checklist | Some items are features; owner must approve | M |
| 4 | Real-car reference tests | "Behaves exactly like the real car" needs measured targets | Per priority car: published 0-60, 60-0, skidpad, steady-state understeer gradient in the robot | Each car inside its band; sources cited in `docs/cars.md` | Some published data is weak (1965 Mustang) | M |
| 5 | Playtest bug capture | Owner reports become repeatable | A key that saves the last 30 s of the drive log with a note | A filed bug includes the CSV and replays in the robot | Disk use; privacy is local only | S |
