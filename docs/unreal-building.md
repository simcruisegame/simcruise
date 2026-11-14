# Building SimCruise with Unreal Engine

The Unreal client is a native C++ project in `SimCruise.uproject`. The local engine install currently resolves to Unreal Engine 5.8.4, changelist 58902052 (`++UE5+Release-5.8`). The `.uproject` association is `5.8`; use the pinned 5.8.4 launcher build for this checkout.

## Windows prerequisites

- Install the Unreal Engine 5.8.4 version selected by the Epic Games Launcher.
- Install a compatible C++ toolchain. Epic's [UE 5.8 release notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes?lang=en-US) list Visual Studio 2022 17.14 or newer, MSVC 14.38 minimum (14.50 recommended), and Windows SDK 10.0.22621.0 minimum (10.0.26100.0 recommended). Visual Studio 2026 is the recommended IDE for general development; see Epic's [hardware and software specifications](https://dev.epicgames.com/documentation/unreal-engine/hardware-and-software-specifications-for-unreal-engine).
- Include the native game/C++ workload, MSVC x64/x86 tools, and the .NET Framework 4.8 SDK/target pack. On this machine, Visual Studio Build Tools 2022 17.14.44 is installed with MSVC 14.44.35207, Windows SDK 10.0.26100.0, and the .NET Framework 4.8 SDK/target pack.

The current machine has the required compiler and SDKs. The Unreal Editor target has built successfully with this toolchain.

## Build and launch

From a Visual Studio Developer PowerShell or a shell with the compiler environment configured:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' SimCruiseEditor Win64 Development -Project='D:\simcruise\SimCruise.uproject' -WaitMutex -FromMsBuild
```

After the editor target builds, open `D:\simcruise\SimCruise.uproject` in Unreal Editor 5.8.4.

## Several agents on one PC

Several Claude Code sessions can work on SimCruise at once on one PC. These rules keep them from breaking each other's builds and runs:

- **One checkout or worktree per session.** `D:\simcruise` is the main checkout: the editor, MCP sessions and play tests run there. Code work goes in the lane worktrees `D:\sc\lane1`, `D:\sc\lane2` and `D:\sc\lane3` (`git worktree add D:\sc\lane1 -b <branch> origin/main`). Each worktree has its own `Binaries/`, `Intermediate/` and `Saved/`, so a build in one does not touch another; the first build in a new worktree is a full build. Reuse a lane for the next task (`git switch -c <branch> origin/main`) instead of making a new worktree.
- **Builds queue.** `Build.bat ... -WaitMutex` (as in `scripts\check.ps1`) waits for another Unreal Build Tool run instead of failing.
- **One GPU editor at a time.** `scripts\editor.ps1`, the captures, the view check and play-test builds take a PC-wide lock (`Global\SimCruiseEditor` in `scripts\EditorLock.ps1`, a named mutex that Windows frees when its process ends). They use the GPU, and the editor serves MCP on port 8000. Open the editor with `powershell -ExecutionPolicy Bypass -File scripts\editor.ps1` (extra arguments go to the editor).
- **Test runs share slots.** `scripts\check.ps1` does not wait for that lock. Its test editor runs with `-nullrhi` and `-DisablePlugins=ModelContextProtocol`, so it uses neither the GPU nor port 8000. It also skips the editor's start-up call to Unreal Build Tool (`UE_SKIP_UBT_SDK_SETUP=1`), which waits while another checkout builds; that wait held one test start for 84 s. It takes one of up to 3 headless slots (`Global\SimCruiseHeadless0` to `2`, one mutex each). The number of slots is `-Slots`, else `SIMCRUISE_TEST_SLOTS`, else (free RAM in GB minus 8) divided by 6.5, and at least 2 when 20 GB or more is free. One full test editor peaks at about 6 GB; this laptop has 32 GB and 8 cores. Two full checks from two worktrees ran side by side and both passed in about 8 minutes, where one after the other they would take 16, plus the queue.
- **Everything a script starts ends.** `scripts\check.ps1` stops its test editor after `-TimeoutMinutes` (default 20) and fails. `scripts\editor.ps1 -TimeoutMinutes N` does the same for a game run (0, the default, means no limit, for an interactive editor); `scripts\drive-robot.ps1` passes 15. Both stop only the process tree they started.
- **No crash windows.** `scripts\editor.ps1` adds `-unattended` to every `-game` run. A crash then ends the game and the crash reporter quits by itself, instead of a dialog that holds the lock until someone clicks it.
- **Stale runs.** `scripts\watchdog.ps1` lists Unreal editors, SimCruise games and crash reporters older than `-MaxMinutes` (default 30) or whose starter is gone, with process id, age, folder and command line. It changes nothing unless you add `-Stop`, which stops only the listed processes. Look at the list first: an editor left open on purpose is old too.
- **Who may stop hung runs.** Aaron allowed it on 2026-11-13: threads may run `scripts\watchdog.ps1 -Stop` on this PC to stop hung SimCruise games and crash reporters. Run it without `-Stop` first and read the list. Stop only the hung or orphaned UnrealEditor, SimCruise and CrashReportClient processes it lists. Never schedule it to run by itself. During a release or pre-flight run, stop a process only when you know that process id is hung.
- **The view check takes the lock once.** `scripts\viewcheck.ps1` holds the GPU lock for all its cases. Taken once per case, it queued three times and waited about 30 minutes on a busy PC.
- **Where the time goes.** Every `scripts\check.ps1` run adds a line to `Saved\CheckTimes.csv`: lock wait, build, test seconds, the test editor's peak memory and the result.
- **Live Coding is off** (`Config/DefaultEditorPerProjectUserSettings.ini`), because an open editor with Live Coding blocks `Build.bat` for every checkout. Rebuild and restart the editor instead.

Build the `SimCruise` game target the same way when you need `-game` runs or a package. If any Unreal Editor with Live Coding is running on the machine (for any project), UnrealBuildTool refuses with "Unable to build while Live Coding is active"; add `-NoHotReloadFromIDE` to build a different checkout (a worktree) without closing it.

## A Windows package

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

It saves the generated materials, runs `BuildCookRun` for Win64 Development (`-Configuration Shipping` for a release), and copies in what the game reads from disk instead of its .pak: `data\`, `Content\Vehicles\CarPresentation.json`, `Content\Vehicle\Calibration\` and `tools\atlas.exe` when present. Run `Saved\Package\Windows\SimCruise.exe`. A packaged game starts on the bundled Miami Beach streets without `-SimCruiseWorld` (`-SimCruiseNoWorld` opens it empty); `-SimCruiseStart=lat,lon` still needs the atlas helper.

For a release: `-Configuration Shipping -Zip` writes `Saved\Package\SimCruise-<ProjectVersion>-windows-x64.zip` (the game at the top, with `LICENSE.txt`, `DATA-SOURCES.md`, `CONTROLS.md`, `licenses/` and a `CREDITS.txt` built from the car profiles; no debug symbols; only the car models a profile uses). `-NoBuild` redoes only the copying and the zip. A Shipping game keeps its logs, settings and captures in `%LOCALAPPDATA%\SimCruise\Saved`. For a full release (installer, checksums, smoke test) run `scripts\release.ps1` and follow [release/RELEASE-CHECKLIST.md](release/RELEASE-CHECKLIST.md).

### Play-test builds

`scripts\playtest.ps1` makes a play-test build of the latest `main` without touching any other checkout: it fetches `origin`, puts the worktree `D:\sc\playtest` on `origin/main` (`-Ref` builds something else), runs `scripts\package.ps1 -Zip` there under the PC-wide editor lock, starts the packaged game headless with `-SimCruiseShellSelfTest` as a smoke test, and copies the zip to `D:\SimCruise-playtests` (`-OutDir`) as `SimCruise-<version>-<date>-<commit>.zip` with a `-NOTES.txt` beside it: the smoke-test result and the commits since the previous play-test build. It keeps the newest five (`-Keep`) and publishes nothing. Exit code 0 only when the smoke test passed.

## Automation tests

Run them with `scripts\check.ps1`. There are two tiers:

- `-Tier fast` skips the slow tests and takes under 2 minutes. Use it while you work.
- `-Tier full`, the default, runs every test (about 8 minutes). Run it before a merge.

The slow tests are the ones tagged `[Slow]` in `Config\Tests\Tags.ini`, one line per test. Unreal reads that file itself, and the fast tier uses Unreal's tag filter (`Automation SetTagFilter NOT Slow`). A test that is not in the list runs in both tiers, so a new test is in the fast tier until someone lists it.

`scripts\check.ps1` skips the build when no file under `Source\` and no `.uproject` changed since the last good build of that checkout (`Saved\CheckBuild.stamp`). `-ForceBuild` builds anyway; `-NoBuild` never builds.

By hand, headless:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\simcruise\SimCruise.uproject' `
  -ExecCmds="Automation RunTests SimCruise.Vehicle;Quit" -unattended -nopause -nullrhi -nosplash -nosound -log -stdout `
  -testexit="Automation Test Queue Empty"
```

The exit code is 0 when every test passed; results are in `Saved/Logs/SimCruise.log` (`Test Completed. Result={...}`). Several filters can be joined with `+` (`SimCruise.Vehicle.CarTrack+SimCruise.Vehicle.LiftOff`).

### Vehicle tests

| Test | What it checks | Time |
|---|---|---|
| `SimCruise.Vehicle.Units.*` | The SI/Unreal boundary: lengths, forces, torques, inertia, angles, road-test units, Godot-to-Unreal axes, mounts and centre of mass against Godot's | instant |
| `SimCruise.Vehicle.Model.*` | Suspension preload, spring and anti-roll rates (CarSuspension.gd's formula), tyre force signs, relaxation, friction circle, locked wheels, engine curve, differential bias, the automatic's reverse inhibitor, the manual gearbox's refusals | instant |
| `SimCruise.Vehicle.Chaos.ForceIntegration` | A known SI force and torque through the physics-thread boundary integrate as F/m and τ/I, once per fixed step | 1 s |
| `SimCruise.Vehicle.Chaos.BodyProperties` | The Chaos body carries the car's mass, centre of mass, inertia and collision box, with inertia conditioning off | 1 s |
| `SimCruise.Vehicle.Chaos.BodyOwnership` | The vehicle keeps stepping its body when the physics state is recreated mid-drive (no stale particle), and the car's mass properties stay in force when the game thread recomputes them | 4 s |
| `SimCruise.Vehicle.Chaos.FixedStepConfig` | Physics ticks asynchronously at the vehicle's 1/360 s | instant |
| `SimCruise.Vehicle.CarTrack.<car>` | 0–60 mph, 70–0 mph and the 300 ft skidpad against the car file's targets and `docs/cars.md` bands | 6–10 s |
| `SimCruise.Vehicle.LiftOff.<car>` | Lift-off and trail-brake stability on a 40 m circle at 0.3, 0.5, 0.7 g; never reverse while rolling forward | 10 s |
| `SimCruise.Vehicle.StaticLoad.<car>` | Tyre loads carry the weight with the file's front share, level, at the static suspension length | 2 s |
| `SimCruise.Vehicle.Convergence.<car>` | CarTrack at 60 (Godot's scheme), 120, 240, 360 and 720 Hz; 360 Hz must be converged | 40 s |
| `SimCruise.Vehicle.Determinism` | Two identical launches end bit for bit identical | 5 s |
| `SimCruise.Vehicle.Interface` | The driving-shell interface with threaded physics: throttle, steering, brakes, manual gearbox, handbrake, reset | 15 s |

The whole `SimCruise.Vehicle` suite takes about 2.5 minutes on the i9-10885H. Set `SIMCRUISE_BENCH_TRACE=1` to log the launch (`LAUNCHTRACE`), the stop (`BRAKETRACE`) and the skidpad (`SKIDTRACE`) every quarter or half second, and `SIMCRUISE_CALIBRATION_DIR=<folder>` to load experimental calibration overlays from another folder instead of `Content/Vehicle/Calibration` (development builds only).

### World and atlas tests

`SimCruise.Atlas.*` and `SimCruise.World.*` (table in `docs/unreal-architecture.md`, real-world streaming). `SimCruise.World.*` builds all 24 bundled tiles and takes about two minutes.

## Drive the real place

A plain `-game` launch drives the streamed world (see "Drive it" below). The world stream's own preview captures start from the command line (`docs/unreal-architecture.md`, real-world streaming):

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'D:\simcruise\SimCruise.uproject' /Engine/Maps/Entry -game -windowed -ResX=1600 -ResY=900 -SimCruiseWorld
```

`-SimCruiseWorld` streams the bundled Miami Beach tiles (no network). `-SimCruiseStart=lat,lon` starts elsewhere and needs the atlas helper (`tools/atlas.exe`, or Bun with `helper/atlas.mjs`; see `docs/building.md`). `-SimCruisePreview=ocean,collins,aerial,top,seam,drop` adds a sun and sky when the map has none, writes those captures to `Saved/Screenshots/SimCruise/` and quits; add `-ExecCmds="scalability 4"` for the highest settings.

Every material is defined in code. The editor always builds them from the code; a cooked build loads saved copies instead. After changing a material, save them all again (exit code 0 when every one saved) and commit `Content/World/Materials`, `Content/Vehicles/Materials` and `Content/Vehicle/Materials`:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\simcruise\SimCruise.uproject' -run=SimCruiseBuildWorldMaterials
```
While any Unreal Editor with Live Coding is open (on any project of this engine install), UnrealBuildTool refuses to build ("Unable to build while Live Coding is active"). Add `-NoHotReloadFromIDE` to build a second checkout or worktree anyway; do not use it to rebuild the module the open editor has loaded.

The `SimCruise` game target builds the same way (`Build.bat SimCruise Win64 Development ...`). The commandlet saves the eight world layers, the three car masters, the dashboard clock, the mirror glass and the low-beam light function (`SimCruise.SaveCarMaterials` in the editor still saves only the car masters).

## Automation tests

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\simcruise\SimCruise.uproject' -ExecCmds="Automation RunTests SimCruise.Presentation;Quit" -unattended -nopause -nullrhi -nosplash -log -testexit="Automation Test Queue Empty"
```

Results are in `Saved/Logs/SimCruise.log`. Add `-SimCruiseRigDebug` to log every steering-wheel candidate the car rig rejects.

## Car turntable captures

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'D:\simcruise\SimCruise.uproject' '/Engine/Maps/Entry?game=/Script/SimCruise.SimCruiseTurntableGameMode' -game -windowed -ResX=1600 -ResY=900 -SimCruiseTurntable=nb1999,mustang65,meridian_z
```

`-SimCruiseTurntable=all` takes every car in `Content/Vehicles/CarPresentation.json`; `-SimCruiseTurntableOut=DIR` changes the output folder (default `Saved/Screenshots/Turntable`); `-SimCruiseTurntableStay` keeps the game open afterwards. The first run of a car builds its meshes (the NB's Nanite body takes about 30 s once, then comes from the DDC). Each car gives 13 PNGs and a section of `report.txt`. In a running game the console command `SimCruise.Turntable [ids|all] [dir]` does the same. See [unreal-architecture.md](unreal-architecture.md) for how the cars are built.
The `SimCruise` game target builds the same way (`Build.bat SimCruise Win64 Development …`). With the installed engine, Unreal Build Tool refuses to build while any editor has a Live Coding session open ("Unable to build while Live Coding is active"), even an editor on another checkout, because the Live Coding lock is named after the shared `UnrealEditor.exe`. A build of a separate checkout (a worktree) can pass `-NoHotReloadFromIDE` to skip that check; never use it on the checkout the open editor is running.

## Tests

Automation tests are named `SimCruise.<Area>.<Test>` and run headless:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\simcruise\SimCruise.uproject' -ExecCmds="Automation RunTests SimCruise.Environment;Quit" -unattended -nopause -nullrhi -nosplash -log -stdout -testexit="Automation Test Queue Empty"
```

Results are in the output and in `Saved/Logs/SimCruise.log` (`Test Completed. Result={Success}`).

## Lighting lab captures and frame timing

The lighting lab ([unreal-architecture.md](unreal-architecture.md#lighting-lab)) runs in a game window from the editor build. Captures at Miami Beach at noon and 17:30 EDT, then quit:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'D:\simcruise\SimCruise.uproject' /Engine/Maps/Entry -game -windowed -ResX=1600 -ResY=900 -nosplash -unattended `
  -SimCruiseLab -SimCruiseLat=25.7781 -SimCruiseLon=-80.1312 `
  -SimCruiseLabShots=noon@2025-04-24T17:19:00Z,late@2025-04-24T21:30:00Z -SimCruiseLabViews=street,props -SimCruiseLabCapturePresets=ultra `
  -SimCruiseLabOut=D:\captures -SimCruiseLabQuit '-ExecCmds="DisableAllScreenMessages"'
```

Frame timing per preset at 1080p (use `-fullscreen -ResX=1920 -ResY=1080`, vsync off, and nothing else using the GPU): replace the shot arguments with `-SimCruiseUtc=2025-04-24T17:19:00Z -SimCruiseLabPerf=low,medium,high,ultra -SimCruiseLabPerfSeconds=12` and add `r.VSync 0,t.MaxFPS 0` to `-ExecCmds`. Each window logs a `SimCruisePerf:` line and appends `Saved/Profiling/SimCruisePerf.csv`. For a pass-by-pass GPU breakdown run `r.ProfileGPU.ShowUI 0` and `ProfileGPU` (the lab accepts console commands after `#` in a shot, with `~` for spaces and `|` between commands); the tree is printed under `LogRHI`.

## Unreal editor skill connection

The project enables Epic's `ModelContextProtocol` and `AllToolsets` editor plugins, so Claude Code can drive a running Unreal Editor through Epic's `unreal-engine-skills-for-claude-code` plugin. It is in the `claude-plugins-official` marketplace; install it with `/plugin install unreal-engine-skills-for-claude-code@claude-plugins-official`. Its start hook needs Git Bash on `PATH`. `.mcp.json` also lists `blender` ([blender-mcp](https://github.com/ahujasid/blender-mcp), started with `uvx blender-mcp`) for the Blender car pipeline in `authoring/cars/`; it needs `uv` and the blender-mcp add-on running in Blender, and Claude Code asks before it uses either server. `.mcp.json` points Claude Code at `http://127.0.0.1:8000/mcp`. To have the editor start the MCP server on launch, add this to your own `Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini` (per user, not in git). Otherwise, run `ModelContextProtocol.StartServer` in the editor console:

```ini
[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]
bAutoStartServer=True
ServerPortNumber=8000
ServerUrlPath=/mcp
```

The server listens on localhost only, but any program running as the same user can reach it, and its tools can run Python inside the editor. Commit your work before a long MCP-driven session.
The UE 5.8.4 installation includes Epic's `ModelContextProtocol` and `AllToolsets` plugins. Their project entries are present but disabled until the local editor connection is explicitly enabled. Once enabled, follow the per-user MCP setup in the installed `unreal-mcp` skill and verify it with a read-only editor call before using mutation tools.

If an Unreal Editor with Live Coding is already open on another checkout of SimCruise (it holds one engine-wide mutex for `UnrealEditor.exe`), add `-NoHotReloadFromIDE` to the build command. That skips only Unreal Build Tool's Live Coding guard; it is safe when the open editor loads a different checkout's binaries.

Build the `SimCruise` game target the same way (replace `SimCruiseEditor` with `SimCruise`) when you need a packaged-style executable; uncooked `-game` runs below use the editor binaries.

## Drive it

Build the `SimCruiseEditor` target, then:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'D:\simcruise\SimCruise.uproject' -game -windowed -ResX=1600 -ResY=900
```

The game starts at the offline Miami Beach start: it builds the nine tiles around Ocean Drive from the bundled data (about 15 s, "Building Ocean Drive, Miami Beach · n of 9 tiles"), then puts you in the 2001 roadster in Ocean Drive's lane, in the driver's seat, in a morning sun: a drive starts at 10:00 local solar time at the place, today (owner decision, 11 November 2026). `-SimCruiseRealTime` follows your computer's clock instead (night included, with your own low beams). Leave out `-windowed -ResX=1600 -ResY=900` for full screen.

Controls follow [controls.md](controls.md): on an Xbox pad, right trigger to accelerate, left trigger to brake, left stick to steer, Y camera, B manual gearbox, RB / X shift, View recover, L3 HUD; on the keyboard W/S/A/D, C camera, T manual, E/Q shift, R recover, F2 HUD, F6 lens, L lamps.

Options:

| Argument | Effect |
|---|---|
| `-SimCruiseCar=mustang65` | Another car from `data/cars/` (`nb2001`, the default; `nb1999`, `mustang65`, `meridian_z`) |
| `-SimCruiseUtc=2026-11-11T16:00:00Z` | Fix the clock (11:00 in Miami Beach) instead of the morning start |
| `-SimCruiseRealTime` | Follow the computer's clock (the sun and moon where they are right now) |
| `-SimCruiseGraphics=ultra` | Graphics preset (`low`, `medium`, `high`, `ultra`; default chosen from the GPU: High on the RTX 2070 Super Max-Q) |
| `-SimCruiseStartRoad=<name>` | Start on another named road near the start |
| `-SimCruiseStart=lat,lon` | Start somewhere else on the nearest public road (needs the atlas helper, see [building.md](building.md)) |
| `-SimCruiseNoWorld` | Drive the loaded map instead (its player start), as before the world stream |

The defaults live in `Config/DefaultGame.ini` under `[/Script/SimCruise.SimCruiseGameMode]` (`VehiclePawnClass`, `StartCarId`, `StartRoad`); see [unreal-architecture.md](unreal-architecture.md#the-drive-integration).

### Drive captures

`-SimCruiseDriveCapture` photographs the drive and quits: the parked car from the driver's seat, from the chase camera and from straight above the road, then the car driving off (seat and chase, with the HUD). Highest settings, daylight:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'D:\simcruise\SimCruise.uproject' -game -windowed -ResX=1600 -ResY=900 -nosplash -unattended `
  -SimCruiseDriveCapture -SimCruiseUtc=2026-11-11T16:00:00Z -SimCruiseGraphics=ultra
```

Pictures go to `Saved/Screenshots/SimCruise/Drive/` (`-SimCruiseDriveCaptureOut=DIR` changes it), with one `SHOT` line per picture in the log; the whole run takes about a minute. Leave out `-SimCruiseUtc` to photograph the morning start, or add `-SimCruiseRealTime` for the real time of day.

Realism score: `-SimCruiseRealismPose=name,lat,lon,heading,hfov` (several joined with `+`) with `-SimCruiseDriveCapture` photographs the world from each pose instead of driving: the eye 1.65 m above the ground, the compass heading, the horizontal field of view, the player car hidden, as `realism-<name>.png`, then quits. `scripts\realism.ps1` runs it at the pose, time and size of each licensed photograph in `lab/realism/poses.json` and scores the captures with `lab/realism/score.py` (see [lab/realism/README.md](../lab/realism/README.md)).

Console variables: `simcruise.Rumble.Strength` (0..1), `simcruise.Camera.HeadMotion` (0..1), `simcruise.Lens.ScreenHeightCm` and `simcruise.Lens.ViewDistanceCm` for the true-to-screen lens. For look development: `simcruise.Lamps.BeamScale` multiplies the low beams (1 = the real 15 000 cd) and `simcruise.Lamps.Pattern 0` takes their cut-off pattern off; `simcruise.World.ParkedCars 0` leaves the parking stalls empty, `simcruise.Traffic.Cars N` sets the moving traffic (0: none), `simcruise.Mirrors 0` turns the live mirrors off and `simcruise.Mirrors.Interval` sets how often their capture runs (every third frame). They can be given at launch, for example `"-ExecCmds=simcruise.Lamps.BeamScale 6"` with `-SimCruiseDriveCapture`.

Driving tests: `-SimCruiseDriveTest` drives the car the way a person with a gamepad does, through the real input stack (stick dead zone, shaper, rack rates), and writes one CSV row per frame to `Saved/DriveTest/drive-test.csv` (`-SimCruiseDriveTestOut=FILE`): stick and pedals, rack, body velocity and yaw rate, and each tyre's load, slip angle, slip ratio, forces, grip use and ground normal. On the start road it drives a launch, a 50 km/h cruise, a lane change, a stick flick, a lift and a stop. `-SimCruiseDriveTestPad` instead puts a flat 4 km pad 60 m above the start and drives the limit manoeuvres there: turning launches and 20 and 30 km/h junction turns at full and 70 % trigger, a 50 km/h circle with a lift, a 50 km/h full-stick turn-in, braking in a turn from 70 km/h and an 80 km/h lane change. Each manoeuvre logs one `DRIVETEST` line with its peak rear-axle slip angle and yaw rate (`SPUN` past 15 degrees). Add `-ExecCmds="t.MaxFPS 15"` to test at a low frame rate, or `p.Chaos.StartVDRecording` to record the run for the Chaos Visual Debugger (`Saved/Profiling/ChaosVD-*.utrace`). `-SimCruiseDriveLog` records the same CSV while you drive yourself (to `Saved/DriveTest/drive-log-<time>.csv`, saved every 10 s and on exit; the `stick` column then holds the shaped rack angle in degrees).

Pad record and replay (`Gameplay/SimCruiseInputTape`, any build, Shipping included): `-SimCruiseRecordInput` (or `-SimCruiseRecordInput=FILE`) records the driver's controls from the moment the car is placed; F9 starts and stops a recording while you drive. Each rendered frame gives one row: time, the stick and triggers after their dead zones, the keys, full brake, handbrake, clutch, a racing wheel's rack angle and pedals, gear changes and the gearbox mode. The header gives the car and the start pose, with longitude and latitude, and the place of the drive (its title, street, longitude and latitude) with the world frame (origin tile and tile size). The tape goes to `Saved/DriveTest/input-<time>.csv` (or FILE) when the recording stops or the game ends. `-SimCruiseReplayInput=FILE` replays it:
1. It waits for the car and its tiles, as the drive robot does. When the car is more than 2 km from the tape's start, it travels to the tape's place (to the recorded start, on its nearest road) and waits for those tiles. A tape without a place (recorded before this) replays at the game's start, with a warning in the log. It logs `REPLAY START place=".." lat=.. lon=.. yaw=..` where it puts the car. `scripts\replay-compare.ps1` starts its games at the tape's start (`-SimCruiseStart`), so they do not load Ocean Drive first.
2. It drives the tape's car (`-SimCruiseReplayCar=<id>` for another one) from the tape's start at rest, or on the robot's test pad with `-SimCruiseReplayPad`.
3. It feeds the recorded controls through the same shaper and vehicle as a pad. The moving traffic is off, because traffic does not drive the same way twice (`-SimCruiseReplayTraffic` keeps it). A `-SimCruiseRecordInput` recording also turns traffic off (`-SimCruiseRecordTraffic` keeps it). An F9 recording keeps the traffic you drive in, writes `# traffic=on` in the tape and warns that its replay can take another path. Each recording also writes its own path as `<tape>-path.csv`, in the replay CSV's columns, so a replay can be compared with the drive it came from. While the replay settles before the tape starts, the handbrake holds the car (a car that rolls down a slope would start the tape moving). Each recording also writes `<tape>-steps.csv`: the controls the car's model took on every physics step (360 a second, after the shaper) and the gear it was in, written exactly. A replay that finds it feeds those steps to the car one for one on the physics thread, so the frame rate no longer matters: frames and physics steps do not line up at a free frame rate, and a frame-by-frame replay drifted apart after about 9 s. A 43 s keen robot drive on the Tail of the Dragon, recorded at a free frame rate, drove 356.8 m; its two step replays drove 358.5 m and 358.3 m and stayed within 1.7 m of the recorded path. `-SimCruiseReplayFrames` replays frame by frame instead (older tapes have no steps file): each replay frame then steps the game by the time its recorded frame took (`-SimCruiseReplayRealTime` replays at this PC's own pace), and a tape recorded and replayed with a fixed frame time (`-benchmark -fps=60`) stays within 1.9 m.
4. It writes `Saved/DriveTest/replay-<tape>.csv` (`-SimCruiseReplayOut=FILE`) with t, speed, yaw rate, body slip, the rear axle's slip angle, position, distance and gear.
5. It logs `REPLAY car=.. seconds=.. distance_m=.. lap_time_s=.. max_kph=.. mean_kph=.. max_yaw_dps=.. max_beta_deg=.. max_rear_slip_deg=..` and quits (`-SimCruiseKeepOpen` stays).

The lap time is the first return within 15 m of the start after more than 200 m (-1: none). A recording made with `-SimCruiseDriveTest -SimCruiseRecordInput` is a quick tape of the robot's street drive.

`scripts\replay-compare.ps1 -Tape FILE` runs one tape twice under `scripts\editor.ps1 -TimeoutMinutes` (default 15), offline on the bundled tiles with no moving traffic (`-Traffic` keeps it). Choose what differs:
- the build: `-RootA`, `-RootB`, two checkouts with built editor binaries;
- the car: `-CarA`, `-CarB`;
- the place: `-Pad` replays both on the test pad.

It prints the REPLAY measures of both runs side by side with B - A, the largest speed, yaw-rate and slip differences and when they happen, and keeps the logs and CSVs in `Saved\DriveTest\compare-<time>\`. The replay drives blind: in a game the frame times differ from run to run, so two replays of one build agree closely but not bit for bit. On the robot's 45 s street tape, two nb2001 runs gave 304.5 m and 303.1 m and the same 89.8 km/h top speed; one run took 3 minutes. The fast-tier test `SimCruise.Drive.InputReplay` replays a 6 s synthetic tape twice on the bench at fixed physics steps and requires every sample to be identical. It also reads the tape format back and checks sample and hold.

The drive robot runs the pad manoeuvres as a regression check: `powershell -ExecutionPolicy Bypass -File scripts\drive-robot.ps1` (`-Cars nb2001,nb2001_aaron` by default, `-LowFps` adds a 15 fps run) takes the editor lock, drives each car, prints each manoeuvre's peak rear-axle slip and yaw rate, appends them with the commit to `Saved/DriveTest/history.csv`, and fails if a manoeuvre a real car drives through in control (part-trigger junction turns and launch, the 50 km/h circle and turn-in, the 80 km/h lane change) slips the rear past 15 degrees. It needs the editor binaries built. To run it every night, register a Windows task once (for example `schtasks /Create /TN "SimCruise drive robot" /SC DAILY /ST 04:00 /TR "powershell -NoProfile -ExecutionPolicy Bypass -File D:\simcruise\scripts\drive-robot.ps1 -LowFps"`).

On the pad, the HUD shows an orange banner "DRIVE ROBOT · TEST PAD 60 m ABOVE <place> · NOT THE REAL ROAD" and does not show the place cards or the place name: the pad is not the real road. The tiles of the real place are 60 m below it.

### Hot reload

A game run from the editor binaries (any development build; not Shipping, not automation runs; `-SimCruiseNoHotReload` turns it off) watches the files it reads and puts a save on about 1.5 s later, with "Reloaded <file>" on screen (`Gameplay/SimCruiseHotReload`, `USimCruiseHotReloadSubsystem`). Nothing is rebuilt.

| File | What happens |
|---|---|
| `data/cars/<id>.json`, `Content/Vehicle/Calibration/<id>.json` (or the `SIMCRUISE_CALIBRATION_DIR` folder) | The car is loaded again; the car you drive is respawned where it stands, at rest, with your saved tuning (car physics is only read at spawn). Another car's file is kept for its next spawn. |
| `Content/Vehicles/CarPresentation.json` | The car is respawned in place with the new model. |
| `Saved/SimCruise/preferences.json` | The seat (`seat_<car>`), the lens (`lens_v3`), the screen size and eye distance go on live. The game writes this file itself, so the message shows only when what you see changes. Checks and captures do not read it. |
| `Saved/SimCruise/look.json` | Exposure and colour grade, live: an object of `SimCruise.Env.Look` names and numbers, for example `{ "ExposureCompensation": 0.5, "Saturation": 0.95, "OpenStops": 1.5 }`. It is read at the start too. It is a scratch file for look development and is not packaged: put the values you keep in `ASimCruiseEnvironment`'s defaults. |

A file with an error is not put on: the screen says "Reload failed: <file>: <reason>" and the car drives on as it was. Test: `SimCruise.Drive.HotReload` (fast tier) saves a calibration overlay next to a bench car and checks the new mass on the car, its movement model and its Chaos body.

### How to test

When to test:

- **After each change:** the fast tier, `powershell -ExecutionPolicy Bypass -File scripts\check.ps1 -Tier fast` (about 1.5 min, no game, no GPU).
- **Once per batch of related changes, before the merge to main:** the full check `scripts\check.ps1`, the pre-flight `scripts\preflight.ps1`, and `scripts\launch-review.ps1` for the places the batch touches (with `scripts\cockpit-review.ps1` when the cockpit changed). Look at the frames, write the verdicts in each `review.md`, and run `scripts\review-check.ps1` on the folders.
- **Once per version (the release candidate):** one full test of the candidate commit for all teams, `scripts\release-test.ps1`: the hero roads and random US places, all three cars, the cockpit test, the controller menus, at Ultra and the screen's own resolution. The rule is fast tests per change and one full release test per version; see [the team loop](team/dev-loop.md) and [the release test](team/release-test.md).
- **Controller menus:** `scripts\pad-review.ps1` (the automation test `SimCruise.UI.PadNavigation`, headless) walks every menu page with pad keys only and fails on any control the pad cannot reach. Its report is `report.md` in its folder.
- **Picture and driving tests run at the top preset and the screen's own resolution** (1920x1080 on this PC; `Get-NativeResolution` in `scripts\ReviewLib.ps1`). A run below the top preset, below the screen's resolution, or below the preset's screen percentage is NOT VALID FOR SIGN-OFF.
- **One GPU test at a time on the PC:** the scripts queue on the GPU test lock and print who holds it and how long they have waited.

Two kinds of test, two ways to run them:

- **Logic and data** (rules, physics benches, file formats, the review rules): no game, no picture. `scripts\check.ps1` runs the `SimCruise.*` automation tests in a headless editor (`-nullrhi`), in one of a few headless slots, next to other work.
- **Picture and driving** (what the player sees and feels): the real game, **off screen** (`-RenderOffScreen`: no window, no focus taken, the renderer, TSR, Lumen, auto exposure and motion blur still run, and the game's own 60 fps cap still paces it), **one at a time on the PC**. Every script that starts a game takes the GPU test lock `Global\SimCruiseGpuTest` and then the editor lock `Global\SimCruiseEditor` (`scripts\EditorLock.ps1`, `Enter-EditorLock -GpuTest`). A test waits in the queue (up to `-LockTimeoutMinutes`). Add `-ShowWindow` to see the game.

| Command | What it runs |
|---|---|
| `scripts\check.ps1` | build, automation tests, then the view check (off screen) |
| `scripts\launch-review.ps1 [-Road ..] [-RandomUS N -Seed S]` | the robot's drive with full evidence and a review sheet (off screen) |
| `scripts\cockpit-review.ps1 [-Cars ..]` | the cockpit steer test (off screen) |
| `scripts\preflight.ps1` | the 12 hero runs (off screen), UNREVIEWED until `scripts\review-check.ps1` |
| `scripts\viewcheck.ps1`, `scripts\drive-robot.ps1`, `scripts\replay-compare.ps1` | off screen; `-ShowWindow` to see |
| `scripts\editor.ps1 -game ...` | a game you watch (a window); it takes the GPU test lock too |

Unreal's own test tools, and what we use them for:

| Unreal tool | Used for now | Kept custom, and why |
|---|---|---|
| Automation Test Framework (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`, `Automation RunTests`) | every `SimCruise.*` test in `check.ps1` | - |
| `-nullrhi` | the headless automation tests | games need a picture: they run with `-RenderOffScreen` |
| `-RenderOffScreen` | every picture and driving test game | - |
| Functional tests and automation screenshot comparison (`FunctionalTesting`, `TakeAutomationScreenshot`, ground-truth frames, Session Frontend comparison) | not yet | the next step: approved reference frames for fixed poses (view check, cockpit poses per car, a fixed Dragon pose) with time, weather and traffic pinned. The cockpit test's own geometry and needle numbers stay: no Unreal tool measures them |
| Gauntlet (`RunUAT RunUnreal`) | not used | it needs a C# test node per test and starts its games outside our PC-wide locks; our launchers already give the lock, the time limit, the crash folder and the evidence. To look at again for runs on other PCs |

### Before you say it works

Numbers can pass while the picture is wrong. Before you say that a change works, launch the game, look at what it shows and write a verdict:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\launch-review.ps1 -Road ocean-drive -Car nb2001_aaron
```

(`-Road dragon`, `nebraska` or `big-sur`, or `-Start lat,lon -StartRoad name`; `-Seconds`, `-Graphics` (default `ultra`, the game's top preset), `-Name`, `-ExtraArgs`.) The robot drives for 60 s from the driver's seat. The game quits itself; a hang is stopped after 20 min. Everything goes into one folder, `Saved\Review\<yyyyMMdd-HHmm>-<commit>\<name>\`:

- `review\NNN-<t>s-<event>.png`: full-size frames at the start, every 5 s and at each event (a spin, a run-off, a hitch over 100 ms, a tile built, a pop-in, a jolt, a place card). Each has a `.json` sidecar: the true speed and engine speed next to what the needles show, the stick, the rack, the road wheels' drawn steer, the steering wheel's drawn angle, and the screen boxes of the close-ups.
- `review\crops\`: close-ups of the steering wheel, the instrument cluster, the mirrors and the windscreen, twice the size.
- `video.mp4` and `sheet.png` (10 frames a second and a contact sheet), `game.log`, `cmdline.txt`, `run.txt` (commit, exit code), `gpu.txt`, `preflight.json`, `review.json`, and `crash\` with the minidump and CrashContext when the game crashed. A crash or a hang still gives the folder, and the launch FAILs.
- `review.json` and `review.md` give the steering wheel as it is drawn: rim degrees per road-wheel degree, lock-to-lock turns (each next to the car data's), and the fastest rim motion. They also give the gauge errors (needle against the true value, while the speed is steady).
- `review.md`: the fault list (launch, place, glitches and flicker, pop-in, missing or floating objects, sky, road seams, gauges, steering wheel, car body and wheels, mirrors and windscreen, HUD, traffic, audio). The numbers fill the launch, gauge and audio verdicts.

Then open the frames and the crops, and for each TODO item write PASS, FAIL or N/A with the frame names, and your name after "Reviewed by". Check the sheet:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\review-check.ps1 Saved\Review\<folder>
```

It exits 0 only when every item has a verdict and none is FAIL. `scripts\preflight.ps1` makes the same evidence and a `review.md` for each of its runs, in `Saved\Review\<stamp>-<commit>-preflight\`. When its game checks pass, the index row says UNREVIEWED. `review-check.ps1` on that folder adds the PASS (or FAIL) row, and `scripts\playtest.ps1` builds only for a commit with a reviewed PASS. `scripts\viewcheck.ps1` and `scripts\drive-robot.ps1` also keep their evidence and a `review.md` under `Saved\Review\`.

#### Cockpit steer test

`launch-review.ps1` first runs the cockpit steer test (`-SimCruiseCockpitTest`; `-NoCockpit` leaves it out). For one car or all of them on their own:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\cockpit-review.ps1 -Cars nb2001_aaron,mustang65,meridian_z
```

(no `-Cars`: every car in `data\cars`). With the car at rest in the driver's seat (Ocean Drive, offline, Ultra, traffic off), the stick goes to the centre, half lock and full lock both ways, and each pose is held for 2 s. At each pose the game reads, in the car's own frame, the transforms of the steering wheel, the dial faces, the needles and the body. It also takes two frames: as the driver sees it, and with the steering wheel hidden. It FAILs when:

- the dial faces or the body move more than 0.05 cm or turn more than 0.1 deg;
- a needle turns about another axis than its own, or turns more than 0.5 deg while the speed and rpm it shows do not change (the gauges turn with the steering);
- a gauge hangs on the wheel, or more than 2 % of the wheel mesh's vertices lie outside its rim;
- the wheel turns about another axis than its column (0.5 deg), its hub (the mesh's centre) moves more than 1 mm, or its rim is tilted more than 2 deg on the column (fitted from the mesh);
- the rim angle at full lock differs from the car data's (road-wheel lock x steering ratio) by more than 5 %, or the wheel turns the other way from the road wheels;
- more than 1 % of the cluster's pixels change by more than 25 of 255 between the centre and full lock, with the wheel hidden.

It writes `cockpit\<pose>.png`, `cockpit\<pose>-nowheel.png`, `cockpit\diff-full-left.png` and `diff-full-right.png` (the cluster's change, bright where it changed), `cockpit\cockpit.json` and one line `COCKPIT PASS|FAIL car=.. lock=.. ratio=.. lock_to_lock=.. ... reasons=..`. The review sheet's items "Cockpit: gauges fixed while steering" and "Steering wheel turns about its column" take their verdicts from it. Look at the frames before you call a fix done.

#### Random places

```powershell
powershell -ExecutionPolicy Bypass -File scripts\launch-review.ps1 -RandomUS 8 -Seed 4242
```

The game builds any place in the US as you drive, so the review must look at ordinary places too. `-RandomUS N` makes N launches, one per place type (desert, mountains, plains and farms, forest, coast, suburb, small town, dense city), each a 45 s drive with the Miata at Ultra from the driver's seat. The places come from `data\test-places.json` when it is there, else from three areas per type in `scripts\RandomPlaces.ps1`, where a point is picked and the game puts the car on the nearest road. The seed (printed, and in the folder's name) repeats the same places. Each run's sheet adds: missing ground or holes, climate colours, floating or sunk objects, trees and plants, buildings, roads, slow frames, load (the car placed more than 60 s after the start fails), and a start that fell back to Miami Beach. `random-us.md` gives a table with the place type, the point, the road the car stood on, the load time, the game's result, the frame p95 and a link to each sheet.

Real photographs next to the game's frames, where the licence allows:

- Wikimedia Commons (used): the geosearch API (`list=geosearch`, file namespace, up to 10 km round the point, the nearest first) and `prop=imageinfo` for the licence and the author; only Creative Commons and public-domain files are kept, as `photo.jpg` with `photo.txt` (title, author, licence, link, distance). Terms: the Wikimedia API etiquette (one request at a time, a User-Agent that names the client, thumbnails rather than originals); each photo's own licence applies.
- Mapillary (CC BY-SA 4.0): it needs a free API token. None is set on this PC, so it is not used.
- USGS / NAIP aerial images (public domain): only as a fall-back, not used yet.
- Google Street View: never (its terms forbid it).

### View check

The view check looks at what the player sees and tells if the world is there. `-SimCruiseViewCheck` (or `-SimCruiseViewCheck=<seconds>`, default 6) waits until the car is placed, the tiles round it are built and the shaders are compiled, then waits the given seconds. Then it:

1. Shoots a grid of 48 x 27 line traces from the player camera through the view, to 2 km, and ignores the player car. It sorts each hit: road (tile asphalt and concrete), building, ground (tile and regional terrain), tree, car (traffic and parked), props, other (any other actor, for example the test pad) and empty (sky). It counts the rays that point more than 1 degree below the horizon.
2. Takes a screenshot to `Saved/Screenshots/ViewCheck/<place>-<yyyymmdd-hhmmss>.png` and measures the lower 60 % of it: the luminance standard deviation, the mean absolute luminance gradient (edges) and the number of colour hue bins.
3. Writes one log line and a JSON file next to the screenshot:

```
VIEWCHECK PASS|FAIL place="..." road=..% building=..% ground=..% tree=..% car=..% props=..% other=..% empty=..% world=..% road_near=..m building_cells=.. edges=.. lumsd=.. hues=.. shot=<png> reason=<...>
```

The view fails when: less than 70 % of the rays below the horizon hit the real world (road, building, ground, tree, car, props); the map has roads round the car but no ray hits a road within 300 m; the place is built up (30 or more mapped buildings on the 9 tiles round the car) but no ray hits a building; the edges value is below 2.0 (a flat image); or there is no screenshot. The thresholds are in `SimCruiseViewCheckRules` (`Source/SimCruise/Public/Gameplay/SimCruiseViewCheck.h`). `-SimCruiseViewCheckQuit` quits the game after the line.

`scripts\viewcheck.ps1` is the gate. It runs three offline cases on the bundled Miami Beach tiles at Ultra, 1280x720: `ocean-drive` (the bundled start, must pass), `collins-ave` (a second start on the bundled tiles, must pass) and `test-pad` (the drive robot's pad, must fail). A missing `VIEWCHECK` line fails the gate. `scripts\check.ps1` runs it after the automation tests (`-NoViewCheck` skips it). After each run, look at the screenshots: `Saved\ViewCheck\latest\` holds `<case>-<pass|fail>.png` with its `.json` and the game log, and `Saved\ViewCheck\<date-time>\` keeps the last 10 runs. A pass with a wrong picture (a wrong place, a black sky, a car in a wall) is a bug in the check: report it and tune the rules.

### Pre-flight check

The owner gets a play-test build only after it passes the pre-flight. `scripts\preflight.ps1` drives each hero road with each hero car (12 runs: Ocean Drive, US 129 Tail of the Dragon, Princeton Road gravel in Nebraska and CA-1 Big Sur, with the Miata `nb2001_aaron`, the Mustang `mustang65` and the Corvette `meridian_z`). It runs one game at a time under the editor lock, at Ultra, 1280x720, at a fixed midday time (`-Utc`), without live feeds. Every game quits itself. The roads other than Ocean Drive need the map helper and the network.

In each game, `-SimCruisePreflight` (`ASimCruisePreflight`) waits for the start's `VIEWCHECK` line, then drives the road for 60 s (`-SimCruisePreflightSeconds=N`) through the real input stack. It keeps to the right lane of the road ribbon under the car, near the road's speed limit, and slows for curves (3 m/s² sideways) and for cars ahead. It records the view at 10 frames a second. It measures and judges:

- **White sky:** of the frame pixels where a view ray hits nothing, the percent with a luminance of 245 or more. It fails above 25 %. The JSON also gives the environment's reference EV100.
- **Pop-in:** a tile built less than 150 m from the camera (fails at 1), or a car that appears or jumps more than 30 m inside 150 m, in front of the camera and in sight (fails above 2). Also frames where more than half of the picture (and more than 3 times the drive's median share) changes by more than 40 of 255 from the frame before, while the camera does not turn: a flash or an exposure jump (fails above 2).
- **Road seams:** the road surface under the car (the path the wheels ride), from the tile's own collision. A height step of more than 5 cm that the slope does not explain fails. So does a gap of less than 1.5 m with no road surface between road samples. A vertical jolt of the body above 8 m/s² within 8 m of a tile border also fails.
- **Stuck traffic:** a traffic car that stands for more than 20 s, and does not wait at a stop line or a stop.
- **Start and road:** the start fell back to another place (more than 3 km from the asked point), the car moved less than 200 m, or fewer than half of the frames were recorded.
- **Spin:** the rear axle's slip angle went past 15 deg above 5 m/s (the drive robot's SPUN line), or the car slid backwards faster than 2 m/s for more than 0.3 s (`spin-rear-slip-..deg-backwards-..s`).
- **Run-off:** the car was off the road surface for more than 1 s (`left-the-road-..s`), or its centre was more than 2 m past the paved edge of the road ribbon for more than 0.5 s (`ran-off-..m-past-the-edge-for-..s`).
- **Slow frames:** after 5 s of driving, and without the frames that the robot's own screenshots slow down, the 95th percentile frame time above 50 ms (`slow-frames-p95-..ms`) or more than 5 frames over 100 ms (`hitches-..-over-100ms`). These rules apply when at least 100 frames are timed.

Each FAIL reason is in the `PREFLIGHT` line, in `preflight.json`, in the report table (rear slip and backwards time, distance past the edge, frame p95 and hitches) and in the last column (`reasons`) of the index row. The negative test `scripts\preflight.ps1 -Roads dragon -Cars nb2001_aaron -ForceSpin` makes the robot pull full stick with the handbrake on for 2 s at speed (`-SimCruisePreflightForceSpin`). Every run must FAIL with a spin: the script then prints `FORCED-SPIN TEST PASSED`, exits 0 and writes no index row.

The game writes `PREFLIGHT PASS|FAIL road=.. car=.. ... reasons=..` and `preflight.json`. The script encodes the frames to `<road>-<car>.mp4` with ffmpeg (free: `winget install Gyan.FFmpeg`) and makes a 4 x 3 contact sheet `<road>-<car>-sheet.png`. It writes `report.md` with a table and links, all in `Saved\Review\<date-time>-<commit>-preflight\`, and adds a row to the PC-wide index `%LOCALAPPDATA%\SimCruise\preflight-index.csv`. Look at every contact sheet, and watch the videos of failed runs. Put real game bugs in `docs/release/bugs.md` with the run, the frame and a screenshot.

`scripts\playtest.ps1` refuses to make a play-test zip unless the index has a passed pre-flight for the same commit. `-SkipPreflight` builds anyway and prints a warning.

Frame time on the drive: `-SimCruiseDrivePerf=medium,high,ultra` (with `-SimCruiseDriveCapture`, ideally `-fullscreen -ResX=1920 -ResY=1080`) waits for the car, then for each preset samples 10 s parked in the driver's seat and 10 s in the chase view, and logs one `SimCruisePerf:` line each (also appended to `Saved/Profiling/SimCruisePerf.csv`). `-SimCruiseDrivePerfProfile` adds a `ProfileGPU` pass breakdown after each window.

Signs cost (2026-11-12, this laptop, Ultra 1920x1080, Ocean Drive start, 159 signs on the 9 tiles): with signs 36.2-37.7 fps in the driver's seat and 45.2-48.0 in the chase view over four runs; with `-DPCVars=simcruise.World.Signs=0` 37.6-37.9 and 43.0-48.1 over two. GPU time is the same (24.8-25.7 ms); the difference is under 1 fps, inside the run-to-run spread. Set cvars that tiles read at install with `-DPCVars=`, not `-ExecCmds` (which runs after the first tiles are in).

Night captures: the same command with `-SimCruiseUtc=2026-11-12T01:30:00Z` (20:30 in Miami Beach).

## Tests

Automation tests are named `SimCruise.<Area>.<Test>`. `scripts\check.ps1` builds and runs them all and exits 0 or 1 (see CONTRIBUTING.md); by hand, headless:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\simcruise\SimCruise.uproject' -ExecCmds="Automation RunTests SimCruise;Quit" -unattended -nopause -nullrhi -nosplash -log -stdout -testexit="Automation Test Queue Empty"
```

Results are in `Saved/Logs/SimCruise.log` (`Test Completed. Result={Success}` per test).

The driving shell also has an end-to-end check that injects stick, key and trigger events through the real Enhanced Input stack in a headless game and prints `SIMCRUISE SHELL SELF-TEST PASSED` or `FAILED` with the reason, then exits with 0 or 1:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\simcruise\SimCruise.uproject' -game -nullrhi -unattended -nopause -nosplash -SimCruiseShellSelfTest
```

Windowed captures for comparing against reality: add `-ExecCmds="Scalability 3" -SimCruiseShellCapture` to the windowed run above. A scripted drive (accelerate, corner, parking-brake slide and its catch, brake) saves six 1600 × 900 screenshots with the HUD to `Saved/Screenshots/WindowsEditor/` and logs speed, rack angle and its range each way, body slip, front-axle slip, chase swing and the four rumble levels at each, plus a 10 Hz trace of the slide, then quits.
