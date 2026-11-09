extends RefCounted
# Builds the player's 1999 NB Sport for Drive.gd: instances miata.glb, applies
# the exterior likeness pass, mounts the Sport wheels on the physical hubs,
# wires the brake lamps and adds the ground shade and the parked cars. Drive.gd
# calls build() once from _ready; the state it reads later (wheels,
# lamp_materials, lamp_nodes, shadow_plane, traffic, GroundShade) is filled
# here exactly as before:  preload("res://CarFinish.gd").new().build(self)
#
# Likeness pass, measured against the reference photographs (Wikimedia Commons
# side, front and rear views of 1999-2000 NB cars) in chassis metres, +Y up,
# -Z forward, before the shell is lowered:
#  - the shell drops 25 mm over the unchanged wheels (161 -> 136 mm clearance,
#    arch gap 359 -> 334 mm as photographed);
#  - one clearcoat paint for body and bumpers, darker glass, black cloth seats;
#  - nose, headlamps and seals rise 65 mm with the hood and fender fronts;
#  - the drooping tail is lifted 80 mm at its edge so the deck reads flat and
#    the tail panel gains room for wrap-around oval lamps at lamp height, and
#    the lower bumper bulge is pulled in 40 mm (smooth fields, no new topology);
#  - the boxed tail-lamps become oval lenses on the skin with a gasket;
#  - shut lines (hood, doors, trunk lid, rear bumper cover, fuel door) are thin
#    dark strips 1.6 mm above the paint, merged into one mesh;
#  - door-top mirrors on stalks, flush body-colour door pulls, rear side
#    reflectors, a neutral SIMCRUISE plate, the third stop lamp on the lid.
# Powertrain MOUNTS and R, the hub pivots and the physics are untouched.
#
# The exterior model stays CC BY-SA 4.0 (photographed lamp textures). The
# geometry generated here and plate.png / tyre_sidewall_normal.png are original
# MIT work for SimCruise (licenses/Car finish.txt). No manufacturer marks.

const CAR := preload("res://art/miata.glb")
const SPORT_WHEEL := preload("res://SportWheel.gd")

const DROP := .025              # ride height
const NOSE_RISE := .065         # headlamp centre 537 -> 577 mm above ground
const DECK_LIFT := .08          # at the tail edge
const BUMPER_TUCK := .04        # lower rear bumper, below y .36
const PAINT_COLOR := Color("#0f3b2a")
const LAMP_Y := .728            # tail-lamp centre (shell coordinates)
const LAMP_HALF_HEIGHT := .088
const LAMP_SPAN := .44          # lens length around the corner
const LAMP_INBOARD_X := .375
const PLATE_Y := .728
const TRUNK_EDGE_Y := .821      # trunk lid trailing edge between the lamps
const SEAM_WIDTH := .0078
const SEAM_LIFT := .0016

var game
var model: Node3D
var body: MeshInstance3D
var paint: StandardMaterial3D
var dark_trim: StandardMaterial3D
var seam_ink: StandardMaterial3D
var lamp_ends := {}
var report := {}

func build(drive) -> void:
 game = drive
 var vehicle = game.vehicle
 vehicle.setup();game.add_child(vehicle);model=CAR.instantiate();vehicle.add_child(model)
 for part in model.find_children("*","MeshInstance3D",true,false):
  part.layers=2
 likeness()
 # Wheel hubs are separate pivots so steering and rotation do not affect the body.
 var sport_mesh=SPORT_WHEEL.wheel()
 var caliper_mesh=SPORT_WHEEL.caliper()
 for axle in ["front","rear"]:
  for lr in ["l","r"]:
   var hub=Node3D.new();vehicle.add_child(hub);hub.position=Vector3(-.779 if lr=="l" else .779,.305,-1.13 if axle=="front" else 1.13)
   var roller=Node3D.new();hub.add_child(roller)
   # The previous rear_l* pattern also matched rear_light. Only these three
   # explicitly authored wheel parts may be attached to a rotating hub.
   for component in ["rubber","alloy","dark"]:
    var part=model.find_child(axle+"_"+lr+"_"+component,true,false)
    assert(part is MeshInstance3D,"Missing wheel part: "+axle+"_"+lr+"_"+component)
    part.reparent(roller,false);part.position-=hub.position
    part.visible=false
   var side_sign=-1. if lr=="l" else 1.
   var mounted=MeshInstance3D.new();mounted.mesh=sport_mesh;mounted.layers=2
   mounted.rotation.y=PI if lr=="l" else 0.
   roller.add_child(mounted)
   var caliper=MeshInstance3D.new();caliper.mesh=caliper_mesh;caliper.layers=2
   caliper.position=Vector3(side_sign*.033,0.,0.)   # sector behind the axle, straddling the disc
   hub.add_child(caliper)
   hub.position=vehicle.MOUNTS[game.wheels.size()]-Vector3.UP*.205
   game.wheels.append([hub,roller,axle=="front"])
 for node in model.find_children("*","MeshInstance3D",true,false):
  if node.name.begins_with("rear_light_") or node.name=="body_red":
   var m=node.mesh.surface_get_material(0).duplicate()
   m.emission_enabled=true;m.emission=Color("#e3281b");m.emission_energy_multiplier=0.
   if m.albedo_texture:m.emission_texture=preload("res://art/stop_mask.png");m.emission=Color.WHITE;m.emission_operator=BaseMaterial3D.EMISSION_OP_MULTIPLY
   node.set_surface_override_material(0,m);game.lamp_materials.append(m);game.lamp_nodes.append(node)
 shade_and_traffic(game)

# The ground shade under the player's car and the parked cars. Shared by
# every car (CarBody.gd calls it with its own shade size).
static func shade_and_traffic(game, shade_size:=Vector2(2.14,4.34)) -> void:
 var ink=StandardMaterial3D.new();ink.transparency=BaseMaterial3D.TRANSPARENCY_ALPHA;ink.albedo_texture=preload("res://art/contact.png");ink.shading_mode=BaseMaterial3D.SHADING_MODE_UNSHADED;ink.render_priority=-1
 game.shadow_plane.size=shade_size;game.shadow_plane.material=ink
 var shadow=MeshInstance3D.new();shadow.name="GroundShade";shadow.layers=2;shadow.mesh=game.shadow_plane;shadow.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF;game.add_child(shadow)
 for i in 22:
  var asset=preload("res://art/pickup.glb") if i%5==0 else preload("res://art/sedan.glb")
  var prop=StaticBody3D.new();var look=asset.instantiate();prop.add_child(look);var shade=MeshInstance3D.new();shade.mesh=game.shadow_plane;shade.position.y=.035;shade.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF;prop.add_child(shade);var hit=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(1.68,1.12,4.12);hit.shape=box;hit.position.y=.70;prop.add_child(hit);prop.collision_layer=0;game.add_child(prop);prop.hide();game.traffic.append(prop)

