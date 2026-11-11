# Building SimCruise from source

## Quick start: play the latest code on a Mac

On an Apple Silicon Mac with nothing installed yet, paste these one at a time into Terminal:

```sh
xcode-select --install                       # Apple's command line tools (git); skip if already installed
curl -fsSL https://bun.sh/install | bash     # Bun, which compiles the map helper
curl -fL -o /tmp/godot.zip https://github.com/godotengine/godot-builds/releases/download/4.7.2-stable/Godot_v4.7.2-stable_macos.universal.zip && unzip -q -o /tmp/godot.zip -d /Applications
git clone https://github.com/simcruisegame/simcruise.git ~/simcruise
sh ~/simcruise/scripts/play.sh
```

After that, `sh ~/simcruise/scripts/play.sh` updates to the newest code and starts the game. Places you visited before keep the roads they were built with until you delete `~/Library/Application Support/Godot/app_userdata/SimCruise/map-cache`.

## Building the app and disk image on a Mac

The released Mac app is built on Windows instead ([below](#building-the-mac-app-on-windows)); this route is for working on a Mac and for a disk image. You need Godot 4.7.2 stable for macOS with its matching export templates ([release page](https://github.com/godotengine/godot-builds/releases/tag/4.7.2-stable)), native Apple Silicon [Bun](https://bun.com/docs/installation) (tested with 1.4.3) and Apple's Command Line Tools (`xcode-select --install`) for `codesign` and `lipo`. From a clone:

```sh
sh scripts/build-macos.sh editor   # installs locked JS dependencies, compiles tools/atlas, imports the Godot project
sh scripts/build-macos.sh app      # exports, thins to arm64, adds the helper and credits, signs ad hoc → build/SimCruise.app
sh scripts/build-macos.sh dmg 1.1.0   # the same, then packages build/SimCruise-1.1.0.dmg
sh scripts/make-dmg.sh [version] [app] [out-dir]   # package an existing build/SimCruise.app on its own
```

After `editor`, open `game/project.godot` in Godot and press F5 (the editor build runs the helper from `tools/atlas`, which is a gitignored build product). Set `GODOT_BIN` and `BUN_BIN` if the executables are not on your PATH; the script also looks in `/Applications/Godot.app`. The export uses Godot's standard macOS preset and the official universal template, then keeps only the arm64 slice. `make-dmg.sh` takes the version from its first argument or `SIMCRUISE_VERSION`, else from `application/short_version` in `game/export_presets.cfg` or `config/version` in `game/project.godot` when one is set, else from the top entry of `CHANGELOG.md` with a warning; it uses only `hdiutil`, `ditto` and `codesign`.

Headless checks run without a window and are how changes are measured; the four calibration checks and the safety rules for running Godot from scripts are in [CONTRIBUTING.md](../CONTRIBUTING.md). The original, more detailed build and diagnostics notes are kept in [docs/architecture-notes.md](architecture-notes.md).

## Building for Windows

`sh scripts/build-windows.sh [version]` builds the Windows release on Linux, macOS or Windows (Git Bash). It needs Godot 4.7.2 with its export templates, Bun and NSIS 3 (`apt install nsis`, `brew install makensis`, or on Windows `winget install NSIS.NSIS` or the installer from nsis.sourceforge.io; the script finds `makensis` on PATH, in `MAKENSIS`, or in NSIS's default folder under Program Files). Bun cross-compiles the map helper to `atlas.exe`, Godot exports `SimCruise.exe` with the game data embedded (the "Windows Desktop" preset in `game/export_presets.cfg`), and NSIS wraps both with the licences into `build/SimCruise-<version>-windows-x64-setup.exe` (`packaging/windows/simcruise.nsi`). A portable `.zip` (written with `zip`, Python 3, or Windows' own `tar.exe`, whichever exists) and `build/SHA256SUMS-windows.txt` are written beside it. The version defaults to `config/version` in `game/project.godot`.

Godot refuses a release export unless both the release and the debug template for the platform are installed. Editor → Manage Export Templates installs them, or from a shell:

```sh
GODOT_BIN=$(sh scripts/install-godot.sh ~/godot-4.7.2 windows | tail -n 1)   # also: macos, linux, all
```

`scripts/install-godot.sh` downloads the editor for the current system and the 1.3 GB template archive from the official [godotengine/godot 4.7.2-stable release](https://github.com/godotengine/godot/releases/tag/4.7.2-stable), checks both against the release's `SHA512-SUMS.txt`, unpacks only the templates the named platforms need into Godot's `export_templates/4.7.2.stable` folder (`%APPDATA%\Godot\…` on Windows, `~/Library/Application Support/Godot/…` on macOS, `~/.local/share/godot/…` on Linux) and prints the editor's path last. Files already in place are kept.

After a build on Windows, `sh scripts/smoke-windows.sh [zip]` unpacks the portable zip into a temporary folder and starts it without a window: `SimCruise.exe --headless --version`, `atlas.exe` with an unknown request, and 600 headless frames of the game with the map helper switched off (the offline Miami Beach start), failing on a non-zero exit or any script or shader error. It points `APPDATA` at the temporary folder, so your own cache and settings are untouched.

To try the installer itself without clicking through it, install it silently into a scratch folder (it installs per machine, so Windows asks once for administrator rights), start the installed copy headless, then uninstall silently. In PowerShell:

```powershell
Start-Process build\SimCruise-1.1.0-windows-x64-setup.exe -ArgumentList '/S','/D=C:\Temp\SimCruiseTest' -Wait   # /D last, no quotes inside
C:\Temp\SimCruiseTest\SimCruise.exe --headless --quit-after 600
Start-Process C:\Temp\SimCruiseTest\Uninstall.exe -ArgumentList '/S','_?=C:\Temp\SimCruiseTest' -Wait          # _?= makes it wait
Remove-Item -Recurse C:\Temp\SimCruiseTest   # the uninstaller cannot delete itself when run with _?=
```

The silent uninstall keeps `%APPDATA%\Godot\app_userdata\SimCruise` (the "Map cache and settings" box is off by default).

## Building the Mac app on Windows

`sh scripts/cross-build-macos.sh [version]` builds the Apple Silicon release without a Mac, on Windows (Git Bash) or Linux, and writes `build/SimCruise-<version>-macos-arm64.zip` with `build/SHA256SUMS-macos.txt`. It needs Godot 4.7.2 with the macOS export template (`sh scripts/install-godot.sh DIR macos`) and Bun. What it does:

1. Bun cross-compiles the map helper for macOS (`--target=bun-darwin-arm64`).
2. Godot exports the standard "macOS" preset from the official universal template, with Godot's own signing switched off for this export (the script edits a temporary copy of `game/export_presets.cfg` and puts the original back). `scripts/macos-bundle.mjs thin` then keeps only the arm64 half of the engine, as `lipo -thin` does on a Mac.
3. The helper goes to `SimCruise.app/Contents/MacOS/atlas`, beside the engine, where `Drive.gd` looks for it, and the licences to `Contents/Resources/Credits`, the same layout `scripts/build-macos.sh` produces.
4. [rcodesign](https://github.com/indygreg/apple-platform-rs) signs the finished bundle ad hoc, the equivalent of `codesign --force --deep --sign -` on a Mac: Apple Silicon refuses to run arm64 code with no signature at all. rcodesign 0.29.0 is used from `RCODESIGN` or PATH, or downloaded once into `tools/` from its official release and checked against a SHA-256 pinned in the script. Godot's "Built-in (ad-hoc only)" signing is not used because adding the helper afterwards would break its seal anyway, and it marks the engine for the hardened runtime, which the Mac build does not.
5. `scripts/macos-bundle.mjs verify` checks the bundle statically: every Mach-O file is a thin arm64 binary with an ad-hoc signature whose SHA-256 page hashes match the file, the main executable's signature matches `Info.plist` and `_CodeSignature/CodeResources`, every file sealed there still has its recorded hash (the helper by its CodeDirectory hash), and no file is left unsealed.
6. `scripts/macos-bundle.mjs zip` writes the zip with Unix permissions recorded (0755 for directories and Mach-O executables, 0644 otherwise) and symbolic links stored as links, because files exported on Windows have no execute bit. `unzip -Z build/SimCruise-<version>-macos-arm64.zip` lists the modes.

The result has not been launched by this route: only a Mac can confirm that it starts. Because the app is signed ad hoc and not notarized, macOS asks once on first launch, as for the Mac-built disk image ([install.md](install.md#first-launch-and-gatekeeper)). A `.dmg` is not made off a Mac (writing a reliable HFS+/UDIF image needs `hdiutil`); on a Mac, `sh scripts/make-dmg.sh VERSION path/to/SimCruise.app` packages an unzipped copy.

## Releasing

Releases are built on one Windows PC with Git Bash; the project uses no GitHub Actions. With Godot 4.7.2, its Windows and macOS export templates (`GODOT_BIN=$(sh scripts/install-godot.sh DIR windows macos | tail -n 1)`), Bun and NSIS 3 (`winget install NSIS.NSIS`) installed, from a clean checkout of the commit to release:

```sh
sh scripts/release-local.sh          # version from config/version
```

1. **Check**: the version must equal `config/version` in `game/project.godot`, and `CHANGELOG.md` must have a `## SimCruise X.Y.Z (date)` entry (`sh scripts/release-notes.sh X.Y.Z` prints it); otherwise nothing is built.
2. **Windows**: `scripts/build-windows.sh`, then `scripts/smoke-windows.sh` starts the portable build headless.
3. **macOS**: `scripts/cross-build-macos.sh` (above).
4. **Checksums and notes**: the per-platform checksum files are re-checked, `build/SHA256SUMS.txt` covers the three release files, and `build/release-notes-X.Y.Z.md` holds the changelog entry (its relative links pointed at the tagged files), a download list with first-launch hints, and the checksums.
5. **Commands**: the script prints, and does not run, the `git tag vX.Y.Z` / `git push origin vX.Y.Z` and `gh release create vX.Y.Z --draft --verify-tag ...` commands that tag the commit and upload the macOS zip, the Windows installer, the portable zip and `SHA256SUMS.txt` to a **draft** release with those notes.

A maintainer runs the printed commands, opens the draft on the Releases page, tries each download on its system (the Mac zip needs a Mac), and presses **Publish release**. Checks (`sh scripts/check.sh`, `sh scripts/check-helper.sh`) are run by hand before tagging; see [CONTRIBUTING.md](../CONTRIBUTING.md#versions-and-releases).
