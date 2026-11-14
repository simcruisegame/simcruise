# Installing OSDS

OSDS 2.0 and later is the Unreal Engine 5.8 client, for Windows. Downloads are on the [Releases page](https://github.com/simcruisegame/simcruise/releases):

| File | Type |
|---|---|
| `OSDS-<version>-windows-x64-setup.exe` | Installer: Start menu entry and uninstaller |
| `OSDS-<version>-windows-x64.zip` | Portable folder: unzip and run `SimCruise.exe` |
| `OSDS-<version>-SHA256SUMS.txt` | SHA-256 checksums of the two files above |

OSDS was named SimCruise until November 2026. The 2.0.x releases still use the old name: their files start with `SimCruise-` and the Start menu entry is **SimCruise**. In all versions the program file is `SimCruise.exe`.

There is no macOS build of the Unreal client yet (it needs a Mac to build on). The Godot 1.x releases for macOS and Windows stay on the Releases page; their source is at the git tag `godot-final`.

## System requirements

| | Minimum | Recommended |
|---|---|---|
| System | Windows 10 or 11, 64-bit | Windows 11, 64-bit |
| Processor | 4 cores, 2017 or newer | 8 cores (tested: Intel Core i9-10885H) |
| Memory | 8 GB | 16 GB |
| Graphics | DirectX 12 GPU with 6 GB of video memory (NVIDIA GTX 1660, AMD RX 5600), Low or Medium preset | NVIDIA RTX 2070 class with 8 GB (tested: RTX 2070 Super with Max-Q Design, a laptop GPU, High preset) |
| Disk | 3 GB for the game, plus up to about 1 GB of map cache | SSD |
| Network | Not needed for the bundled Ocean Drive, Miami Beach | Broadband for the rest of the US (open data is fetched as you drive) |
| Controls | Keyboard | Xbox controller, or a USB wheel with a Windows GameInput driver |

The game picks a graphics preset for your GPU at the start (`Low`, `Medium`, `High` or `Ultra`) and caps the frame rate at 60 fps. The measured frame rates on the tested laptop are in [the release report](release/v2.0.0-report.md).

## Install with the installer

1. Download `OSDS-<version>-windows-x64-setup.exe`.
2. Optional: check the download. In PowerShell, `Get-FileHash .\OSDS-<version>-windows-x64-setup.exe` must print the value in `OSDS-<version>-SHA256SUMS.txt`.
3. Run it. Windows SmartScreen may say "Windows protected your PC", because this free open-source build is not code-signed (signing needs a paid certificate). Click **More info**, then **Run anyway**.
4. Choose **Install for me only** (no administrator rights; the game goes to `%LOCALAPPDATA%\Programs\OSDS`; an update goes into the folder of the version you have) or **Install for all users** (Program Files; needs administrator rights).
5. Follow the installer. It adds OSDS to the Start menu, and to the desktop if you tick it.

To uninstall, use **Settings > Apps > Installed apps > OSDS > Uninstall**, or **Uninstall OSDS** in the Start menu.

## Run the portable zip

Unzip `OSDS-<version>-windows-x64.zip` to any folder and run `SimCruise.exe`. Keep the folder together: `SimCruise\tools\atlas.exe` (the map helper) and `SimCruise\data\` must stay beside the game. To remove it, delete the folder.

## Where it keeps things

OSDS writes its settings, map cache and captures to `%LOCALAPPDATA%\SimCruise\Saved` (`SimCruise\preferences.json` holds your car, gearbox, sound, HUD, lens, head motion and rumble choices). The uninstaller does not remove this folder; delete it to remove everything.

## Problems

- **The game starts on Ocean Drive instead of a random road:** the random start needs the internet. Without it, the game always starts on the bundled Ocean Drive, Miami Beach.
- **Low frame rate:** start the game with a lower preset, for example `SimCruise.exe -SimCruiseGraphics=medium` (a shortcut's target can carry it).
- **Report a bug:** open an issue on GitHub with your Windows version, GPU and what you did.

## The Godot 1.x client (old)

The 1.x releases (Godot engine, macOS and Windows) are kept for reference. Their install notes are in this file at the git tag `godot-final`.