# ---------------------------------------------------------------------------
func likeness() -> void:
 var started := Time.get_ticks_usec()
 body = model.find_child("body_British racing green", true, false)
 assert(body is MeshInstance3D, "Miata body shell missing")
 make_materials()
 reshape()
 model.position.y = -DROP
 var shell := Shell.new().add(body.mesh, AABB(Vector3(-1.2, .1, -2.2), Vector3(2.4, 1.2, 4.4)))
 for lamp in model.find_children("rear_light_*", "MeshInstance3D", true, false):
  var side := signf(lamp.get_aabb().get_center().x)
  var lens := tail_lamp(side, shell, lamp.mesh.surface_get_material(0))
  if lens:
   lamp.mesh = lens
 place_third_stop_lamp(shell)
 var plate := number_plate(shell)
 if plate:
  model.add_child(plate)
 model.add_child(shut_lines(shell))
 model.add_child(wipers())
 for side in [-1., 1.]:
  model.add_child(mirror(side, shell))
  model.add_child(door_handle(side, shell))
  model.add_child(side_reflector(side, shell))
 report["build_ms"] = roundf((Time.get_ticks_usec() - started) / 100.) / 10.
 report["ground_clearance_mm"] = roundf((lowest_paint() - DROP - .005) * 1000.)
 print("CAR FINISH ", JSON.stringify(report))

func make_materials() -> void:
 paint = StandardMaterial3D.new()
 paint.resource_name = "racing green paint"
 paint.albedo_color = PAINT_COLOR
 paint.metallic = .42; paint.roughness = .36; paint.metallic_specular = .5
 paint.clearcoat_enabled = true; paint.clearcoat = 1.; paint.clearcoat_roughness = .07
 paint.cull_mode = BaseMaterial3D.CULL_DISABLED
 dark_trim = StandardMaterial3D.new()
 dark_trim.albedo_color = Color("#121414"); dark_trim.roughness = .74; dark_trim.metallic = .05
 dark_trim.cull_mode = BaseMaterial3D.CULL_DISABLED
 seam_ink = StandardMaterial3D.new()
 seam_ink.albedo_color = Color("#080a09"); seam_ink.roughness = .9; seam_ink.metallic_specular = .1
 seam_ink.cull_mode = BaseMaterial3D.CULL_DISABLED
 var cloth := StandardMaterial3D.new()
 cloth.albedo_color = Color("#1c1d1e"); cloth.roughness = .94; cloth.metallic_specular = .18
 cloth.cull_mode = BaseMaterial3D.CULL_DISABLED
 var glass := StandardMaterial3D.new()
 glass.albedo_color = Color(.10, .13, .13, .58); glass.roughness = .04; glass.metallic = 0.
 glass.metallic_specular = .9; glass.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
 glass.cull_mode = BaseMaterial3D.CULL_DISABLED
 for part in model.find_children("*", "MeshInstance3D", true, false):
  for si in part.mesh.get_surface_count():
   var sm = part.mesh.surface_get_material(si)
   if not (sm is StandardMaterial3D):
    continue
   var name: String = sm.resource_name.to_lower()
   var replacement: Material = null
   if "racing" in name or "bumper" in name:
    replacement = paint   # one paint: the separate bumper green is gone
   elif name == "glass":
    replacement = glass
   elif name == "leather":
    replacement = cloth   # Sport package: black cloth, not tan leather
   if replacement:
    part.set_surface_override_material(si, replacement)

# ---------------------------------------------------------------------------
# Smooth displacement fields shared by every exterior mesh, so neighbouring
# parts stay closed. Positions are the original shell coordinates.
static func ramp(a: float, b: float, x: float) -> float:
 var t := clampf((x - a) / (b - a), 0., 1.)
 return t * t * (3. - 2. * t)

static func tail_edge_z(x: float) -> float:
 return 1.915 - .453 * x * x   # plan-view curve of the deck's rear edge

static func field(p: Vector3) -> Vector3:
 var ax := absf(p.x)
 var hood := ramp(-.75, -1.62, p.z)
 var fender := ramp(-1.35, -1.75, p.z)
 var rise := NOSE_RISE * lerpf(hood, fender, ramp(.60, .72, ax)) * ramp(.38, .46, p.y)
 var s := p.z - tail_edge_z(minf(ax, .85))
 var lift := DECK_LIFT * ramp(-.32, 0., s) * ramp(.60, .74, p.y)
 var tuck := BUMPER_TUCK * (1. - ramp(.36, .52, p.y)) * ramp(1.60, 1.86, p.z)
 return Vector3(p.x, p.y + rise + lift, p.z - tuck)

