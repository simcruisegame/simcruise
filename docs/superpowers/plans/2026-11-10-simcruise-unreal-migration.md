# SimCruise Unreal Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the Godot shipping game with a complete native Unreal Engine SimCruise while preserving its real-place driving experience and calibrating each retained car against real-world evidence.

**Architecture:** Add a native Unreal C++ client at the repository root while keeping the Godot client as a temporary reference. Keep the existing Bun atlas helper as the only network-facing process; port its client boundary, bounded cache, generated world, game systems, and vehicle dynamics into Unreal in independently reviewable stages. Retire Godot only after feature parity, vehicle calibration, and platform release gates are met.

**Tech Stack:** The stable Unreal Engine version installed for this project, Unreal C++, Unreal JSON/asset systems, the existing Bun helper, and existing JSON car definitions and licensed content where their provenance permits reuse.

**Spec:** [2026-11-10-simcruise-unreal-rewrite-design.md](../specs/2026-11-10-simcruise-unreal-rewrite-design.md)

## Global Constraints

- The final game client is Unreal; Godot is not required to run or package it.
- Pin the installed stable Unreal version; do not target a preview release.
- Keep `helper/` engine-neutral and preserve its public-data source policies, provenance, rate limits, retries, and bounded cache.
- Keep all three cars data-driven; retain published evidence and label measured, estimated, and unknown values.
- Use custom, SI-unit vehicle dynamics and measured calibration gates; Unreal's default vehicle tuning is not evidence of real-car accuracy.
- Keep tile/network/decoding work off the render-critical game thread, with bounded memory and stale-result handling.
- Windows and macOS remain target platforms; measure performance and verify packages on each native platform.
- Preserve licensing, privacy, and no-account/no-telemetry rules.
- Preserve the current user-modified Godot/import files while the rewrite is in progress; do not overwrite them as generated migration output.

## Review Focus

- A helper timeout, bad JSON response, or offline cache hit must not hang driving or discard already-built tiles.
- A new search or rapid drive must not let late tile jobs from an old location overwrite the active world.
- Tile coordinate conversion at distant US locations and across tile edges must not create visible seams or physics scale drift.
- Tire forces under combined braking and cornering, wheel lock/spin, and changing normal load must remain stable across the selected fixed physics step.
- Missing, malformed, or unlicensed source assets must fail visibly in development and must not silently ship as a substitute.

## File and component map

- Create root Unreal project metadata under `SimCruise.uproject`, `Config/`, and `Source/SimCruise/` after the installed engine version is known.
- Keep the helper contract in `helper/`; add an Unreal process adapter under `Source/SimCruise/Private/Atlas/` and shared response models under `Public/Atlas/`.
- Add vehicle data conversion and runtime types under `Source/SimCruise/Public/Vehicle/` and `Private/Vehicle/`; retain `game/cars/*.json` as canonical inputs until the importer proves lossless conversion.
- Add generated tile/world code under `Public/World/` and `Private/World/`; avoid copying generated Godot import metadata into Unreal content.
- Add Unreal assets under `Content/` only after asset-format, license, and model-rig checks; retain source credits in `licenses/` and `Credits.txt`.
- Keep the existing Godot runtime and its uncommitted local edits intact until the final migration gate.

---

### Task 1: Establish the Unreal project and reproducible build

**Files:**
- Create: `SimCruise.uproject`
- Create: `Config/DefaultEngine.ini`, `Config/DefaultGame.ini`
- Create: `Source/SimCruise/SimCruise.Build.cs`, `Source/SimCruise/Private/SimCruise.cpp`, `Source/SimCruise/Public/SimCruise.h`
- Create: `Source/SimCruise.Target.cs`, `Source/SimCruiseEditor.Target.cs`
- Create: `docs/unreal-building.md`

**Interfaces:**
- Consumes: the locally installed stable Unreal Engine and its supported Visual Studio toolchain.
- Produces: a root `SimCruise.uproject` that opens and builds from a clean checkout without Godot.

- [x] Record the installed stable engine build: UE 5.8.4, changelist 58902052; document the compatible C++ toolchain requirements.
- [x] Create a native C++ project with a single `SimCruise` runtime module and editor target; set the project association to the installed 5.8 line.
- [x] Configure baseline renderer/input settings and leave the requested editor MCP plugins disabled pending the local access confirmation.
- [x] Document the engine/toolchain prerequisites and command-line build/launch steps in `docs/unreal-building.md`.
- [x] Verify the Unreal Editor target builds and launches from the root project without relying on the Godot installation. `Build.bat SimCruiseEditor Win64 Development -Project=D:\simcruise\SimCruise.uproject -WaitMutex` succeeded, and an unattended game launch loaded all three retained car definitions.

