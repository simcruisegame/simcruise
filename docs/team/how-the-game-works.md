# How SimCruise works (one page for every role; read this first)

SimCruise is **a realistic driving game that builds the world from real-world data**, anywhere in the US. There are no hand-made levels. The world is built at run time, tile by tile, around the car. The rule for every role: **it must work at a random US place**, not only on the hero roads (charter, `docs/team/ownership.md`). Test places: `data/test-places.json` (`scripts\preflight.ps1 -Roads random`).

## The parts and where they live

| Part | What it does | Code |
|---|---|---|
| Map helper | A Bun program (`tools/atlas.exe`, built from `helper/*.mjs`). For each tile the game runs it; it fetches public data (OSM/OpenFreeMap, USGS 3DEP, NAIP aerial, Overture, NOAA, NLCD and more), fits road profiles, and answers with one JSON document. The game itself never opens a socket for map data. Cache: `%LOCALAPPDATA%/SimCruise/map-cache`. | `helper/atlas.mjs` and its 52 modules (`road-*.mjs`, `regional-*.mjs`, `overture-buildings.mjs`, `guard-rails.mjs`, ...). Contract: `docs/unreal-architecture.md` "The helper contract". |
| Atlas client | Runs the helper on a worker thread and parses its answer. | `Source/SimCruise/Private/Atlas/` |
| World streamer | Web Mercator zoom-16 tiles (611.5 m × cos(latitude): 576 m at Hilo, 550 m at Miami, 418 m in North Dakota, 294 m at Anchorage) in a ring around the car; built on worker threads, one tile installed per frame; floating origin. | `World/SimCruiseWorldStreamer`, `SimCruiseTileManager`, `SimCruiseGeo` |
| Tile builder | One procedural mesh per tile: terrain fitted under the roads, road ribbons (crown, curbs, gutters, sidewalks, markings), water, buildings by regional recipe, trees, street furniture, guard rails, signals, bridges, traffic. | `World/SimCruiseTileBuilder`, `SimCruiseRoadNetwork`, `SimCruiseTileDetail`, `SimCruiseTrees`, `SimCruiseStreetFurniture`, `SimCruiseGuardRails`, `SimCruiseBridges`, `SimCruiseTraffic`, `SimCruiseRegionalTerrain` |
| World materials | Code-built materials, CC0 detail photos, aerial photo, world-anchored UVs (no repeat at seams). | `World/SimCruiseWorldResources`, `SimCruiseBuildWorldMaterialsCommandlet` |
| Place knowledge graph | The hub. Facts about a place (climate, region, road kind, building style, trees, signs) from open data and local AI; real-time systems read from it. | `Knowledge/` (`SimCruisePlaceGraph*`, `SimCruisePlaceFacts`, `SimCruisePlaceAI`, `SimCruiseFurnitureData`), `docs/place-graph.md`, `data/knowledge` |
| Live world and AI detail | Live sky, weather and feeds; the local AI detail layer (ONNX Runtime, never on the game thread) and the scene parser. | `Live/` (`SimCruiseLiveData`, `SimCruiseLiveSky`, `SimCruiseNeuralDetail`, `SimCruiseSceneParser`), `data/ai` |
| Car physics | Chaos at a fixed 360 Hz. A physics-thread callback computes each wheel's contact, suspension, combined-slip tyre, engine, clutch, gearbox, differential and brakes (ABS) in SI units, from the car files. | `Vehicle/` (`SimCruiseVehicleSim`, `SimCruiseTireModel`, `SimCruiseSuspensionModel`, `SimCruisePowertrainModel`, `CarDefinitionLoader`, `SimCruiseTuning`), `data/cars/*.json`, `docs/cars.md` |
| Car look | Downloaded glTF models rigged at load (wheels, steering wheel, gauges, lamps, materials by role), mirrors, clock, low-beam pattern. | `Presentation/` (`SimCruiseCarModelRig`, `SimCruiseCarPresentationComponent`, `SimCruiseCarMaterialLibrary`, `SimCruiseGltfModel`, `SimCruiseLowBeamPattern`), `Content/Vehicles/CarPresentation.json` |
| Light and sky | Sun and moon for the place and time, light in real units, photographer-style exposure, graphics presets, lighting lab. | `Environment/` (`SimCruiseSolarPosition`, `SimCruiseDaylightModel`, `SimCruiseEnvironment`, `SimCruiseGraphics`, `SimCruiseWeather`, `SimCruiseLightingLab`, `SimCruisePerfSampler`) |
| Sound | Inline-four engine synth, engine sound component, road noise synth. | `Audio/` |
| Driving shell | Controller, input (Enhanced Input), steering and pedal shaping (never fakes physics), cameras and lenses, HUD, rumble, menus (place search, tune car, seat and view), route and GPS. | `Gameplay/` (`SimCruisePlayerController*`, `SimCruiseDriverShaping`, `SimCruiseCameraModels`, `SimCruisePlayerCameraManager`, `SimCruiseDriveHud`, `SimCruiseHaptics`, `SimCruisePauseMenu`, `SimCruiseTuningUI`, `SimCruiseSeatUI`, `SimCruiseAtlasUI`, `SimCruiseRoute*`, `SimCruisePlaces`, `SimCruiseGameMode`) |
| Start places | Normal play starts on a random road from `data/start/start-pool.json` (36 roads); `-SimCruiseStart=lat,lon` starts at any point and snaps to the nearest road. | `Gameplay/SimCruiseGameMode.cpp`, `SimCruisePlaces.cpp` |
| Tests and checks | Unreal automation tests (`SimCruise.Vehicle.*`, `.World.*`, `.Drive.*`, ...), drive robot, view check, pre-flight video, replay tapes (F9), frame check, realism compare, soak, latency probe, watchdog, packaging. | `Private/Tests`, `*/Tests`, `Gameplay/SimCruisePreflight`, `SimCruiseViewCheck`, `SimCruiseInputTape`, `SimCruiseSoak`, `SimCruiseLatencyProbe`; `scripts/*.ps1`; `docs/unreal-building.md` |

## One launch, start to drive

1. `SimCruiseGameMode` picks the start (pool road, chosen point, or the bundled Ocean Drive tiles for checks).
2. The world subsystem starts the streamer at that point; the atlas client runs the helper for the tiles in the ring.
3. Worker threads build each tile's mesh, trees, furniture and traffic; the game thread installs one tile per frame.
4. The place graph fills in facts about the place; the tile builder and the detail layer read them (building recipe, wall palette, street furniture colours). Tree species do not read the graph yet: they come from lat/lon rules (`ConiferShare`, `IsCoconutPalmRegion`).
5. The car spawns on the nearest road; physics runs at 360 Hz; the controller feeds shaped input; the camera, HUD and sound read the car state.
6. As the car moves, new tiles stream in ahead and old ones leave; the floating origin moves with the car.

## What "works everywhere" means for each part

- Data: every source has US-wide coverage or a fallback that still looks right (no blank ground, no missing roads) where map data is poor.
- Rules from the place, not from the road: climate, region, terrain and road class choose materials, trees, furniture and buildings.
- Hero roads (Ocean Drive, Tail of the Dragon, Nebraska gravel, Big Sur) are regression tests, not the target.
- Test at least 3 random places from `data/test-places.json` (seeded per day) plus the hero roads, and look at the frames.

## More to read

`README.md` · `docs/technical-overview.md` · `docs/unreal-architecture.md` (the detail of every part) · `docs/unreal-building.md` (build and test rules) · `docs/cars.md` · `docs/place-graph.md` · `docs/data-sources.md` · `docs/plan/player-first-roadmap.md` · `docs/team/ownership.md`.
