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
5. Car id (for example `nb2001_lowered`).
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
2. `scripts/drive-robot.ps1 -Cars nb2001_lowered,meridian_z,mustang65 -LowFps` passes.
3. Compare `history.csv` with the last release. A peak slip change of more than
   2 deg or a yaw-rate change of more than 10 % needs a reason.
4. Play test with the three priority cars: NB 2001 (lowered), Corvette C8
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

## What I now know (onboarding, 2026-11-14)

**How my area works today.**
- `scripts/preflight.ps1:40` runs the 4 hero roads by default with 3 cars for 60 s at a fixed `-Utc 2026-11-12T17:00:00Z` (`:43`). Every run uses `-SimCruiseNoLive -SimCruiseNoNeural` (`:121`). The `us-*` keys come from `data/test-places.json` (`:86-89`, 18 places). They pass only `-SimCruiseStart=lat,lon` and no road name. `random` draws `-RandomCount 3` places with `Get-Random -SetSeed yyyyMMdd` (`:95`).
- `SimCruisePreflight.cpp:376-383` waits until the car is placed, the 3x3 tiles are ready, the shaders are done and the view check is done. After `ReadyTimeoutS = 600` s (`:44`) it drives anyway.
- The robot (`DriveTick`, `:499-640`) follows pure pursuit to the right-lane centre. It looks ahead 8 to 30 m (`:525`) and corners at 3 m/s² (`:52`). Its target speed comes from the OSM class (`SimCruiseTraffic.cpp:63-74`: motorway 29.1, unclassified 11.2 m/s), with a cap of `MaxSpeedMps = 30` (`:54`). It skips service ribbons (`:258`). With no road near, it brakes and waits (`:514-521`). At a line end it takes the best-aligned line within 3 m (`:338-350`).
- `Judge` (`SimCruisePreflight.cpp:76-99`; limits in `SimCruisePreflight.h:78-100`) fails on any of these: distance < 200 m, off the road > 1 s, start > 3 km from the asked point (`:964-971`), tile pop-in ≥ 1 within 150 m, car pop-in > 2, flash frames > 2, road step > 5 cm, gap < 1.5 m, body jolt > 8 m/s² at a tile border, traffic stuck > 20 s, rear slip > 15° or sliding backwards > 0.3 s, more than 2 m past the edge for > 0.5 s, frame p95 > 50 ms or > 5 hitches over 100 ms.
- `SimCruiseViewCheck.cpp:117-145` fails when world rays are < 70 %, no road is within 300 m, there is no building in view (only checked if ≥ 30 buildings are mapped, `SimCruiseViewCheck.h:61`), or the image edges are < 2.
- `check.ps1:139` runs the automation tests (`fast` skips the Slow tag). It fails on `Ensure`, `Fatal` or `LogSimCruise*: Error:` (`:182`). Then it runs `viewcheck.ps1` on ocean-drive, collins-ave and test-pad only (`viewcheck.ps1:48-50`). All three are in Miami.
- `drive-robot.ps1:23` runs 4 cars by default (nb2001, nb2001_lowered, mustang65, meridian_z) on the drive-test pad at the bundled start (`:52`). `-LowFps` adds a 15 fps cap (`:46`).
- The soak cycles the car every 240 s and travels every 360 s, through 9 fixed presets (`SimCruiseSoak.cpp:127-143`, `SimCruisePlaces.cpp:49-59`).
- The input tape replays with a fixed time step from the tape's recorded frame time (`SimCruiseInputTape.cpp:814-818`).
- `playtest.ps1:54-55` builds a play-test only if the last index row for the commit is PASS. It does not check which roads that run used.