### Task 2: Port car definitions without losing evidence

**Files:**
- Create: `Source/SimCruise/Public/Vehicle/CarDefinition.h`
- Create: `Source/SimCruise/Private/Vehicle/CarDefinition.cpp`
- Create: `Source/SimCruise/Public/Vehicle/CarDefinitionLoader.h`
- Create: `Source/SimCruise/Private/Vehicle/CarDefinitionLoader.cpp`
- Preserve as inputs: `game/cars/nb1999.json`, `game/cars/mustang65.json`, `game/cars/meridian_z.json`

**Interfaces:**
- Consumes: the existing JSON schemas and `game/CarSpec.gd` unit/default semantics.
- Produces: `FSimCruiseCarDefinition` and a loader that returns either a complete validated definition with source annotations or a path-specific error.

- [x] Map every current car field to an explicitly unit-labelled C++ property; preserve `sources`, `sources.estimates`, and `calibration_open` without flattening or dropping them.
- [x] Load all three retained definitions and report missing required fields, invalid units/ranges, and missing model identifiers.
- [x] Keep JSON as the canonical authoring format until round-trip comparison shows no lost or silently defaulted fields.
- [ ] Verify each retained car loads and that an invalid definition produces a useful error rather than a partially initialized car.

### Task 3: Build the native vehicle dynamics core

**Files:**
- Create: `Source/SimCruise/Public/Vehicle/SimCruiseVehicleMovementComponent.h`
- Create: `Source/SimCruise/Private/Vehicle/SimCruiseVehicleMovementComponent.cpp`
- Create: focused implementation files under `Source/SimCruise/Private/Vehicle/` for tires, suspension, powertrain, braking, and vehicle state.
- Create: an isolated flat test-pad map under `Content/Maps/VehicleBench/`.

**Interfaces:**
- Consumes: `FSimCruiseCarDefinition` from Task 2 and a Chaos rigid-body component.
- Produces: a fixed-step simulation component with four explicit wheel/contact states and a narrow force-application adapter to Chaos.

**Progress:** The first ported physics slice lives in `SimCruiseVehicleDynamics.h` plus the suspension, tire, and powertrain model implementation files. It preserves the Godot spring/damper and anti-roll equations, relaxed slip-angle response, load-sensitive combined tire force with wheel inertia, and torque-bias differential splits in SI units. The vehicle core (branch `unreal/vehicle-core`) puts the full Powertrain.gd model on Chaos: `ASimCruiseVehicle` and `USimCruiseVehicleMovementComponent` run it in a Chaos sim callback on the physics thread at a fixed 360 Hz; see `docs/unreal-architecture.md`. The flat test pad is spawned at runtime by the automation benches, so no `Content/Maps/VehicleBench` map is needed.

- [x] Port the spring-rate, damper, anti-roll, relaxed-slip, combined-tire-force, wheel-inertia, and FWD/RWD/AWD differential calculations as SI-unit model helpers.

- [x] Spike the installed engine's supported physics callback/substep path and prove per-wheel forces can be evaluated safely at the chosen rate before committing to that integration point. Done: Chaos `TSimCallbackObject::OnPreSimulate_Internal` at the async fixed step; `SimCruise.Vehicle.Chaos.ForceIntegration` and `Convergence.<car>` chose 360 Hz.
- [ ] Implement SI-unit suspension, wheel angular inertia, tire slip state, combined longitudinal/lateral grip with load sensitivity, transient relaxation, and aligning moment using a data-driven tire law. Done except the aligning moment (next, with force feedback).
- [x] Implement engine/motor, clutch/torque converter, gearbox, differential, service/parking brakes, ABS, traction control, aerodynamic drag, rolling resistance, and reverse-selection interlock from car data.
- [x] Keep contact and force calculations deterministic for the same definition, controls, initial conditions, and fixed-step sequence.
- [x] Verify force direction, unit conversion, static load balance, wheel contact, low-speed gear behavior, and bounded behavior under tire saturation before adding world complexity.

### Task 4: Calibrate retained cars against road-test evidence

**Files:**
- Create: `Source/SimCruise/Private/VehicleBench/` fixtures and repeatable bench drivers.
- Create: `docs/vehicle-calibration.md`.
- Update only with evidence: `game/cars/*.json` and `docs/cars.md`.

