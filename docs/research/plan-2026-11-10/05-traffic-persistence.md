# 5. Stop traffic vanishing: stable segment identity across tile streaming and rebase, 'minor' streets, view-aware spawn/despawn, CI check (traffic-persistence)
category traffic · impact 5 · 4 d · depends: none

## Rationale
This is a standing owner complaint, and the cause is clear. Every tile attach or unload and every rebase sets car.seg = -1 for every car, so crossing one tile edge teleports the whole fleet about 4 times. The tiles' residential class 'minor' is missing from DRIVABLE, so arterials dead-end and cars vanish mid-junction in plain view. No check covers any of this.

## Code claims (proposer)
AmbientLife.gd:291 runs `for car in cars:car.seg = -1` on any signature change. signature() (:201-204) hashes the tile ids and the origin, polled at :285-291. AmbientLife.gd:31 DRIVABLE lists OSM highway values (residential, unclassified...) but not the OpenMapTiles 'minor' that the shipped tiles use (helper/road-widths.mjs:26). :384 sets seg=-1 at a dead end. :396 despawns by distance only, DESPAWN 320 (:35). AmbientLife.rebase (:528-531) shifts car.pos but not segs, ends or signals. height() (:407, :412) calls game.surface without near_y. Drive.update_parked (Drive.gd:775-793) gives a 115 m radius and reassigns models by sort order. merged_mesh (:163-174) has no surface-count guard. GraphicsQuality.gd:61-64 sets traffic to 0 in headless runs.

## Design (proposer)
1) Stable keys: segment key = (tile_id, index in tile.segments). rebuild_graph() returns an old_key->new_index map. Each car's seg, s and dir are remapped, and any route entry that fails to map is cleared. A car is retired only when its tile left game.tiles.
2) Run the rebuild synchronously from Drive.attach and from the stream() unload via ambient_life.tiles_changed(), replacing the 1 s poll.
3) Rebase: shift segs[k][0..1] and director.signals[].at, key `ends` by absolute map metres, and refresh tile_signature after the shift.
4) Add 'minor' to DRIVABLE (11 m/s), TrafficDirector.RANK (0), DELIVERY_ROADS and the classify residential share. Keep 'service' as a turn-in sink only.
5) At a true two-way dead end, stop, then reverse dir. At a one-way dead end or the map edge, hold the car and retire it out of view. Have the helper emit tunnel roads with a tunnel flag so traffic continues through them, hidden.
6) View-aware lifetime:
- Despawn when (d > 320 and not camera.is_position_in_frustum(pos + up*0.8)) or d > 600.
- Spawn only outside the frustum, or more than 400 m ahead.
- MultiMesh visibility_range_end raised to about 650, or removed.
7) Height: bridge flag on on-deck segments (WorldTile.gd:428-433), so off-bridge cars use ground().
8) Parked cars: one MultiMesh per fleet model, model = hash(absolute spot coordinate), drawn to 400 m. StaticBody only for the nearest 12.
9) merged_mesh guard: real primitive type. Fall back to the unlodded mesh when the surface count or AABB check fails, and log it.
10) New checks/TrafficPersistence.gd on the bundled Miami tiles:
- configure(32,0) directly, using BoxMesh parts if the fleet does not merge headless.
- Persistent car id and a gone_reason at every seg=-1 site.
- A scripted drive across 3 or more tile edges, plus a forced game.rebase(Vector2i(1,0)).

## Gate
TrafficPersistence over 3 tile crossings and 1 rebase:
- 0 visible vanishes: cars in the frustum under 300 m that disappear or jump more than 2 m + 3*v*dt in one frame.
- 0 appearances in the frustum under 260 m.
- |dy| under 0.5 m per frame.
- 'rebuild' retirements = 0 for tiles that stayed loaded.
- Parked spots never change model.
TrafficCheck gains a two-tile fixture where a tertiary road meets minor streets, so options() is non-empty. Every helper class except service/track must be in DRIVABLE. TrafficCheck and AmbientCheck must still pass. QualityBench ambient_ms rises by at most +0.3 ms at Ultra. Player physics and CarTrack are untouched.

## Files
game/AmbientLife.gd
game/TrafficDirector.gd
game/Drive.gd
game/WorldTile.gd
game/CarFinish.gd
helper/atlas.mjs
game/checks/TrafficPersistence.gd
game/checks/TrafficCheck.gd
scripts/check.sh
CHANGELOG.md

## Skeptic verdict
claims_hold true · already_done false · keep true

