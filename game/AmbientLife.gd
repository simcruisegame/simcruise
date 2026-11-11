extends Node3D
## Ambient life: moving traffic on the mapped roads and flocks of birds, built so
## that the cost is a handful of draw calls and almost no CPU.
##
## Traffic. Cars follow the same road segments the tile builder drew
## (WorldTile.segments: [a, b, name, width, oneway, class, surface]), on the
## right-hand lane. Each car is driven by the Intelligent Driver Model (desired
## speed from the road class, time headway, standstill gap, comfortable
## acceleration and braking) towards the car or the player ahead, a red signal or
## a sharp turn; TrafficDirector.gd gives every car a persona with its own
## parameters and decides routes, signal stops and delivery pull-overs about once
## a second. Every model part is one MultiMesh, so 32 cars of seven
## models cost one draw call per part, not one per car. The few cars near the
## player also carry a moving collision box, so they cannot be driven through.
##
## Persistence. A car in view never vanishes, teleports or pops in. The street
## graph is rebuilt the moment the set of loaded tiles changes or the map
## rebases (Drive.gd calls tiles_changed()), and every segment keeps a stable key
## (tile id, index in that tile's segments), so each car, its planned route and
## its signal stop carry straight over to the new graph. A car on a tile that
## was just unloaded keeps driving on a copy of its segment ("ghost") and is
## retired once nobody can see it. Cars are spawned only out of view (main
## camera and, in the cockpit, the rear-view mirror) or beyond 400 m, and
## despawned only out of view or beyond 600 m. A dead end makes a two-way
## street's car turn round and a one-way street's car (or one at the edge of
## the loaded map) wait until it is out of view.
##
## Birds. Each flock is one MultiMesh whose birds are animated entirely in the
## vertex shader, the way a drone light show flies pre-computed paths: every
## bird has a phase, orbit radius and height in its instance data, and the GPU
## turns time into position, heading and wing beat. The CPU moves only the
## flock centre, a few times a second.
##
## Counts come from GraphicsQuality.gd presets; zero (headless and QA runs)
## builds nothing at all.

# Traffic uses only real models (game/art/traffic/manifest.json, Sketchfab CC BY):
# the old low-poly sedan/pickup and the generated garage bodies were retired
# because they did not look as good. Utility kinds stand in for delivery vans.
const CAR_MODELS := []
const UTILITY_KINDS := ["f150raptor18", "tahoe", "odyssey", "grandcherokee13"]
# "minor" is the OpenMapTiles class of residential and unclassified streets
# (helper/road-widths.mjs), the most common class in the tiles; "service" and
# "track" (driveways, parking aisles, farm tracks) are not through routes.
const DRIVABLE := ["motorway","trunk","primary","secondary","tertiary","minor","residential","unclassified","living_street","motorway_link","trunk_link","primary_link","secondary_link","tertiary_link"]
const SPEED := {"motorway":29.,"trunk":25.,"primary":17.,"secondary":15.,"tertiary":13.,"minor":11.,"residential":10.,"unclassified":11.,"living_street":5.}
# Where new cars appear: road length weighted by how many cars a kilometre of
# that class carries. Traffic density is flow over speed; the flows are typical
# urban daily volumes by functional class, in vehicles per day (interstate
# 80 000, other freeway 50 000, principal arterial 25 000, minor arterial
# 12 000, collector 6 000, local street 700), so a kilometre of arterial holds
# about twenty times the cars of a kilometre of side street.
const DAILY_FLOW := {"motorway":80000.,"trunk":50000.,"primary":25000.,"secondary":12000.,"tertiary":6000.,"minor":700.,"residential":700.,"unclassified":700.,"living_street":200.}
const LINK_FLOW := 8000.
const SPAWN_MIN := 45.
const SPAWN_MAX := 450.
const SPAWN_IN_VIEW := 400.          # nearer than this a car may only appear out of view
const DESPAWN := 320.                # out of view and further than this: retired
const DESPAWN_HARD := 600.           # further than this: retired even in view
const DRAW_RANGE := 650.
const SOLID_RADIUS := 45.
const FLOCK_COUNT := 3

var game
var traffic_target := 0
var bird_target := 0
var cars: Array = []                 # {seg, dir, s, speed, model, pos:Vector3, yaw}
var parts: Array = []                # per model: [[MultiMesh, Transform3D local], ...]
var model_bounds: Array = []
var segs: Array = []                 # world-space [a, b, width, oneway, class]
var ends: Dictionary = {}            # Vector2i(round end) -> [[seg index, end 0/1], ...]
var seg_keys: Array = []             # per segment: Vector3i(tile x, tile y, index in tile.segments)
var seg_source: Array = []           # per segment: the tile's own segment array (tile metres)
var key_index: Dictionary = {}       # Vector3i key -> segment index
var real_count := 0                  # segments [0, real_count) are loaded; the rest are ghosts
var spawn_cdf := PackedFloat32Array()
var next_id := 1
var retired := {}                    # reason -> count, for checks and the bench
var u_turns := 0
var views: Array = []                # [camera position, frustum planes, range] per active view
var tile_signature := 0
var graph_clock := 0.
var rng := RandomNumberGenerator.new()
var solids: Array = []
var flocks: Array = []               # {node, center:Vector3, goal:Vector3}
var bird_material: ShaderMaterial
const DIRECTOR := preload("res://TrafficDirector.gd")
const CAR_LENGTH := 4.6
var director = DIRECTOR.new()
var utility_models: Array = []      # model indices for delivery vans (pickups)

