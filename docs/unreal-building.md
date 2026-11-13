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
- **One editor at a time.** `scripts\check.ps1` and `scripts\editor.ps1` take a PC-wide lock (`scripts\EditorLock.ps1`, a named mutex that Windows frees when its process ends) before they start the editor, so test runs, play tests and MCP sessions take turns on the GPU and on MCP port 8000. Open the editor with `powershell -ExecutionPolicy Bypass -File scripts\editor.ps1` (extra arguments go to the editor).
- **Test runs always end.** `scripts\check.ps1` stops the editor and everything it started after `-TimeoutMinutes` (default 20) and fails.
- **Live Coding is off** (`Config/DefaultEditorPerProjectUserSettings.ini`), because an open editor with Live Coding blocks `Build.bat` for every checkout. Rebuild and restart the editor instead.

Build the `SimCruise` game target the same way when you need `-game` runs or a package. If any Unreal Editor with Live Coding is running on the machine (for any project), UnrealBuildTool refuses with "Unable to build while Live Coding is active"; add `-NoHotReloadFromIDE` to build a different checkout (a worktree) without closing it.

## A Windows package

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

It saves the generated materials, runs `BuildCookRun` for Win64 Development (`-Configuration Shipping` for a release), and copies in what the game reads from disk instead of its .pak: `data\`, `Content\Vehicles\CarPresentation.json`, `Content\Vehicle\Calibration\` and `tools\atlas.exe` when present. Run `Saved\Package\Windows\SimCruise.exe`. A packaged game starts on the bundled Miami Beach streets without `-SimCruiseWorld` (`-SimCruiseNoWorld` opens it empty); `-SimCruiseStart=lat,lon` still needs the atlas helper.

For a release: `-Configuration Shipping -Zip` writes `Saved\Package\SimCruise-<ProjectVersion>-windows-x64.zip` (the game at the top, with `LICENSE.txt`, `DATA-SOURCES.md`, `CONTROLS.md`, `licenses/` and a `CREDITS.txt` built from the car profiles; no debug symbols; only the car models a profile uses). `-NoBuild` redoes only the copying and the zip. A Shipping game keeps its logs, settings and captures in `%LOCALAPPDATA%\SimCruise\Saved`. Publish with `gh release create v<version> <zip> --prerelease` while the version is a preview.

### Play-test builds

`scripts\playtest.ps1` makes a play-test build of the latest `main` without touching any other checkout: it fetches `origin`, puts the worktree `D:\sc\playtest` on `origin/main` (`-Ref` builds something else), runs `scripts\package.ps1 -Zip` there under the PC-wide editor lock, starts the packaged game headless with `-SimCruiseShellSelfTest` as a smoke test, and copies the zip to `D:\SimCruise-playtests` (`-OutDir`) as `SimCruise-<version>-<date>-<commit>.zip` with a `-NOTES.txt` beside it: the smoke-test result and the commits since the previous play-test build. It keeps the newest five (`-Keep`) and publishes nothing. Exit code 0 only when the smoke test passed.

## Automation tests

Tests are Unreal automation tests named `SimCruise.<Area>.<Test>`. Run them headless:

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

The drive robot runs the pad manoeuvres as a regression check: `powershell -ExecutionPolicy Bypass -File scripts\drive-robot.ps1` (`-Cars nb2001,nb2001_aaron` by default, `-LowFps` adds a 15 fps run) takes the editor lock, drives each car, prints each manoeuvre's peak rear-axle slip and yaw rate, appends them with the commit to `Saved/DriveTest/history.csv`, and fails if a manoeuvre a real car drives through in control (part-trigger junction turns and launch, the 50 km/h circle and turn-in, the 80 km/h lane change) slips the rear past 15 degrees. It needs the editor binaries built. To run it every night, register a Windows task once (for example `schtasks /Create /TN "SimCruise drive robot" /SC DAILY /ST 04:00 /TR "powershell -NoProfile -ExecutionPolicy Bypass -File D:\simcruise\scripts\drive-robot.ps1 -LowFps"`).

On the pad, the HUD shows an orange banner "DRIVE ROBOT · TEST PAD 60 m ABOVE <place> · NOT THE REAL ROAD" and does not show the place cards or the place name: the pad is not the real road. The tiles of the real place are 60 m below it.

### View check

The view check looks at what the player sees and tells if the world is there. `-SimCruiseViewCheck` (or `-SimCruiseViewCheck=<seconds>`, default 6) waits until the car is placed, the tiles round it are built and the shaders are compiled, then waits the given seconds. Then it:

1. Shoots a grid of 48 x 27 line traces from the player camera through the view, to 2 km, and ignores the player car. It sorts each hit: road (tile asphalt and concrete), building, ground (tile and regional terrain), tree, car (traffic and parked), props, other (any other actor, for example the test pad) and empty (sky). It counts the rays that point more than 1 degree below the horizon.
2. Takes a screenshot to `Saved/Screenshots/ViewCheck/<place>-<yyyymmdd-hhmmss>.png` and measures the lower 60 % of it: the luminance standard deviation, the mean absolute luminance gradient (edges) and the number of colour hue bins.
3. Writes one log line and a JSON file next to the screenshot:

```
VIEWCHECK PASS|FAIL place="..." road=..% building=..% ground=..% tree=..% car=..% props=..% other=..% empty=..% world=..% road_near=..m building_cells=.. edges=.. lumsd=.. hues=.. shot=<png> reason=<...>
```

The view fails when: less than 70 % of the rays below the horizon hit the real world (road, building, ground, tree, car, props); the map has roads round the car but no ray hits a road within 300 m; the map has buildings round the car but no ray hits one; the edges value is below 2.0 (a flat image); or there is no screenshot. The thresholds are in `SimCruiseViewCheckRules` (`Source/SimCruise/Public/Gameplay/SimCruiseViewCheck.h`). `-SimCruiseViewCheckQuit` quits the game after the line.

`scripts\viewcheck.ps1` is the gate. It runs three offline cases on the bundled Miami Beach tiles at Ultra, 1280x720: `ocean-drive` (the bundled start, must pass), `collins-ave` (a second start on the bundled tiles, must pass) and `test-pad` (the drive robot's pad, must fail). A missing `VIEWCHECK` line fails the gate. `scripts\check.ps1` runs it after the automation tests (`-NoViewCheck` skips it). After each run, look at the screenshots: `Saved\ViewCheck\latest\` holds `<case>-<pass|fail>.png` with its `.json` and the game log, and `Saved\ViewCheck\<date-time>\` keeps the last 10 runs. A pass with a wrong picture (a wrong place, a black sky, a car in a wall) is a bug in the check: report it and tune the rules.

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
