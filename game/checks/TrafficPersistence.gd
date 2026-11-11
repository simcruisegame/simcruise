extends SceneTree
## Headless: traffic and parked cars must not vanish, pop in or jump while they
## are in view. Loads the real Drive scene offline in bundled Miami Beach, turns
## the Ultra traffic count on (headless runs keep it at zero), and moves the
## player kinematically up a long two-way arterial of the bundled tiles (the
## physics car is frozen; the camera, tile streaming, traffic and parked cars run
## exactly as in play). The drive crosses at least three tile seams (each one a
## tile attach and unload) and one forced rebase (Drive.rebase).
##
## Every frame, every moving car that was drawn, inside the main camera's view
## frustum (or the rear-view mirror's, when the cockpit is shown) and nearer than
## 300 m must still be drawn the next frame, as the same car, no further away
## than 2 m + 3 v dt from where it was (no teleport), and no more than 0.5 m
## higher or lower. No car may first appear inside the view nearer than 260 m.
## Occlusion is not modelled, so a car hidden behind a building still counts as
## in view: the check is stricter than the eye.
## Parked cars: a parking spot keeps its model for the whole drive.
## Windowed with PERSIST_SHOTS=/abs/dir it also saves the frames before and
## after (side by side) each of the first events seen through the windscreen,
## the car boxed in red, and one frame every 15 s.
##   SIMCRUISE_QA_PREFS_DIR=/tmp/x godot --headless --path game --fixed-fps 30 --script res://checks/TrafficPersistence.gd
const ROUTE_NAME := "Collins Avenue"      # a two-way primary road through five bundled tiles
const ROUTE_CLASS := "primary"
const SPEED := 14.                         # m/s, a little over 30 mph
const VIEW_M := 300.
const APPEAR_M := 260.
const WARMUP_S := 12.
const MAX_SIM_S := 600.
var game
var route := PackedVector2Array()
var route_s := PackedFloat32Array()
var travelled := 0.
var lane_offset := 3.
var stage := 0
var clock := 0.
var sim := 0.
var measuring := false
var last_tile := Vector2i.ZERO
var crossings := 0
var rebases := 0
var rebase_done := false
var prev := {}                 # car slot -> {id, pos, speed, seen}
var stats := {"vanish":0, "appear":0, "jump":0, "dy":0, "seen_ids":{}, "frames":0, "max_seen":0, "parked_changes":0, "parked_spots":{}, "parked_vanish":0, "parked_seen":0}
var reasons := {}
var examples := []
var parked_model := {}         # absolute spot key -> mesh
var parked_prev := {}          # absolute spot key -> was drawn and in view
var failures := []
var spots := []                # [tile id, point in the tile, yaw, rnd % 5, rnd]
var spotted := {}

func _initialize():
 if OS.get_environment("SIMCRUISE_QA_PREFS_DIR").is_empty():
  push_error("PERSIST needs SIMCRUISE_QA_PREFS_DIR so it cannot touch the player's preferences")
  quit(2);return
 OS.set_environment("SIMCRUISE_MAP_HELPER", "/usr/bin/false")
 game = load("res://Drive.tscn").instantiate()
 game.start_mode = "bundled"
 root.add_child(game)
 game.helper = "/usr/bin/false"

func expect(ok: bool, what: String) -> void:
 if not ok:failures.append(what)

func finish() -> void:
 var life = game.ambient_life
 print("PERSIST drive %.0f m in %.0f s, %d tile seams crossed, %d rebases, %d tiles loaded at the end" % [travelled, sim, crossings, rebases, game.tiles.size()])
 print("PERSIST %d measured frames, %d distinct cars seen in view (at most %d at once), graph %d segments" % [stats.frames, stats.seen_ids.size(), stats.max_seen, life.segs.size()])
 print("PERSIST visible vanishes %d, jumps %d, pop-ins under %d m %d, height steps over 0.5 m %d" % [stats.vanish, stats.jump, int(APPEAR_M), stats.appear, stats.dy])
 if not reasons.is_empty():print("PERSIST vanish reasons %s" % [reasons])
 if "retired" in life:print("PERSIST retirements %s, U-turns at dead ends %d" % [life.retired, life.u_turns])
 print("PERSIST parked spots seen %d, model changes %d, drawn spots lost while in view %d" % [stats.parked_spots.size(), stats.parked_changes, stats.parked_vanish])
 for e in examples:print("PERSIST example ", e)
 expect(crossings >= 3, "only %d tile seams crossed" % crossings)
 expect(rebases >= 1, "no rebase")
 expect(stats.seen_ids.size() >= 12, "only %d cars seen in view: the check would pass vacuously" % stats.seen_ids.size())
 expect(stats.vanish == 0, "%d cars vanished in view" % stats.vanish)
 expect(stats.jump == 0, "%d cars jumped in view" % stats.jump)
 expect(stats.appear == 0, "%d cars appeared in view nearer than %d m" % [stats.appear, int(APPEAR_M)])
 expect(stats.dy == 0, "%d height steps over 0.5 m" % stats.dy)
 expect(stats.parked_spots.size() >= 10, "only %d parked spots seen" % stats.parked_spots.size())
 expect(stats.parked_changes == 0, "%d parked spots changed model" % stats.parked_changes)
 if "retired" in life:
  expect(int(life.retired.get("rebuild", 0)) == 0, "%d cars retired by a graph rebuild" % int(life.retired.get("rebuild", 0)))
 if failures.is_empty():
  print("PERSIST PASS")
  quit(0)
 else:
  for f in failures:print("PERSIST FAIL ", f)
  quit(1)