func configure(traffic_count: int, bird_count: int) -> void:
 traffic_target = traffic_count
 bird_target = bird_count
 if traffic_target > 0 and parts.is_empty():build_models()
 if bird_target > 0 and flocks.is_empty():build_flocks()
 for f in flocks:
  f.node.multimesh.visible_instance_count = mini(f.node.multimesh.instance_count, bird_target / FLOCK_COUNT)
 # Fewer cars wanted (a lower preset): the surplus is retired out of view
 # by step_traffic, never in front of the player; none at all clears at once.
 if traffic_target <= 0:cars.clear()
 visible = traffic_target > 0 or bird_target > 0

func _ready() -> void:
 rng.seed = 1999
 director.name = "TrafficDirector";director.life = self;add_child(director)

# ---------------------------------------------------------------- traffic ----
const TRAFFIC_MANIFEST := "res://art/traffic/manifest.json"

func build_models() -> void:
 var sources := []
 for path in CAR_MODELS:sources.append({"scene":path})
 # Models fetched from Sketchfab by authoring/cars/sketchfab.mjs (CC0 / CC BY,
 # credited in licenses/Traffic models.txt), normalised to their stated length.
 if FileAccess.file_exists(TRAFFIC_MANIFEST):
  var manifest = JSON.parse_string(FileAccess.get_file_as_string(TRAFFIC_MANIFEST))
  if manifest is Dictionary:
   for m in manifest.get("models", []):
    if ResourceLoader.exists(str(m.get("scene",""))):sources.append(m)
 for source in sources:
  var list := []
  var merged := merged_mesh(str(source.scene))
  if merged == null:continue
  var fit := Transform3D.IDENTITY
  if source.has("length_m"):fit = normalise(merged.get_aabb(), float(source.length_m), bool(source.get("flip", false)))
  var mm := MultiMesh.new()
  mm.transform_format = MultiMesh.TRANSFORM_3D
  mm.mesh = merged
  mm.instance_count = 40
  mm.visible_instance_count = 0
  var node := MultiMeshInstance3D.new()
  node.multimesh = mm
  node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
  # Past this a car is retired anyway. The range is measured to the centre of
  # the box round every car of this model, so it must exceed the furthest car.
  node.visibility_range_end = DRAW_RANGE
  add_child(node)
  list.append([mm, fit])
  if str(source.get("kind", "")) in UTILITY_KINDS:utility_models.append(parts.size())
  parts.append(list);model_bounds.append(fit * merged.get_aabb())
 for i in 6:
  var body := AnimatableBody3D.new();body.sync_to_physics = false
  var hit := CollisionShape3D.new();var box := BoxShape3D.new();box.size = Vector3(1.8,1.3,4.4);hit.shape = box;hit.position.y = .7
  body.add_child(hit);body.collision_layer = 0;body.collision_mask = 0
  add_child(body);solids.append(body)

## Long axis along Z, scaled to the stated length, wheels on y = 0, centred.
## Downloaded models come in any unit and orientation; the length is the one
## fact the manifest states, so everything else is derived from the bounds.
static func normalise(box: AABB, length_m: float, flip: bool) -> Transform3D:
 var turn := Basis.IDENTITY
 if box.size.x > box.size.z:turn = Basis(Vector3.UP, PI * .5)
 if flip:turn = Basis(Vector3.UP, PI) * turn
 var turned: AABB = Transform3D(turn, Vector3.ZERO) * box
 var s := length_m / maxf(.001, maxf(turned.size.x, turned.size.z))
 var scaled := Transform3D(turn.scaled(Vector3.ONE * s), Vector3.ZERO)
 var b: AABB = scaled * box
 return Transform3D(scaled.basis, Vector3(-(b.position.x + b.size.x * .5), -b.position.y, -(b.position.z + b.size.z * .5)))

static var merged_cache := {}

## The real traffic fleet as [mesh, fit transform, utility] rows, built once
## and shared by moving traffic and parked cars (CarFinish.gd).
static func fleet() -> Array:
 if merged_cache.has("__fleet"):return merged_cache["__fleet"]
 var rows := []
 if FileAccess.file_exists(TRAFFIC_MANIFEST):
  var manifest = JSON.parse_string(FileAccess.get_file_as_string(TRAFFIC_MANIFEST))
  if manifest is Dictionary:
   for m in manifest.get("models", []):
    var mesh := merged_mesh(str(m.get("scene", "")))
    if mesh == null:continue
    rows.append([mesh, normalise(mesh.get_aabb(), float(m.get("length_m", 4.7)), bool(m.get("flip", false))), str(m.get("kind", "")) in UTILITY_KINDS])
 merged_cache["__fleet"] = rows
 return rows