### Wrong claims
- merged_mesh has a surface-count guard. AmbientLife.gd:153 breaks at 255 surfaces, and it returns null when the merged mesh has 0 surfaces. Commit 55fcce9 (#11) has also already fixed the vanishing-car cause there by nulling the CUSTOM0-3, BONES and WEIGHTS arrays. The only real gap left in step 9 is that im.add_surface hardcodes PRIMITIVE_TRIANGLES.
- GraphicsQuality.gd:61-64 does not set traffic to 0. Headless runs set managed=false, so apply() returns at :78 and AmbientLife.configure (:92) is never called. traffic_target therefore stays at its default of 0. The effect is the same, but the mechanism is different.
- Adding 'minor' to TrafficDirector.RANK changes nothing, because score() uses RANK.get(class, 0) and the default is already 0. Speed likewise falls back to SPEED.get(...,12.). Only DRIVABLE actually excludes 'minor'.
- Rebase is not a routine per-tile-edge event. Drive.gd:647 rebases only when |delta| >= 6 tiles. The 'about 4 times per tile edge' figure comes from attaches and unloads alone: up to 3 unloads in one stream() frame plus up to 3 sequential attach jobs, coalesced by the 1 s poll.

### Design problems
- Step 3 says to key `ends` by absolute map metres. Godot Vector2 is float32, and at US-scale absolute coordinates (~1e6-1e7 m) the precision is ~0.1-1 m, which breaks the 1.5 m end quantisation and the 2.5 m join test in options(). Step 1's stable (tile_id, index) remap already makes a rebase a plain rebuild_graph() + remap from the local tile offsets. No shifting of segs or ends is needed, and signals are rebuilt anyway.
- Step 5 has the helper emit tunnel roads into tile.segments. Every segment consumer would then see them: WorldTile surface strokes, curbs and walks (:957), parked spots, Junction.gd, LaneMarkings, RoadSurface, nearest_world/move_to_road, wheel_grip and road_grid. That would paint roads over tunnels and snap the player to them. It also changes the bundled-tile format. Tunnels belong in a separate tile field read only by AmbientLife, or should be dropped from this proposal.
- Step 7's bridge flag cannot go at seg[6]. Index 6 is already the paved/unpaved surface tag, read by AmbientLife.rebuild_graph and WorldTile.wheel_grip (:626 `seg[6]!="paved"`). Use index 7 or a separate set. A cheaper general fix is to pass near_y = car.pos.y (or the last hy sample) to game.surface(), whose near_y logic already exists at Drive.gd:504-509. A flag is then needed only at spawn.
- The view-aware spawn and despawn in step 6 checks only the main camera. Cockpit.gd has a rear-view SubViewport (:120), so cars spawned 'outside the frustum' 45 m behind the player pop in the mirror. Spawning behind needs a minimum distance (e.g. >150 m) or a mirror frustum test.
- Adding 'minor' with spawn() picking a uniformly random segment index (AmbientLife.gd:240) shifts most traffic onto side streets and onto short segments, which are numerous. Arterials would look emptier than today. Spawn must be weighted by segment length × class density.
- The 'retire only when its tile left game.tiles' rule still pops cars in view. A tile unloads as soon as it is more than 1 tile from the centre, so cars on it can be 300-600 m away, inside the frustum, and inside the raised 650 m visibility range. Hold them (stop advancing, keep drawing) until they are out of the frustum or past the visibility range, then retire them.
- Steps 1-2 need one more trigger: Drive.gd:458 tiles.clear() on a jump must also call tiles_changed() and retire everything. Signals' seg_signal (keyed by segment index) must be rebuilt alongside the remap, which rebuild_signals already does.
- The TrafficPersistence gate depends on Camera3D.is_position_in_frustum in --headless, where the viewport size comes from project settings. Assert a non-zero viewport and frustum in the check first, or compute the view test from camera fov/transform, or the 'visible vanish' metric can be vacuously 0.
- Step 4's 'Keep service as a turn-in sink only' is unspecified. It is not in DRIVABLE, and options() has no notion of sink segments, so either drop it or define it.
- Step 8 (parked MultiMesh to 400 m) is a separate rendering and perf change. The props carry a shadow-plane child and a 22-node pool sized for 115 m. It should be split out or gated by its own QualityBench draw-call/GPU budget, not hidden inside the +0.3 ms ambient_ms gate, which measures CPU only.

### Corrected design (USE THIS where it differs)
1) Stable keys and remap: segment key = (tile_id, index in tile.segments). rebuild_graph() returns key->new_index. Each car's seg/dir/s and its route entries are remapped. Unmappable route entries are cleared, and director.rebuild_signals() runs.
2) Triggers: ambient_life.tiles_changed() is called from Drive.attach, from the stream() unload loop, from the jump path (Drive.gd:458, which also retires all cars) and from Drive.rebase. A rebase is just rebuild + remap from local tile offsets (no absolute-coordinate keys). tile_signature is refreshed, and the 1 s poll stays only as a safety net.
3) Unloaded tiles: a car whose tile left keeps its last segment geometry in a 'ghost' list and is held drawn. It is retired once it is out of the main and mirror frusta or beyond the visibility range.
4) Add 'minor' (11 m/s) to DRIVABLE and SPEED. Add it to DELIVERY_ROADS and to the residential share in classify. The RANK entry is optional, since the default is 0. Spawn is weighted by segment length × a per-class density so arterials stay busy.
5) Dead ends: on a two-way road, stop and then U-turn. On a one-way road or at the map edge, hold and retire out of view. Tunnels are dropped from this proposal; if wanted later, they go in a separate tile field consumed only by AmbientLife.
6) View-aware lifetime: test the main camera, plus the rear-view camera when it is active. Despawn when d > 320 and not visible, or when d > 600. Spawn when not visible and d >= 150 behind, or d > 400 ahead (raise SPAWN_MAX accordingly). Set visibility_range_end to about 650.
7) Height: game.surface(p, car.pos.y) using the near_y logic that already exists. Optionally add an on-deck flag at segment index 7, never index 6.
8) Parked cars: split into their own follow-up with a GPU/draw-call budget. The minimal fix is to make the model a function of hash(absolute spot): the spot's rnd plus the fleet row, with a node per row pool.
9) merged_mesh: only pass the source primitive type through. The guard already exists, so log a warning on fallback.
10) TrafficPersistence check on the bundled tiles: assert a valid headless viewport and frustum first, then use the stated gates. Add the TrafficCheck minor-street fixture. TrafficCheck, AmbientCheck and CarTrack/LiftOff stay unchanged, and QualityBench ambient_ms rises by at most 0.3 ms.