## Collins Avenue from the bundled tile files, in this game's coordinates,
## south to north, on the right-hand lane.
func build_route() -> void:
 var pts := []
 for x in range(18170, 18190):
  for y in range(27900, 27915):
   var name := "res://data/%d_%d.json" % [x, y]
   if not FileAccess.file_exists(name):continue
   var d = JSON.parse_string(FileAccess.get_file_as_string(name))
   if not d is Dictionary:continue
   var o: Vector2 = game.tile_offset(Vector2i(x, y))
   for road in d.get("roads", []):
    if str(road[0]) != ROUTE_CLASS or str(road[2]) != ROUTE_NAME:continue
    lane_offset = float(road[4]) * .25
    for uv in road[3]:pts.append(o + Vector2(uv[0], uv[1]) * game.side)
 pts.sort_custom(func(a, b): return a.y > b.y)
 var line := []
 for p in pts:
  if line.is_empty() or line[-1].distance_to(p) > 2.:line.append(p)
 # Keep the right-hand lane: northbound (-y), right is +x.
 var s := 0.
 for i in line.size():
  var t: Vector2 = (line[mini(i + 1, line.size() - 1)] - line[maxi(i - 1, 0)]).normalized()
  route.append(line[i] + Vector2(-t.y, t.x) * lane_offset)
  if i > 0:s += route[i].distance_to(route[i - 1])
  route_s.append(s)
 # Parked cars: the bundled tiles are live-canopy tiles, which build no parking
 # spots of their own (WorldTile.street_details), so give the route's kerbs a
 # spot every 30 m (WorldTile spaces them 27-37 m) to exercise Drive.update_parked.
 var kerb := lane_offset * 2. - 1.05
 var at_s := 30.
 while at_s < route_s[-1]:
  var a := at(at_s)
  var t: Vector2 = a[1]
  var centre: Vector2 = a[0] - Vector2(-t.y, t.x) * lane_offset
  for side in [-1., 1.]:
   var p: Vector2 = centre + Vector2(-t.y, t.x) * kerb * side
   var id: Vector2i = game.tile_id(p)
   var rnd := absi(hash(Vector3i(roundi(p.x), roundi(p.y), int(side))))
   spots.append([id, p - game.tile_offset(id), atan2(t.x, t.y), rnd % 5, rnd])
  at_s += 30.

## Put the route's parking spots into each tile as it streams in.
func give_spots() -> void:
 for id in game.tiles:
  if spotted.has(id):continue
  spotted[id] = true
  for spot in spots:
   if spot[0] == id:game.tiles[id].parked.append(spot.slice(1))

func at(s: float) -> Array:
 for i in range(1, route.size()):
  if route_s[i] >= s:
   var f := (s - route_s[i - 1]) / maxf(route_s[i] - route_s[i - 1], .001)
   var p := route[i - 1].lerp(route[i], f)
   var d := (route[i] - route[i - 1]).normalized()
   return [p, d]
 return [route[-1], (route[-1] - route[-2]).normalized()]

func place_player(s: float) -> void:
 var a := at(s)
 var p: Vector2 = a[0]
 var d: Vector2 = a[1]
 var yaw := atan2(-d.x, -d.y)
 game.pos = p;game.yaw = yaw
 var y: float = game.surface(p, game.vehicle.position.y) + .13
 game.vehicle.global_transform = Transform3D(Basis(Vector3.UP, yaw), Vector3(p.x, y, p.y))
 game.vehicle.reset_physics_interpolation()

func in_view(p: Vector3) -> bool:
 var cam: Camera3D = game.camera
 var probe := p + Vector3.UP * .8
 if cam.global_position.distance_to(probe) < VIEW_M and cam.is_position_in_frustum(probe):return true
 var cockpit = game.cockpit
 if cockpit.visible and cockpit.rear_camera and cockpit.rear_camera.is_inside_tree():
  var rc: Camera3D = cockpit.rear_camera
  if rc.global_position.distance_to(probe) < rc.far and rc.is_position_in_frustum(probe):return true
 return false