func reshape() -> void:
 var shell_arrays := body.mesh.surface_get_arrays(0)
 var v: PackedVector3Array = shell_arrays[Mesh.ARRAY_VERTEX]
 var idx: PackedInt32Array = shell_arrays[Mesh.ARRAY_INDEX]
 var comp := components(v.size(), idx)
 var mirrors := [comp[argmax(v, Vector3.RIGHT)], comp[argmax(v, Vector3.LEFT)]]
 report["old_mirror_verts"] = [comp.count(mirrors[0]), comp.count(mirrors[1])]
 deform(body, func(p: Vector3, i: int) -> Vector3:
  if comp[i] in mirrors:
   return Vector3(signf(p.x) * .70, .70, -.28)   # old beltline mirrors, collapsed inside
  if absf(p.x) < .27 and p.y > .485 and p.y < .645 and p.z > 1.82 and p.z < 1.876:
   p.z = 1.878   # the old plate pocket below the lamps, filled: the plate now sits between them
  return field(p))
 for part in model.find_children("*", "MeshInstance3D", true, false):
  var n: String = part.name
  if part == body or n.begins_with("rear_light_") or n.begins_with("front_") or n.begins_with("rear_l_") or n.begins_with("rear_r_"):
   continue   # replaced lamp boxes and the hidden legacy wheel parts
  if n == "body_alloy":
   deform(part, func(p: Vector3, _i: int) -> Vector3:
    if absf(p.x) > .79 and p.z > .3 and p.z < .5 and p.y > .65 and p.y < .70:
     return Vector3(signf(p.x) * .70, .67, .40)   # 400 mm alloy bars: flush pulls replace them
    if absf(p.x) < .03 and p.z > 1.9 and p.y > .65 and p.y < .70:
     return Vector3(0., .675, 1.80)               # badge blob behind the plate
    return field(p))
  elif n == "body_dark":
   deform(part, func(p: Vector3, _i: int) -> Vector3:
    var ax := absf(p.x)
    if ax > .79 and p.y > .29 and p.y < .71 and ((p.z > -.44 and p.z < -.36) or (p.z > .58 and p.z < .66)):
     return Vector3(signf(p.x) * .70, .50, .10)   # 43 mm door bars
    if ax > .52 and ax < .60 and p.y > .61 and p.y < .77 and p.z < -.45:
     return Vector3(signf(p.x) * .40, .45, -1.0)  # 20 mm hood strips
    return field(p))
  elif n == "body_trim":
   deform(part, func(p: Vector3, _i: int) -> Vector3:
    if absf(p.x) > .76 and p.y > .67 and p.y < .78 and p.z > -.34 and p.z < -.25:
     return Vector3(signf(p.x) * .70, .70, -.28)  # old mirror stalks
    if absf(p.x) < .2 and p.y > .485 and p.y < .645 and p.z > 1.86 and p.z < 1.9:
     return Vector3(0., .56, 1.80)                # old plate backing panel
    if absf(p.x) < .68 and p.y > .175 and p.y < .345 and absf(p.z - 1.825) < .004:
     return Vector3(p.x, p.y, 1.62)               # underbody box: its tail no longer shows under the tucked bumper
    return field(p))
  elif n.begins_with("Bumper side marker"):
   deform(part, func(p: Vector3, _i: int) -> Vector3: return field(p) + Vector3(0., .045, 0.))
  else:
   deform(part, func(p: Vector3, _i: int) -> Vector3: return field(p))

static func argmax(v: PackedVector3Array, axis: Vector3) -> int:
 var best := -INF
 var at := 0
 for i in v.size():
  var d := v[i].dot(axis)
  if d > best:
   best = d; at = i
 return at

static func components(count: int, idx: PackedInt32Array) -> PackedInt32Array:
 var parent := PackedInt32Array(range(count))
 for t in range(0, idx.size(), 3):
  for k in 2:
   var ra := root(parent, idx[t + k])
   var rb := root(parent, idx[t + k + 1])
   if ra != rb:
    parent[ra] = rb
 var out := PackedInt32Array()
 out.resize(count)
 for i in count:
  out[i] = root(parent, i)
 return out

static func root(parent: PackedInt32Array, i: int) -> int:
 while parent[i] != i:
  parent[i] = parent[parent[i]]
  i = parent[i]
 return i

# Moves the vertices of every surface of a part (on a private copy of its mesh)
# and refreshes the normals of whatever moved from the moved faces, keeping
# hard edges, materials and the other arrays. Imported LODs and shadow meshes
# describe the old shape, so the copy has neither.
static func deform(part: MeshInstance3D, mover: Callable) -> void:
 var source: Mesh = part.mesh
 var surfaces := []
 var changed := false
 var base := 0
 for s in source.get_surface_count():
  var arrays := source.surface_get_arrays(s)
  var v: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
  var n: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
  var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
  var moved := PackedByteArray()
  moved.resize(v.size())
  var any := false
  for i in v.size():
   var q: Vector3 = mover.call(v[i], base + i)
   if not q.is_equal_approx(v[i]):
    v[i] = q; moved[i] = 1; any = true
  base += v.size()
  if any and n.size() == v.size() and not idx.is_empty():
   var acc := PackedVector3Array()
   acc.resize(v.size())
   for t in range(0, idx.size(), 3):
    var i0 := idx[t]; var i1 := idx[t + 1]; var i2 := idx[t + 2]
    if moved[i0] == 0 and moved[i1] == 0 and moved[i2] == 0:
     continue
    var fn := (v[i1] - v[i0]).cross(v[i2] - v[i0])   # area weighted
    if fn.dot(n[i0] + n[i1] + n[i2]) < 0.:
     fn = -fn
    acc[i0] += fn; acc[i1] += fn; acc[i2] += fn
   for i in v.size():
    if moved[i] == 1 and acc[i].length_squared() > 1e-18:
     n[i] = acc[i].normalized()
   arrays[Mesh.ARRAY_NORMAL] = n
  arrays[Mesh.ARRAY_VERTEX] = v
  changed = changed or any
  surfaces.append([arrays, source.surface_get_material(s)])
 if not changed:
  return
 var mesh := ArrayMesh.new()
 for entry in surfaces:
  mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, entry[0])
  mesh.surface_set_material(mesh.get_surface_count() - 1, entry[1])
 var overrides := []
 for s in surfaces.size():
  overrides.append(part.get_surface_override_material(s))
 part.mesh = mesh
 for s in surfaces.size():
  if overrides[s]:
   part.set_surface_override_material(s, overrides[s])

func lowest_paint() -> float:
 var low := INF
 for p in body.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]:
  if p.y > .1:
   low = minf(low, p.y)
 return low

