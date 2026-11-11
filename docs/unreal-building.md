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

If another Unreal Editor with Live Coding is running on the machine, Unreal Build Tool refuses to build ("Unable to build while Live Coding is active"); add `-NoHotReloadFromIDE` to the command line.

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

## Automation tests

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\simcruise\SimCruise.uproject' -ExecCmds="Automation RunTests SimCruise;Quit" -unattended -nopause -nullrhi -nosplash -log -testexit="Automation Test Queue Empty"
```

Results are in `Saved/Logs/SimCruise.log` (`Test Completed. Result={...}` lines). `SimCruise.World.*` builds all 24 bundled tiles and takes about two minutes.

## Unreal editor skill connection

The UE 5.8.4 installation includes Epic's `ModelContextProtocol` and `AllToolsets` plugins. Their project entries are present but disabled until the local editor connection is explicitly enabled. Once enabled, follow the per-user MCP setup in the installed `unreal-mcp` skill and verify it with a read-only editor call before using mutation tools.