func note(kind: String, car: Dictionary, extra: String) -> void:
 var why := str(car.get("gone", "?"))
 reasons[kind + ":" + why] = int(reasons.get(kind + ":" + why, 0)) + 1
 if examples.size() < 12:examples.append("%s t=%.1f s %s (%s)" % [kind, sim, extra, why])

func measure(dt: float) -> void:
 var life = game.ambient_life
 var cam_pos: Vector3 = game.camera.global_position
 var now := {}
 var seen := 0
 for i in life.cars.size():
  var car: Dictionary = life.cars[i]
  var drawn: bool = car.seg >= 0
  var id = car.get("id", i)
  var p: Vector3 = car.pos
  var visible := drawn and in_view(p)
  if visible:seen += 1;stats.seen_ids[str(id) + "/" + str(i)] = true
  var was: Dictionary = prev.get(i, {})
  var dist := cam_pos.distance_to(p)
  if not was.is_empty() and was.seen:
   var limit: float = 2. + 3. * maxf(float(was.speed), float(car.speed)) * dt
   if not drawn or id != was.id:
    stats.vanish += 1;note("vanish", car, "car %s at %.0f m" % [str(was.id), cam_pos.distance_to(was.pos)]);events_now.append(["vanish", was.pos])
   elif p.distance_to(was.pos) > limit and Vector2(p.x - was.pos.x, p.z - was.pos.z).length() > limit:
    stats.jump += 1;note("jump", car, "car %s moved %.1f m in one frame at %.0f m" % [str(id), p.distance_to(was.pos), dist]);events_now.append(["jump", was.pos])
   elif absf(p.y - was.pos.y) > .5:
    stats.dy += 1;note("dy", car, "car %s height step %.2f m at %.0f m" % [str(id), p.y - was.pos.y, dist])
  if visible and dist < APPEAR_M and stats.frames > 0:
   var fresh: bool = was.is_empty() or not was.drawn or id != was.id or p.distance_to(was.pos) > 2. + 3. * maxf(float(was.speed), float(car.speed)) * dt
   if fresh:stats.appear += 1;note("appear", car, "car %s at %.0f m" % [str(id), dist]);events_now.append(["appear", p])
  now[i] = {"id":id, "pos":p, "speed":car.speed, "seen":visible, "drawn":drawn}
 prev = now
 stats.frames += 1
 stats.max_seen = maxi(stats.max_seen, seen)
 capture()

func measure_parked() -> void:
 var shown := {}
 for node in game.traffic:
  if not node.visible:continue
  var p: Vector3 = node.position
  var key := Vector2i(roundi(p.x + float(game.origin.x) * game.side), roundi(p.z + float(game.origin.y) * game.side))
  var look: MeshInstance3D = node.get_child(0)
  shown[key] = true
  stats.parked_spots[key] = true
  if parked_model.has(key) and parked_model[key] != look.mesh:
   stats.parked_changes += 1
   if examples.size() < 12:examples.append("parked model change at %s t=%.1f s" % [key, sim])
  parked_model[key] = look.mesh
  if in_view(p):parked_prev[key] = true
 for key in parked_prev.keys():
  if not shown.has(key):
   stats.parked_vanish += 1;parked_prev.erase(key)