**Interfaces:**
- Consumes: the vehicle component from Task 3 and the published targets and procedures in `docs/cars.md`.
- Produces: reproducible calibration reports that identify source values, estimated values, tolerances, and open data gaps for each retained car.

- [x] Recreate the existing 0–60 mph, 70–0 mph, skidpad, reverse interlock, lift-off, and trail-braking procedures with matching starting conditions and measurement definitions.
- [x] Preserve the existing tolerance bands unless a primary source justifies a change; retain each car's documented exception/open item.
- [ ] Add combined-slip, step-steer/S-bend, bump/contact, and timestep-convergence procedures where defensible real targets or stability criteria exist. Timestep convergence done (`SimCruise.Vehicle.Convergence.<car>`); the others remain.
- [x] Tune only fields explicitly marked estimated; do not scale published engine torque or hide gaps behind a visual-feel adjustment.
- [x] Verify all three cars independently and record the data provenance and remaining unknowns in the report. Results and the one Unreal calibration overlay are in `docs/cars.md` ("The Unreal client's benches").

### Task 5: Connect Unreal to the existing atlas helper

**Files:**
- Create: `Source/SimCruise/Public/Atlas/SimCruiseAtlasClient.h`
- Create: `Source/SimCruise/Private/Atlas/SimCruiseAtlasClient.cpp`
- Create: focused response types under `Public/Atlas/` and `Private/Atlas/`.
- Modify only if needed: `helper/` command protocol and packaging scripts.

**Interfaces:**
- Consumes: the current helper command/JSON/PNG contract and cache directory rules.
- Produces: an asynchronous request API returning typed results, errors, and generated-file paths without opening network connections in Unreal.

- [ ] Inventory actual helper commands, response shapes, executable packaging, cache paths, rate limits, retries, and provenance fields before writing the adapter.
- [ ] Launch one helper job per request through a platform abstraction; capture stdout/stderr, timeout/exit status, cancellation, and malformed-response errors.
- [ ] Preserve warm-cache/offline behavior and ensure concurrent requests do not exceed the helper's established limits.
- [ ] Verify helper failure never blocks the game thread and that valid cache results continue to work while offline.

### Task 6: Stream a real location with bounded world tiles

**Files:**
- Create: `Source/SimCruise/Public/World/WorldTileManager.h`
- Create: `Source/SimCruise/Private/World/WorldTileManager.cpp`
- Create: typed tile data, coordinate conversion, floating-origin, terrain, and road builder files under `Public/World/` and `Private/World/`.
- Create: `Content/Maps/Driving/` starter map and `Content/Maps/VehicleBench/` fixtures as needed.

**Interfaces:**
- Consumes: typed atlas responses from Task 5 and the vehicle collision contract from Task 3.
- Produces: a bounded set of local-coordinate world tiles with drivable road collision, stable tile ownership, and explicit cancellation/retirement.

- [ ] Port Web Mercator tile math and local origin conversion with documented axis, handedness, height, and meter-to-centimeter boundaries.
- [ ] Construct the offline Miami Beach starter area and one searched US location from helper output; road surfaces and collision must share the fitted grade/crossfall source.
- [ ] Port the helper-backed world layers that make locations recognizable: elevation and regional terrain, aerial imagery, graded/banked roads and MUTCD lane markings, sidewalks, buildings and roofs, land cover and tree canopy, guard rails, and mapped street objects/signals.
- [ ] Build terrain/road data off-thread and create Unreal objects on supported engine threads; use instancing for repeated foliage/props.
- [ ] Bound tile count, generated mesh memory, image residency, and cache use; cancel stale work and reject results whose search/world generation is no longer active.
- [ ] Verify cold start, warm-cache drive, offline starter area, cross-tile roads, road/terrain collision, and a second US search.

### Task 7: Port the driving experience and world systems

**Files:**
- Create: separate components under `Source/SimCruise/Public/Gameplay/` and `Private/Gameplay/` for search, traffic, weather, cameras, controls, settings, and HUD.
- Create: Unreal UI assets under `Content/UI/` and input assets under `Content/Input/`.
- Reference: `game/Drive.gd`, `game/BuildingStream.gd`, `game/AmbientLife.gd`, `game/GraphicsQuality.gd`, and feature details in `README.md` / `docs/technical-overview.md`.

**Interfaces:**
- Consumes: the tile manager, atlas client, car definitions, and vehicle component.
- Produces: parity for address search, keyboard/Xbox controls, automatic/manual driving, cockpit/chase/scenic cameras, road atlas, HUD, traffic/signals, weather, graphics presets, and cache controls.

