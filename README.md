# SimCruise

SimCruise is a free, open-source driving simulator for real places. Type any US address, street or pair of coordinates and drive there in a 1999 Mazda MX-5 Miata Sport, on roads built live from public map, elevation, aerial and survey data. There is no account, no paid API key and no subscription: the Mac app and its small map helper talk directly to open data services (OpenStreetMap, USGS, USDA, NOAA, NASA, Copernicus, Overture, city open-data portals) and cache what they fetch on your disk. The ambition is Microsoft Flight Simulator's "the whole real world" met with a Forza-grade feel for one well-understood car. The current build is honest about where it is: roads, terrain, buildings and trees come from real data, the car is calibrated against published test numbers, but many surfaces are representative rather than photographed, it only knows one afternoon's light, and nothing stands beyond about 700 m. The [roadmap](docs/roadmap.md) says what comes next and why.

Game code, shaders, the map helper and the tests are MIT-licensed. Map databases, surveys, photographs, models and fonts keep their own licenses, listed under [Data sources and licenses](#data-sources-and-licenses). SimCruise was called **Sunday Driver** through version 1.0.8; older notes under `lab/` and in [docs/history.md](docs/history.md) still use that name.

[![How SimCruise works: your input goes to the Godot client, which streams a 3 × 3 ring of map tiles around the car through the atlas helper and its 96 MiB cache from fifteen public data layers; WorldTile builds terrain, roads, buildings and trees from each tile, Powertrain drives the Miata on it, and the renderer and procedural audio return the picture and sound](docs/how-it-works/diagram.svg)](docs/how-it-works/index.html)

*How it works, in one picture.* The [interactive version](docs/how-it-works/index.html) lets you click any stage or data layer for what it does, the files behind it, its licence and its limits, and press Play to watch tiles stream around the car. It runs in a browser from a clone, or from GitHub Pages once enabled; GitHub's file view shows only its source. [docs/how-it-works/README.md](docs/how-it-works/README.md) says how to open it. The prose version is under [How it works](#how-it-works) below.

## Screenshots

Placeholders until the 1.1 captures are taken. See [docs/images/README.md](docs/images/README.md) for the shot list.

| | |
|---|---|
| ![Driver's view from the Miata cockpit on a two-lane road](docs/images/cockpit.jpg) | ![Chase camera on a rural highway with streamed terrain](docs/images/chase.jpg) |
| ![The road atlas with a pinned location and imagery date](docs/images/atlas.jpg) | ![Monument Valley on US 163 with the regional terrain horizon](docs/images/landscape.jpg) |

## Install

Requirements: a Mac with Apple Silicon (M1 or later) running macOS 13 Ventura or newer, a few hundred megabytes of disk for the app plus a cache that stays under roughly 100 MB, and an internet connection for any place outside the bundled starter area. Intel Macs are not supported: the release executable is Apple Silicon only. An Xbox controller is optional (pair it in System Settings → Bluetooth).

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

## Play

The game opens in the cockpit on a bundled starter road in rural Virginia. Press `/` (or click **Find a place**) and type a place name, a street with its town, or `latitude, longitude`; pick a result to load the nearest mapped road. Places worth trying: `Monument Valley · US 163, UT`, `Capitol Avenue · Cheyenne, WY`, `Princeton Road · Lancaster County, NE` (gravel), `Clay Center · Charleston, WV`, or the coordinates `40.808017, -96.675328` for a Nebraska street whose roofs are measured from public lidar as you drive. A new area needs a few seconds to stream; the horizon terrain can take longer on a first visit.

### Controls

| Action | Keyboard | Xbox controller |
|---|---|---|
| Accelerate | W or ↑ | Right trigger |
| Brake, then reverse once stopped | S or ↓ | Left trigger |
| Steer | A / D or ← / → | Left stick |
| Full brake (service brake) | Space | — |
| Parking brake, rear wheels | H (hold) | A (hold) |
| Automatic ↔ manual gearbox | T | B |
| Clutch (manual mode) | Shift (hold) | LB (hold) |
| Shift up / shift down (manual mode) | E / Q | RB / X |
| Restart a stalled engine | I, with the clutch held | Right-stick click, with the clutch held |
| Camera: chase, driver, scenic | C | Y |
| Hide or show the driving HUD | F2 | Left-stick click |
| Frame rate, active tiles, map requests | F3 | — |
| Engine sound on / off | M | — (HUD button) |
| Road atlas open / close | Tab (Esc also closes) | Menu |
| Atlas: zoom, recenter | + / −, mouse wheel, drag to pan, click to pin | D-pad up / down, A |
| Minimap zoom | Mouse wheel over the minimap, its + / − buttons | D-pad up / down |
| Find a place | / | — (HUD button; typing needs a keyboard) |
| Recover to the nearest road | R | View |

**Manual gearbox.** Press T (B) to take over the five-speed. Hold the clutch (Shift / LB), tap E (RB) for first, add throttle and let the clutch out. Shifts need the clutch; the gearbox refuses a gear that would over-rev the engine or change direction while rolling. If you stall, hold the clutch and press I (right-stick click). Automatic mode is an assist on the same simulated gearbox, not a different car. Your transmission choice is remembered on this Mac.

**Road atlas.** Tab opens a full-screen OpenStreetMap-based map. Drag to pan, scroll to zoom, click to pin a spot and load its nearest road. Its buttons switch **Transmission**, **Street photos** (colour hints from licensed KartaView thumbnails near the road), **Imagery** (recent Sentinel-2 colour, with the capture date), **Weather** (nearest METAR observation drives clouds, visibility and fog) and **USDA ground cover**. **Source photo ↗** opens the credited photograph. **Compare Street View ↗** opens Google Maps in your browser at the pinned or current position so you can compare. **Source photo ↗** and **Compare Street View ↗** are the only two things in the game that open your browser, and both fire only when you click them.

## How it works

SimCruise is two programs and a cache:

```
Godot 4.7.2 client (game/)                 atlas helper (Bun, helper/*.mjs)          public data services
Drive.gd, WorldTile.gd, Powertrain.gd …    one process per job, no server            OSM/OpenFreeMap, USGS, USDA,
 worker thread ── OS.execute("atlas tile X Y") ──▶ fetch · decode · convert ── HTTPS ─▶ NOAA, NASA, Copernicus,
                ◀── one JSON object on stdout ───  write PNG/JSON into the cache        Overture, KartaView, cities …
                                           user://map-cache, ≤ 96 MiB, LRU eviction
```

**The client** is a Godot 4.7.2 project using the Mobile renderer on Metal and Jolt physics at a fixed 60 Hz. `Drive.gd` owns the scene: it decides which tiles are needed, runs helper jobs on worker threads, builds the tiles it gets back, places the car, drives the camera, HUD and weather. `WorldTile.gd` turns one tile's data into terrain, road ribbons with curbs and markings, sidewalks, building walls and roofs, land cover and tree placements. `Grove.gd` instances three detail levels of a street-oak mesh for canopies; nothing is a sprite. `Powertrain.gd` with `NB1999Sport.gd` is the car.

**The helper** (`helper/atlas.mjs` and the modules beside it) is JavaScript compiled by Bun into a single native executable, `atlas`, shipped inside the app bundle. Godot never opens a network socket. For every job (`tile`, `aerial`, `canopy`, `buildings`, `roofs`, `landscape`, `conditions`, `recent`, `search`, and a dozen more) a worker thread runs the helper once with the cache directory and tile coordinates; the helper fetches whatever that job needs, converts it into plain JSON or PNG files inside the cache and prints one JSON object. Every upstream request goes through one rate-limited, retry-aware fetcher (`atlas-cache.mjs`) that honours `Retry-After`, keeps per-host pauses and stores responses with time-to-live. The cache is bounded: `cache-eviction.mjs` trims it to about 96 MiB, least recently used first, so a warm area replays from disk with zero requests and a long trip never fills your disk.

**Tiles.** The world is Web Mercator zoom-16 tiles, roughly 400 to 550 m across in the lower 48. For each one the helper reads the OpenFreeMap vector tile (roads, buildings, land use, water, points of interest), samples elevation into a 32 × 32 height grid (Terrarium tiles, replaced by USGS 3DEP 1/3-arc-second blocks where they exist, with paved-road grades fitted to the samples), estimates road widths and surfaces from tags, fetches a 1024-pixel aerial per 4 × 4 block of tiles where USGS, NAIP or Forest Service imagery covers the spot, and places tree crowns from the WRI/Meta canopy height map. The occupied tile and the ring around it stream in as you drive. Later passes refine what is already drivable: verified building heights and extra footprints from Overture and Esri's OSM replica, building use from the National Structure Inventory, roof shapes and road camber from USGS lidar point clouds, municipal tree inventories, traffic signals and other street objects, and colour hints from licensed street photographs. A separate regional terrain worker fills the horizon with a 45 km mesh simplified by Martini and draped with aerial imagery, snow observations and Sentinel-2 colour on distant ridges.

**The car.** The Miata is a Jolt rigid body of 1,043 kg with a 51.5 % front weight split, a custom centre of mass and inertia tensor, and four suspension contacts. Coil rates (168 / 130 lb/in), lever ratios and anti-roll-bar dimensions are the NB Sport row of a published suspension calculator; the rear damper interpolates a published Bilstein Hard S dynamometer trace and the front damper is an estimate. Tyres are 195/50R15: lateral force builds over a 0.35 m relaxation length, cornering and braking share one grip budget, and the surface class from the map changes the friction. The 1.8 L engine uses a tabulated torque curve to a 7,000 rpm cut, the real gear ratios (3.136, 1.888, 1.330, 1.000, 0.814) with a 4.30 final drive, a clutch that can stall, separate front and rear brakes and a Torsen-style limited-slip model. Keyboard and pad steering run through an Assisted map whose rack travel is capped by the grip the tyres can actually use at that speed, so a half-second tap at 45 mph is a lane correction rather than a spin (`checks/KeyboardTap.gd` measures it); a Direct map keeps the full 35.5° rack for a future wheel.

Calibration is a headless test, `game/checks/Track.gd`, run at a fixed 60 Hz on a flat pad. Its targets follow the numbers Car and Driver's 1999 road test and Mazda's specifications give for this car:

| Measure | Target | SimCruise (commit 2f1d1cf) | Band that must hold |
|---|---|---|---|
| 0–60 mph | 7.97 s | 7.95 s | ± 0.10 s |
| 70–0 mph braking | 173 ft | 173.0 ft | ± 3 ft |
| 300 ft skidpad | 0.89 g | 0.885 g | ± 0.01 g |
| Turning circle (`TurningCircle.gd`, outer front tyre) | 30.2 ft | 30.4–30.6 ft | ≤ 0.20 m error |

Three magazine numbers and the suspension spec are what "feels like a Miata" rests on; there is no public tyre, dyno or damper data for this exact car, and the roadmap says so.

**Rendering.** Godot's Mobile renderer on Metal with 2× MSAA at 0.9 render scale, a filmic tonemap, one `DirectionalLight3D` sun with a 2048-pixel shadow map reaching 85 m, and a Poly Haven HDR panorama sky. With Weather on, the METAR cloud cover swaps in a procedural overcast sky and sets fog density from reported visibility; a dated street photograph can also reconstruct an archived sky from NOAA satellite cloud masks. The sun is currently fixed to a June afternoon everywhere; real-time sun, longer shadows and a single sun (the sky panorama's own sun still adds unshadowed light, which is the washed-out look) are the first items on the visual roadmap. The Mobile renderer has no SSAO, SSR, global illumination or volumetric fog, so everything is lit directly.

**Privacy.** No telemetry, analytics, accounts, keys or crash reporting. The only network traffic is the helper's requests to the data services in the table below, sent with the User-Agent `SimCruise/1.1`. Nothing about you leaves the machine; the services see the tile coordinates you drive through, as any map viewer would. "Source photo ↗" and "Compare Street View ↗" open your browser only when you click them. The cache is a plain folder you can delete.

## Build from source

You need Godot 4.7.2 stable for macOS with its matching export templates ([release page](https://github.com/godotengine/godot-builds/releases/tag/4.7.2-stable)), native Apple Silicon [Bun](https://bun.com/docs/installation) (tested with 1.4.3) and Apple's Command Line Tools (`xcode-select --install`) for `codesign` and `lipo`. From a clone:

```sh
sh scripts/build-macos.sh editor   # installs locked JS dependencies, compiles tools/atlas, imports the Godot project
sh scripts/build-macos.sh app      # exports, thins to arm64, adds the helper and credits, signs ad hoc → build/SimCruise.app
sh scripts/build-macos.sh dmg 1.1.0   # the same, then packages build/SimCruise-1.1.0.dmg
sh scripts/make-dmg.sh [version] [app] [out-dir]   # package an existing build/SimCruise.app on its own
```

After `editor`, open `game/project.godot` in Godot and press F5 (the editor build runs the helper from `tools/atlas`, which is a gitignored build product). Set `GODOT_BIN` and `BUN_BIN` if the executables are not on your PATH; the script also looks in `/Applications/Godot.app`. The export uses Godot's standard macOS preset and the official universal template, then keeps only the arm64 slice. `make-dmg.sh` takes the version from its first argument or `SIMCRUISE_VERSION`, else from `application/short_version` in `game/export_presets.cfg` or `config/version` in `game/project.godot` when one is set, else from the top entry of `CHANGELOG.md` with a warning; it uses only `hdiutil`, `ditto` and `codesign`.

Headless checks run without a window and are how changes are measured; the four calibration checks and the safety rules for running Godot from scripts are in [CONTRIBUTING.md](CONTRIBUTING.md). The original, more detailed build and diagnostics notes are kept in [docs/architecture-notes.md](docs/architecture-notes.md).

## Is there anything else like this?

Not quite this combination, as far as we know; the pieces exist separately:

- **Microsoft Flight Simulator** streams the whole planet from Bing aerial imagery and photogrammetry, which is the scale SimCruise aims at, but it is proprietary, needs an account, and you fly over the roads rather than drive them. **FlightGear** is the open-source counterpart with scenery generated from public elevation and OpenStreetMap data, still from the air.
- **Forza Horizon**, **The Crew** and **American Truck Simulator** drive through real regions, but condensed, rearranged or scaled (ATS uses roughly 1:20), hand-built from reference rather than generated from the map, and all are paid, closed titles.
- **BeamNG.drive** and **Assetto Corsa** have the vehicle fidelity (soft-body physics, laser-scanned circuits) but on fictional or closed-course maps. Open-source racers such as **Speed Dreams** or **Rigs of Rods** have fictional maps too.
- **Streets GL**, **OSM2World** and similar open-source renderers draw real OpenStreetMap towns in 3D, and research simulators such as **CARLA** (open source, Unreal) simulate driving for autonomous-vehicle work, but on synthetic towns, not where you live.
- Engine plugins like **Cesium for Unreal/Unity** or the Mapbox and ArcGIS SDKs stream real-world 3D tiles into a game engine, which is the closest technical cousin, but they depend on a vendor account and API key, and someone still has to build the game.

SimCruise's niche is the intersection: real roads anywhere in the US, generated at runtime from public data with recorded provenance, one physically modelled real car with published calibration targets, and an MIT code base with no keys or accounts. If you know a project that does this, open an issue so we can link it here.

## Contributing and roadmap

Read [CONTRIBUTING.md](CONTRIBUTING.md) for the ground rules (no keys, measurable changes, CC0 or public-domain assets only, headless checks) and the code style. The design-panel report that drives the 1.1 work is [docs/roadmap.md](docs/roadmap.md); what is in progress is in [CHANGELOG.md](CHANGELOG.md). Research and measurement studies behind individual features live under `lab/`, each with its own README; the index is at the end of this file.

## Data sources and licenses

The MIT license in [LICENSE](LICENSE) covers the original game code, shaders, the helper's JavaScript, the Python authoring tools and the tests. Everything geographic or photographic keeps its own terms, recorded per source in [`licenses/`](licenses/) (start with `licenses/Road Atlas.txt`) and summarised in [Credits.txt](Credits.txt). The rule for bundling is simple: ODbL and ShareAlike data is fetched at runtime and cached on the player's machine, never redistributed in the MIT repository, except the small starter tiles under `game/data/` that are marked as derivative databases in `licenses/Road Atlas.txt`.

| Layer | Source | License | Reaches the game |
|---|---|---|---|
| Roads, buildings, land use, water, place names | OpenStreetMap via [OpenFreeMap](https://openfreemap.org/) vector tiles | ODbL 1.0 | At runtime; starter tiles bundled |
| Place search | [Photon](https://photon.komoot.io/) geocoder on OSM data | ODbL 1.0 (data) | At runtime |
| Ground elevation | Mapzen/Tilezen Terrarium tiles; USGS 3DEP 1/3-arc-second and 1 m products (The National Map) | US public domain | At runtime |
| Aerial imagery | USGS The National Map imagery; USDA NAIP (Hawaii, low-vegetation detection); USDA Forest Service Alaska orthophotos; Virginia VGIN for the starter area | US public domain / state public domain | At runtime; starter imagery bundled |
| Recent surface colour | Copernicus Sentinel-2 L2A via Element 84 Earth Search | Copernicus Sentinel data terms (free, attribution) | At runtime, optional |
| Tree canopy heights | WRI/Meta High Resolution Canopy Height Maps v2 | CC BY 4.0 | At runtime |
| Forest type, shrub cover | USGS/USDA LANDFIRE EVT; MRLC NLCD and RCMAP; USDA Rangeland Analysis Platform | US public domain | At runtime |
| City street trees | City of Lincoln NE and City of Cambridge MA inventories | City open-data terms; PDDL 1.0 (Cambridge) | At runtime |
| Building heights, roofs, extra footprints | Overture Maps buildings (OSM, Microsoft ML, Esri Community Maps); Esri's OSM North America replica | ODbL 1.0; CC BY 4.0 for Esri entries | At runtime |
| Building use | US Army Corps of Engineers National Structure Inventory | US public domain | At runtime |
| Measured roofs and road camber | USGS 3DEP lidar point clouds (EPT on AWS); Miami-Dade County lidar | US public domain; CC0 (Miami-Dade) | At runtime; 15 Miami roofs bundled |
| Traffic signals, street objects | Overture transportation theme; optional self-hosted Overpass | ODbL 1.0 | At runtime |
| Weather and sky | NOAA Aviation Weather Center METAR; NWS observations via Iowa Environmental Mesonet; NOAA GOES cloud mask; NASA GIBS snow | US public domain | At runtime, optional |
| Street photographs | KartaView, Panoramax (thumbnails and credits only) | CC BY-SA 4.0 | At runtime, optional |
| Car, props, textures, sky, fonts | Original Miata model with Wikimedia lamp textures; Poly Haven and OpenGameArt textures; Comrade1280 parked cars; Barlow Semi Condensed and Instrument Serif | CC BY-SA 4.0; CC0; CC BY 4.0; SIL OFL | Bundled in `game/art/` |

Paths in this repository that are not MIT: `game/data/*.json` map tiles and `game/data/observed-honolulu.json` (ODbL derivative databases), `game/art/miata.glb` and its textures plus the Chula capture and scan assets (CC BY-SA 4.0), `game/art/sedan.glb` and `game/art/pickup.glb` (CC BY 4.0), `game/art/title.ttf` and `game/art/ui.ttf` (SIL OFL), and the CC0 textures and HDR listed in `Credits.txt`. Third-party code in the engine, runtime and helper (Godot, Jolt, Bun, Martini, GeoTIFF.js, proj4js, earcut, fast-png, pbf, laz-perf, h5wasm) has its license texts in `licenses/` and ships inside the app under `Contents/Resources/Credits`.

## Credits

Built on [Godot Engine](https://godotengine.org/) with [Jolt Physics](https://github.com/jrouwe/JoltPhysics), [Bun](https://bun.com/), [Martini](https://github.com/mapbox/martini), [GeoTIFF.js](https://geotiffjs.github.io/), [proj4js](http://proj4js.org/), earcut, fast-png, pbf and laz-perf. Map data © OpenStreetMap contributors, served by OpenFreeMap and Photon; elevation, imagery, lidar, vegetation and weather from USGS, USDA, NOAA, NASA, the US Army Corps of Engineers, LANDFIRE and MRLC; building data from the Overture Maps Foundation, Microsoft and Esri; Sentinel-2 imagery from the Copernicus programme; canopy heights from WRI and Meta; street photographs from KartaView and Panoramax contributors; tree inventories from the cities of Lincoln and Cambridge; state and county data from Virginia, Chesterfield County, Miami-Dade County, Utah DOT and others named in `licenses/`. Textures and models by the Poly Haven, OpenGameArt and Sketchfab artists, and photographs by the Wikimedia Commons, KartaView and Panoramax photographers credited by name in [Credits.txt](Credits.txt). Miata suspension specifications after Fat Cat Motorsports' NB Ride Harmonizer; calibration targets from Car and Driver's 1999 test.

## History and studies

- [docs/history.md](docs/history.md): the release notes and study summaries from the 1.0.x READMEs, word for word.
- [docs/architecture-notes.md](docs/architecture-notes.md): the original build, diagnostics and geographic-detail notes.
- [docs/roadmap.md](docs/roadmap.md): the design-panel report and plan.
- `lab/` studies with their own READMEs: [vehicle controller comparison](lab/vehicle/README.md), [engine sound](lab/vehicle/engine-sound/README.md), [pad corrections](lab/vehicle/pad-corrections/README.md), [locked braking](lab/vehicle/locked-braking/README.md), [Miata appearance](lab/miata/appearance/README.md), [Nebraska sustained drive](lab/sustained/README.md), [road camber](lab/road-crossfall/README.md), [lane markings](lab/road-layout/README.md), [road paint](lab/road-paint/README.md), [Medford shoulder](lab/ma-road-section/README.md), [terrain source](lab/terrain-source/README.md), [landscapes](lab/landscapes/README.md), [rangelands](lab/rangelands/README.md), [airborne forest](lab/airborne-forest/README.md), [forest form](lab/forest-form/README.md), [sky archive](lab/sky-archive/README.md), [daylight](lab/daylight/README.md), [roof streaming](lab/lidar/roof-stream/README.md), [Charleston landmark](lab/lidar/landmark/README.md), [Cheyenne](lab/lidar/cheyenne/README.md), [Bartlesville](lab/lidar/bartlesville/README.md), [Anchorage cladding](lab/lidar/anchorage/cladding/README.md), [verified footprints](lab/lidar/verified-footprints.md), [independent footprints](lab/footprints/README.md), [city trees](lab/holdout/civic-trees.md), [street walls](lab/holdout/ordinary-street-walls/README.md), [mapped storefronts](lab/mapped-storefronts/README.md), [street vehicles](lab/street-vehicles/README.md), [survey striping](lab/survey-striping/README.md). Studies written before November 2026 refer to the game by its former name, Sunday Driver.