# ---------------------------------------------------------------------------
# Triangle soup of the shell inside a region, binned in 1 cm slabs along each
# axis, for probing the skin with rays that keep one coordinate fixed.
class Shell:
 const BIN := .015
 var a := PackedVector3Array()
 var b := PackedVector3Array()
 var c := PackedVector3Array()
 var na := PackedVector3Array()
 var nb := PackedVector3Array()
 var nc := PackedVector3Array()
 var bins := [{}, {}, {}]
 func add(mesh: Mesh, region: AABB) -> Shell:
  for s in mesh.get_surface_count():
   var arrays := mesh.surface_get_arrays(s)
   var v: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
   var n: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
   var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
   if idx.is_empty():
    idx = PackedInt32Array(range(v.size()))
   var smooth := n.size() == v.size()
   for t in range(0, idx.size(), 3):
    var p0 := v[idx[t]]
    var p1 := v[idx[t + 1]]
    var p2 := v[idx[t + 2]]
    var mn := p0.min(p1).min(p2)
    var mx := p0.max(p1).max(p2)
    if (mx - mn).length_squared() < 1e-12:
     continue   # collapsed parts
    if not region.intersects(AABB(mn, mx - mn + Vector3.ONE * 1e-5)):
     continue
    var i := a.size()
    a.append(p0); b.append(p1); c.append(p2)
    if smooth:
     na.append(n[idx[t]]); nb.append(n[idx[t + 1]]); nc.append(n[idx[t + 2]])
    else:
     var fn := (p1 - p0).cross(p2 - p0).normalized()
     na.append(fn); nb.append(fn); nc.append(fn)
    for axis in 3:
     for k in range(floori(mn[axis] / BIN), floori(mx[axis] / BIN) + 1):
      if not bins[axis].has(k):
       bins[axis][k] = []
      bins[axis][k].append(i)
  return self
 # First skin point along a ray: [position, smooth normal facing the ray] or [].
 func hit(from: Vector3, dir: Vector3, length := 3.) -> Array:
  dir = dir.normalized()
  var candidates = null
  for axis in 3:
   if absf(dir[axis]) < 1e-6:
    var list: Array = bins[axis].get(floori(from[axis] / BIN), [])
    if candidates == null or list.size() < candidates.size():
     candidates = list
  if candidates == null:
   candidates = range(a.size())
  var best := length
  var best_i := -1
  var best_p := Vector3.ZERO
  for i in candidates:
   var p = Geometry3D.ray_intersects_triangle(from, dir, a[i], b[i], c[i])
   if p == null:
    continue
   var d: float = from.distance_to(p)
   if d < best:
    best = d; best_i = i; best_p = p
  if best_i < 0:
   return []
  var w := Geometry3D.get_triangle_barycentric_coords(best_p, a[best_i], b[best_i], c[best_i])
  var n := (na[best_i] * w.x + nb[best_i] * w.y + nc[best_i] * w.z).normalized()
  if n.dot(dir) > 0.:
   n = -n
  return [best_p, n]

# ---------------------------------------------------------------------------
# Tail-lamp: an oval lens wrapping the rear corner. Rays fan around a focus
# inside the tail, so the outer end follows the quarter panel; the lens dome
# and a rubber gasket sit on the sampled skin. The photographed lens texture
# (Kevauto, CC BY-SA 4.0) and the brake mask keep working through the UVs:
# red outboard, amber, then the clear reversing chamber inboard.
func tail_lamp(side: float, shell: Shell, lens_material: Material) -> ArrayMesh:
 var focus := Vector3(side * .33, LAMP_Y, 1.40)
 var fan := func(phi: float, y: float) -> Array:
  var dir := -Vector3(side * sin(phi), 0., cos(phi))
  return shell.hit(Vector3(focus.x, y, focus.z) - dir * 2.4, dir, 2.6)
 var phis := PackedFloat32Array()
 var arcs := PackedFloat32Array()
 var hits := []
 var arc := 0.
 var inboard_arc := -1.
 for k in 121:
  var phi := .0 + 1.45 * k / 120.
  var h: Array = fan.call(phi, LAMP_Y)
  if h.is_empty():
   continue
  if not hits.is_empty():
   arc += hits[-1][0].distance_to(h[0])
  hits.append(h); phis.append(phi); arcs.append(arc)
  if inboard_arc < 0. and absf(h[0].x) >= LAMP_INBOARD_X:
   inboard_arc = arc
 if hits.size() < 8 or inboard_arc < 0.:
  push_warning("Tail-lamp skin not found; keeping the boxed lamp")
  return null
 var span := minf(LAMP_SPAN, arc - inboard_arc - .015)
 report["lamp_%d_span_mm" % int(side)] = roundf(span * 1000.)
 var phi_of_u := func(u: float) -> float:
  var target := inboard_arc + (u + 1.) * .5 * span
  if target <= arcs[0]:
   return phis[0]
  for i in range(1, arcs.size()):
   if arcs[i] >= target:
    var t := (target - arcs[i - 1]) / maxf(1e-6, arcs[i] - arcs[i - 1])
    return lerpf(phis[i - 1], phis[i], t)
  return phis[-1]
 var st := SurfaceTool.new()
 st.begin(Mesh.PRIMITIVE_TRIANGLES)
 var gasket := SurfaceTool.new()
 gasket.begin(Mesh.PRIMITIVE_TRIANGLES)
 var rings := 8
 var spokes := 40
 var grid := [[]]   # [ring][spoke] -> [position, normal, uv]; ring 0 is the centre point
 var half_span := span * .5
 for r in range(1, rings + 2):   # rings 1..8 lens, ring 9 = gasket edge
  var row := []
  var radius := float(r) / rings
  var grow := Vector2(1., 1.)
  if r > rings:
   radius = 1.
   grow = Vector2(1. + .011 / half_span, 1. + .010 / LAMP_HALF_HEIGHT)
  for s in spokes:
   var theta := TAU * s / spokes
   var ct := cos(theta); var sn := sin(theta)
   var shape := pow(pow(absf(ct), 2.6) + pow(absf(sn), 2.6), -1. / 2.6)
   var u := radius * ct * shape * grow.x
   var w := radius * sn * shape * grow.y
   var h: Array = fan.call(phi_of_u.call(u), LAMP_Y + w * LAMP_HALF_HEIGHT)
   var shrink := 1.
   while h.is_empty() and shrink > .2:
    shrink -= .1
    h = fan.call(phi_of_u.call(u * shrink), LAMP_Y + w * shrink * LAMP_HALF_HEIGHT)
   if h.is_empty():
    h = hits[hits.size() / 2]
   if shrink < 1.:
    report["lamp_ray_retries"] = report.get("lamp_ray_retries", 0) + 1
   var dome := .0012
   if r <= rings:
    dome = .0028 + .011 * sqrt(maxf(0., 1. - radius * radius))
   row.append([h[0] + h[1] * dome, h[1], Vector2(.5 - .47 * clampf(u, -1., 1.), .5 - .45 * clampf(w, -1., 1.))])
  grid.append(row)
 var centre: Array = fan.call(phi_of_u.call(0.), LAMP_Y)
 var centre_point: Vector3 = centre[0] + centre[1] * .0138
 for s in spokes:
  var s2 := (s + 1) % spokes
  tri(st, centre_point, grid[1][s][0], grid[1][s2][0], Vector2(.5, .5), grid[1][s][2], grid[1][s2][2], centre[1])
  for r in range(1, rings + 1):
   var q00: Array = grid[r][s]; var q01: Array = grid[r][s2]
   var q10: Array = grid[r + 1][s]; var q11: Array = grid[r + 1][s2]
   var out: Vector3 = q00[1]
   var target := st if r < rings else gasket
   tri(target, q00[0], q10[0], q11[0], q00[2], q10[2], q11[2], out)
   tri(target, q00[0], q11[0], q01[0], q00[2], q11[2], q01[2], out)
 # Outer end of the lens at its centre line, where the bumper-cover seam starts.
 var outer: Array = fan.call(phi_of_u.call(1.06), LAMP_Y - LAMP_HALF_HEIGHT * .9)
 if not outer.is_empty():
  lamp_ends[side] = outer[0]
 st.index(); st.generate_normals()
 gasket.index(); gasket.generate_normals()
 var mesh := st.commit()
 var lens: StandardMaterial3D = lens_material.duplicate() if lens_material is StandardMaterial3D else StandardMaterial3D.new()
 lens.roughness = .12; lens.metallic = 0.; lens.metallic_specular = .7
 lens.clearcoat_enabled = true; lens.clearcoat = .8; lens.clearcoat_roughness = .06
 lens.cull_mode = BaseMaterial3D.CULL_BACK
 mesh.surface_set_material(0, lens)
 gasket.commit(mesh)
 mesh.surface_set_material(1, dark_trim)
 var c: Vector3 = centre[0]
 report["lamp_%d_centre" % int(side)] = [snappedf(c.x, .001), snappedf(c.y - DROP - .005, .001), snappedf(c.z, .001)]
 return mesh

