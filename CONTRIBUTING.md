# Contributing to SimCruise

Thanks for looking. Everyone taking part follows the [Code of Conduct](CODE_OF_CONDUCT.md). SimCruise is a small project with a large ambition, and the way it stays honest is by measuring. This page is the short version of how we work; the [README](README.md) explains what the game is and the [roadmap](docs/roadmap.md) explains what is wanted next.

## Ground rules

- **No keys, no accounts, no paid services.** Every data source the game talks to must be free to use without registration. If a source needs a token, it does not go in.
- **Licences are part of the change.** New assets must be CC0 or public domain unless they are fetched at runtime under their own terms; record the source URL and licence in `licenses/` (and in `Credits.txt` when it is a bundled asset) in the same commit. ODbL and ShareAlike data is cached on the player's machine, never committed to the repository.
- **Every change is measurable.** Add or extend a headless check under `game/checks/` for what you change, and put the before and after numbers in the pull request. Screenshots are welcome; numbers are required.
- **Calibration must not regress.** `game/checks/Track.gd` at a fixed 60 Hz has to stay inside 0–60 mph 7.97 ± 0.10 s, 70–0 mph 173 ± 3 ft and 300 ft skidpad 0.89 ± 0.01 g, whatever else moves.
- **Keep what works.** Tile streaming, the bounded cache, the atlas map, the manual gearbox and Xbox controller support are features, not side effects.
- **Trust Godot 4.7 documentation.** When a change depends on engine behaviour (renderer features, physics settings, `user://` paths), cite the documentation in the commit or comment. The Mobile renderer has no SSAO, SSIL, SSR, SDFGI, VoxelGI, volumetric fog or TAA; do not plan on them.

## Code style

- GDScript follows the existing terse style: one space of indentation, several short statements per line where the file already does that, no type hints unless the surrounding code has them. New functions should still be readable: a short comment above a function saying what it measures or decides is better than a long one inside.
- The helper is plain modern JavaScript (`.mjs`), one space of indentation, no build step beyond Bun's compiler. Dependencies are pinned in `bun.lock`; adding one needs its licence text in `licenses/`.
- Python under `lab/` and `authoring/` is tooling, standard library first. The game must build and run without any of it.
- Prefer measured values with their source over tuned constants. When a constant is an estimate, say so in a comment, as `Powertrain.gd` and `NB1999Sport.gd` do.

## Running the headless checks

The quickest way: `sh scripts/check.sh` runs every standalone Godot check with a 600 s cap per run and output to `build/check-logs/`, then prints a pass/fail table (and fails if the car leaves its calibration bands). `sh scripts/check.sh Track Wall` runs only the named checks. Set `GODOT_BIN` if Godot 4.7.2 is not on your PATH. `sh scripts/check-helper.sh` runs the map helper's offline tests. Run both before opening a pull request; they also run on Linux, since the headless checks need no GPU.

Checks are GDScript files under `game/checks/` run by the Godot executable with `--headless`. They print `RESULT` lines and exit. The four calibration checks and their expected values on the current baseline:

```sh
GODOT=/Applications/Godot.app/Contents/MacOS/Godot   # or wherever 4.7.2 lives
cap() { perl -e 'alarm shift @ARGV; exec @ARGV' 600 "$@"; }   # or: cap() { gtimeout 600 "$@"; }
trap '' XFSZ; ulimit -f 2097152   # 2 GiB per file; over-limit writes fail instead of killing Godot
cd simcruise
cap $GODOT --headless --editor --path game --import > /tmp/simcruise-import.log 2>&1   # once per fresh checkout, 1–3 min
cap $GODOT --headless --path game --fixed-fps 60 --script res://checks/Track.gd        > /tmp/simcruise-track.log 2>&1
cap $GODOT --headless --path game --fixed-fps 60 --script res://checks/RideCheck.gd    > /tmp/simcruise-ride.log 2>&1
cap $GODOT --headless --path game --fixed-fps 60 --script res://checks/TurningCircle.gd > /tmp/simcruise-turning.log 2>&1
cap $GODOT --headless --path game --fixed-fps 30 --script res://checks/Wall.gd         > /tmp/simcruise-wall.log 2>&1
tail -5 /tmp/simcruise-*.log
```

| Check | Expect |
|---|---|
| `Track.gd` | `RESULT flat: 0-60 mph = 7.95 s; 70-0 mph = 173.0 ft`, skidpad maximum stable 0.885 g |
| `RideCheck.gd` | left and right static loads within 60 N, peak difference above 300 N, settled roll below 0.4°, four contacts |
| `TurningCircle.gd` | `TURN_SUMMARY {"max_error_m": ≤ 0.20, "stable": true}`, inner wheel 38 ± 3°, outer 33 ± 3° |
| `Wall.gd` | `contact=true crossed=false` at 70 mph into a 15 cm wall |

The other scripts in `game/checks/` are unit-style checks (`DifferentialCheck`, `RearDamperCheck`, `TireStep`, `SplitGrip`, `SteerTransient`, `LockedBrakeTurn`, `LiftOff` (no spin on a lift or trail brake mid-corner), `TractionControlCheck`, `CombinedSlip`, `AbsReverse`, `ParkedStart` (a start or jump stands level and still on a 0–15° grade), `DaylightWeatherCheck`, `RoadBankMeshCheck`, `CrownArrivalOrder`, `SignalControlCheck`, …) and in-game QA modes selected with arguments after `--` (`--car-audit`, `--field-check`, `--us-check`, `--reference`, …). Read the header comment of each; most say how to run them. Those that drive real places need the network and the compiled helper at `tools/atlas`.

