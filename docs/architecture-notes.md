# SimCruise architecture notes (as documented for 1.0.8)

These sections were moved verbatim out of the project README when SimCruise (formerly **Sunday Driver**) got a newcomer-oriented README in November 2026. They describe how the 1.0.8 build was built, tested and fed with geographic data, in the original wording. Where they say "Sunday Driver" or name `Sunday Driver.app`, read SimCruise and `build/SimCruise.app`. Relative links were re-pointed so they resolve from `docs/`; the text was not edited.

For the short, current overview see "How it works" in the [README](../README.md). Release notes and study summaries from the same README are in [history.md](history.md).

## Build and run from source on Apple Silicon

Use the complete source archive, Godot 4.7.2 stable for macOS, and native arm64 Bun. This build was tested with Bun 1.4.3. The project uses Godot’s Mobile renderer on Metal and Jolt physics. Download the editor and its matching export templates from the [Godot 4.7.2 release](https://github.com/godotengine/godot-builds/releases/tag/4.7.2-stable). Install [Bun](https://bun.com/docs/installation) and Apple's free Command Line Tools (`xcode-select --install`) for local app signing and architecture selection.

From the extracted directory containing this README, prepare the helper, lockfile dependencies, and Godot imports:

```sh
sh scripts/build-macos.sh editor
```

Open `game/project.godot` in Godot and press F6 on `Drive.tscn`, or press F5 to run the project's main scene. The source game uses `tools/atlas`. To choose a specific editor executable, set `GODOT_BIN` to its full path. The script looks on PATH and then in `/Applications/Godot.app`. Set `BUN_BIN` if Bun is installed outside PATH.

For a standalone Mac app, install the matching templates using **Editor → Manage Export Templates**. Build and launch the result:

```sh
sh scripts/build-macos.sh
open "build/Sunday Driver.app"
```

The build uses Godot’s standard macOS release exporter and the official universal export template. It selects the Apple Silicon executable, installs the ARM64 streaming helper beside it, includes the attribution files, and verifies a local ad-hoc signature. Local builds need no paid Apple developer account. The exported executable and its helper run without a separately installed editor, Bun or Node.js. The first source build downloads the locked dependencies. Downloaded builds can receive a macOS Gatekeeper prompt because local ad-hoc signing provides no Apple notarization. [Godot macOS export guidance](https://docs.godotengine.org/en/stable/tutorials/export/exporting_for_macos.html)

All runtime assets are in `game/`. Images in `game/data/` are read as original bytes inside the exported PCK. Preserve their `.import` files with `importer="keep"` when editing or replacing these images. Third-party software, photographs, models, fonts and geodata have individual terms in `licenses/`. The map helper can be rebuilt from the included JavaScript and `bun.lock`. The native runtime and its linked libraries are documented in `licenses/bun.txt`.

WorldTile builds terrain, walls, curb geometry and road markings. Grove instances three mesh detail levels near the car; no tree sprites face the player. Drive manages geographic tiles, the camera, telemetry and the scene. Powertrain computes four suspension contacts, tire slip and shared cornering/braking grip, crank and wheel angular velocities, gear and differential ratios, and separate front/rear brake torque. Parameters are approximations for one dry, stock-like 1999 1.8L five-speed.

## Diagnostics
Run the fixed 60 Hz test track (accelerate, stop and complete a measured skidpad lap):

    godot --headless --path game --fixed-fps 60 --script res://checks/Track.gd

Every check mode starts in the bundled Miami Beach tiles (normal play starts on a random road from game/data/start-pool.json). Run the rendered offline search, seam-crossing and atlas test with --field-check and the offline launch fallback with checks/LaunchCheck.gd. The cross-country regression was invoked with:

    "Sunday Driver.app/Contents/MacOS/Sunday Driver" -- --road-test

It writes screenshots, progress and drive-result.json inside the Godot user-data directory under Sunday Driver/road-test. Set DRIVE_QA to choose another output directory. The test loads a Kansas highway over the internet and drives using the same input and physics as a player. It will exit when done. Measurements cover these specific conditions; they are not representative of all US streets or all Mac hardware.

Choose **Find a place → Princeton Road · Lancaster County, NE** for the eastbound gravel drive. On this county-confirmed section, the Miata drove 1.45 km across three streamed boundaries. The cold route used 31 source requests and the warm route used zero. The runtime now preserves explicitly mapped unpaved surfaces for the tires and road material. The complete source includes the isolated driver, matched-speed braking comparison, individual seam measurements and paired Metal captures. [Nebraska sustained drive](../lab/sustained/README.md)

## Geographic detail
The bundled Miami Beach start (Ocean Drive, Collins Avenue and the Flamingo Park blocks) was prepared ahead of time from the same public sources the streamer uses, so it loads with no network. Remote places derive from OpenStreetMap vectors served by OpenFreeMap, USGS 3DEP or Terrarium elevations and available public aerial imagery. The contiguous states use The National Map. Hawaii uses the 2021–2023 USDA NAIP survey, and parts of southeast Alaska use Forest Service orthophotos. Each optional 512-pixel regional image covers up to 16 playable tiles. Incomplete or blank regional mosaics fall back to terrain and the other available map layers. A bounded ~96 MiB cache of upstream assets feeds several smaller playable chunks without per-frame API calls. The cache is stored under the Godot app's user-data directory. Connected provider availability, map detail, and imagery dates vary. Building faces, estimated road widths, tree shapes and parked vehicles are representative unless explicitly sourced. Surveyed camber is available in selected districts. Grade-separated deck geometry, small pavement defects, traffic, wet surfaces and building-level photographic coverage still vary substantially or remain missing.

## Development and acceptance
These notes describe the Godot client, which lives at the `godot-final` tag (its asset contract, `authoring/car-assets.json`, and `authoring/check_assets.py` are there too; the Unreal client fits downloaded models instead, see docs/cars.md). From a checkout of that tag:

    godot --headless --path game --fixed-fps 30 --script res://checks/Wall.gd
    "Sunday Driver.app/Contents/MacOS/Sunday Driver" -- --profile-trees

The profile runs an on/off/off/on comparison of identical Miami Beach views and writes render-cost.json. Frame intervals include presentation and scheduling. GPU time reported as zero means unavailable, not zero GPU cost. The visual code uses the same trees for ordinary play; only the explicitly requested benchmark hides them temporarily.


## Optional OSM detail service

Public play uses road and structure tiles from OpenFreeMap. Extra street furniture can come from the bundled or locally cached OSM observations. To query a separately operated Overpass interpreter, set `SIMCRUISE_OVERPASS_URL` to its endpoint before launching the helper. Street objects perform zero requests to the community Overpass service by default. Mapped guard rails (`helper/guard-rails.mjs`) are the one exception: one small query per z14 area (2.4 km square), cached for 90 days, rotated across public mirrors with exponential backoff inside a 3 s budget, with a per-area negative cache (1 h doubling to 7 days) and a provider-wide pause (10 min doubling to 6 h) after failures, so a saturated service costs at most one short attempt per pause. `SIMCRUISE_GUARD_RAILS=offline` keeps it to the cache; `SIMCRUISE_OVERPASS_URL` sends it to your own interpreter instead. [Public service policy](https://dev.overpass-api.de/overpass-doc/en/preface/commons.html)
