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