static func merged_mesh(path: String) -> ArrayMesh:
 if merged_cache.has(path):return merged_cache[path]
 if not ResourceLoader.exists(path):return null
 var packed = load(path)
 if not packed is PackedScene:return null
 # Bake every part of the model into one multi-surface mesh, so a car is one
 # instance transform however many parts its file has.
 var root: Node = packed.instantiate()
 var merged := ArrayMesh.new()
 for mi in root.find_children("*", "MeshInstance3D", true, false):
  if mi.mesh == null or not mi.visible:continue
  var local: Transform3D = relative(root, mi)
  for s in mi.mesh.get_surface_count():
   if merged.get_surface_count() >= 255:
    push_warning("traffic model %s has more than 255 surfaces; the rest are dropped" % path);break
   var st := SurfaceTool.new()
   st.append_from(mi.mesh, s, local)
   var mat: Material = mi.get_surface_override_material(s) if s < mi.get_surface_override_material_count() and mi.get_surface_override_material(s) else mi.mesh.surface_get_material(s)
   st.set_material(mat)
   st.commit(merged)
 root.free()
 if merged.get_surface_count() == 0:
  push_warning("traffic model %s has no visible mesh; it is left out of the fleet" % path);return null
 # Merging drops the importer's detail levels; rebuild them (meshoptimizer via
 # ImporterMesh) so a distant 300k-triangle car costs a few thousand.
 var im := ImporterMesh.new()
 for s in merged.get_surface_count():
  # Custom channels and skinning need format flags ImporterMesh is not given;
  # a parked or moving car uses neither, so drop them (they made
  # add_surface fail with "Invalid array format" and the car vanish).
  var arrays := merged.surface_get_arrays(s)
  for k in [Mesh.ARRAY_CUSTOM0, Mesh.ARRAY_CUSTOM1, Mesh.ARRAY_CUSTOM2, Mesh.ARRAY_CUSTOM3, Mesh.ARRAY_BONES, Mesh.ARRAY_WEIGHTS]:arrays[k] = null
  im.add_surface(merged.surface_get_primitive_type(s), arrays, [], {}, merged.surface_get_material(s))
 im.generate_lods(25., 60., [])
 var lodded: ArrayMesh = im.get_mesh()
 merged_cache[path] = lodded
 return lodded

static func relative(root: Node, node: Node) -> Transform3D:
 var t := Transform3D.IDENTITY
 var n := node
 while n != null and n != root:
  if n is Node3D:t = (n as Node3D).transform * t
  n = n.get_parent()
 return t

## Called by Drive.gd whenever a tile is attached or unloaded and after a
## rebase: the graph follows at once, so no car is ever left on a stale one.
func tiles_changed() -> void:
 if game == null:return
 tile_signature = signature()
 # Traffic off (headless and QA runs): build the graph when it is turned on.
 if traffic_target <= 0 or parts.is_empty():
  tile_signature = 0;return
 rebuild_graph()
 director.rebuild_signals()

## A teleport to another place: every car goes (none of them is near the new
## place), and the graph starts from nothing.
func reset() -> void:
 for car in cars:
  if car.seg >= 0:retire(car, "jump")
 segs = [];ends = {};seg_keys = [];seg_source = [];key_index = {};real_count = 0
 spawn_cdf = PackedFloat32Array()
 tile_signature = 0

## The street graph from the loaded tiles, keeping every car where it is: the
## old graph's segment keys carry each car's segment and planned route over to
## the new one. Segment geometry is in tile metres plus the tile's offset, so a
## rebase is the same rebuild and needs no shifting of its own.
func rebuild_graph() -> void:
 var old_keys := seg_keys
 var old_source := seg_source
 segs = [];ends = {};seg_keys = [];seg_source = [];key_index = {}
 var weights := PackedFloat32Array()
 var total := 0.
 for id in game.tiles:
  var tile = game.tiles[id]
  var o: Vector2 = game.tile_offset(id)
  var list: Array = tile.segments
  for i in list.size():
   var seg: Array = list[i]
   if seg.size() < 6 or not str(seg[5]) in DRIVABLE:continue
   if seg.size() > 6 and seg[6] == "unpaved":continue
   if seg[0].distance_squared_to(seg[1]) < 1.:continue
   var k := add_segment(seg, o, Vector3i(id.x, id.y, i), true)
   total += seg_length(k) * density(str(seg[5]))
   weights.append(total)
 real_count = segs.size()
 spawn_cdf = weights
 for car in cars:
  if car.seg < 0:continue
  if car.seg >= old_keys.size():
   retire(car, "rebuild");continue
  var key: Vector3i = old_keys[car.seg]
  var k: int = key_index.get(key, -1)
  if k < 0:
   # Its tile was unloaded. Out of sight it simply goes; in sight it drives on
   # along a copy of its segment and goes once nobody can see it.
   if not seen(car.pos):
    retire(car, "tile_unloaded");continue
   k = add_segment(old_source[car.seg], game.tile_offset(Vector2i(key.x, key.y)), key, false)
  car.seg = k
  var route := []
  for step in car.route:
   if step[0] >= old_keys.size():break
   var j: int = key_index.get(old_keys[step[0]], -1)
   if j < 0 or j >= real_count:break
   route.append([j, step[1]])
  car.route = route

