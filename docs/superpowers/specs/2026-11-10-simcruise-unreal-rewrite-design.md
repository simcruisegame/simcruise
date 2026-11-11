# SimCruise Unreal Rewrite Design

**Status:** Draft for owner review  
**Date:** 2026-11-10

## Intent

Replace the Godot game client with a native Unreal Engine game. The goal is a full SimCruise experience in Unreal: enter a US place, stream and build the surrounding real-world scene, and drive the retained cars with handling grounded in published specifications and road-test measurements. The current repository is the behavior, data, asset-license, and calibration reference during the rewrite.

This is a project-sized rewrite. The existing Godot client remains available as a temporary reference while the Unreal client is being built. The end state ships Unreal as the game; it does not depend on Godot at runtime.

## Goals

- Preserve the experience documented in `README.md`: US place search, streamed roads and terrain, buildings and vegetation, mapped traffic, weather, road atlas, cockpit/chase/scenic cameras, HUD, keyboard/Xbox input, graphics presets, offline starter area, and local cache.
- Retain the three current cars (1999 Mazda MX-5 Miata, 1965 Ford Mustang 289, Corvette C8), their source notes, and data-driven specifications.
- Match each car to the best available real evidence. Preserve the existing acceleration, braking, skidpad, and stability procedures as migration baselines, then add procedures where the source data supports them. Mark estimated values and open data gaps instead of presenting them as measured facts.
- Preserve the current public-data, licensing, privacy, and bounded-cache rules: no keys, accounts, telemetry, or paid services; keep source and derived-data provenance visible in the development process.
- Retain Windows and macOS as target platforms, subject to validation on each platform and Unreal's supported features.
- Preserve scalable graphics presets so the game remains usable on the current class of 8 GB GPU as well as higher-end systems.

## Non-goals

- Multiplayer, online services, or a new account system.
- Replacing public map-data sources without an evidence or licensing reason.
- Treating Unreal's default vehicle tuning as proof of real-car accuracy.
- Claiming perfect replication where tire, damper, engine, alignment, or instrumented test data is not public or has not been measured.
- Rewriting the standalone Bun atlas helper merely to change languages. Its engine-neutral API, cache, rate limits, and data transforms can remain in place.

## Approaches considered

1. **Complete Unreal rewrite (selected).** Build a new Unreal client and make it the only shipping game after parity gates pass. Reuse only engine-independent data, helper behavior, licensed source assets, and measured baselines. This matches the owner's request and avoids a permanent dual-engine product.
2. **Godot/Unreal bridge.** Keep the Godot client as the live renderer or simulation while Unreal supplies selected systems. Rejected because it does not switch the game to Unreal and creates a fragile cross-engine runtime boundary.
3. **Improve Godot in place.** Continue enhancing the current client and reconsider Unreal later. Rejected because it does not meet the requested engine change.

## Architecture proposal

### Project and code

- Create a root-level `SimCruise.uproject`, with an Unreal C++ module under `Source/SimCruise/`, engine configuration under `Config/`, and migrated runtime content under `Content/`.
- Use C++ for physics, map-data orchestration, tile construction, caching, and performance-sensitive systems. Use Blueprints and Unreal assets for editable vehicle presentation, UI composition, material instances, and scene setup.
- Pin the exact installed Unreal stable release in project metadata and build documentation once the owner's download is available. Do not target a preview release.
- Retain Godot source during migration as a read-only behavioral reference. After Unreal parity, remove Godot-only project files and import metadata from the shipping tree; migrate or retain reusable licensed media and data with their credits.

### Map data and world streaming

- Keep `helper/` as a separate Bun-built atlas process. The Unreal client will launch it through a platform abstraction, pass the existing commands and cache paths, parse its JSON response, and consume generated PNG/JSON files. The helper continues to own network access, rate limiting, retry behavior, licensing provenance, and cache eviction.
- Port the existing bounded tile lifecycle into a native Unreal tile manager. Use local tile coordinates and a floating origin so arbitrary US coordinates do not lose precision. Generate terrain, graded road surfaces, markings, sidewalks, buildings, trees, and other tile features from helper output on worker tasks; create or update engine objects on the game thread.
- Use instanced rendering for repeated trees and props. Keep generated road collision authoritative for car contact. Use explicit runtime tile lifetimes for the unbounded, data-generated map instead of treating authored World Partition cells as the only source of streamed roads.
- Port search, cache controls, weather, traffic, camera modes, map atlas, HUD, and input as separate Unreal systems with narrow interfaces.

### Vehicle physics and realism