- [ ] Port controls and camera/HUD interactions while keeping the listed keyboard and Xbox mappings stable; retain manual/automatic modes, recovery-to-road, camera switching, and the full-screen road atlas.
- [ ] Port address/place search, the verified-start pool and Surprise Me behavior; atlas/search transitions must not race existing tile jobs.
- [ ] Port traffic behavior, mapped signals, birds, nearest-station METAR weather, and cache controls on worker-safe boundaries.
- [ ] Port graphics quality settings with measured Low-to-Ultra scalability, including the current 8 GB GPU target class.
- [ ] Preserve the no-account, no-key, no-telemetry client behavior and expose data-source/cache provenance through documentation and development diagnostics.
- [ ] Verify every feature from the README inventory is implemented or recorded as blocked by specific source-data/platform evidence.

### Task 8: Migrate appearance, content, and audio with provenance

**Files:**
- Create: Unreal materials, meshes, textures, sounds, and presentation assets under `Content/`.
- Update as assets are accepted: `licenses/`, `Credits.txt`, and the Unreal content manifest/documentation.
- Reference: `authoring/`, `game/art/`, `game/data/`, and `docs/data-sources.md`.

**Interfaces:**
- Consumes: reviewed source assets and their existing licenses/credits.
- Produces: Unreal-ready vehicle/world content with documented origin and reproducible import settings.

- [ ] Audit every migrated model, texture, font, sound, aerial, map-derived feature, and third-party dependency for format and redistribution rights.
- [ ] Import car bodies, separable/animated wheels, cockpit, lights, materials, road surfaces, buildings, foliage, sky/weather, and sounds without losing attribution.
- [ ] Replace or clearly flag content that cannot legally or technically migrate; do not silently substitute invented or unlicensed assets.
- [ ] Compare fixed-pose captures for the Miata, road, and reference locations against existing licensed reference captures.

### Task 9: Complete cross-platform parity and retire Godot

**Files:**
- Update: `README.md`, build/install docs, release packaging, and CI workflows for Unreal.
- Remove only after the gates pass: Godot project/runtime files and import metadata that have no retained research or asset value.
- Preserve: helper, car source data/provenance, useful research, licenses, and any reusable source assets.

**Interfaces:**
- Consumes: all Unreal systems above and clean Windows/macOS build environments.
- Produces: the Unreal-only shipping client and release/build instructions, with Godot no longer required at runtime or build time.

- [ ] Build and launch a clean checkout without Godot on native Windows and macOS systems.
- [ ] Re-run the complete feature inventory, all car calibration gates, offline/warm-cache/world-streaming gates, provenance audit, and performance captures.
- [ ] Build a migration ledger covering every runtime file under `game/`, every scene/shader, every script under `game/checks/`, and every helper command; classify each as ported, intentionally replaced, or retained as reference with evidence before removing Godot.
- [ ] Verify the Unreal package includes the helper and required content, preserves privacy/no-account behavior, and does not bundle build-only Godot artifacts.
- [ ] Switch primary documentation/release packaging to Unreal, then remove obsolete Godot runtime/import files while retaining useful references and research.

## Source-to-target traceability

The current checkout contains 71 top-level Godot runtime scripts, 20 scene/shader files, and 71 scripts under `game/checks/`. The following are the source anchors; the migration ledger in Task 9 must extend this mapping to every runtime and check file before Godot is removed.