## One segment into the graph; joinable segments can be turned onto at their
## ends, ghosts (segments of unloaded tiles) cannot.
func add_segment(seg: Array, o: Vector2, key: Vector3i, joinable: bool) -> int:
 var a: Vector2 = seg[0] + o;var b: Vector2 = seg[1] + o
 var k := segs.size()
 segs.append([a, b, float(seg[3]), int(seg[4]) if seg[4] is int or seg[4] is float else (1 if seg[4] else 0), str(seg[5])])
 seg_keys.append(key);seg_source.append(seg);key_index[key] = k
 if joinable:
  for e in 2:
   var cell := Vector2i(((a if e == 0 else b) / 1.5).round())
   if not ends.has(cell):ends[cell] = []
   ends[cell].append([k, e])
 return k

## Cars per metre of a road class, relative (DAILY_FLOW over the class speed).
static func density(road_class: String) -> float:
 var flow: float = DAILY_FLOW.get(road_class, LINK_FLOW if road_class.ends_with("_link") else 700.)
 return flow / float(SPEED.get(road_class, 12.))

func retire(car: Dictionary, reason: String) -> void:
 car.seg = -1;car.held = false;car.gone = reason
 retired[reason] = int(retired.get(reason, 0)) + 1

## The views a car can be seen in this frame: the main camera and, in the
## cockpit, the rear-view mirror's camera.
func update_views() -> void:
 views.clear()
 var cam = game.get("camera")
 if cam is Camera3D and cam.is_inside_tree():
  views.append([cam.global_position, cam.get_frustum(), minf(cam.far, DRAW_RANGE)])
 var cockpit = game.get("cockpit")
 if cockpit != null and cockpit.visible and cockpit.rear_camera is Camera3D and cockpit.rear_camera.is_inside_tree():
  var rc: Camera3D = cockpit.rear_camera
  views.append([rc.global_position, rc.get_frustum(), rc.far])

## Whether a car at p (its ground point) shows in any view. A car whose centre
## is just outside the frame still pokes into it, so the frame is widened by
## 3 m; buildings are not considered, so a car behind one counts as seen.
func seen(p: Vector3) -> bool:
 var probe := p + Vector3.UP * .8
 for v in views:
  if probe.distance_to(v[0]) > float(v[2]) + 3.:continue
  var inside := true
  for plane in v[1]:
   if plane.distance_to(probe) > 3.:
    inside = false;break
  if inside:return true
 return false

## Whether a road end at p is the edge of the loaded map rather than a real
## dead end: the road may well go on in a tile that is not loaded.
func at_map_edge(p: Vector2) -> bool:
 for d in [Vector2.ZERO, Vector2(4, 0), Vector2(-4, 0), Vector2(0, 4), Vector2(0, -4)]:
  if not game.tiles.has(game.tile_id(p + d)):return true
 return false

func signature() -> int:
 var h := 17
 for id in game.tiles:h = h * 31 + hash(id)
 return h + game.origin.x * 7919 + game.origin.y * 104729

func seg_length(k: int) -> float:
 return segs[k][0].distance_to(segs[k][1])

func seg_heading(k: int, dir: int) -> Vector2:
 return ((segs[k][1] - segs[k][0]) * dir).normalized()

## Every way on from the end of segment k travelled in direction dir:
## [segment, direction, cosine of the turn].
func options(k: int, dir: int) -> Array:
 # dir +1 travels a->b, -1 travels b->a. Find segments that start where this ends.
 var s: Array = segs[k]
 var at: Vector2 = s[1] if dir > 0 else s[0]
 var heading: Vector2 = ((s[1] - s[0]) * dir).normalized()
 var found := []
 for dx in [-1, 0, 1]:
  for dy in [-1, 0, 1]:
   for pair in ends.get(Vector2i((at / 1.5).round()) + Vector2i(dx, dy), []):
    var j: int = pair[0]
    if j == k:continue
    var t: Array = segs[j]
    var nd := 1 if pair[1] == 0 else -1
    if t[3] != 0 and t[3] != nd:continue        # against a one-way
    var start: Vector2 = t[0] if nd > 0 else t[1]
    if start.distance_to(at) > 2.5:continue
    var out: Vector2 = ((t[1] - t[0]) * nd).normalized()
    var turn := heading.dot(out)
    if turn < -.3:continue                        # no U-turns at junctions
    found.append([j, nd, turn])
 return found

