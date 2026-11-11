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

## Unreal editor skill connection

The UE 5.8.4 installation includes Epic's `ModelContextProtocol` and `AllToolsets` plugins. Their project entries are present but disabled until the local editor connection is explicitly enabled. Once enabled, follow the per-user MCP setup in the installed `unreal-mcp` skill and verify it with a read-only editor call before using mutation tools.

If an Unreal Editor with Live Coding is already open on another checkout of SimCruise (it holds one engine-wide mutex for `UnrealEditor.exe`), add `-NoHotReloadFromIDE` to the build command. That skips only Unreal Build Tool's Live Coding guard; it is safe when the open editor loads a different checkout's binaries.

Build the `SimCruise` game target the same way (replace `SimCruiseEditor` with `SimCruise`) when you need a packaged-style executable; uncooked `-game` runs below use the editor binaries.

## Drive it

`Config/DefaultEngine.ini` sets `SimCruiseGameMode` as the global game mode, so any map becomes drivable. Until the world streams land, the default map is the engine's OpenWorld template:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' 'D:\simcruise\SimCruise.uproject' -game -windowed -ResX=1600 -ResY=900
```

Controls follow [controls.md](controls.md) (Xbox triggers and left stick, Y camera, B manual, RB / X shift, View recover, L3 HUD; W/S/A/D, C, T, E/Q, R, F2, F6 lens on the keyboard). The car is a placeholder until the vehicle pawn is plugged in through `[/Script/SimCruise.SimCruiseGameMode] VehiclePawnClass`; see [unreal-architecture.md](unreal-architecture.md#driving-shell).

Console variables: `simcruise.Rumble.Strength` (0..1), `simcruise.Camera.HeadMotion` (0..1), `simcruise.Lens.ScreenHeightCm` and `simcruise.Lens.ViewDistanceCm` for the true-to-screen lens.

## Tests

Automation tests are named `SimCruise.<Area>.<Test>`. Run them headless:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\simcruise\SimCruise.uproject' -ExecCmds="Automation RunTests SimCruise;Quit" -unattended -nopause -nullrhi -nosplash -log -stdout -testexit="Automation Test Queue Empty"
```

Results are in `Saved/Logs/SimCruise.log` (`Test Completed. Result={Success}` per test).

The driving shell also has an end-to-end check that injects stick, key and trigger events through the real Enhanced Input stack in a headless game and prints `SIMCRUISE SHELL SELF-TEST PASSED` or `FAILED` with the reason:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\simcruise\SimCruise.uproject' -game -nullrhi -unattended -nopause -nosplash -SimCruiseShellSelfTest
```

Windowed captures for comparing against reality: add `-ExecCmds="Scalability 3" -SimCruiseShellCapture` to the windowed run above. A scripted drive (accelerate, corner, parking-brake slide, brake) saves six 1600 × 900 screenshots with the HUD to `Saved/Screenshots/WindowsEditor/` and logs speed, rack angle, body slip, chase swing and the four rumble levels at each, then quits.
