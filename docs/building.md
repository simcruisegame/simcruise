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

## Building the app and disk image

You need Godot 4.7.2 stable for macOS with its matching export templates ([release page](https://github.com/godotengine/godot-builds/releases/tag/4.7.2-stable)), native Apple Silicon [Bun](https://bun.com/docs/installation) (tested with 1.4.3) and Apple's Command Line Tools (`xcode-select --install`) for `codesign` and `lipo`. From a clone:

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

## Releasing

Releases are built by GitHub Actions (`.github/workflows/release.yml`). Pushing a tag `vX.Y.Z` (or running the workflow by hand with an existing tag) does this:

1. **Check**: the tag must equal `config/version` in `game/project.godot`, and `CHANGELOG.md` must have a `## SimCruise X.Y.Z (date)` entry (`sh scripts/release-notes.sh X.Y.Z` prints it); otherwise nothing is built.
2. **Windows** (windows-2025 runner): Bun, NSIS (Chocolatey when the image lacks it), Godot and the Windows templates through `scripts/install-godot.sh`, then `scripts/build-windows.sh` and `scripts/smoke-windows.sh`.
3. **macOS** (macos-15, Apple Silicon runner): Bun, Godot and the macOS template, `scripts/build-macos.sh app`, a `--headless --version` start of the signed app, then `scripts/make-dmg.sh` (retried, because `hdiutil` on hosted runners sometimes reports a busy resource).
4. **Draft release**: the per-platform checksums are verified after transfer, `SHA256SUMS.txt` covers the three files, and a **draft** GitHub release for the tag gets the disk image, the installer, the portable zip and `SHA256SUMS.txt`, with the changelog entry plus a download list and the checksums as notes. Re-running for the same tag replaces the files and notes of a draft; a published release is never changed.

Nothing is published automatically: a maintainer opens the draft on the Releases page, tries the builds, and presses **Publish release**. Action versions are pinned to commit hashes. The manual route in [CONTRIBUTING.md](../CONTRIBUTING.md#versions-and-releases) still works when the hosted runners are unavailable.

`.github/workflows/checks.yml` runs `sh scripts/check.sh` on Linux (ubuntu-24.04, Godot fetched by `scripts/install-godot.sh`, no GPU needed under `--headless`) for every pull request and push to `main` that touches more than documentation, and keeps the check logs as an artifact when something fails.