## The way on at a junction: the director's planned route if it still holds,
## otherwise the driver's own choice now.
func next_segment(car: Dictionary) -> Array:
 var opts := options(car.seg, car.dir)
 if not car.route.is_empty():
  var planned: Array = car.route.pop_front()
  for o in opts:
   if o[0] == planned[0] and o[1] == planned[1]:return o
  car.route.clear()
 return director.choose(car, opts)

## A new car somewhere on the loaded roads, picked by road length times the
## class's traffic density, never where it could be seen appearing.
func spawn(car: Dictionary) -> bool:
 if real_count == 0 or spawn_cdf.is_empty():return false
 var player: Vector2 = game.pos
 var total: float = spawn_cdf[spawn_cdf.size() - 1]
 for attempt in 24:
  var k := mini(spawn_cdf.bsearch(rng.randf() * total), real_count - 1)
  var s: Array = segs[k]
  var t := rng.randf()
  var p: Vector2 = s[0].lerp(s[1], t)
  var d := p.distance_to(player)
  if d < SPAWN_MIN or d > SPAWN_MAX:continue
  # Pop-in is the one thing people notice: in view a car may appear only
  # beyond SPAWN_IN_VIEW (the mirror counts as a view); out of view it appears
  # within the despawn range, or it would be retired again at once.
  if seen(Vector3(p.x, game.ground(p), p.y)):
   if d < SPAWN_IN_VIEW:continue
  elif d > DESPAWN - 20.:continue
  var dir: int = s[3] if s[3] != 0 else (1 if rng.randf() < .5 else -1)
  car.id = next_id;next_id += 1;car.gone = "";car.held = false;car.snap = Vector2.ZERO
  car.seg = k;car.dir = dir;car.s = t * s[0].distance_to(s[1]) if dir > 0 else (1. - t) * s[0].distance_to(s[1])
  director.assign(car, SPEED.get(s[4], 12.))
  car.speed = car.cruise * .7                    # appears already moving with the traffic
  car.model = car.base_model
  if car.drv.persona == "delivery" and not utility_models.is_empty():car.model = utility_models[rng.randi() % utility_models.size()]
  return true
 return false

func lane_point(car: Dictionary) -> Array:
 var s: Array = segs[car.seg]
 var a: Vector2 = s[0] if car.dir > 0 else s[1]
 var b: Vector2 = s[1] if car.dir > 0 else s[0]
 var length := a.distance_to(b)
 var t := (b - a) / maxf(length, .001)
 var right := Vector2(-t.y, t.x)
 var width: float = s[2]
 # Right-hand traffic: two-way roads keep right of the centre line; one-way
 # roads use the lane right of centre on wide roads, the middle on narrow ones.
 var offset := width * .25 if s[3] == 0 else (width * .2 if width > 7. else 0.)
 # car.lat moves the car off the lane centre: towards the kerb to pull over,
 # towards the centre line to pass a van that has. [3] is the room to the kerb.
 return [a + t * car.s + right * (offset + car.lat), t, length, maxf(0., width * .5 - offset - 1.1)]

