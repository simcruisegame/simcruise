# Unreal client architecture

How the Unreal client is put together, one section per area. The Godot client under `game/` stays the behavioural and calibration reference until the Unreal client reaches parity; see the [rewrite design](superpowers/specs/2026-11-10-simcruise-unreal-rewrite-design.md) and the [migration plan](superpowers/plans/2026-11-10-simcruise-unreal-migration.md).

## Vehicle physics core

Source: `Source/SimCruise/{Public,Private}/Vehicle/`, tests in `Source/SimCruise/Private/Tests/Vehicle*`.

### The pieces

| Piece | What it is |
|---|---|
| `FSimCruiseCarDefinition`, `FSimCruiseCarDefinitionLoader` | The car file (`game/cars/<id>.json`, canonical) in C++, every field unit-labelled and validated, sources kept. An Unreal calibration overlay (`Content/Vehicle/Calibration/<id>.json`) may replace estimated fields; each overlay states its reason in `_note`, and the bench logs what it changed. |
| `FSimCruiseVehicleSim` (`SimCruiseVehicleSim.h`) | The vehicle model, independent of any engine object: a faithful port of `game/Powertrain.gd` and `game/CarSuspension.gd`. Ray contacts, springs with installed preload, dampers (ratio with a knee, or a shaft-speed dyno table), anti-roll bars, bump stops, camber gain and caster, Ackermann steering with toe, relaxed slip angles with load-dependent relaxation length, the saturating slip-angle curve with camber thrust, the slip-ratio tyre curve with combined slip through a friction ellipse and load sensitivity, wheel inertia solved together with the contact force, locked-wheel sliding, engine torque curve with friction, idle governor and fuel cut, clutch (or converter, or dual clutch) with launch control, automatic (with the 0.45 m/s reverse inhibitor) and manual gearboxes with stalling, torque-sensing differentials (front, rear, centre), service and parking brakes, ABS and traction control on the cars that had them, aerodynamic drag and rolling resistance, plus Godot's steering maps (assisted keys, thumbstick shaping, direct). |
| `SimCruiseDynamics::*` (`SimCruiseVehicleDynamics.h`) | The per-stage formulas the model is made of (suspension load, bar and spring rates, relaxed slip angle, friction coefficient, lateral force, one longitudinal substep, engine curve, differential split), unit-tested on their own. |
| `SimCruiseUnits` (`SimCruiseUnits.h`) | The only place SI meets Unreal: m to cm, N to kg·cm/s², N·m and kg·m² to their cm² forms, degrees, road-test units, and Godot's axes (X right, Y up, −Z forward) to Unreal's (X forward, Y right, Z up). |
| `USimCruiseVehicleMovementComponent` | Puts the model on Chaos. Configures the root body from the car (mass, centre of mass, principal inertia in Unreal axes, Godot's damping, inertia conditioning off, never sleeps) and registers a Chaos sim callback that runs the model on the physics thread before every fixed step. Holds the controls, gearbox commands and telemetry behind one lock. |
| `ASimCruiseVehicle` | The pawn: a simulating box (the car file's collision box, centred `collision_box_y` above the Godot car origin) plus the movement component, with the driving-shell interface. |
| `FSimCruiseVehicleTelemetry` | What HUD, rumble, audio and presentation read: speed, rpm, gear text, manual/reverse/stall/shift state, brake lamps, steering input and rack angle, clutch, throttle after traction control, ABS and TC activity, sound observers (Powertrain.gd's `sound_*`), stranded timer, and per wheel: contact, load, slip ratio and its peak, slip angle, grip use and combined grip use, angular speed and accumulated spin angle, suspension length, steer angle, inclination, tyre forces, surface grip, ABS pressure, contact point. |

### Where the forces are computed

Chaos ticks asynchronously at a fixed step (`Config/DefaultEngine.ini`: `bTickPhysicsAsync=True`, `AsyncFixedTimeStepSize=1/360 s`). Before every step, `FSimCruiseVehicleSimCallback::OnPreSimulate_Internal` (a `Chaos::TSimCallbackObject`) runs on the physics thread:

1. read the particle (`FRigidBodyHandle_Internal`: position, rotation, centre-of-mass velocity, angular velocity, centre of mass) and convert to SI;
2. apply pending gearbox commands and the latest controls; run a bench driver if one is installed;
3. cast the four suspension rays (`UWorld::LineTraceSingleByChannel` on `GroundTraceChannel`, the owner ignored; the same physics-thread query Epic's Chaos Vehicles make);
4. step the model over the interval, splitting it into wheel/driveline substeps no longer than `MaxWheelSubstepSeconds` (1/360 s);
5. add the summed force and its torque about the centre of mass to the particle (`AddForce`, `AddTorque`), converted to Unreal units;
6. publish telemetry.

`SimCruise.Vehicle.Chaos.ForceIntegration` proves the boundary: a known SI force and torque applied this way for 360 steps gives exactly `F·t/m` and `τ·t/I` (within float precision, 1.4e-6 of 0.55 m/s), one callback per step, every step the fixed interval. `SimCruise.Vehicle.Determinism` runs the same launch twice and compares 22 state values bit for bit (all equal). That holds for a single-threaded solver with the controls applied by a physics-thread driver (`SetPhysicsThreadDriver`), as the benches do. Controls set from the game thread are picked up by whichever physics step runs next, so with Chaos on its own threads the step on which a pedal change lands can differ by up to a game frame between runs; replays and anything that must reproduce step for step drive the car from the physics thread.

### Why 360 Hz

Godot evaluated contacts, loads and slip angles once per 60 Hz tick and integrated wheels and driveline in six 1/360 s substeps. In Unreal everything runs at 360 Hz. `SimCruise.Vehicle.Convergence.<car>` runs CarTrack at five rates:

| Rate (body / wheels) | NB 0–60 · 70–0 · skidpad | Mustang | Corvette |
|---|---|---|---|
| 60 / 360 Hz (Godot's scheme) | 7.99 s · 172.9 ft · 0.884 g | 9.01 s · 265.2 ft · 0.699 g | 3.47 s · 151.7 ft · 1.025 g |
| 120 / 360 Hz | 7.99 · 172.4 · 0.884 | 8.94 · 264.0 · 0.698 | 3.47 · 150.7 · 1.024 |
| 240 / 480 Hz | 7.98 · 172.0 · 0.884 | 8.95 · 263.7 · 0.698 | 3.46 · 150.0 · 1.023 |
| **360 / 360 Hz (shipping)** | **7.97 · 171.9 · 0.884** | **8.95 · 263.6 · 0.698** | **3.46 · 150.1 · 1.024** |
| 720 / 720 Hz | 7.97 · 171.8 · 0.884 | 8.94 · 263.4 · 0.698 | 3.46 · 149.6 · 1.021 |

(Mustang with its Unreal brake calibration; see `docs/cars.md`.) From 360 to 720 Hz no result moves by more than a fifth of its calibration band (the test's gate), so 360 Hz is converged; 240 Hz would nearly do, but 360 Hz keeps the wheel substep the cars were calibrated with and leaves margin for curbs and the Corvette's stiff springs. The vehicle's own physics-thread work measured 21–34 µs a step on the target i9-10885H (four traces included), about 8–11 ms of CPU per simulated second per car, around 1 % of one core. A whole bench (engine tick included, single-threaded) runs 15–20 times faster than real time.

Two Godot behaviours turned out to depend on the step and were fixed in the port rather than copied:

- The clutch caps its coupling at the torque that closes the slip gap in one substep. In first and second gear that cap binds below `clutch_stiffness`, so it is part of each car's calibrated launch; it is now evaluated at the calibration substep (1/360 s) instead of the actual one, which keeps launches identical at every rate (0–60 at 720 Hz moved 0.03 s before).
- Godot's 60 Hz contact step let the Corvette's left and right loads swap every tick during its traction-controlled launch (a numerical roll oscillation that cost traction); at 120 Hz and above it is gone and the car reaches 60 mph in 3.46 s instead of Godot's 3.55 s (target 2.8 s, a stated open item).

The port also found a bug in the first Unreal helper: the anti-roll bar rate returned 0 for any bar whose second moment of area is under `UE_DOUBLE_SMALL_NUMBER` (1e-8 m⁴), which is every bar under about 21 mm, including the NB's 11 mm rear bar. That shifted the roll stiffness forward and cost the NB 0.022 g on the skidpad; `SimCruise.Vehicle.Model.SuspensionPreload` now checks both bars against CarSuspension.gd's formula.

### Who owns the body

The movement component owns the root body's Chaos particle and mass properties:

- When the body's physics state is recreated (collision switched off and on, a re-register), the old particle is queued for deletion. The component listens to `OnComponentPhysicsStateChanged`: on *destroyed* it stops stepping the old particle at once, on *created* it configures the new one. Before this, the physics thread kept the old pointer and crashed on its next step (use after free); `SimCruise.Vehicle.Chaos.BodyOwnership` recreates the body mid-drive.
- Anything that makes the game thread recompute the body's mass properties (welding an attached collider into the body, rescaling it, a mass override or physical-material change) would swap in a uniform box's mass, centre and inertia. The physics step compares the particle with the car's values every step and puts them back, logs a warning the first time, and counts it in `MassPropertyRestores` (telemetry; should stay 0). The game-thread copy of the particle keeps the recomputed values until the car is set again, so read mass from the car definition, not from the body. Presentation meshes attached to the pawn should have collision off: a welded collider also changes the body's collision shape, which the restore does not undo.
- Lock order is vehicle lock, then physics scene lock: the physics step holds the vehicle lock across its four scene queries. Never call the vehicle's setters while holding a physics scene write lock.

### Interface for the driving shell

On `ASimCruiseVehicle` (and the movement component), all game-thread calls:

- `SetThrottle(0..1)`, `SetBrake(0..1)` (in automatic, below 0.45 m/s the brake pedal selects reverse and drives, as in Godot), `SetSteer(-1..1, bFromThumbstick)` (+ right; a thumbstick gets Godot's centre shaping and rates), `SetHandbrake(bool)` (rear parking brake), `SetClutch(0..1)`, `ShiftUp()`, `ShiftDown()`, `ToggleManual()`; on the component also `SetFullBrake`, `SetManualTransmission`, `RequestEngineStart`, `SetSteeringAssist` (assisted for keys and sticks, off for a real wheel), `ResetVehicle`.
- `ResetAt(GroundLocation, YawDeg)` puts the car on its wheels, stopped (Powertrain.gd `restart`).
- `GetVehicleTelemetry()` / `GetTelemetry()` return the last physics step (fields above). Manual gear refusals arrive as `LastGearMessage` ("Hold the clutch to change gear", "Stop before changing direction", "Speed is too high for that gear").
- `SetPhysicsThreadDriver(TFunction)` runs code on the physics thread before every step with the model, body and controls (benches, replays, autopilots).

Controls set on the game thread reach the physics thread on its next step; telemetry is at most one game frame old. `SimCruise.Vehicle.Interface` drives a car through this interface only, with Chaos on its own task-graph threads as in the game: launch and upshift, steering right (rack, road wheels, load transfer and yaw all to the right), braking to a stop without ever selecting reverse, manual neutral/clutch/first gear and a stall-free clutch release, the handbrake holding, and a reset.

### Not yet done

- Surface grip comes from `FSimCruiseGroundHit::SurfaceGrip` (1.0 on the bench); the world stream needs to supply Godot's per-tile wheel grip and road roughness (`RoadProfileM`, `RoadProfileRateMps` are in place).
- Contacts are rays as in Godot; a wheel-radius sphere cast for curbs and potholes, and an aligning moment for force feedback, are next.
- The ground query reads the game thread's scene-query structure from the physics thread, as Chaos Vehicles do; streamed road collision must therefore be in place before the car reaches it (the world stream's job).
- Packaging: the car files (`game/cars/`) and calibration overlays (`Content/Vehicle/Calibration/*.json`) are loose JSON, which a cooked build does not stage by default; a packaged build needs them added to `DirectoriesToAlwaysStageAsNonUFS` (or moved into assets), or the Mustang loses its overlay. `SIMCRUISE_CALIBRATION_DIR` is read in development builds only.

## Real-world streaming (`Source/SimCruise/{Public,Private}/{Atlas,World}/`)

The streaming layer turns the atlas helper's tile documents into the drivable
place around the car. The Godot client (`game/Drive.gd`, `WorldTile.gd`,
`RoadSurface.gd`, `Kerbs.gd`, `LaneMarkings.gd`) and the helper
(`helper/road-grade.mjs`) are the behavioural reference; the design choices
and their reasons are ported, not reinvented.

### The helper contract

- `helper/atlas.mjs <command> <cache> <args...>` prints one JSON document on
  stdout (last line) and exits 0; a failure prints its message on stderr and
  exits 1. Commands used here: `tile <x> <y>` (zoom-16 tile: `h` heights on a
  `grid` (64 for bundled, 128 for live v2 data), `road_grade`, `roads`,
  `road_surface` (v2), `bridges` (v2), `pedestrian`, `buildings`, `land`,
  `pois`, `aerial_key`, `crowns`) and `aerial <x14> <y14>` (fetches the
  zoom-14 photograph `a-<x>-<y>.jpg` into the cache). The other commands
  (search, roofs, canopy, weather, ...) are reachable through the same client.
- Coordinates in every row are tile units (0..1, north-west origin, y south).
  Road rows are `[class, oneway, name, points, width m, surface?, "bridge"?]`.
- The helper owns the network, rate limits, retries, provenance and its
  96 MB cache eviction. The Unreal client never opens a connection.
- Offline start: `game/data/<x>_<y>.json` holds the 24 Miami Beach tiles and
  `game/art/a-*.jpg` their photographs; a bundled tile is always read before
  the helper is asked.

### Atlas client (`Atlas/SimCruiseAtlasClient`)

One helper process per request on its own worker thread; at most
`MaxConcurrent` (2) run, the rest queue. Each request has a timeout; timeout
and `Cancel()`/`CancelAll()` kill the process tree. Results are typed:
`None`, `NotConfigured`, `LaunchFailed`, `Timeout`, `Cancelled`, `ExitCode`
(stderr kept), `EmptyOutput`, `MalformedOutput`. Issuing work returns at once
with a future; nothing blocks the game thread. The helper is found as in the
Godot client: `SIMCRUISE_MAP_HELPER`, then `<project>/tools/atlas(.exe)`,
then beside the game binary, then Bun running `helper/atlas.mjs`. The cache
is `<user settings>/SimCruise/map-cache` (`SIMCRUISE_MAP_CACHE` overrides).

### Frame and floating origin (`World/SimCruiseGeo.h`)

Unreal X = east, Y = south, Z = up (centimetres): facing east, south is on
the right, so this is Unreal's own handedness with no mirroring. A drive has
one origin tile and one tile size (the Mercator scale at the start latitude,
`Drive.gd scale_at`); a tile's actor sits at `(tile - origin) * size`. When
the focus is more than `RebaseDistanceM` (2 km) from the origin, the origin
moves to the focus tile, every tile actor moves by whole tiles and
`ASimCruiseWorldStreamer::OnRebased` broadcasts the offset so the car,
cameras and traffic move with it.

### Tile lifetime (`FSimCruiseTileManager`, `ASimCruiseWorldStreamer`)

The 3 x 3 ring around the car's tile is wanted, best first by the Drive.gd
score (40 % distance from the car, 60 % from where it will be in 5 s); tiles
beyond the ring plus one are retired; at most 2 build at once and never two
edge neighbours together (see seam contracts). Every job carries the
generation of the place it was started for and a serial: a result for an
older place, a tile no longer wanted or a superseded job is dropped. A
failed tile waits 15 s. Tiles are read and built on worker threads (a helper
result is parsed and built on the thread pool, never in the future's
continuation, which can run on the game thread); the game thread installs one
finished tile per frame (mesh sections, collision, materials). Collision is
cooked asynchronously, so a tile is drawn a few frames before it can be hit:
`AreTilesReadyAround` only reports a tile once a trace through its centre hits
its collision, so a car placed after it lands on the road. Tiles of one
zoom-14 photograph that build together run the helper's `aerial` command
once; the others wait for that file.

### Roads (`World/SimCruiseRoadNetwork`)

1. Pieces in tile metres; ends on the tile edge are exact, and they are
   never chained to another piece there (they continue in the neighbour).
2. Chains through junctions (the straightest pair joins; road-grade.mjs).
   A small closed ring (a cul-de-sac bulb) becomes a paved pad.
3. Plan view: each mapped vertex is filleted (edge-authority's
   `chainVertices`: at most 1 m off a gentle bend, a radius the whole
   cross-section can follow on a sharp one), then a local-linear smoothing
   (sigma 4 m) removes the 0.6 m quantisation of zoom-14 vector tiles. A bend
   the fillet cannot round, or a sharp joint between pieces, is a corner:
   the ribbon ends and the next begins, each running on by its half width.
4. Profile: robust Gaussian local-linear fit of the ground under the
   centreline (sigma 16 m, one Tukey pass), 2 % crown, superelevation from
   curvature on 60/90 km/h classes (AASHTO, halved for side friction, <= 6 %).
5. Seams: a road end on a tile edge either takes the **seam contract** of the
   already built neighbour (point, direction, width and the three
   carriageway heights, blended in over 10 m) or, on the first tile, is lifted
   just clear of the shared ground across its width. The two tiles of a seam
   therefore build one cross-section, whichever came first. Bundled and
   streamed tiles come from different zoom-14 vector tiles whose lines cross
   a shared edge 0.2-0.3 m apart; without contracts the curbs stepped.
6. Junctions: ribbons are joined major first. Where a lower ribbon overlaps
   one already built, its stations are refined to 0.5 m and all five
   carriageway columns take that surface (and stay under its edge just
   outside it), fading back to its own profile over 15 m. Pinned seam ends
   are never moved by a join.
7. Beyond the paved edge: curb and gutter with a sidewalk where a mapped
   sidewalk or footway runs alongside (18 in gutter, 6 in face, sidewalk
   draining 2 % to the street), a paved edge in town, a gravel shoulder in the
   country, each with a skirt. A side stops where another carriageway crosses
   it (junction corners, driveways), found to 3 cm by bisection, with an end
   cap on the curb.
8. Markings (MUTCD Part 3 as in LaneMarkings.gd, with US default lane
   counts): double yellow centre lines on two-way major roads, broken white
   lane lines (3.05 m / 12.2 m, anchored to the world along a canonical
   direction so dashes run on across seams), white and yellow edge lines on
   arterials; none on local streets. A stripe stops where a higher road or a
   road passing through crosses it, and carries on across a T.

### Terrain, water, buildings (`World/SimCruiseTileBuilder`)

- Terrain: the tile grid upsampled to ~2.2 m (exact on the helper's
  triangles, so seam vertices equal the neighbour's). Every vertex under a
  paved footprint sits 8 cm under its lowest surface and the ground fades
  back to the elevation model over 6 m. Seam vertices always keep the shared
  ground.
- Water: open-water polygons lower the ground inside them; a water surface
  at mean sea level (NAVD88 - 0.1 m) for the sea, the low ground of the
  polygon for lakes. Pools are shaded through the land-cover mask.
- Buildings: footprints and heights from the tile (walls, flat roof, collision)
  as a massing model until the Godot client's observed facades and LiDAR
  roofs are ported. Wall colours come from one palette for every place, the
  Godot client's 12 common US exterior paints and 4 brick colours
  (`Drive.gd` `house_paints`, `bricks`), chosen per building by id.
- Ground mask (1024 px per tile): vegetation, sand, paved paths, water.

### Materials and textures (`World/SimCruiseWorldResources`)

One HLSL node per surface (ground, asphalt, concrete, paint, water, building)
built in code; `-run=SimCruiseBuildWorldMaterials` saves them under
`/Game/World/Materials` for cooked builds, and an editor build creates them
in memory when the assets are absent. Detail is world-anchored: local
position plus a per-tile anchor (`tile * size mod 64 m`), so textures run on
across seams without half-precision UV loss. Ground: the aerial photograph
with its highlights compressed toward street-level albedo, modulated by the
Godot client's detail photographs per land-cover class (`game/data/grass-detail.jpg`,
`game/data/mineral-ground.png`; their sources are not named in `Credits.txt`
yet). Asphalt: Poly Haven Asphalt Floor
normalised to an aged-asphalt albedo with a luminance bump. Water: six
directional waves, F0 0.02.

### Running it

- `-SimCruiseWorld` starts the bundled Miami Beach drive; `-SimCruiseStart=lat,lon`
  starts elsewhere (helper and network needed).
- `-SimCruisePreview=ocean,collins,aerial,top,seam,drop` adds a capture rig
  (sun, sky, clouds, sky light, fog when the map has none), captures to
  `Saved/Screenshots/SimCruise/` and quits; `drop` drops a 1000 kg box on
  Ocean Drive and logs where it rests.
- Gameplay code: `GetWorld()->GetSubsystem<USimCruiseWorldSubsystem>()->StartDrive(lon, lat, title)`,
  then `ASimCruiseWorldStreamer::SetFocus(carLocation, carVelocity)` each frame,
  `SurfaceHeightAt`, `FindRoadPose`, `OnRebased`.

### Tests (`SimCruise.Atlas.*`, `SimCruise.World.*`)

| Test | Proves |
|---|---|
| `Atlas.ProcessResults` | valid JSON, malformed, empty, exit code, timeout (< 5 s), missing helper, not configured |
| `Atlas.AsyncQueue` | issuing never blocks, at most 2 processes, queued and running jobs cancel promptly |
| `World.TileMath` | lon/lat <-> tile <-> world round trips at six US places, rebasing keeps positions, Drive.gd tile size |
| `World.TileParse` | bundled tile parses; malformed documents fail with the field named |
| `World.OfflineStart` | the 3 x 3 ring of the bundled start streams with the helper switched off; Ocean Drive found, its surface height agrees, and a trace at the spawn pose hits collision as soon as the ring reports ready |
| `World.SeamContinuity` | all 24 bundled tiles built as a drive builds them: terrain, road heights and road edges equal across every seam (0.0 mm), every crossing road matched |
| `World.RoadCollision` | collision under every carriageway vertex, equal to the drawn surface within 1 cm for 99.5 % |
| `World.RoadEdges` | straight roads build straight edges (< 1 mm), sharp corners turn smoothly, no back-and-forth kink over 8 deg on a public road edge (16 deg on service ways); 10 smaller kinks remain, 9 of them on public roads (Alton Road, 6th Street, Washington Avenue, Jefferson Avenue, 16th Street; worst 7.7 deg), the worst one (15.8 deg) on a service way |
| `World.StaleJobs` | older places, tiles left behind and superseded jobs are rejected; retries wait; resident tiles stay bounded |
| `World.Rebase` | in a live streamer, a car past the rebase distance moves the origin once by one whole tile; tile actors, the car, the ground height and the collision under the car move together |
| `World.BuildDeterminism` | the same tile with the same seam contracts, built twice concurrently, gives bit-identical geometry |

### What it looks like (10 November 2026, Cinematic scalability, preview rig lighting)

![Ocean Drive from the driver's seat](images/unreal-world-ocean.jpg)
![Collins Avenue](images/unreal-world-collins.jpg)
![Ocean Drive and Lummus Park from 70 m](images/unreal-world-aerial.jpg)
![Ocean Drive crossing a tile seam, from 35 m](images/unreal-world-seam.jpg)

Against photographs of Ocean Drive the road surface, curbs, sidewalks and
markings read right; the place does not yet: no palms or street trees, no
parked cars, signals or street lights, massing buildings with a uniform
window grid instead of the Art Deco facades (their colours come from the
general US palette, so Collins Avenue shows red brick that South Beach does
not have), and a 2 m/px photograph as the ground. The ocean, Collins and
aerial views were recaptured on 11 November after the palette change; the
seam view is from 10 November. The seam view shows the road, curb and sidewalk running straight
across the tile edge; the line in the ground there is where two zoom-14
aerial photographs of different tone meet.

### Known gaps

- Bridges (`bridges`, v2 decks) are not built yet; bridge roads are left out.
- Live v2 tiles: `road_surface` profiles are not used directly (the ribbon fits
  the benched `h` grid the helper conditioned to the same profile).
- No trees, street furniture, signals or traffic; buildings are massing only.
- Tile builds take 1-9 s on a worker thread (junction refinement and terrain
  conditioning dominate); fine at driving speed, worth profiling.
- Packaged builds need `game/data`, `game/art` and the helper staged.
- A seam's exact position follows build order: the tile built first keeps its
  own road ends (the two vector tiles disagree by 0.2-0.3 m) and the later one
  takes them, so the same place can differ by that much between drives.
- The sea surface is NAVD88 - 0.1 m everywhere (east-coast mean sea level);
  on the Pacific coast mean sea level is closer to NAVD88 + 1 m.
- Surfaces carry no physical materials yet: asphalt, concrete, terrain and
  buildings have the same friction for the car's tyres.
- Each collision section (terrain, asphalt, concrete, buildings) restarts the
  asynchronous cook, so a dense tile cooks up to four times on a worker thread.
## Car presentation (`Source/SimCruise/{Public,Private}/Presentation/`, `Content/Vehicles/`)

The visible car: the downloaded Sketchfab model of each car, fitted to the car's data, cut into moving parts and drawn with physically based materials. It replaces `game/ModelRig.gd`, `CarBody.gd` and the material parts of `CarFinish.gd`. Physics never reads anything from here.

### Pipeline

1. **glTF in at runtime** (`SimCruiseGltfModel`). A small, strict glTF 2.0 reader loads `scene.gltf` + `scene.bin` (or `.glb`) with every node transform applied, in glTF's own space. Unreal's Interchange glTF importer was not used: it is an editor import that writes `.uasset`s, and the rig needs the raw triangles anyway. The glTF stays the only source of truth, so nothing binary has to be merged.
2. **Rig** (`SimCruiseCarModelRig`), a port of `ModelRig.gd` plus the `cars/rig-infra` steering-wheel search, working in the Godot vehicle frame so it can be compared line by line:
   - upright check; wheels by node name (`Front Left Wheel`, `wheel_fl` …) or by the four ground-contact patches, refined from the tyre surfaces (new) so the hub sits at the tyre's true centre and the model's real tyre radius is measured;
   - front/back from the named wheels, else `game/art/traffic/manifest.json`'s `flip` when the model has an entry there, then the rear lamps overrule it (new: a model whose red lenses sit at the front end is turned round; the NB has no manifest entry and is placed by its lamps alone);
   - uniform scale to the car's wheelbase, then (new) a lateral fit so the model's mean track equals the car's (applied for misses of 1.5–10 %);
   - static ride: hubs at `mount_height_m - static_length_m`;
   - wheel cut by **connected parts** wholly inside a tyre cylinder (a suspension arm that reaches outside stays on the body); off-axis parts inboard of the wheel's centre plane, or parts with the caliper role, become the non-spinning **upright** (calipers steer and travel but do not spin);
   - badges: by name (`badge|logo|emblem|brand|invisible|unimportant`), by profile box, and (new, general) by shape and place — a small, thin, detached part on the centreline of the nose or tail, or a small part of another material on a wheel's axle at its outer face (centre-cap logo). Lettering off the centreline (the NB's `MX-5` and `mazda` on the tail panel, the Mustang's `FORD`) needs a profile box;
   - role areas (profile boxes, new): parts wholly inside a box take another role through a copy of their author material, for models that draw several surfaces with one texture atlas (the Mustang's tail-lamp lens segments, so its brake lamps light);
   - reversing lamps (new, general): white lenses (`lamp_front`) behind the axles are reversing or plate lamps and become unlit clear glass, so they do not light with the head lamps;
   - steering wheel: by name, else by geometry (a flat closed ring of 0.24–0.48 m across in the driver's half of the cabin — the side of the car data's driver eye, so right-hand-drive cars work too — facing fore/aft), else (new) by fitting circles to arcs of a rim modelled in pieces; when only the rim was found, the spokes and hub welded to the column are cut out in the wheel's disc;
   - output in Unreal axes: X forward, Y right, Z up, centimetres, origin midway between the axles at the physics body origin. The axis change keeps glTF's counter-clockwise front faces as Unreal front faces (checked with two-sided materials, whose facing sign turns the shading normal).
3. **Meshes** (`SimCruiseCarAssets`): one transient `UStaticMesh` per part (body, 4 wheels, 4 uprights, steering wheel), glass and lenses in a separate non-Nanite mesh. Editor builds run the full mesh build with **Nanite for parts over 20 000 triangles** (the 1.1 M-triangle NB body), MikkTSpace tangents and the author's normals; the result is cached in the DDC by content hash, so later starts take seconds. Cooked games take the fast runtime build (no Nanite). Built cars are cached per process and shared between instances.
4. **Materials** (`SimCruiseCarMaterialLibrary`): three master materials generated from C++ — a two-sided clear-coat surface, its masked twin, and thin-translucent glass — each around HLSL custom nodes. Roles (`SimCruiseCarMaterialRoles`: paint, glass, chrome, wheel alloy, tyre, brake disc, caliper, grained plastic, interior plastic, cloth, leather, carpet, lamps, gauges …) are guessed from author material names and overridden per car. Surface detail is procedural in each part's own space, so it moves with the part and needs no UVs:
   - paint: a cellular field of 0.55 mm flakes tilts the **base-coat normal under a smooth clear coat** (`r.ClearCoatNormal=True`), orange peel tilts the coat; flakes fade into base roughness below a pixel;
   - pebble grain on plastics, leather grain, cloth weave, brushed metal, lathe marks and rusty edges on brake discs, rubber grain and road dust on tyres, procedural asphalt for the stage;
   - glass is thin translucent with coloured transmittance (faint green laminated glass, red/amber lenses) and Fresnel reflection; lamp lenses glow in nits (red channel: tail 300, brake 6000) in a near-pure linear red, since signal red lies outside the sRGB gamut and any green or blue tone-maps a bright lens to salmon.
   The editor builds the masters in memory on first use, so a code change is never hidden by a stale asset; `SimCruise.SaveCarMaterials` writes them under `/Game/Vehicles/Materials` for cooking.
5. **Component** (`USimCruiseCarPresentationComponent`, actor `ASimCruiseCarPresentationActor`).

### Interface for the vehicle pawn

```cpp
// Implement on the pawn or movement component:
class ISimCruiseWheelStateProvider { virtual bool GetCarVisualState(FSimCruiseCarVisualState& Out) const = 0; };

USimCruiseCarPresentationComponent* Look = CreateDefaultSubobject<USimCruiseCarPresentationComponent>(TEXT("Look"));
Look->SetupAttachment(Body);              // identity: the physics body origin, Unreal axes
Look->InitializeForCar(TEXT("nb1999"));    // blocking on a car's first build
Look->SetWheelStateProvider(this);         // polled every frame in TG_PostPhysics
```

`FSimCruiseCarVisualState` per wheel (FL, FR, RL, RR): `SteerAngleRad` (+ left), `CamberRad` (+ top outboard), `SpinAngleRad` (+ rolling forward; keep it wrapped to one turn, since a float accumulated over a long drive loses the precision a spinning wheel needs), `SuspensionTravelCm` (+ bump); plus `bBrake` and `bLamps`. The component polls the provider on the game thread in `TG_PostPhysics`: a provider fed from the physics thread must hand over a copy of the last completed step (no reads of live physics-thread state). Part components get unique names, so one actor can carry several cars and a car can be re-initialised. The steering wheel turns by the mean front steer angle times the car's overall `steering_ratio` (anticlockwise for a left turn as the driver sees it). `GetStaticHubLocation`, `GetDriverEye` and `GetSteeringWheelRestTransform` give the cockpit camera and HUD what they need. The sign conventions are pinned by `SimCruise.Presentation.Component.Conventions`.

### Data: `Content/Vehicles/CarPresentation.json`

Per car id: `model` (glTF path), `credit`, `steering_ratio` with its source, `paint` (name, sRGB base, metallic, base roughness, flake, clear-coat roughness, orange peel, source), `roles` (author material → role), `tints` (role → colour), `badges` (brand notes, or `{note, material?, box}` in the Unreal vehicle frame, metres), `role_areas` (`{note, role, material?, box}`), optional `flip`, `fit_track`, `auto_badges`, `hide_pattern` and a `steering_wheel` override (`node`, or `centre`/`axis`/`radius`, or `none`).

### Turntable

`SimCruiseTurntableGameMode` on `/Engine/Maps/Entry` (or the `SimCruise.Turntable [ids|all] [dir]` console command in any world) lights an outdoor stage — physical sun 42° up and 215° from the nose as in the Godot taste test, sky atmosphere, volumetric clouds, real-time sky light, procedural asphalt — sets scalability 4 with Lumen GI and reflections, and photographs each car from eight angles plus the driver's seat, a wheel close-up, a steered front three-quarter, the steered cockpit and a braking rear three-quarter. Shots and `report.txt` (rig log, build time, mean frame time) go to `Saved/Screenshots/Turntable`. See `docs/unreal-building.md` for the command line.

### What it looks like (turntable, 11 November 2026)

![1999 roadster, front three-quarter](images/unreal-nb1999-front-34-left.jpg)
![1965 Mustang, front three-quarter](images/unreal-mustang65-front-34-left.jpg)
![Corvette C8, rear three-quarter](images/unreal-meridian-z-rear-34-left.jpg)
![NB wheel: tyre, alloy, disc and caliper](images/unreal-nb1999-wheel.jpg)
![Mustang cockpit with the wheel turned for 8 degrees of road-wheel lock](images/unreal-mustang65-steer-driver.jpg)

### Tests

`SimCruise.Presentation.*`: glTF loading of all three models; the rig for each car (wheelbase, hubs, track and tyre within tolerance of the car data, wheels centred on hubs, steering wheel ahead of and below the eye, rear-lamp surfaces present and behind the axles, white head-lamp lenses ahead of them, calipers on uprights for the C8, badge removal); role guessing; component conventions; building and posing the C8 in a world.

### Known gaps (review, 11 November 2026)

- Lit lamps glow evenly over the whole lens: there is no reflector or bulb structure in the glow, so the NB's brake lamps read as a flat red oval at close range.
- Shots dominated by shade (the wheel close-up, the cockpit) come out strongly blue: the shade is lit by the blue sky and Unreal has no automatic white balance. A real phone photo of the same scene would be white-balanced. Lighting and camera belong to the rendering stream.
- The C8's rear quarters show jagged reflection patches where the horizon reflects off the curved paint; the model's normals are the likely cause, and this has not been checked further.
- The Mustang's steering wheel is 0.26 m across in the model, well short of the real wheel; its tail-lamp lenses are dark when unlit in shade.
- The cooked-game path (fast mesh build, saved masters, runtime textures) has been compiled but never run.
