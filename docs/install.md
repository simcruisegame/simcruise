# Installing SimCruise

Downloads are on the [Releases page](https://github.com/simcruisegame/simcruise/releases). Every release has the same version number on both systems:

| System | File | Type |
|---|---|---|
| macOS (Apple Silicon) | `SimCruise-<version>.dmg` | Disk image: drag to Applications |
| Windows 10/11 (64-bit) | `SimCruise-<version>-windows-x64-setup.exe` | Installer with Start menu entry and uninstaller |
| Windows 10/11 (64-bit) | `SimCruise-<version>-windows-x64.zip` | Portable folder: unzip and run `SimCruise.exe` |

Each release lists SHA-256 checksums for its files in its notes and in `SHA256SUMS.txt`. To check a download, compare the list with `shasum -a 256 SimCruise-<version>.dmg` in Terminal on a Mac, or `certutil -hashfile SimCruise-<version>-windows-x64-setup.exe SHA256` in a Windows command prompt. The release files are built from the tagged source by the project's GitHub Actions workflow, not on anyone's own computer.

## macOS

Requirements: a Mac with Apple Silicon (M1 or later) running macOS 13 Ventura or newer, a few hundred megabytes of disk for the app plus a cache that stays under roughly 100 MB, and an internet connection for the random start and any place outside the bundled offline neighbourhood (without one the game starts on Ocean Drive, Miami Beach). Intel Macs are not supported: the release executable is Apple Silicon only. An Xbox controller is optional (pair it in System Settings → Bluetooth).

1. Download `SimCruise-<version>.dmg` from this repository's Releases page.
2. Open the disk image and drag **SimCruise** onto the **Applications** shortcut beside it.
3. Launch it from Applications. The first launch shows a Gatekeeper warning; see below.

### First launch and Gatekeeper

The app is signed *ad hoc* by the build script and is not notarized by Apple, because notarization needs a paid developer account and this project deliberately has none. macOS therefore warns once that it "cannot verify" the developer. Nothing is wrong with the download; this is what every unsigned open-source Mac app shows. Allow it one of these ways:

- **macOS 13 Ventura and 14 Sonoma:** Control-click (right-click) `SimCruise.app` in Applications, choose **Open**, then click **Open** in the dialog. The choice is remembered.
- **macOS 15 Sequoia and later:** double-click the app once and dismiss the warning, open **System Settings → Privacy & Security**, scroll to *Security*, click **Open Anyway** next to SimCruise, authenticate, and open the app again.
- **Terminal:** `xattr -dr com.apple.quarantine /Applications/SimCruise.app` removes the download quarantine flag; the app then opens like any other.

A copy of these instructions ships inside the disk image as `README-first-launch.txt`. If you build from source yourself there is no quarantine flag and no prompt.

### Where it keeps things

Everything SimCruise writes lives in `~/Library/Application Support/Godot/app_userdata/SimCruise/`: `map-cache/` (fetched source data, trimmed to about 96 MiB), `driving-preferences/` (transmission, street-photo and ground-cover choices) and `logs/`. To uninstall, delete the app and that folder.

## Windows

Requirements: 64-bit Windows 10 or 11, a graphics card with Vulkan or Direct3D 12 drivers (any GPU from about 2016 on), a few hundred megabytes of disk plus the map cache, and an internet connection. An Xbox controller is optional.

1. Download `SimCruise-<version>-windows-x64-setup.exe` from the Releases page.
2. Run it. Windows SmartScreen may say "Windows protected your PC", because this free open-source build is not code-signed (signing needs a paid certificate). Click **More info**, then **Run anyway**.
3. Follow the installer. It adds SimCruise to the Start menu, optionally to the desktop, and to **Settings → Apps** for uninstalling.

Prefer no installer? Unzip `SimCruise-<version>-windows-x64.zip` anywhere and run `SimCruise.exe`; keep `atlas.exe` beside it.

SimCruise keeps its map cache, preferences and logs in `%APPDATA%\Godot\app_userdata\SimCruise`. The uninstaller removes them only if you tick **Map cache and settings**.

The Windows build is new in 1.1. If something goes wrong, please open an issue with your Windows version, graphics card and the newest file in `%APPDATA%\Godot\app_userdata\SimCruise\logs`.