### Safety rules for scripted Godot runs

A runaway print loop once filled a disk with a multi-gigabyte log, so these are not optional:

1. Run headless only; never launch the windowed game from a script.
2. Wrap every Godot invocation in a wall-clock cap and send all output to a size-capped file, then read the file with `tail` or `grep`. GNU `timeout 600 …` where available; stock macOS has no `timeout`, so use `perl -e 'alarm shift @ARGV; exec @ARGV' 600 "$GODOT" …` or `brew install coreutils` for `gtimeout`. Cap the log through a pipe (`2>&1 | head -c 104857600 > log`), not with a low `ulimit -f`: Apple's Metal shader cache for the Godot binary can pass 200 MiB, and a low file-size limit then kills every run that compiles shaders. `sh scripts/check.sh` does all of this for you, using `timeout` where it exists (Linux, Git Bash on Windows) and perl otherwise, and it fails any check whose log comes back empty.
3. Parse-check a new or edited script before running it: `$GODOT --headless --path game --check-only --script res://checks/X.gd > /tmp/x.log 2>&1`.
4. Never `print` inside `_process` or `_physics_process` except once per state change, and stop printing after `request_game_close()`: the game keeps its frame loop alive until worker threads return, which is unbounded if a network worker never does.
5. One Godot process at a time. If a check hangs, kill it, keep the log and report it; do not retry in a loop.
6. Godot mirrors stdout into `~/Library/Application Support/Godot/app_userdata/SimCruise/logs/` as well, so anything printed per frame lands on disk twice.

## Repository map

| Path | What it is |
|---|---|
| `game/` | The Godot 4.7.2 project. `Drive.gd` (scene, streaming, camera, weather), `WorldTile.gd` (tile geometry), `Powertrain.gd` and `NB1999Sport.gd` (the car), `Hud.gd` and `RoadMap.gd` (HUD and atlas), shaders, `art/` (models, textures, fonts, HDR), `data/` (bundled offline tiles, atlas tiles and the start pool), `checks/` (headless checks and QA modes). |
| `helper/` | The atlas helper: `atlas.mjs` dispatches jobs; each module owns one data source or layer; its JSON datasets and the vendored HDF5 reader (`helper/vendor/`) sit beside them. `scripts/build-macos.sh` compiles it to `tools/atlas`. |
| `authoring/` | Asset validation and model-building tools (`check_assets.py`, `car-assets.json`). |
| `lab/` | Measurement studies behind individual features, with their own READMEs, data and reproduction scripts. Older ones say "Sunday Driver". |
| `licenses/`, `Credits.txt` | Per-source terms and attribution; `licenses/` ships inside the app. |
| `scripts/` | `play.sh` (update and play), `build-macos.sh` (editor, app and dmg modes), `build-windows.sh` (installer and zip), `make-dmg.sh`, `check.sh` (Godot checks) and `check-helper.sh` (helper tests). |
| `.github/` | Issue and pull-request templates. |
| `packaging/` | Platform packaging: the Windows NSIS installer script and icon. |
| `docs/` | Player and developer documentation (install, controls, technical overview, data sources, building), the roadmap, history and architecture notes; `docs/images/` holds README images. |

## Versions and releases

SimCruise uses [Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`. A patch release fixes bugs; a minor release adds features, cars, places or data layers; a major release breaks saved preferences, the cache format or the helper's command line. The version lives in one place, `config/version` in `game/project.godot`, and the build scripts read it from there.

`CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com/): every change a player would notice gets a line in the top, unreleased section in the same pull request.

To release, a maintainer:

1. Moves the unreleased changelog section under `## SimCruise X.Y.Z (YYYY-MM-DD)` and sets `config/version="X.Y.Z"`.
2. Merges that to `main`, runs `sh scripts/check.sh` and `sh scripts/check-helper.sh` on it, then tags it: `git tag vX.Y.Z && git push origin vX.Y.Z`.
3. Builds the macOS disk image on an Apple Silicon Mac with `sh scripts/build-macos.sh dmg X.Y.Z` and its checksum with `shasum -a 256 build/SimCruise-X.Y.Z.dmg > build/SimCruise-X.Y.Z.dmg.sha256`.
4. Builds the Windows installer and portable zip on any system with Godot 4.7.2, its export templates, Bun and NSIS: `sh scripts/build-windows.sh X.Y.Z` (it writes `build/SHA256SUMS-windows.txt`).
5. Starts each build once on its system, then attaches the disk image, the installer, the zip and the checksum files to a new GitHub Release for the tag, with the changelog section as its notes.

## Security

Never commit keys, tokens, passwords, personal paths or personal information; the game needs none of them. GitHub secret scanning blocks known token formats on push. Report vulnerabilities privately as described in [SECURITY.md](SECURITY.md).

## Pull requests and issues

Keep a pull request to one stream of work and say in its description what you changed, which check measures it, the numbers before and after, and what is still unverified. For data or asset changes, include the licence entry. For rendering changes, a pair of screenshots from the same place and time helps, but the renderer runs only on Apple Silicon Macs, so say what you tested on.

For a bug, include macOS and Godot versions, the F3 readout (frame rate, active tiles, map requests), the place you were driving (search text or coordinates), whether Weather, Imagery or Street photos were on, and the tail of the newest file in `~/Library/Application Support/Godot/app_userdata/SimCruise/logs/`. Bumps, launches and rollovers are especially useful with a location, since the ground there can be reproduced.