func step_traffic(dt: float) -> void:
 if game.tiles.is_empty():return
 update_views()
 graph_clock -= dt
 if graph_clock <= 0.:
  graph_clock = 1.
  # A safety net only: Drive.gd calls tiles_changed() on every attach, unload
  # and rebase, and the rebuild keeps every car where it is.
  if signature() != tile_signature:tiles_changed()
 if segs.is_empty():
  for list in parts:
   for part in list:part[0].visible_instance_count = 0
  return
 while cars.size() < traffic_target:
  var m := cars.size() % parts.size()
  cars.append({"seg":-1,"dir":1,"s":0.,"speed":0.,"cruise":12.,"model":m,"base_model":m,"pos":Vector3.ZERO,"fwd":Vector2.UP,"route":[],"hy":[],"id":0,"gone":"","held":false,"snap":Vector2.ZERO})
 # Fewer cars wanted: the surplus leaves only where nobody sees it go.
 var i_surplus := cars.size() - 1
 while cars.size() > traffic_target and i_surplus >= 0:
  if cars[i_surplus].seg < 0 or not seen(cars[i_surplus].pos):cars.remove_at(i_surplus)
  i_surplus -= 1
 director.step(dt)
 var now: float = director.now
 var player := Vector3(game.pos.x, 0., game.pos.y)
 var player_v: Vector3 = game.vehicle.linear_velocity
 for car in cars:
  if car.seg < 0 or car.seg >= segs.size():
   if spawn(car):
    car.hy = []
    var l0 := lane_point(car)
    car.pos = Vector3(l0[0].x, height(car, l0[0], l0, INF), l0[0].y);car.fwd = l0[1]
 # A snapshot of every car at the start of the step, in packed arrays: the
 # all-pairs leader search reads these instead of dictionaries.
 var n := cars.size()
 var at := PackedVector2Array();at.resize(n)
 var heading := PackedVector2Array();heading.resize(n)
 var info := PackedFloat32Array();info.resize(n * 2)     # speed, lateral offset
 var live := PackedByteArray();live.resize(n)
 for i in n:
  var c: Dictionary = cars[i]
  live[i] = (2 if c.parked else 1) if c.seg >= 0 and c.has("drv") else 0
  at[i] = Vector2(c.pos.x, c.pos.z);heading[i] = c.fwd;info[i * 2] = c.speed;info[i * 2 + 1] = c.get("lat", 0.)
 for ci in n:
  if live[ci] == 0:continue
  var car: Dictionary = cars[ci]
  var drv: Dictionary = car.drv
  var here: Vector2 = at[ci]
  var fwd: Vector2 = heading[ci]
  var horizon := maxf(70., car.speed * 5.)
  # Leader: the nearest car or the player in the lane in front, as a net
  # (bumper to bumper) gap and its speed along our heading.
  var gap := INF
  var lead_v := 0.
  var shift := 0.
  for oi in n:
   if oi == ci or live[oi] == 0:continue
   var rel: Vector2 = at[oi] - here
   var ahead := rel.dot(fwd)
   if ahead <= 0. or ahead - CAR_LENGTH >= minf(gap, horizon):continue
   if absf(rel.cross(fwd)) >= 2.2:continue
   var along := heading[oi].dot(fwd)
   if along <= .5:continue
   var other_lat := info[oi * 2 + 1]
   if live[oi] == 2 or other_lat > .8:
    # A van pulled over at the kerb: ease out round it, slowly (on a
    # street too narrow to clear it this means crossing the centre line).
    if ahead < 25.:shift = maxf(shift, clampf(2.1 - other_lat, 0., 1.4))
    continue
   gap = ahead - CAR_LENGTH;lead_v = info[oi * 2] * along
  var rel_p := Vector2(player.x, player.z) - here
  var ahead_p := rel_p.dot(fwd)
  if ahead_p > -2. and ahead_p - CAR_LENGTH < minf(gap, horizon) and absf(rel_p.cross(fwd)) < 2.6:
   gap = maxf(0., ahead_p - CAR_LENGTH);lead_v = Vector2(player_v.x, player_v.z).dot(fwd)
  # Standing virtual leaders: the stop bar of a red signal, a delivery stop.
  if now < car.stop_until and car.stop_at - car.s - CAR_LENGTH * .5 > -.5:
   var g: float = car.stop_at - car.s - CAR_LENGTH * .5 + drv.s0
   if g < gap:gap = maxf(g, .1);lead_v = 0.
  if car.park_at < car.s - 2.:car.park_at = INF
  var lane := lane_point(car)
  var lat_goal := -shift
  if car.park_at < INF:
   var g2: float = car.park_at - car.s + drv.s0
   if g2 < gap:gap = maxf(g2, .1);lead_v = 0.
   if car.park_at - car.s < 30.:lat_goal = lane[3]
   if absf(car.park_at - car.s) < 1.5 and car.speed < .5:
    car.parked = true;car.park_until = now + car.dwell;car.park_at = INF
  # Desired speed: the road's, capped on the approach to a sharp turn so the
  # driver can brake comfortably down to the cornering speed, and while passing.
  var v0: float = car.cruise
  if car.turn_at < INF and car.turn_at > car.s - 6.:
   v0 = minf(v0, sqrt(car.turn_v * car.turn_v + 2. * float(drv.b) * maxf(0., car.turn_at - car.s)))
  if shift > 0.:v0 = minf(v0, 7.)
  if car.parked:
   car.speed = 0.;lat_goal = lane[3]
   if now >= car.park_until:car.parked = false;lat_goal = 0.
  else:
   var acc: float = DIRECTOR.idm(car.speed, v0, gap, car.speed - lead_v, drv)
   var moved: Vector2 = DIRECTOR.advance(car.speed, acc, dt)
   car.s += moved.x;car.speed = moved.y
  car.lat = move_toward(car.lat, lat_goal, dt * .7)
  lane = lane_point(car)
  var guard := 0
  var from: Vector2 = lane[0]
  while car.s > lane[2] and guard < 4:
   guard += 1
   var nxt := next_segment(car)
   if nxt.is_empty():
    dead_end(car, lane)
    lane = lane_point(car)
    break
   var l: float = lane[2]
   car.s -= l;car.seg = nxt[0];car.dir = nxt[1];car.held = false
   car.turn_at -= l;car.stop_at -= l;car.park_at -= l
   car.cruise = SPEED.get(segs[car.seg][4], 12.) * float(drv.speed)
   car.hy = []
   lane = lane_point(car)
  # Segments meet only roughly (ends within 2.5 m, lane offsets by width):
  # the step at a change of segment is taken up over the next second or so,
  # never shown as a jump.
  if guard > 0:car.snap += from - Vector2(lane[0])
  car.snap *= exp(-dt * 2.5)
  if car.snap.length_squared() < 1e-4:car.snap = Vector2.ZERO
  var p2: Vector2 = lane[0] + car.snap
  var target_fwd: Vector2 = lane[1]
  car.fwd = car.fwd.slerp(target_fwd, minf(1., dt * 4.)) if car.fwd.length_squared() > .5 else target_fwd
  car.pos = Vector3(p2.x, height(car, lane[0], lane, car.pos.y), p2.y)
  # Leave only unseen: far away, waiting at the end of the road, or on a
  # segment whose tile has been unloaded. Very far cars go regardless.
  var d := Vector2(car.pos.x - player.x, car.pos.z - player.z).length()
  if d > DESPAWN_HARD:retire(car, "far")
  elif (d > DESPAWN or car.held or car.seg >= real_count) and not seen(car.pos):
   retire(car, "far" if d > DESPAWN else ("dead_end" if car.held else "tile_unloaded"))
 publish()

