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

## Unreal editor skill connection

The UE 5.8.4 installation includes Epic's `ModelContextProtocol` and `AllToolsets` plugins. Their project entries are present but disabled until the local editor connection is explicitly enabled. Once enabled, follow the per-user MCP setup in the installed `unreal-mcp` skill and verify it with a read-only editor call before using mutation tools.