- Keep the current JSON car files as the source dataset until a versioned Unreal importer or runtime parser preserves every field and its source annotation.
- Implement a native vehicle-dynamics component around an Unreal Chaos rigid body. The component owns four wheel/contact states, spring and damper loads, suspension geometry, tire slip, combined longitudinal/lateral grip, wheel inertia, brakes, ABS/traction control, engine or motor, clutch, gearbox, differential, and aerodynamic/road-load forces.
- Use a custom, data-driven tire force law (Magic Formula or TMeasy) with combined slip, load sensitivity, transient relaxation, and aligning moment. Do not rely on default wheel friction curves as the simulation truth. Unreal documents Chaos Vehicles as a lightweight system; its current simple wheel configuration describes tire forces using a simplified friction model. The modular vehicle modules remain a candidate integration point only if they can host the measured tire and suspension model without hidden force behavior.
- Keep calculations in SI units. Convert at the Unreal boundary and explicitly test mass, length, force, torque, angle, inertia, and coordinate-system conversions.
- Run the solver at a fixed asynchronous physics interval with explicit substeps; choose the final rate by convergence and CPU measurements on the target machine. Unreal's substepping guidance notes stability gains and additional CPU cost, so the rate is a measured decision, not a guessed setting.
- Build deterministic vehicle benches from the current tests. At minimum gate the three kept cars on 0–60 mph, 70–0 mph distance, skidpad, reverse-selection interlock, and lift-off/trail-braking stability. Add step-steer, S-bend, tire combined-slip, bump/contact, and tick-rate convergence measurements as their real-world targets and procedures are documented.
- Preserve the current target bands unless stronger primary measurements justify changing them. The existing car guide gives broad bands of ±0.15 s, ±5 ft, and ±0.02 g; the 1999 Miata's Track bench has tighter targets and must keep its documented band. Do not recalibrate from visual feel alone.

### Visual and audio realism

- Port the current real-location geometry, observed building and roof data, road profiles, forests, aerials, material provenance, traffic, and weather rather than replacing them with a hand-built showcase map.
- Use Unreal's physically based materials, sunlight/sky, shadows, reflections, fog, and scalable global illumination to improve depth and material response. Keep the single sun consistent with the selected place and time; compare fixed-location captures against the existing licensed references.
- Rebuild vehicle material roles, interiors, wheel motion, lights, sound, and cameras using the existing licensed models and textures where the mesh format and license permit. Record any asset that needs replacement instead of silently changing its provenance.
- Keep NPC and traffic behavior algorithmic and off any generative-AI frame path. AI remains optional and may assist only with offline or background content where the existing provenance and licensing rules can be enforced.

## Requirements and constraints

- The final game client is Unreal; Godot is not required to run or package it.
- The atlas helper's network policy and existing public-data source licenses remain in force.
- All three cars must be data-driven; car-specific tuning must not leak into one shared magic set of runtime constants.
- Physics claims must identify their source and separate measured, published, estimated, and unknown values.
- Tile generation must remain bounded in memory and avoid blocking the frame on network, image decoding, or mesh generation.
- Windows and macOS remain intended builds. Performance and rendering quality must be measured separately on available hardware rather than inferred from editor screenshots.
- No Unreal Editor MCP plugin or project configuration will be enabled until the Unreal project path and installed engine are known. Once configured, use Epic's `unreal-mcp` skill for editor actions and verify each editor operation's result.

## Proposed work order

1. Record the installed Unreal version and create a minimal native project and build pipeline.
2. Port the JSON car data and implement the vehicle solver plus deterministic road-test benches on a flat test surface. Resolve scale, coordinate, time-step, contact, and force-application behavior before building the full world.
3. Port the atlas-process boundary, cache, address search, and one generated tile; verify tile coordinates, road elevation, collision, and car contact at a fixed reference location.
4. Port the remaining world layers and gameplay systems: terrain, roads and markings, buildings, vegetation, traffic, weather, map atlas, controls, cameras, HUD, audio, and graphics settings.
5. Migrate and license-check the full content library, complete Windows/macOS packaging, and compare repeatable scene captures and drive procedures against the current references.
6. When all required feature, physics, visual, and platform gates pass, switch the repository's primary build/docs/releases to Unreal and remove obsolete Godot runtime files while retaining useful historical research and reusable assets.

## Acceptance evidence

- A clean Unreal checkout builds and launches the game without Godot installed.
- Each of the three vehicles loads its own sourced spec and passes its documented road-test bands in the Unreal bench.
- A cold start and a warm-cache drive to the Miami Beach starter area work; address search can stream a second US location; offline behavior and cache bounds still work.
- The feature inventory in `README.md` is accounted for, with each item implemented or explicitly reported as blocked by missing source data or platform support.
- Fixed-pose captures for the Miata, road, and representative locations are reproducible and reviewed against the current reference captures.
- The helper and client preserve source licensing, privacy, retry/rate-limit behavior, and cache limits.
- Windows and macOS release builds are verified on their native platforms before the Godot client is retired.

## Builder decisions left open

- The exact installed Unreal version, compiler/toolchain, and required plugins are determined after the download finishes.
- The final physics callback or Chaos integration point is selected only after a minimal force/substep spike proves that custom per-wheel forces can be evaluated on the physics thread at the required rate.
- Any platform or performance tradeoff that prevents the stated Windows/macOS and 8 GB GPU goals must be surfaced before narrowing the product scope.

## References

- [Epic: Chaos Vehicles](https://dev.epicgames.com/documentation/unreal-engine/chaos-vehicles)
- [Epic: `FSimpleWheelConfig`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/ChaosVehiclesCore/FSimpleWheelConfig)
- [Epic: Physics Sub-Stepping](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-sub-stepping-in-unreal-engine)
- [Epic: World Partition](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine)
- Repository evidence: `README.md`, `docs/technical-overview.md`, `docs/cars.md`, `docs/research/plan-2026-11-10/00-index.md`, and `docs/research/detail-engine.md`.
