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

## Unreal editor skill connection

The UE 5.8.4 installation includes Epic's `ModelContextProtocol` and `AllToolsets` plugins. Their project entries are present but disabled until the local editor connection is explicitly enabled. Once enabled, follow the per-user MCP setup in the installed `unreal-mcp` skill and verify it with a read-only editor call before using mutation tools.
