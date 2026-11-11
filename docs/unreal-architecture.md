# SimCruise Unreal architecture

How the Unreal client is put together. Each stream of the migration
(`docs/superpowers/plans/2026-11-10-simcruise-unreal-migration.md`) owns a
section here.

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
failed tile waits 15 s. Tiles are read and built on worker threads; the game
thread installs one finished tile per frame (mesh sections, collision,
materials).

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
  roofs are ported.
- Ground mask (1024 px per tile): vegetation, sand, paved paths, water.

### Materials and textures (`World/SimCruiseWorldResources`)

One HLSL node per surface (ground, asphalt, concrete, paint, water, building)
built in code; `-run=SimCruiseBuildWorldMaterials` saves them under
`/Game/World/Materials` for cooked builds, and an editor build creates them
in memory when the assets are absent. Detail is world-anchored: local
position plus a per-tile anchor (`tile * size mod 64 m`), so textures run on
across seams without half-precision UV loss. Ground: the aerial photograph
with its highlights compressed toward street-level albedo, modulated by CC0
detail photographs per land-cover class. Asphalt: Poly Haven Asphalt Floor
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
| `World.OfflineStart` | the 3 x 3 ring of the bundled start streams with the helper switched off; Ocean Drive found and its surface height agrees |
| `World.SeamContinuity` | all 24 bundled tiles built as a drive builds them: terrain, road heights and road edges equal across every seam (0.0 mm), every crossing road matched |
| `World.RoadCollision` | collision under every carriageway vertex, equal to the drawn surface within 1 cm for 99.5 % |
| `World.RoadEdges` | straight roads build straight edges (< 1 mm), sharp corners turn smoothly, no visible zig-zag on any bundled road edge |
| `World.StaleJobs` | older places, tiles left behind and superseded jobs are rejected; retries wait; resident tiles stay bounded |

### What it looks like (10 November 2026, Cinematic scalability, preview rig lighting)

![Ocean Drive from the driver's seat](images/unreal-world-ocean.jpg)
![Collins Avenue](images/unreal-world-collins.jpg)
![Ocean Drive and Lummus Park from 70 m](images/unreal-world-aerial.jpg)
![Ocean Drive crossing a tile seam, from 35 m](images/unreal-world-seam.jpg)

Against photographs of Ocean Drive the road surface, curbs, sidewalks and
markings read right; the place does not yet: no palms or street trees, no
parked cars, signals or street lights, massing buildings with a uniform
window grid instead of the Art Deco facades, and a 2 m/px photograph as the
ground. The seam view shows the road, curb and sidewalk running straight
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
