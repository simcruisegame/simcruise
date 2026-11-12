# QA playtester: study notes

Role: QA lead and playtester for SimCruise, a driving sim.
Scope: test plans, regression, soak tests, repro steps, bug severity,
controller-only play, telemetry capture, the scripts and the automation tests.
Style: ASD-STE100 Simplified Technical English. One instruction per sentence.
"[verified]" = read in a primary source on 2026-11-12. "[unverified]" = not checked.

## 1. The tools you have

| Tool | What it does | Use it for |
|---|---|---|
| `scripts/check.ps1` | Builds and runs `SimCruise.*` automation tests headless. Fails on a failed test, a missing test report, an ensure or an Error log line. | Every change. `-Filter SimCruise.Vehicle` for a subset. |
| `scripts/drive-robot.ps1` | Drives limit manoeuvres on a flat pad through the real input stack. Fails if the rear axle slips past 15 deg in a "stable" manoeuvre. Appends to `Saved/DriveTest/history.csv`. | Any change to physics, steering, input. Add `-LowFps` for 15 fps. |
| `scripts/playtest.ps1` | Packages origin/main, runs a smoke self-test on the packaged game, zips it with notes. | Before the owner plays. |
| `-SimCruiseDriveTest` | Scripted drive on the start road. One CSV row per frame. | Regression of feel. |
| `-SimCruiseDriveLog` | Records the same CSV while a person drives. | Repro of a playtest bug. |
| `p.Chaos.StartVDRecording` | Chaos Visual Debugger recording. | Physics contact bugs. |
| `-ExecCmds="t.MaxFPS 15"` | Caps the frame rate. | Frame-rate bugs. |

Tests live in `Source/SimCruise/Private/Tests/` and `Source/SimCruise/Private/*/Tests/`.

## 2. Severity scale (use it on every bug)

- **P0**: crash, hang, data loss, the car cannot drive, or the game does not
  start in a packaged build. Fix before any other work.
- **P1**: the driving is wrong in a way the player notices in normal play.
  Examples: slow motion, a camera swoop, a lost control input, a wrong car.
  Fix in the current release.
- **P2**: a rare edge case, a cosmetic error, or a test gap. Plan it.

Rate severity by player impact times frequency. A rare crash is still P0.

## 3. How to write a bug report

Use this format. It follows the incident report of ISO/IEC/IEEE 29119-3
(identifier, summary, steps, expected, actual, environment, severity) [verified, standard exists; text is paywalled].

1. Title: one line. Say what fails and where.
2. Build: commit hash, Development or Shipping, editor or packaged.
3. Hardware: GPU, frame rate (from F3), power (battery or mains).
4. Input: Xbox pad, keyboard or wheel.
5. Car id (for example `nb2001_aaron`).
6. Place and time of day.
7. Steps: numbered. Start from a fresh launch.
8. Expected result. Actual result.
9. Frequency: for example 5 of 5 tries.
10. Evidence: CSV from `-SimCruiseDriveLog`, a video, log lines.
11. Severity and the reason for it.

A bug without repro steps is not done. Try three times before you file "can't repro".

## 4. Test plan for a driving release

### 4.1 Smoke (10 minutes, every build)
1. Start the packaged game. The car appears on Ocean Drive within 30 s.
2. Drive with the pad only. Accelerate, brake, steer, reverse.
3. Press View (Recover). The car is on the road, at rest.
4. Open and close the atlas (Menu). The car continues.
5. Toggle manual (B). Shift up and down with clutch (LB + RB / X).
6. Toggle camera (Y). Toggle HUD (left-stick click).
7. Check the log for `Error` lines.

### 4.2 Regression (each release)
1. `scripts/check.ps1` passes.
2. `scripts/drive-robot.ps1 -Cars nb2001_aaron,meridian_z,mustang65 -LowFps` passes.
3. Compare `history.csv` with the last release. A peak slip change of more than
   2 deg or a yaw-rate change of more than 10 % needs a reason.
