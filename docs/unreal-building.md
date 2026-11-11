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

Build the `SimCruise` game target the same way when you need `-game` runs or a package. If any Unreal Editor with Live Coding is running on the machine (for any project), UnrealBuildTool refuses with "Unable to build while Live Coding is active"; add `-NoHotReloadFromIDE` to build a different checkout (a worktree) without closing it.

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

The streamed world starts from the command line (`docs/unreal-architecture.md`, real-world streaming):

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'D:\simcruise\SimCruise.uproject' /Engine/Maps/Entry -game -windowed -ResX=1600 -ResY=900 -SimCruiseWorld
```

`-SimCruiseWorld` streams the bundled Miami Beach tiles (no network). `-SimCruiseStart=lat,lon` starts elsewhere and needs the atlas helper (`tools/atlas.exe`, or Bun with `helper/atlas.mjs`; see `docs/building.md`). `-SimCruisePreview=ocean,collins,aerial,top,seam,drop` adds a sun and sky when the map has none, writes those captures to `Saved/Screenshots/SimCruise/` and quits; add `-ExecCmds="scalability 4"` for the highest settings.

The world materials are defined in code. A cooked build needs them as assets:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\simcruise\SimCruise.uproject' -run=SimCruiseBuildWorldMaterials
```
While any Unreal Editor with Live Coding is open (on any project of this engine install), UnrealBuildTool refuses to build ("Unable to build while Live Coding is active"). Add `-NoHotReloadFromIDE` to build a second checkout or worktree anyway; do not use it to rebuild the module the open editor has loaded.

The `SimCruise` game target builds the same way (`Build.bat SimCruise Win64 Development ...`). Cooked games need the car master materials saved as assets: run the console command `SimCruise.SaveCarMaterials` once in the editor (it writes `Content/Vehicles/Materials`).

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

The project enables Epic's `ModelContextProtocol` and `AllToolsets` editor plugins, so Claude Code can drive a running Unreal Editor through Epic's `unreal-engine-skills-for-claude-code` plugin. It is in the `claude-plugins-official` marketplace; install it with `/plugin install unreal-engine-skills-for-claude-code@claude-plugins-official`. `.mcp.json` points Claude Code at `http://127.0.0.1:8000/mcp`. To have the editor start the MCP server on launch, add this to your own `Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini` (per user, not in git). Otherwise, run `ModelContextProtocol.StartServer` in the editor console:

```ini
[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]
bAutoStartServer=True
ServerPortNumber=8000
ServerUrlPath=/mcp
```

The server listens on localhost only, but any program running as the same user can reach it, and its tools can run Python inside the editor. Commit your work before a long MCP-driven session.