| Current source area | Authoritative source files | Unreal destination / evidence |
|---|---|---|
| Car data and dynamics | `game/CarSpec.gd`, `game/cars/*.json`, `game/Powertrain.gd`, `game/CarSuspension.gd`, `game/NB1999Sport.gd`, `game/DrivingPreferences.gd`, `game/CarWear.gd` | `Source/SimCruise/{Public,Private}/Vehicle/`; preserve definitions and calibration evidence, then pass each road-test and stability gate |
| Car presentation | `game/CarBody.gd`, `game/CarFinish.gd`, `game/ModelRig.gd`, `game/Cockpit.gd`, `game/SportWheel.gd`, `game/ViewLens.gd`, `game/InlineFourSound.gd` | `Content/Vehicles/` plus presentation/audio components; prove rig dimensions, cockpit view, wheel/light behavior, and repeatable turntable captures |
| Core drive and tile lifecycle | `game/Drive.gd`, `game/Drive.tscn`, `game/WorldTile.gd`, `game/StartPool.gd`, `game/CaptureSite.gd` | `Source/SimCruise/Private/{World,Gameplay}/` and `Content/Maps/`; prove offline start, random-start pool, search transitions, cancellation, and bounded tile retirement |
| Roads and street geometry | `game/RoadSurface.gd`, `game/RoadLayout.gd`, `game/RoadMap.gd`, `game/RoadBankStream.gd`, `game/RoadBankMesh.gd`, `game/RoadCrossfallGround.gd`, `game/LaneMarkings.gd`, `game/Junction.gd`, `game/RoadEdges.gd`, `game/BridgeDeck.gd`, `game/GuardRails.gd`, `game/Roadside.gd`, `game/StreetDetail.gd` | `Source/SimCruise/Private/World/Roads/`; prove fitted grade/crossfall, lane rules, intersections, collision, seams, rails, and street-object placement |
| Buildings, terrain, and vegetation | `game/BuildingStream.gd`, `game/HouseRecipe.gd`, `game/MappedStorefront.gd`, `game/ObservedBuildings.gd`, `game/LidarRoof.gd`, `game/PhotoFacades.gd`, `game/ForestForm*.gd`, `game/Grove.gd`, `game/RegionalTerrain.gd`, `game/Rangeland.gd`, `game/SummitSnow.gd`, `game/Survey*.gd`, `game/SpectralGround.gd`, `game/TerrainEdges.gd`, `game/MeasuredGranite.gd` | `Source/SimCruise/Private/World/{Buildings,Terrain,Vegetation}/` and `Content/World/`; prove source-layer coverage, surveyed geometry, arrival-order stability, and fixed-pose visual comparisons |
| Traffic, signals, weather, and ambience | `game/AmbientLife.gd`, `game/TrafficDirector.gd`, `game/SignalControls.gd`, `game/DaylightWeather.gd`, `game/ArchiveWeather.gd` | `Source/SimCruise/Private/Gameplay/{Traffic,Weather}/`; preserve mapped signal behavior, traffic flow/personas, birds, current/archive weather, and lighting provenance |
| User interface and quality | `game/Hud.gd`, `game/MapHUD.gd`, `game/GraphicsQuality.gd`, plus the project input settings | `Source/SimCruise/Private/Gameplay/UI/`, `Content/UI/`, `Content/Input/`; prove search, atlas, HUD, controls, settings, and performance tiers |
| Public data and cache | `helper/atlas.mjs`, `helper/atlas-cache.mjs`, all imported helper modules and manifests under `helper/` | Keep `helper/` as the engine-neutral network boundary; prove every command remains reachable through the Unreal adapter and preserve source licensing, retry, rate-limit, and eviction behavior |
| Calibration and behavior suite | Entire `game/checks/` tree, including `CarTrack.gd`, `LiftOff.gd`, `CombinedSlip.gd`, `TractionControlCheck.gd`, `LaneMarkingCheck.gd`, `SeamCheck.gd`, `TrafficCheck.gd`, `VegetationCheck.gd`, `ReferencePose.gd`, and `QualityBench.gd` | Port each check's intent and measurement definition into an Unreal automation/bench equivalent or document an evidence-backed replacement; do not drop a check just because its Godot harness is being retired |

## Handoff notes

- The current checkout was fetched from `origin/main`; `HEAD` and fresh `FETCH_HEAD` both resolve to `6cadf05422cde2c954276137b39c3ca1d9b47e80` (`Next-level plan of 10 November 2026: audited proposals and agreed order`). No newer upstream commit was available at planning time.
- The working tree contains pre-existing modified Godot `.import` files and `game/project.godot`, plus untracked `scripts/simcruise/`; preserve these. The design and this plan are also untracked planning artifacts.
- `Engine/Build/Build.version` now confirms installed UE `5.8.4`, changelist `58902052`, branch `++UE5+Release-5.8`, and `Engine/Binaries/Win64/UnrealEditor.exe` is present. The Launcher UI still shows `Installing` at 82%, so wait for it to finish before opening the editor or generating the project.
- No Visual Studio installation or `vswhere.exe` is present in the standard Windows locations checked; confirm the C++ toolchain before generating a code project.
- The engine includes `Engine/Plugins/Experimental/ModelContextProtocol/ModelContextProtocol.uplugin` and `Engine/Plugins/Experimental/Toolsets/AllToolsets/AllToolsets.uplugin`. No Unreal Editor process, project `.uproject`, or live Unreal MCP toolset is available yet; configure the requested skill in the new project and verify a read-only tool call after install completes.
- Confirm the installed `UE_5.8` build/version and resolve the final physics integration point with a minimal engine-backed spike before locking project metadata.