**Does it work at a random US place?**
- These parts are general: the start snap (a non-service road within 900 m, `SimCruiseGameMode.cpp:42,563`, `SimCruiseWorldStreamer.cpp:168`), the robot (OSM ribbons only), and all Judge metrics. A failed or empty helper answer shows up only as `start-fell-back` (> 3 km) or as `robot-moved-only`.
- These parts are tied to one region: all 25 bundled tiles are Miami (`data/tiles/18178-18182_*`). Every streamed World or Drive test uses `BundledLon/Lat` (`SimCruiseWorldSubsystem.h:35-36`; `DriveIntegrationTests.cpp:200`; `SimCruiseWorldTests.cpp:208,583`). The only tests outside Miami are pure math: TileMath round trips (Anchorage, Honolulu, Death Valley, Bethel ME; `SimCruiseWorldTests.cpp:124-131`), CarClock time zones (`WorldDetailTests.cpp:82-89`) and palm regions (`:66-73`). No automated test builds or drives a tile outside Miami.
- The fixed time is a problem. At 17:00Z on 12 Nov the sun is at -6.8° in us-anchorage-ak, +6.4° in us-hilo-hi and +13.9° in us-seattle-wa, against +46° in Miami (my own solar formula, low precision). Anchorage drives in twilight. The sheets go dark, and `flat-image-edges` can fail for no real fault (estimate).
- us-welch-wv and us-casselton-nd (poor or empty map data): the building check is skipped below 30 mapped buildings, so missing buildings pass with no warning. No metric detects blank ground or a missing road. Only a person who reads the sheet can catch them (estimate).
- us-casselton-nd and us-delta-ut: unclassified roads run at 11.2 m/s, so 60 s covers about 670 m, about 1 tile border. us-rawlins-wy (I-80): the robot drives at 29.1 m/s, but the real limit is 80 mph (35.8 m/s). Seams and rebases at real highway speed get no test (estimate).
- us-bronx-ny and us-doraville-ga: the robot never turns at a junction. It always takes the best-aligned line. It ignores signals. Ramps on bridges work only through the bridge trace (`SimCruisePreflight.cpp:718`).

**Main gaps against "realistic everywhere".**
1. P1: Random places are optional. The default roads are hero only (`preflight.ps1:40`), and `playtest.ps1:54-55` accepts a hero-only PASS, even a 1-car 20 s PASS.
2. P1: No automated World or Drive test runs outside Miami. Seam, collision, edge and drive tests use only the bundled tiles (`SimCruiseWorldTests.cpp:36-40`, `DriveIntegrationTests.cpp:184-260`). Fix: bundle cached helper tiles for 3 to 4 test places (OSM ODbL with attribution; licence check needed).
3. P1: The fixed UTC (`preflight.ps1:43`) gives a twilight or low-sun drive west of the Central zone, and there is no night pass at all.
4. P2: The pre-flight cannot catch: a wrong regional look (Art Deco applies only in Miami Beach, `StyleForPlace`), blank ground or missing roads where data is poor, junction turns, dead ends, live weather and the AI detail layer (both off, `preflight.ps1:121`), the packaged build (it uses editor binaries), and a cold map cache (nothing records cache state).
5. P2: The 60 s at class speed is too short for random rural places to cross seams. The soak and the view check use only fixed presets and Miami (`SimCruiseSoak.cpp:136`, `viewcheck.ps1:48-50`).

**My first three actions.**
1. Run `scripts\preflight.ps1 -Roads random,us-welch-wv,us-anchorage-ak,us-hilo-hi -Cars nb2001_lowered,meridian_z,mustang65 -Seconds 120`. Done when: I have opened every `*-sheet.png` and `report.md`. Each defect has a ranked entry with repro steps, the frame and the run folder. Each false FAIL from the fixed UTC has its own entry.
2. Propose a change to `scripts\preflight.ps1` (needs the caller's approval): if the caller gives no `-Utc`, use local solar noon from the place's longitude (12:00 UTC − lon/15 h). Done when: us-anchorage-ak shows a daylight reference EV100 in `preflight.json`, and `-Roads dragon -ForceSpin` still prints `FORCED-SPIN TEST PASSED`.
3. Propose a change to the `scripts\playtest.ps1` gate: the index row must hold all 4 hero roads and ≥ 3 `us-*` keys, with all 3 priority cars. Done when: a fake index row with only ocean-drive is refused, and a full row is accepted.

**Docs that are wrong or old.**
- `docs/team/qa-playtester.md:122` says the drive robot defaults to the two NB cars. `scripts/drive-robot.ps1:23` runs 4 cars, including mustang65 and meridian_z.
- `docs/unreal-building.md:270` says the pre-flight drives the hero roads (12 runs) "at a fixed midday time". The script also has `us-*`, `random` and `pool` (`preflight.ps1:26-30`). 17:00Z is twilight in Anchorage.
- `docs/team/qa-playtester.md:56` says the smoke car "appears on Ocean Drive". Normal play now starts on a random pool road (`how-the-game-works.md:21`). I did not check the packaged self-test start.
- `docs/team/qa-playtester.md:78` says the soak travels to 10 places with "Surprise me". `SimCruiseSoak.cpp:136-141` cycles 9 fixed presets.

Proposed line for `docs/team/qa-playtester.md`: "Pre-flight default `-Utc 2026-11-12T17:00:00Z`: sun altitude Miami +46°, Bronx +31°, Seattle +14°, Hilo +6°, Anchorage -7° (low-precision solar formula; not run in game)."
