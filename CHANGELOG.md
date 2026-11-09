# Changelog

Notable changes to SimCruise, newest first. Dates are release dates, not the survey dates of the data a release uses. The release notes written for Sunday Driver 1.0.0 to 1.0.8 are preserved word for word in [docs/history.md](docs/history.md).

## SimCruise 1.1.0 (unreleased)

Renamed from Sunday Driver (commit `2f1d1cf`). The work below follows the 8 November 2026 design-panel report kept as [docs/roadmap.md](docs/roadmap.md) and runs as parallel streams; the owner finalises this list at release.

### In progress

- **Controls** — speed-aware cap on keyboard and pad steering (the rack still allowed 18.8° at 45 mph where about 3° is usable), slower pedal and steering ramps, a keyboard-tap bench, and one-press recovery that notices a roof-down or stuck car, prompts for R and searches every road, not only main roads.
- **Roads** — a smoother driving surface: the 32 × 32 tile height grid produced a slope step at every grid crease in both the visible mesh and the collider, which launched the car; the fix takes the roadbed off the raw grid.
- **Light** — one sun instead of two (the sky panorama's unclipped sun added unshadowed light on top of the `DirectionalLight3D`, giving a shadow-to-sun ratio near 0.5 where photographs show 0.18 to 0.32), longer shadow range, tonemap white point, debanding and anisotropy settings, and the real clock for the sun angle instead of a fixed June afternoon.
- **Immersion** — HUD levels (Off / Glance / Full) with translucent panels instead of opaque bars that were on by default, sound on by default with tyre, wind and gravel layers added to the engine, and a driver head that lags and settles instead of a camera welded to the chassis.
- **Vegetation** — canopies with backlight, occlusion and warm tint instead of opaque dark blobs, near fade and alpha-to-coverage, mown verges and textured swaying field clumps instead of knee-high grass spikes.
- **Docs and release** — newcomer README; `docs/roadmap.md`, `docs/history.md` and `docs/architecture-notes.md`; this changelog; `CONTRIBUTING.md`; `scripts/make-dmg.sh` and the `dmg` mode of `scripts/build-macos.sh` (disk image with an Applications shortcut and a first-launch Gatekeeper note); `.gitattributes`; `.gitignore` additions; former-name notes in the `lab/` studies; the helper's User-Agent now identifies as SimCruise; the helper's modules, JSON datasets and `vendor/` moved from the repository root into `helper/`, so `scripts/build-macos.sh` compiles `helper/atlas.mjs` and the root holds only the top-level folders and project files.

### Calibration guard

Every stream keeps the headless `game/checks/Track.gd` run (`--fixed-fps 60`) inside its bands: 0–60 mph 7.97 ± 0.10 s, 70–0 mph 173 ± 3 ft, 300 ft skidpad 0.89 ± 0.01 g. Baseline on `2f1d1cf`: 7.948 s, 173.0 ft, 0.885 g. `RideCheck.gd`, `TurningCircle.gd` (30.35 ft left, 30.58 ft right at 1.0 and 1.8 m/s against the 30.2 ft specification) and `Wall.gd` (70 mph into a 15 cm wall, not crossed) also pass on that commit.

## Sunday Driver 1.0.8 (2026-11-08)

- Source snapshot before the SimCruise work (commit `ce412d0`). Release notes for 1.0.5 to 1.0.8 and the studies they cite: [docs/history.md](docs/history.md).