# Adds a triangle whose front face looks along `outward`. Godot treats
# clockwise triangles as front faces, so the right-hand normal must point in.
# A non-zero `normal` is written to all three vertices.
static func tri(st: SurfaceTool, p0: Vector3, p1: Vector3, p2: Vector3, uv0: Vector2, uv1: Vector2, uv2: Vector2, outward: Vector3, normal := Vector3.ZERO) -> void:
 if (p1 - p0).cross(p2 - p0).dot(outward) > 0.:
  var tp := p1; p1 = p2; p2 = tp
  var tu := uv1; uv1 = uv2; uv2 = tu
 var lit := normal != Vector3.ZERO
 for corner in [[p0, uv0], [p1, uv1], [p2, uv2]]:
  if lit:
   st.set_normal(normal)
  st.set_uv(corner[1]); st.add_vertex(corner[0])

# The NB's high-mount stop lamp is a short red bar on the lid, a hand's width
# ahead of the tail; the lifted deck would otherwise split it at the edge.
func place_third_stop_lamp(shell: Shell) -> void:
 var chmsl: MeshInstance3D = model.find_child("body_red", true, false)
 if chmsl == null:
  return
 var box := chmsl.get_aabb()
 var target_z := 1.80
 var h := shell.hit(Vector3(0., 3., target_z), Vector3.DOWN, 3.)
 if h.is_empty():
  return
 var offset := Vector3(0., h[0].y + .001 - box.position.y, target_z - box.get_center().z)
 deform(chmsl, func(p: Vector3, _i: int) -> Vector3: return p + offset)

# ---------------------------------------------------------------------------
func number_plate(shell: Shell) -> MeshInstance3D:
 var h := shell.hit(Vector3(0., PLATE_Y, 3.), Vector3.FORWARD, 2.)
 if h.is_empty():
  return null
 var plate := MeshInstance3D.new()
 var face := QuadMesh.new()
 face.size = Vector2(.305, .152)
 var m := StandardMaterial3D.new()
 m.albedo_texture = preload("res://art/plate.png")
 m.roughness = .38; m.metallic = .05; m.metallic_specular = .6
 m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
 face.material = m
 plate.mesh = face
 var n: Vector3 = h[1]
 n = Vector3(n.x * .3, n.y * .5, n.z).normalized()   # hang it nearly upright
 var x := Vector3.UP.cross(n).normalized()
 plate.basis = Basis(x, n.cross(x), n)
 plate.position = h[0] + n * .006
 plate.layers = 2
 plate.name = "Plate"
 report["plate_centre_mm_above_ground"] = roundf((plate.position.y - DROP - .005) * 1000.)
 return plate

