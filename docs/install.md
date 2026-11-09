# Installing SimCruise

Requirements: a Mac with Apple Silicon (M1 or later) running macOS 13 Ventura or newer, a few hundred megabytes of disk for the app plus a cache that stays under roughly 100 MB, and an internet connection for any place outside the bundled starter area. Intel Macs are not supported: the release executable is Apple Silicon only. An Xbox controller is optional (pair it in System Settings → Bluetooth).

1. Download `SimCruise-<version>.dmg` from this repository's Releases page.
2. Open the disk image and drag **SimCruise** onto the **Applications** shortcut beside it.
3. Launch it from Applications. The first launch shows a Gatekeeper warning; see below.

## First launch and Gatekeeper

The app is signed *ad hoc* by the build script and is not notarized by Apple, because notarization needs a paid developer account and this project deliberately has none. macOS therefore warns once that it "cannot verify" the developer. Nothing is wrong with the download; this is what every unsigned open-source Mac app shows. Allow it one of these ways:

- **macOS 13 Ventura and 14 Sonoma:** Control-click (right-click) `SimCruise.app` in Applications, choose **Open**, then click **Open** in the dialog. The choice is remembered.
- **macOS 15 Sequoia and later:** double-click the app once and dismiss the warning, open **System Settings → Privacy & Security**, scroll to *Security*, click **Open Anyway** next to SimCruise, authenticate, and open the app again.
- **Terminal:** `xattr -dr com.apple.quarantine /Applications/SimCruise.app` removes the download quarantine flag; the app then opens like any other.

A copy of these instructions ships inside the disk image as `README-first-launch.txt`. If you build from source yourself there is no quarantine flag and no prompt.

## Where it keeps things

Everything SimCruise writes lives in `~/Library/Application Support/Godot/app_userdata/SimCruise/`: `map-cache/` (fetched source data, trimmed to about 96 MiB), `driving-preferences/` (transmission, street-photo and ground-cover choices) and `logs/`. To uninstall, delete the app and that folder.