## The end of the road with no way on. A two-way street's driver turns round
## (the same spot on the road, now facing back; the drift back to its own lane
## is the turn). On a one-way street, a ghost segment or at the edge of the
## loaded map the car waits at the end; it moves on if the road ahead loads,
## and step_traffic retires it once it is out of view.
func dead_end(car: Dictionary, lane: Array) -> void:
 var s: Array = segs[car.seg]
 var end: Vector2 = s[1] if car.dir > 0 else s[0]
 if s[3] == 0 and car.seg < real_count and not at_map_edge(end):
  var over: float = car.s - float(lane[2])
  car.dir = -car.dir;car.s = maxf(0., over);u_turns += 1
  car.lat = -2. * float(s[2]) * .25 - car.lat
  car.route = [];car.turn_at = INF;car.stop_at = INF;car.park_at = INF
  car.speed = minf(car.speed, 3.)
  car.hy = []
  return
 car.s = lane[2];car.speed = 0.;car.held = true

## Road height under a car. game.surface() follows the road ribbon and costs
## tens of microseconds, so it is sampled every HEIGHT_STEP metres along the
## lane and interpolated in between (the ribbon is smooth at that scale).
## near_y is the car's height a frame ago: under a bridge it keeps the car on
## the road below rather than lifting it onto the deck (Drive.surface).
const HEIGHT_STEP := 6.
func height(car: Dictionary, p: Vector2, lane: Array, near_y: float) -> float:
 var hy: Array = car.hy
 if hy.size() == 4 and car.s >= hy[0] and car.s <= hy[2]:
  return lerpf(hy[1], hy[3], (car.s - hy[0]) / maxf(hy[2] - hy[0], .001))
 var y0: float = game.surface(p, near_y)
 var s1: float = minf(car.s + HEIGHT_STEP, lane[2])
 if s1 - car.s < .5:
  car.hy = [];return y0
 var p1: Vector2 = p + lane[1] * (s1 - car.s)
 car.hy = [car.s, y0, s1, game.surface(p1, y0)]
 return y0

func publish() -> void:
 var counts := []
 counts.resize(parts.size());counts.fill(0)
 var near := []
 for car in cars:
  if car.seg < 0:continue
  var m: int = car.model
  var yaw := atan2(-car.fwd.x, -car.fwd.y)
  var base := Transform3D(Basis(Vector3.UP, yaw), car.pos + Vector3.UP * .02)
  for part in parts[m]:
   var mm: MultiMesh = part[0]
   if counts[m] < mm.instance_count:mm.set_instance_transform(counts[m], base * part[1])
  counts[m] += 1
  var d := Vector2(car.pos.x - game.pos.x, car.pos.z - game.pos.y).length()
  if d < SOLID_RADIUS:near.append([d, base])
 for m in parts.size():
  for part in parts[m]:part[0].visible_instance_count = mini(counts[m], part[0].instance_count)
 near.sort_custom(func(x, y): return x[0] < y[0])
 for i in solids.size():
  var body: AnimatableBody3D = solids[i]
  if i < near.size():
   body.global_transform = near[i][1];body.collision_layer = 1
  else:
   body.collision_layer = 0;body.position = Vector3(0, -500, 0)