# ---------------------------------------------------------------------------
# Shut lines: thin near-black strips just above the paint, one merged mesh.
func shut_lines(shell: Shell) -> MeshInstance3D:
 var st := SurfaceTool.new()
 st.begin(Mesh.PRIMITIVE_TRIANGLES)
 var count := 0
 # Hood: the sides follow the model's fender crown (inboard of the peak), the
 # rear edge crosses in front of the windscreen, the front edge is the joint
 # with the nose cap between the headlamps.
 var crown := [[-1.72, .540], [-1.585, .554], [-1.43, .566], [-1.20, .583], [-1.05, .586], [-.90, .581], [-.70, .573], [-.50, .568]]
 for side in [-1., 1.]:
  var path := []
  var z := -1.66
  while z <= -.519:
   path.append([Vector3(side * table(crown, z), 3., z), Vector3.DOWN])
   z += .01
  count += tally("hood_side", strip(st, shell, path))
 var cowl := []
 var x := -.566
 while x <= .5661:
  cowl.append([Vector3(x, 3., -.52), Vector3.DOWN])
  x += .01
 count += tally("cowl", strip(st, shell, cowl))
 count += tally("nose", nose_seam(st))
 # Doors: front edge under the A-pillar base, rear edge with the rounded
 # lower corner, bottom edge over the sill.
 for side in [-1., 1.]:
  var inward := Vector3(-side, 0., 0.)
  var front := []
  var y := .31
  while y <= .745:
   front.append([Vector3(side * 2., y, -.47), inward])
   y += .01
  count += tally("door_front", strip(st, shell, front))
  var rear := []
  y = .775
  while y >= .42:
   rear.append([Vector3(side * 2., y, .64), inward])
   y -= .01
  for k in range(1, 13):
   var t := PI * .5 * k / 12.
   rear.append([Vector3(side * 2., .42 - .11 * sin(t), .53 + .11 * cos(t)), inward])
  var zz := .52
  while zz >= -.47:
   rear.append([Vector3(side * 2., .31, zz), inward])
   zz -= .01
  count += tally("door_rear", strip(st, shell, rear))
 # Trunk lid: leading edge behind the top well, sides along the deck that
 # roll over the tail to the lamps' inner top corners, trailing edge across.
 var lead := []
 x = -.55
 while x <= .5501:
  lead.append([Vector3(x, 3., 1.25), Vector3.DOWN])
  x += .01
 count += tally("trunk_lead", strip(st, shell, lead))
 for side in [-1., 1.]:
  var path := []
  var z := 1.25
  while z <= 1.60:
   path.append([Vector3(side * .55, 3., z), Vector3.DOWN])
   z += .01
  # Roll over the tail: rays from inside the tail, swept from up to back.
  var angle := 90.
  while angle >= 0.:
   var t := ramp(90., 18., angle)
   var px: float = side * lerpf(.55, LAMP_INBOARD_X + .02, t)
   var dir := Vector3(0., sin(deg_to_rad(angle)), cos(deg_to_rad(angle)))
   var probe := shell.hit(Vector3(px, .70, 1.60), dir, 1.)
   if probe.is_empty():
    angle -= 2.
    continue
   if probe[0].y < TRUNK_EDGE_Y:
    break
   path.append([probe[0] - probe[1] * .3, probe[1]])   # from outside, back onto the skin
   angle -= 2.
  count += tally("trunk_side", strip(st, shell, path))
 var trailing := []
 x = -LAMP_INBOARD_X - .02
 while x <= LAMP_INBOARD_X + .0201:
  trailing.append([Vector3(x, TRUNK_EDGE_Y, 3.), Vector3.FORWARD])
  x += .01
 count += tally("trunk_trailing", strip(st, shell, trailing))
 # Rear bumper cover: from each lamp's lower outer end forward to the arch.
 for side in [-1., 1.]:
  if not lamp_ends.has(side):
   continue
  var end: Vector3 = lamp_ends[side]
  var path := []
  var z: float = end.z
  while z >= 1.22:
   path.append([Vector3(side * 2., end.y, z), Vector3(-side, 0., 0.)])
   z -= .01
  count += tally("bumper_cover", strip(st, shell, path))
 # Fuel filler door on top of the right rear fender, behind the top.
 var ring := []
 for k in 49:
  var t := TAU * k / 48.
  var ct := cos(t); var sn := sin(t)
  var shape := pow(pow(absf(ct), 4.) + pow(absf(sn), 4.), -.25)
  ring.append([Vector3(.655 + .052 * ct * shape, 3., 1.17 + .052 * sn * shape), Vector3.DOWN])
 count += tally("fuel_door", strip(st, shell, ring))
 report["seam_points"] = count
 st.index()
 var node := MeshInstance3D.new()
 node.mesh = st.commit()
 node.mesh.surface_set_material(0, seam_ink)
 node.layers = 2
 node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
 node.name = "ShutLines"
 return node

# Joint between the hood and the nose cap ("Upper bumper"), between the lamps:
# the top boundary of the cap, sampled from its own vertices.
func nose_seam(st: SurfaceTool) -> int:
 var cap: MeshInstance3D = model.find_child("Upper bumper", true, false)
 var lamp_inner := INF
 for lamp in model.find_children("Headlamp*", "MeshInstance3D", true, false):
  if lamp.name.begins_with("Headlamp seal"):
   continue
  var box: AABB = lamp.get_aabb()
  lamp_inner = minf(lamp_inner, minf(absf(box.position.x), absf(box.end.x)))
 if cap == null or lamp_inner == INF:
  return 0
 var v: PackedVector3Array = cap.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
 var bins := {}
 for p in v:
  if absf(p.x) > lamp_inner + .01:
   continue
  var k := roundi(p.x / .02)
  if not bins.has(k) or p.y > bins[k].y:
   bins[k] = p
 var keys := bins.keys()
 keys.sort()
 var pts := []
 var nrm := []
 for k in keys:
  pts.append(bins[k]); nrm.append(Vector3(0., .62, -.78).normalized())
 return ribbon(st, pts, nrm)

func tally(name: String, points: int) -> int:
 report["seam_" + name] = report.get("seam_" + name, 0) + points
 return points

static func table(rows: Array, z: float) -> float:
 if z <= rows[0][0]:
  return rows[0][1]
 for i in range(1, rows.size()):
  if z <= rows[i][0]:
   return lerpf(rows[i - 1][1], rows[i][1], (z - rows[i - 1][0]) / (rows[i][0] - rows[i - 1][0]))
 return rows[-1][1]

# Projects [origin, direction] samples onto the shell and lays a strip over
# the longest continuous run of hits (a jump means the ray found another
# surface, such as the cockpit side above the beltline).
func strip(st: SurfaceTool, shell: Shell, path: Array) -> int:
 var runs := [[[], []]]
 for sample in path:
  var h := shell.hit(sample[0], sample[1], 4.)
  if h.is_empty():
   continue
  var run: Array = runs[-1]
  if not run[0].is_empty() and run[0][-1].distance_to(h[0]) > .06:
   run = [[], []]
   runs.append(run)
  run[0].append(h[0]); run[1].append(h[1])
 var best: Array = runs[0]
 for run in runs:
  if run[0].size() > best[0].size():
   best = run
 return ribbon(st, best[0], best[1])

static func ribbon(st: SurfaceTool, pts: Array, nrm: Array) -> int:
 if pts.size() < 2:
  return 0
 var half := SEAM_WIDTH * .5
 var corners := []
 for i in pts.size():
  var t: Vector3 = (pts[mini(i + 1, pts.size() - 1)] - pts[maxi(i - 1, 0)]).normalized()
  var n: Vector3 = nrm[i]
  var across := n.cross(t).normalized()
  var centre: Vector3 = pts[i] + n * SEAM_LIFT
  corners.append([centre - across * half, centre + across * half, n])
 for i in range(1, corners.size()):
  var p: Array = corners[i - 1]
  var q: Array = corners[i]
  tri(st, p[0], q[0], q[1], Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, p[2], p[2])
  tri(st, p[0], q[1], p[1], Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, p[2], p[2])
 return pts.size()

