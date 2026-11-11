# Contributing to SimCruise

Thanks for looking. Everyone taking part follows the [Code of Conduct](CODE_OF_CONDUCT.md). SimCruise is a small project with a large ambition, and the way it stays honest is by measuring. This page is the short version of how we work; the [README](README.md) explains what the game is and the [roadmap](docs/roadmap.md) explains what is wanted next.

## Ground rules

- **No keys, no accounts, no paid services.** Every data source the game talks to must be free to use without registration. If a source needs a token, it does not go in.
- **Licences are part of the change.** New assets must be CC0 or public domain unless they are fetched at runtime under their own terms; record the source URL and licence in `licenses/` (and in `Credits.txt` when it is a bundled asset) in the same commit. ODbL and ShareAlike data is cached on the player's machine, never committed to the repository.
- **Every change is measurable.** Add or extend an automation test under `Source/SimCruise` (`SimCruise.*`) for what you change, and put the before and after numbers in the pull request. Screenshots are welcome; numbers are required.
- **Calibration must not regress.** `SimCruise.Vehicle.CarTrack.<car>` at the shipping 360 Hz keeps every car inside its bands (0-60 mph ± 0.15 s, 70-0 mph ± 5 ft, skidpad ± 0.02 g of its published figures, see docs/cars.md), whatever else moves.
- **Keep what works.** Tile streaming, the bounded cache, the atlas map, the manual gearbox and Xbox controller support are features, not side effects.
- **Trust the Unreal Engine 5.8 documentation and source.** When a change depends on engine behaviour (renderer features, Chaos, light functions), cite the documentation or the engine source file in the commit or comment.

## Code style

- C++ follows Unreal's conventions (UObject reflection, `F`/`U`/`A` prefixes, tabs) and the existing files' habit of explaining the real-world reason for a number in a comment next to it.
- The helper is plain modern JavaScript (`.mjs`), one space of indentation, no build step beyond Bun's compiler. Dependencies are pinned in `bun.lock`; adding one needs its licence text in `licenses/`.
- Python under `lab/` and `authoring/` is tooling, standard library first. The game must build and run without any of it.
- Prefer measured values with their source over tuned constants. When a constant is an estimate, say so in a comment, as the car files' `sources` do.

## Running the tests

Build the editor target with the editor closed, then run every SimCruise test headless (about 12 minutes):

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' SimCruiseEditor Win64 Development -Project="$PWD\SimCruise.uproject" -WaitMutex
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "$PWD\SimCruise.uproject" -ExecCmds="Automation RunTests SimCruise;Quit" -unattended -nopause -nullrhi -nosplash -testexit="Automation Test Queue Empty"
```

`Automation RunTests SimCruise.Vehicle` (or any prefix) runs a part. Results are in `Saved/Logs`; the benches log one `CARTRACK`, `LIFTOFF`, `STATICLOAD` or `CONVERGENCE` line per car. The map helper's tests run with `sh scripts/check-helper.sh`. What each test checks is listed in [docs/unreal-building.md](docs/unreal-building.md#automation-tests) and [docs/unreal-architecture.md](docs/unreal-architecture.md).

Visual changes come with captures (`-SimCruiseDriveCapture`, docs/unreal-building.md) compared against licensed photographs of the place or car.

## Repository map

| Path | What it is |
|---|---|
| `Source/SimCruise/` | The Unreal client: `Vehicle/` (Chaos vehicle physics), `World/` (tile streaming, roads, buildings, palms, street lights), `Presentation/` (car models), `Environment/` (sun, sky, exposure), `Gameplay/` (input, cameras, HUD, rumble), tests beside each. |
| `Config/`, `Content/` | Unreal project settings; car presentation profiles (`Content/Vehicles/CarPresentation.json`) and calibration overlays. |
| `data/` | Engine-neutral data the client reads: car files, glTF car models, detail photographs, the bundled offline tiles, helper fixtures. |
| `helper/` | The atlas helper: `atlas.mjs` dispatches jobs; each module owns one data source or layer; its JSON datasets and the vendored HDF5 reader (`helper/vendor/`) sit beside them. `scripts/build-macos.sh` compiles it to `tools/atlas`. |
| `authoring/` | Asset validation and model-building tools (`check_assets.py`, `car-assets.json`). |
| `lab/` | Measurement studies behind individual features, with their own READMEs, data and reproduction scripts. Older ones say "Sunday Driver". |
| `licenses/`, `Credits.txt` | Per-source terms and attribution; `licenses/` ships with the game. |
| `scripts/` | `check-helper.sh` (map helper tests). |
| `.github/` | Issue and pull-request templates. |
| `docs/` | Player and developer documentation (install, controls, technical overview, data sources, building), the roadmap, history and architecture notes; `docs/images/` holds README images. |

## Versions and releases

SimCruise uses [Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`. A patch release fixes bugs; a minor release adds features, cars, places or data layers; a major release breaks saved preferences, the cache format or the helper's command line. The version lives in one place, `config/version` in `game/project.godot`, and the build scripts read it from there.

`CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com/): every change a player would notice gets a line in the top, unreleased section in the same pull request.

To release, a maintainer moves the unreleased changelog section under `## SimCruise X.Y.Z (YYYY-MM-DD)`, merges it to `main`, runs every `SimCruise.*` test and `sh scripts/check-helper.sh` on it, and tags it (`git tag vX.Y.Z && git push origin vX.Y.Z`). The Unreal client's packaging (a Windows installer first; macOS needs a Mac to build on) is still to be written; the Godot 1.x release scripts are at the git tag `godot-final`.

## Security

Never commit keys, tokens, passwords, personal paths or personal information; the game needs none of them. GitHub secret scanning blocks known token formats on push. Report vulnerabilities privately as described in [SECURITY.md](SECURITY.md).

## Pull requests and issues

Keep a pull request to one stream of work and say in its description what you changed, which check measures it, the numbers before and after, and what is still unverified. For data or asset changes, include the licence entry. For rendering changes, a pair of screenshots from the same place and time helps, but the renderer runs only on Apple Silicon Macs, so say what you tested on.

For a bug, include your OS, GPU and Unreal Engine version, the F3 readout, the place you were driving (search text or coordinates), whether Weather, Imagery or Street photos were on, and the tail of the newest log in `Saved/Logs`. Bumps, launches and rollovers are especially useful with a location, since the ground there can be reproduced.