# ------------------------------------------------------------------ birds ----
const BIRD_SHADER := """
shader_type spatial;
render_mode unshaded, cull_disabled, shadows_disabled;
uniform vec4 ink : source_color = vec4(.10, .10, .11, 1.);
// INSTANCE_CUSTOM: x phase (0..1), y orbit radius (m), z height offset (m), w speed (rad/s)
void vertex() {
	float t = TIME * INSTANCE_CUSTOM.w + INSTANCE_CUSTOM.x * 6.2832;
	float r = INSTANCE_CUSTOM.y * (0.75 + 0.25 * sin(TIME * 0.21 + INSTANCE_CUSTOM.x * 11.0));
	// A lissajous orbit: an ellipse that drifts and folds, so the flock breathes.
	vec3 p = vec3(cos(t) * r, INSTANCE_CUSTOM.z + sin(t * 1.7 + INSTANCE_CUSTOM.x * 4.0) * 2.5, sin(t * 0.83) * r * 0.7);
	vec3 v = normalize(vec3(-sin(t) * r, cos(t * 1.7) * 4.25, cos(t * 0.83) * 0.581 * r) + vec3(0.0001));
	vec3 side = normalize(cross(vec3(0., 1., 0.), v));
	vec3 up = cross(v, side);
	// Wing beat: vertices away from the body rise and fall; a short glide every few seconds.
	float beat = sin(TIME * 11.0 + INSTANCE_CUSTOM.x * 40.0) * step(0.25, fract(TIME * 0.18 + INSTANCE_CUSTOM.x));
	vec3 local = VERTEX;
	local.y += beat * abs(local.x) * 0.55;
	VERTEX = p + side * local.x + up * local.y + v * local.z;
}
void fragment() {
	ALBEDO = ink.rgb;
}
"""

func bird_mesh() -> ArrayMesh:
 # A swept wing silhouette: body plus two wings, ~0.8 m span (a crow or gull at range).
 var st := SurfaceTool.new()
 st.begin(Mesh.PRIMITIVE_TRIANGLES)
 var tri := func(a: Vector3, b: Vector3, c: Vector3):
  st.add_vertex(a);st.add_vertex(b);st.add_vertex(c)
 tri.call(Vector3(0, 0, -.28), Vector3(.05, 0, .18), Vector3(-.05, 0, .18))
 tri.call(Vector3(.04, 0, -.08), Vector3(.42, .02, .06), Vector3(.04, 0, .1))
 tri.call(Vector3(-.04, 0, -.08), Vector3(-.04, 0, .1), Vector3(-.42, .02, .06))
 return st.commit()

func build_flocks() -> void:
 bird_material = ShaderMaterial.new()
 var shader := Shader.new();shader.code = BIRD_SHADER;bird_material.shader = shader
 var mesh := bird_mesh()
 for f in FLOCK_COUNT:
  var mm := MultiMesh.new()
  mm.transform_format = MultiMesh.TRANSFORM_3D
  mm.use_custom_data = true
  mm.mesh = mesh
  mm.instance_count = 80
  var tight := rng.randf_range(.6, 1.4)
  for i in mm.instance_count:
   mm.set_instance_transform(i, Transform3D.IDENTITY)
   mm.set_instance_custom_data(i, Color(rng.randf(), rng.randf_range(6., 22.) * tight, rng.randf_range(-3., 3.), rng.randf_range(.35, .6) * (1. if f % 2 == 0 else -1.)))
  mm.custom_aabb = AABB(Vector3(-40, -15, -40), Vector3(80, 30, 80))
  var node := MultiMeshInstance3D.new()
  node.multimesh = mm;node.material_override = bird_material
  node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
  node.visibility_range_end = 700.
  add_child(node)
  flocks.append({"node":node, "center":Vector3.INF, "goal":Vector3.ZERO, "clock":0.})

func step_birds(dt: float) -> void:
 var player := Vector3(game.pos.x, 0., game.pos.y)
 for f in flocks:
  f.clock -= dt
  var c: Vector3 = f.center
  if not c.is_finite() or Vector2(c.x - player.x, c.z - player.z).length() > 520.:
   f.center = place_flock(player, 140., 380.);f.goal = place_flock(player, 120., 360.);c = f.center
  elif f.clock <= 0.:
   f.clock = rng.randf_range(6., 14.)
   if Vector2(f.goal.x - c.x, f.goal.z - c.z).length() < 20.:f.goal = place_flock(player, 120., 360.)
  # Drift toward the goal at bird cruising speed, a few metres per second.
  var to: Vector3 = f.goal - c
  if to.length() > 1.:f.center = c + to.normalized() * minf(to.length(), 9. * dt)
  f.node.position = f.center

func place_flock(player: Vector3, near: float, far: float) -> Vector3:
 var a := rng.randf() * TAU
 var d := rng.randf_range(near, far)
 var p := Vector2(player.x + cos(a) * d, player.z + sin(a) * d)
 return Vector3(p.x, game.ground(p) + rng.randf_range(28., 70.), p.y)

var cost_us := 0

func _process(dt: float) -> void:
 if game == null or not game.ready_drive or not visible:return
 var started := Time.get_ticks_usec()
 if traffic_target > 0 and not parts.is_empty():step_traffic(dt)
 if bird_target > 0:step_birds(dt)
 cost_us = Time.get_ticks_usec() - started

func rebase(offset: Vector3) -> void:
 for car in cars:car.pos -= offset
 for f in flocks:
  if f.center.is_finite():f.center -= offset;f.goal -= offset
 # The camera moved with the map (Drive.rebase); the graph follows the new
 # origin with every car on the same segment.
 update_views()
 tiles_changed()