# ---------------------------------------------------------------------------
# Two black wipers parked along the foot of the windscreen, blades lying on the
# glass, arms a little above them, pivots on the cowl.
func wipers() -> MeshInstance3D:
 var node := MeshInstance3D.new()
 node.layers = 2
 node.name = "Wipers"
 var glass_part: MeshInstance3D = model.find_child("body_glass", true, false)
 if glass_part == null:
  return node
 var screen := Shell.new().add(glass_part.mesh, AABB(Vector3(-.8, .7, -.6), Vector3(1.6, .6, .7)))
 var st := SurfaceTool.new()
 st.begin(Mesh.PRIMITIVE_TRIANGLES)
 var blades := 0
 for span in [[-.60, -.13], [-.06, .43]]:
  var pts := []
  var nrm := []
  var x: float = span[0]
  while x <= span[1] + .0001:
   var h := screen.hit(Vector3(x, 3., -.405 + .02 * (x - span[0])), Vector3.DOWN, 3.)
   if not h.is_empty():
    pts.append(h[0]); nrm.append(h[1])
   x += .02
  if pts.size() < 2:
   continue
  blades += 1
  bar(st, pts, nrm, .010, .007, .013)    # blade with its rubber edge
  var arm := []
  for k in range(1, pts.size() - 1):
   arm.append(pts[k])
  bar(st, arm, nrm.slice(1, nrm.size() - 1), .021, .006, .006)   # arm above it
 report["wiper_blades"] = blades
 st.index(); st.generate_normals()
 node.mesh = st.commit()
 var black := StandardMaterial3D.new()
 black.albedo_color = Color("#0d0e0e"); black.roughness = .55; black.metallic = .1
 node.mesh.surface_set_material(0, black)
 return node

# A thin bar of the given width and height following points over a surface.
static func bar(st: SurfaceTool, pts: Array, nrm: Array, lift: float, width: float, height: float) -> void:
 if pts.size() < 2:
  return
 var rows := []
 for i in pts.size():
  var t: Vector3 = (pts[mini(i + 1, pts.size() - 1)] - pts[maxi(i - 1, 0)]).normalized()
  var n: Vector3 = nrm[i]
  var across := n.cross(t).normalized()
  var base: Vector3 = pts[i] + n * lift
  rows.append([base - across * width * .5, base + across * width * .5, base + across * width * .5 + n * height, base - across * width * .5 + n * height])
 for i in range(1, rows.size()):
  for f in 4:
   var a: Vector3 = rows[i - 1][f]; var b: Vector3 = rows[i - 1][(f + 1) % 4]
   var c: Vector3 = rows[i][(f + 1) % 4]; var d: Vector3 = rows[i][f]
   var centre: Vector3 = (rows[i - 1][0] + rows[i - 1][2]) * .5
   var out := ((a + b) * .5 - centre).normalized()
   tri(st, a, b, c, Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, out)
   tri(st, a, c, d, Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, out)

# ---------------------------------------------------------------------------
# Door mirror: the NB's bullet housing on a short stalk from the door top,
# about a third of the way back along the door, glass facing aft.
func mirror(side: float, shell: Shell) -> MeshInstance3D:
 var node := MeshInstance3D.new()
 node.layers = 2
 node.name = "Mirror" + ("R" if side > 0. else "L")
 var top := shell.hit(Vector3(side * .745, 3., -.12), Vector3.DOWN, 3.)
 var door_top: float = top[0].y if not top.is_empty() and top[0].y > .65 and top[0].y < .85 else .735
 report["door_top_at_mirror_mm_above_ground"] = roundf((door_top - DROP - .005) * 1000.)
 # Housing frame: x outward, y up, z aft. The glass face is at z = 0.
 var face_centre := Vector3(side * .885, door_top + .062, .035)
 var half := Vector2(.078, .049)     # glass face half-width, half-height
 var depth := .13                    # housing length ahead of the glass
 var housing := SurfaceTool.new()
 housing.begin(Mesh.PRIMITIVE_TRIANGLES)
 var rows := 14
 var around := 28
 var grid := []
 for k in rows + 1:
  var t := float(k) / rows               # 0 at the glass rim, 1 at the nose
  var s := sqrt(maxf(0., 1. - t * t)) if k < rows else 0.
  var lean := .30 * t * t                # the nose leans inboard toward the stalk
  var row := []
  for j in around:
   var a := TAU * j / around
   # local x points outward on either side; the nose leans inboard and down
   var local := Vector3(half.x * s * cos(a) - lean * half.x, half.y * s * sin(a) - .25 * t * t * half.y, -depth * t)
   row.append(face_centre + Vector3(local.x * side, local.y, local.z))
  grid.append(row)
 for k in rows:
  for j in around:
   var j2 := (j + 1) % around
   var out: Vector3 = (grid[k][j] - face_centre + Vector3(0., 0., depth * .5)).normalized()
   tri(housing, grid[k][j], grid[k + 1][j], grid[k + 1][j2], Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, out)
   tri(housing, grid[k][j], grid[k + 1][j2], grid[k][j2], Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, out)
 # A narrow lip around the glass, so the housing reads as a shell.
 for j in around:
  var j2 := (j + 1) % around
  var p0: Vector3 = grid[0][j]; var p1: Vector3 = grid[0][j2]
  var q0 := face_centre + (p0 - face_centre) * .9 + Vector3(0., 0., -.006)
  var q1 := face_centre + (p1 - face_centre) * .9 + Vector3(0., 0., -.006)
  tri(housing, p0, q0, q1, Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, Vector3.BACK)
  tri(housing, p0, q1, p1, Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, Vector3.BACK)
 housing.index(); housing.generate_normals()
 var mesh := housing.commit()
 mesh.surface_set_material(0, paint)
 # Stalk: a flattened, slightly swept post from the door top to the housing.
 var stalk := SurfaceTool.new()
 stalk.begin(Mesh.PRIMITIVE_TRIANGLES)
 var base := Vector3(side * .795, door_top - .004, -.075)
 var tip := face_centre + Vector3(-side * .045, -half.y * .55, -depth * .62)
 for j in 16:
  var a0 := TAU * j / 16.
  var a1 := TAU * (j + 1) / 16.
  var r0 := Vector3(.012 * cos(a0), 0., .030 * sin(a0))
  var r1 := Vector3(.012 * cos(a1), 0., .030 * sin(a1))
  var out := Vector3(cos(a0), 0., sin(a0))
  tri(stalk, base + r0, tip + r0 * .8, tip + r1 * .8, Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, out)
  tri(stalk, base + r0, tip + r1 * .8, base + r1, Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, out)
 stalk.index(); stalk.generate_normals()
 stalk.commit(mesh)
 mesh.surface_set_material(1, dark_trim)
 # Mirror glass, slightly inside the lip.
 var glass := SurfaceTool.new()
 glass.begin(Mesh.PRIMITIVE_TRIANGLES)
 var glass_centre := face_centre + Vector3(0., 0., -.005)
 for j in around:
  var j2 := (j + 1) % around
  var p0: Vector3 = glass_centre + (grid[0][j] - face_centre) * .88
  var p1: Vector3 = glass_centre + (grid[0][j2] - face_centre) * .88
  tri(glass, glass_centre, p0, p1, Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, Vector3.BACK)
 glass.generate_normals()
 glass.commit(mesh)
 var silver := StandardMaterial3D.new()
 silver.albedo_color = Color("#8f9a9c"); silver.metallic = 1.; silver.roughness = .05
 mesh.surface_set_material(2, silver)
 node.mesh = mesh
 report["mirror_centre_mm_above_ground"] = roundf((face_centre.y - DROP - .005) * 1000.)
 return node