func _process(dt: float) -> bool:
 if game == null or not game.ready_drive:return false
 sim += dt
 match stage:
  0:
   # Bundled place loaded: freeze the physics car, turn traffic on, go to the route.
   if game.job or game.tiles.size() < 4:return false
   game.set_physics_process(false)
   game.vehicle.freeze = true
   game.ambient_life.configure(32, game.ambient_life.bird_target)
   if not shots.is_empty():game.hud.root.visible = false
   build_route()
   if route.size() < 10:
    failures.append("route %s not found in the bundled tiles" % ROUTE_NAME);finish();return true
   var vp: Vector2 = root.get_visible_rect().size
   expect(vp.x > 0 and vp.y > 0, "headless viewport has no size")
   place_player(0.)
   game.camera_snap = true
   stage = 1;clock = 0.
  1:
   # Let the tiles round the route start stream in and the traffic spawn.
   clock += dt
   place_player(0.)
   give_spots()
   var cam: Camera3D = game.camera
   if clock > 1. and clock < 1.2:
    var ahead := cam.global_position - cam.global_basis.z * 50.
    var behind := cam.global_position + cam.global_basis.z * 50.
    expect(cam.is_position_in_frustum(ahead) and not cam.is_position_in_frustum(behind), "camera frustum is not usable headless")
   if clock > WARMUP_S and not game.job:
    stage = 2;last_tile = game.tile_id(game.pos);clock = 0.
    print("PERSIST start at %s, %d tiles, %d cars, route %.0f m" % [last_tile, game.tiles.size(), game.ambient_life.cars.size(), route_s[-1]])
  2:
   clock += dt
   give_spots()
   measure(dt)
   if int(sim * 30.) % 22 == 0:measure_parked()
   # Drive on unless the tile just ahead has not streamed yet.
   var a := at(travelled)
   var ahead_id: Vector2i = game.tile_id(a[0] + a[1] * 60.)
   if game.tiles.has(ahead_id):travelled += SPEED * dt
   place_player(travelled)
   var here: Vector2i = game.tile_id(game.pos)
   if here != last_tile:crossings += 1;last_tile = here
   # One rebase mid-drive (Drive rebases on its own only after six tiles).
   if crossings >= 2 and not rebase_done:
    rebase_done = true
    var delta := Vector2i(0, -1)
    game.rebase(delta)
    for i in route.size():route[i] -= Vector2(delta) * game.side
    var shift: Vector3 = Vector3(delta.x, 0, delta.y) * float(game.side)
    for i in prev:prev[i].pos -= shift
    rebases += 1
    last_tile = game.tile_id(game.pos)
   # The bundle ends 60 m past the route, so the last stretch never streams.
   if travelled >= route_s[-1] - 80. or sim > MAX_SIM_S:
    finish();return true
 return false

# ------------------------------------------------------------ capture ---------
# PERSIST_SHOTS=/abs/dir (windowed runs only): the frame before and after each
# of the first in-view events, the car boxed in red, and a frame every 15 s.
var shots := OS.get_environment("PERSIST_SHOTS")
var last_image: Image
var shot_count := 0
var events_now := []          # [kind, world position] found this frame
var next_periodic := 0.

func mark(image: Image, p: Vector3) -> void:
 var cam: Camera3D = game.camera
 var probe := p + Vector3.UP * .8
 if cam.is_position_behind(probe):return
 var scale := Vector2(image.get_size()) / root.get_visible_rect().size
 var c := cam.unproject_position(probe) * scale
 var half := int(clampf(1400. / maxf(cam.global_position.distance_to(probe), 1.), 8., 90.))
 var r := Rect2i(Vector2i(c) - Vector2i(half * 2, half), Vector2i(half * 4, half * 2)).intersection(Rect2i(Vector2i.ZERO, image.get_size()))
 if r.size.x <= 0 or r.size.y <= 0:return
 var red := Color(1, .1, .1)
 for t in 3:
  image.fill_rect(Rect2i(r.position + Vector2i(0, t), Vector2i(r.size.x, 1)), red)
  image.fill_rect(Rect2i(r.position + Vector2i(0, r.size.y - 1 - t), Vector2i(r.size.x, 1)), red)
  image.fill_rect(Rect2i(r.position + Vector2i(t, 0), Vector2i(1, r.size.y)), red)
  image.fill_rect(Rect2i(r.position + Vector2i(r.size.x - 1 - t, 0), Vector2i(1, r.size.y)), red)

func capture() -> void:
 if shots.is_empty() or DisplayServer.get_name() == "headless":return
 var image := root.get_viewport().get_texture().get_image()
 # Only events through the windscreen: the mirror is too small to show them.
 var cam: Camera3D = game.camera
 var ahead := events_now.filter(func(e): return cam.global_position.distance_to(e[1]) < VIEW_M and cam.is_position_in_frustum(e[1] + Vector3.UP * .8))
 if not ahead.is_empty() and shot_count < 12 and last_image != null:
  var before := last_image.duplicate()
  var after := image.duplicate()
  for e in ahead:
   mark(before, e[1]);mark(after, e[1])
  # Before and after side by side, one frame (1/30 s) apart.
  var pair := Image.create(before.get_width() * 2 + 8, before.get_height(), false, before.get_format())
  pair.fill(Color.WHITE)
  pair.blit_rect(before, Rect2i(Vector2i.ZERO, before.get_size()), Vector2i.ZERO)
  pair.blit_rect(after, Rect2i(Vector2i.ZERO, after.get_size()), Vector2i(before.get_width() + 8, 0))
  pair.save_jpg(shots.path_join("%02d-%s-t%03d.jpg" % [shot_count, ahead[0][0], int(sim)]), .88)
  shot_count += 1
 if sim >= next_periodic:
  next_periodic = sim + 15.
  image.save_jpg(shots.path_join("drive-%03d.jpg" % int(sim)), .9)
 events_now.clear()
 last_image = image