4. Play test with the three priority cars: NB 2001 (Aaron's), Corvette C8
   (`meridian_z`), 1965 Mustang (`mustang65`).

### 4.3 Soak (2 hours, before release)
1. Drive or run the robot for 2 hours on mains and 1 hour on battery.
2. Record memory every 5 minutes (`stat memory`, or the Task Manager working set).
3. Pass: memory growth < 5 % after the first 10 minutes.
4. Pass: no hitch above 100 ms after the first minute.
5. Pass: physics steps match wall time (see 5.1).
6. Travel to 10 places with "Surprise me". Each one loads or falls back.

### 4.4 Controller-only play
1. Unplug the keyboard and mouse. Play 30 minutes.
2. Every menu must work with the pad (atlas, search, settings).
3. Disconnect the pad while you hold RT. The car must not keep the throttle.
4. Reconnect the pad. Input must work again without a restart.
5. Check accidental presses: left-stick click while steering hard, View while
   reaching for the D-pad.
6. Xbox guidance asks for remapping and sensitivity of sticks and triggers
   (+/-50 %) (XAG 107) [verified]. Log missing options as next-phase items.

### 4.5 Frame-rate matrix
Run the same manoeuvre at `t.MaxFPS` 15, 30, 45, 60 and uncapped.
The peak slip, yaw rate and stopping distance must match within 5 %.

## 5. Telemetry checks with numbers

### 5.1 Physics time vs wall time
- Columns: `t`, `frame_dt`, `phys_steps` in the drive-test CSV.
- Expected: delta `phys_steps` per row = `frame_dt * 360` on average.
- Pass: total error < 1 % over 60 s, at 15, 30 and 60 fps.
- Why: Unreal clamps each physics frame to `MaxPhysicsDeltaTime`. The Epic docs
  say physics then moves "artificially slow" [verified].

### 5.2 Reset
- After Recover: speed < 0.1 m/s within 0.5 s. Four wheel contacts.
- The camera must not travel visibly between the old and the new place.

### 5.3 Rebase (floating origin)
- Drive straight at 100-130 km/h for 2 km. A rebase happens about every tile.
- Pass: `contacts` = 4 on every row. No spike in `susp_cm` larger than 2 cm.

### 5.4 Handling (robot)
- Stable manoeuvres: rear-axle slip < 15 deg (`drive-robot.ps1`).
- Spin threshold `SPUN` past 15 deg.

## 6. Where the coverage is thin (known gaps)

1. Bench worlds tick at exactly 1/60 s (`VehicleBenchWorld.cpp:120`).
   No automated test ticks the vehicle at a low or jittery frame rate.
2. `SimCruise.World.Rebase` moves a plain vector, not a physics car.
3. No test drives Recover through `ASimCruiseGameMode::RecoverVehicle`.
4. No test for pad disconnect, pause, or atlas open/close at speed.
5. The drive robot defaults to the two NB cars. The C8 and Mustang are not in the default run.
6. No soak test for the player car (only `SimCruise.World.TrafficSoak`).
7. No test cycles cars (`CycleCar`) or travels while the car moves.

## 7. Common failure modes in driving games

1. Frame-rate dependence: slow motion, different handling at 30 fps.
2. Stale state after reset: gear, traction control, ABS, steering.
3. Input lost or stuck across pause, focus loss or pad disconnect.
4. Camera jumps after a teleport, a reset or an origin shift.
5. The car falls through new terrain at a tile seam.
6. A hitch during streaming makes the car jump.
7. Packaged-only crashes from missing loose files or editor-only code.
8. Preferences that save a wrong value (remembered car or gearbox).

## 8. References

- ISO/IEC/IEEE 29119-3:2021, Test documentation (paywalled; summary): https://www.microtool.de/en/document-management/test-documentation-with-iso-iec-ieee-29119-32021/
- IEEE 829 overview: https://wikipedia.com/wiki/IEEE_829
- Incident report term (ISO 29119): https://www.imbus.de/en/glossar/term/incident-report
- Epic, Write C++ Tests: https://dev.epicgames.com/documentation/en-us/unreal-engine/write-cplusplus-tests-in-unreal-engine
- Epic, Automation Test Framework: https://dev.epicgames.com/documentation/unreal-engine/automation-test-framework-in-unreal-engine
- Epic, Gauntlet Automation Framework: https://dev.epicgames.com/documentation/en-us/unreal-engine/gauntlet-automation-framework-in-unreal-engine
- Epic, Physics Settings (MaxPhysicsDeltaTime): https://dev.epicgames.com/documentation/unreal-engine/physics-settings-in-the-unreal-engine-project-settings
- Epic forum, async physics slows at low fps: https://forums.unrealengine.com/t/async-physics-slows-down-at-low-game-thread-fps/2731575
- Microsoft, Xbox Accessibility Guideline 107 (Input): https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/107
- Microsoft, Xbox Accessibility Guidelines index: https://devdocs.xbox.com/build/game-principles/accessibility/xbox-accessibility-guidelines
- Fiedler, G., "Fix Your Timestep!": https://gafferongames.com/post/fix_your_timestep/

Licences: all sources are read for reference only. Do not copy text from the paywalled ISO standard.