# ---------------------------------------------------------------------------
# NB door handle: a flush body-colour pull over a dark finger recess,
# 150 x 30 mm, toward the rear of the door.
func door_handle(side: float, shell: Shell) -> MeshInstance3D:
 var node := MeshInstance3D.new()
 node.layers = 2
 node.name = "Handle" + ("R" if side > 0. else "L")
 var h := shell.hit(Vector3(side * 2., .66, .44), Vector3(-side, 0., 0.), 2.)
 if h.is_empty():
  return node
 var n: Vector3 = h[1]
 var along := Vector3.UP.cross(n).normalized()
 var up := n.cross(along).normalized()
 var st := SurfaceTool.new()
 st.begin(Mesh.PRIMITIVE_TRIANGLES)
 rounded_plate(st, h[0] + n * .0012, along, up, n, .086, .022, .02)
 st.index(); st.generate_normals()
 var mesh := st.commit()
 mesh.surface_set_material(0, dark_trim)
 var pull := SurfaceTool.new()
 pull.begin(Mesh.PRIMITIVE_TRIANGLES)
 var centre: Vector3 = h[0] + n * .0042 + up * .004
 rounded_plate(pull, centre, along, up, n, .074, .0115, .011)
 rounded_rim(pull, centre, along, up, n, .074, .0115, .011, .003)
 pull.index(); pull.generate_normals()
 pull.commit(mesh)
 mesh.surface_set_material(1, paint)
 node.mesh = mesh
 return node

# Flat rounded rectangle (half sizes hx, hy, corner radius r) facing `n`.
static func rounded_plate(st: SurfaceTool, centre: Vector3, ex: Vector3, ey: Vector3, n: Vector3, hx: float, hy: float, r: float) -> void:
 var outline := rounded_outline(hx, hy, r)
 for k in outline.size():
  var p0: Vector2 = outline[k]
  var p1: Vector2 = outline[(k + 1) % outline.size()]
  tri(st, centre, centre + ex * p0.x + ey * p0.y, centre + ex * p1.x + ey * p1.y, Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, n)

static func rounded_rim(st: SurfaceTool, centre: Vector3, ex: Vector3, ey: Vector3, n: Vector3, hx: float, hy: float, r: float, depth: float) -> void:
 var outline := rounded_outline(hx, hy, r)
 for k in outline.size():
  var p0: Vector2 = outline[k]
  var p1: Vector2 = outline[(k + 1) % outline.size()]
  var a := centre + ex * p0.x + ey * p0.y
  var b := centre + ex * p1.x + ey * p1.y
  var out := (ex * (p0.x + p1.x) + ey * (p0.y + p1.y)).normalized()
  tri(st, a, b, b - n * depth, Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, out)
  tri(st, a, b - n * depth, a - n * depth, Vector2.ZERO, Vector2.ZERO, Vector2.ZERO, out)

static func rounded_outline(hx: float, hy: float, r: float) -> Array:
 var out := []
 var corners := [Vector2(hx - r, hy - r), Vector2(-hx + r, hy - r), Vector2(-hx + r, -hy + r), Vector2(hx - r, -hy + r)]
 for q in 4:
  for k in 7:
   var a := PI * .5 * q + PI * .5 * k / 6.
   out.append(corners[q] + Vector2(cos(a), sin(a)) * r)
 return out

# Red side reflector on each rear bumper corner, under the lamp, ahead of the tail.
func side_reflector(side: float, shell: Shell) -> MeshInstance3D:
 var node := MeshInstance3D.new()
 node.layers = 2
 node.name = "Reflector" + ("R" if side > 0. else "L")
 var h := shell.hit(Vector3(side * 2., .525, 1.66), Vector3(-side, 0., 0.), 2.)
 if h.is_empty():
  return node
 var n: Vector3 = h[1]
 var along := Vector3.UP.cross(n).normalized()
 var up := n.cross(along).normalized()
 var st := SurfaceTool.new()
 st.begin(Mesh.PRIMITIVE_TRIANGLES)
 rounded_plate(st, h[0] + n * .003, along, up, n, .048, .0115, .006)
 rounded_rim(st, h[0] + n * .003, along, up, n, .048, .0115, .006, .003)
 st.index(); st.generate_normals()
 var mesh := st.commit()
 var red := StandardMaterial3D.new()
 red.albedo_color = Color("#8e0d12"); red.roughness = .18; red.metallic_specular = .8
 red.clearcoat_enabled = true; red.clearcoat = .6
 mesh.surface_set_material(0, red)
 node.mesh = mesh
 return node
