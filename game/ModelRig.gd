extends RefCounted
## Fits a downloaded car model (Sketchfab glTF) to a CarSpec so CarBody.gd can
## drive it like a generated model:
##
##  * Finds the four wheels: by node name when the author separated them
##    ("Front Left Wheel", "wheel_fl", "FL_wheel" …), otherwise by geometry —
##    the four ground-contact patches give the axles and track, and every
##    triangle inside a tyre radius of a hub is cut out of the body into that
##    wheel (models exported "merged by material" fuse wheels into the body).
##  * Scales the model so its measured wheelbase equals the spec's, turns it so
##    the front points along -Z, centres it between the axles and sets the hub
##    height to the suspension's static position, all baked into new meshes in
##    the vehicle frame. Wheel meshes are re-centred on their hub.
##  * Maps the author's material names onto the roles CarBody.gd knows
##    (lamp_rear, glass) and hides badge surfaces.
##
##  * Finds the cabin steering wheel and splits it out as its own node
##    "steering_wheel", origin at the rim centre and local +Z along the column
##    toward the driver, so CarBody.gd can turn it (see find_steering_wheel).
##  * Orients a model without named wheels from its manifest entry (the same
##    "flip" that turns traffic around), unless the car file says "flip".
##
## Returns a Node3D with children body, wheel_fl/fr/rl/rr and (when found)
## steering_wheel, or null. The spec's model_rig may hold:
##   "flip": bool          the model faces the other way
##   "relabel": bool       recompute left/right of named wheels
##   "steering_wheel": {"node": "name"} | {"centre": [x,y,z], "axis": [x,y,z],
##       "radius": m} (vehicle frame, axis toward the driver) | {"none": true}
## and the manifest entry's "branded" list may hold, besides brand names,
## {"material": "name", "box": [[x,y,z],[x,y,z]]} areas (vehicle frame) whose
## triangles are badges to remove.

const MANIFEST := "res://art/traffic/manifest.json"
# Badge, logo and maker-name surfaces: by material, texture, mesh or node name.
const BADGE_PATTERN := "(?i)badge|badging|logo|emblem|brand|invisible|unimportant"
# Steering-wheel names in the languages model authors use.
const STEER_PATTERN := "(?i)steer|wheel_s(?![a-z])|volant|lenkrad|lenkung|stuur|volante"
# A steering wheel rim: 0.24-0.48 m across (real ones are 14-18 in; some
# downloaded models draw them smaller).
const RIM_RADIUS_MIN := .12
const RIM_RADIUS_MAX := .24

const WHEEL_NAMES := {
 "wheel_fl":["front left wheel","front_left_wheel","wheel_fl","fl_wheel","wheel.fl","wheelfl","wheel_lf"],
 "wheel_fr":["front right wheel","front_right_wheel","wheel_fr","fr_wheel","wheel.fr","wheelfr","wheel_rf"],
 "wheel_rl":["rear left wheel","rear_left_wheel","wheel_rl","rl_wheel","wheel.rl","wheelrl","wheel_lr"],
 "wheel_rr":["rear right wheel","rear_right_wheel","wheel_rr","rr_wheel","wheel.rr","wheelrr","wheel_rr"],
}
const ORDER := ["wheel_fl","wheel_fr","wheel_rl","wheel_rr"]

var log_lines: Array = []

static func relative(root: Node, node: Node) -> Transform3D:
 var t := Transform3D.IDENTITY
 var n := node
 while n != null and n != root:
  if n is Node3D:t = (n as Node3D).transform * t
  n = n.get_parent()
 return t

## Some uploads are not Y-up after their own root transform (an OBJ converted
## twice can lie on its side or upside down). A car is lower than it is wide or
## long, so the shortest axis is up; tyre surfaces sit below the middle of the body.
func upright(parts: Array) -> void:
 var lo := Vector3.INF;var hi := -Vector3.INF
 for p in parts:
  for v in p[0][Mesh.ARRAY_VERTEX]:
   var w: Vector3 = p[2] * v;lo = lo.min(w);hi = hi.max(w)
 var size := hi - lo
 var turn := Basis.IDENTITY
 if size.x < size.y and size.x <= size.z:turn = Basis(Vector3.FORWARD, PI * .5)
 elif size.z < size.y and size.z < size.x:turn = Basis(Vector3.RIGHT, -PI * .5)
 var tyre := Vector3.ZERO;var tyres := 0;var mid := (lo + hi) * .5
 for p in parts:
  var label: String = ((str(p[1].resource_name) if p[1] else "") + p[3]).to_lower()
  if not ("tire" in label or "tyre" in label):continue
  for v in p[0][Mesh.ARRAY_VERTEX]:
   tyre += turn * (p[2] * v);tyres += 1
 if tyres > 0 and (tyre / tyres).y > (turn * mid).y:turn = Basis(Vector3.RIGHT, PI) * turn
 if turn == Basis.IDENTITY:return
 note("upright: turned %s" % turn.get_euler())
 for p in parts:p[2] = Transform3D(turn, Vector3.ZERO) * p[2]

func note(s: String) -> void:
 log_lines.append(s)

## Every mesh surface as [arrays, material, transform-to-root, owner name].
func collect(root: Node) -> Array:
 var parts := []
 for mi in root.find_children("*", "MeshInstance3D", true, false):
  if mi.mesh == null or not mi.visible:continue
  var t := relative(root, mi)
  var owner_name := ""
  var n: Node = mi
  while n != null and n != root:
   owner_name += " " + str(n.name).to_lower();n = n.get_parent()
  owner_name += " " + mi.mesh.resource_name.to_lower()
  for s in mi.mesh.get_surface_count():
   if mi.mesh.surface_get_primitive_type(s) != Mesh.PRIMITIVE_TRIANGLES:continue
   var mat: Material = mi.get_surface_override_material(s) if s < mi.get_surface_override_material_count() and mi.get_surface_override_material(s) else mi.mesh.surface_get_material(s)
   parts.append([mi.mesh.surface_get_arrays(s), mat, t, owner_name])
 return parts

func named_wheel(owner_name: String) -> String:
 for key in WHEEL_NAMES:
  for alias in WHEEL_NAMES[key]:
   if owner_name.contains(alias):return key
 return ""

func build(scene_path: String, spec, rig: Dictionary = {}) -> Node3D:
 var root: Node = null
 if scene_path.begins_with("res://"):
  var packed = load(scene_path)
  if not packed is PackedScene:return null
  root = packed.instantiate()
 else:
  # A private model outside the project (user://cars/<id>.glb): read at run
  # time, never imported or committed.
  var doc := GLTFDocument.new();var state := GLTFState.new()
  if doc.append_from_file(scene_path, state) != OK:return null
  var scene: Node = doc.generate_scene(state)
  if scene == null:return null
  # Keep the scene root's own transform (Sketchfab exports turn Z-up to Y-up
  # there): the editor importer wraps it the same way.
  root = Node3D.new();root.add_child(scene)
 var parts := collect(root)
 root.free()
 if parts.is_empty():return null
 upright(parts)

 # Root-space bounds: the glTF importer has already made +Y up.
 var lo := Vector3.INF;var hi := -Vector3.INF
 for p in parts:
  for v in p[0][Mesh.ARRAY_VERTEX]:
   var w: Vector3 = p[2] * v;lo = lo.min(w);hi = hi.max(w)
 var size := hi - lo
 var long_x := size.x > size.z
 note("bounds %s size %s long axis %s" % [lo, size, "X" if long_x else "Z"])

 # Wheel centres in root space, by name or by ground-contact geometry.
 var centres := {}
 var by_name := {}
 for p in parts:
  var key := named_wheel(p[3])
  if key == "":continue
  if not by_name.has(key):by_name[key] = AABB(p[2] * p[0][Mesh.ARRAY_VERTEX][0], Vector3.ZERO)
  for v in p[0][Mesh.ARRAY_VERTEX]:by_name[key] = by_name[key].expand(p[2] * v)
 var named := by_name.size() == 4
 if named:
  for key in by_name:centres[key] = by_name[key].get_center()
 else:
  centres = contact_wheels(parts, lo, hi, long_x, spec)
  if centres.is_empty():
   note("could not find four wheels");return null
 # Tyre radius in model units: half the named wheel height, or from the spec.
 var axle_front: Vector3 = (centres.wheel_fl + centres.wheel_fr) * .5
 var axle_rear: Vector3 = (centres.wheel_rl + centres.wheel_rr) * .5
 var wheelbase_model := axle_front.distance_to(axle_rear)
 var s: float = spec.wheelbase_m / maxf(wheelbase_model, 1e-6)
 note("%s wheels, model wheelbase %.3f units, scale %.5f m/unit" % ["named" if named else "geometric", wheelbase_model, s])

 # Frame: forward (-Z in the vehicle) points from the rear axle to the front.
 var fwd := (axle_front - axle_rear);fwd.y = 0.;fwd = fwd.normalized()
 var entry := manifest_entry(scene_path)
 if rig.has("flip") or named:
  if rig.get("flip", false):fwd = -fwd
 elif entry.has("flip"):
  # The manifest's flip is checked on the traffic lineup (AmbientLife.normalise:
  # long axis turned onto Z, front toward -Z unless flipped). Geometric wheels
  # put the front at the positive end of the long axis, so turn when the
  # manifest's front is the negative end.
  var front_positive: bool = (not bool(entry.flip)) if long_x else bool(entry.flip)
  if not front_positive:fwd = -fwd
  note("front from the manifest: %s end of %s" % ["positive" if front_positive else "negative", "X" if long_x else "Z"])
 var up := Vector3.UP
 var right := fwd.cross(up).normalized()   # vehicle +X
 var mid := (axle_front + axle_rear) * .5
 # Hub height in the vehicle frame at static ride: mount minus static travel.
 var hub_y: float = spec.mount_height_m - spec.static_length_m
 var centre_y: float = (centres.wheel_fl.y + centres.wheel_fr.y + centres.wheel_rl.y + centres.wheel_rr.y) * .25
 var basis := Basis(right * s, up * s, -fwd * s).transposed()
 # to_vehicle(v) = basis * (v - origin_model) + (0, hub_y, 0)
 var origin_model := Vector3(mid.x, centre_y, mid.z)
 var to_vehicle := Transform3D(basis, Vector3(0, hub_y, 0) - basis * origin_model)
 # Left/right as the vehicle sees them (names in a mirrored export can lie).
 if not named or rig.get("relabel", false):
  var relabel := {}
  for key in centres:
   var v: Vector3 = to_vehicle * centres[key]
   relabel[("wheel_f" if v.z < 0. else "wheel_r") + ("l" if v.x < 0. else "r")] = centres[key]
  if relabel.size() == 4:centres = relabel
 var hub := {}
 for key in centres:hub[key] = to_vehicle * centres[key]
 var radius_m: float = spec.tire_radius_m()
 note("hubs (vehicle frame): " + ", ".join(ORDER.map(func(k): return "%s %s" % [k, hub[k].snapped(Vector3.ONE * .001)])))

 # Rebuild every surface in the vehicle frame, cutting wheel triangles out
 # into per-wheel meshes centred on their hub.
 var body_mesh := ArrayMesh.new()
 var wheel_tools := {}
 for key in ORDER:wheel_tools[key] = {}
 var hide := RegEx.create_from_string(BADGE_PATTERN)
 var hidden := 0
 # Every surface as [material, arrays, transform to the vehicle frame,
 # {destination: triangle indices}, owner and material names].
 var jobs := []
 for p in parts:
  var arrays: Array = p[0];var mat: Material = p[1];var t: Transform3D = to_vehicle * p[2]
  if hide.search(surface_label(mat, p[3])) != null:hidden += 1;continue
  var label := (mat.resource_name if mat else "") + " " + str(p[3])
  mat = role_material(mat)
  var key := named_wheel(p[3]) if named else ""
  var split := {}
  if named:split[key if key != "" else "body"] = all_indices(arrays)
  else:
   # Geometric split: a triangle belongs to a wheel when all three corners lie
   # inside the tyre cylinder (radius + 4 %) and within a tyre width of the hub.
   split = split_triangles(arrays, t, hub, radius_m * 1.04, spec.tire_width_m * .75)
  jobs.append([mat, arrays, t, split, label, p[1].resource_name if p[1] else ""])
 hidden += hide_badge_areas(jobs, entry)
 if hidden:note("badges hidden: %d surfaces or areas" % hidden)
 var steer := find_steering_wheel(jobs, spec, rig, hub)
 var out := Node3D.new();out.name = "rigged"
 for j in jobs:
  for k in j[3]:
   if k == "body":add_surface({"body":body_mesh}, j[0], j[1], j[2], Vector3.ZERO, j[3][k])
   elif k == "steering_wheel":pass
   else:add_surface(wheel_tools[k], j[0], j[1], j[2], -hub[k], j[3][k])
 if not steer.is_empty():
  var pivot: Transform3D = steer.pivot
  var tools := {}
  for j in jobs:
   if j[3].has("steering_wheel"):add_surface(tools, j[0], j[1], pivot.affine_inverse() * j[2], Vector3.ZERO, j[3].steering_wheel)
  var sw := Node3D.new();sw.name = "steering_wheel";sw.transform = pivot
  var swm := ArrayMesh.new()
  for m in tools:
   tools[m][1].set_material(tools[m][0]);tools[m][1].commit(swm)
  var swi := MeshInstance3D.new();swi.mesh = swm;sw.add_child(swi)
  out.add_child(sw)
 var body := MeshInstance3D.new();body.name = "body";body.mesh = body_mesh;out.add_child(body)
 for key in ORDER:
  var node := Node3D.new();node.name = key
  var mesh := ArrayMesh.new()
  for m in wheel_tools[key]:
   var st: SurfaceTool = wheel_tools[key][m][1]
   st.set_material(wheel_tools[key][m][0]);st.commit(mesh)
  var mi := MeshInstance3D.new();mi.mesh = mesh;node.add_child(mi)
  node.position = hub[key]
  out.add_child(node)
 note("body surfaces %d, wheel surfaces %s" % [body_mesh.get_surface_count(), ORDER.map(func(k): return wheel_tools[k].size())])
 return out

static var manifest_cache := {}

## The traffic manifest's entry for a model scene ({} when it has none).
static func manifest_entry(scene_path: String) -> Dictionary:
 if manifest_cache.is_empty():
  manifest_cache["_"] = true
  if FileAccess.file_exists(MANIFEST):
   var m = JSON.parse_string(FileAccess.get_file_as_string(MANIFEST))
   if m is Dictionary:
    for e in m.get("models", []):manifest_cache[str(e.get("scene", ""))] = e
 return manifest_cache.get(scene_path, {})

## Names a surface is known by: material, its textures, the mesh and its nodes.
static func surface_label(mat: Material, owner_name: String) -> String:
 var s := owner_name
 if mat:
  s += " " + mat.resource_name
  if mat is BaseMaterial3D:
   for tex in [mat.albedo_texture, mat.emission_texture, mat.normal_texture]:
    if tex:s += " " + tex.resource_path.get_file() + " " + tex.resource_name
 return s

static func all_indices(arrays: Array) -> PackedInt32Array:
 var idx = arrays[Mesh.ARRAY_INDEX]
 if idx != null:return PackedInt32Array(idx)
 var out := PackedInt32Array();out.resize(arrays[Mesh.ARRAY_VERTEX].size())
 for i in out.size():out[i] = i
 return out

## Badges a manifest entry lists for models whose badges share a material
## and texture with the body, so no name finds them. {"material": name}
## removes that material; with "box": [[x,y,z],[x,y,z]] (vehicle frame) it
## removes every connected part (vertices welded at 0.5 mm) lying wholly
## inside the box, so a grille bar or bonnet panel that reaches outside it
## stays. "note" names the badge in the log.
func hide_badge_areas(jobs: Array, entry: Dictionary) -> int:
 var count := 0
 for area in entry.get("branded", []):
  if not area is Dictionary:continue
  var which := []
  for j in range(jobs.size()):
   if area.has("material") and str(jobs[j][5]) != str(area.material):continue
   which.append(j)
  var removed := 0
  if not area.has("box"):
   for j in which:
    for k in jobs[j][3]:removed += jobs[j][3][k].size() / 3;jobs[j][3][k] = PackedInt32Array()
  else:
   var a: Array = area.box[0];var b: Array = area.box[1]
   var box := AABB(Vector3(a[0], a[1], a[2]), Vector3(b[0] - a[0], b[1] - a[1], b[2] - a[2])).abs()
   var body_jobs := which.filter(func(j): return jobs[j][3].has("body"))
   var gone := {}
   for part in connected_parts(jobs, body_jobs, box):
    if part.clipped:continue
    for tri in part.tris:
     if not gone.has(tri[0]):gone[tri[0]] = {}
     gone[tri[0]][Vector3i(tri[1], tri[2], tri[3])] = true
   for j in gone:
    var keep := PackedInt32Array();var list: PackedInt32Array = jobs[j][3].body
    for n in range(0, list.size(), 3):
     if gone[j].has(Vector3i(list[n], list[n+1], list[n+2])):removed += 1;continue
     keep.append_array(PackedInt32Array([list[n], list[n+1], list[n+2]]))
    jobs[j][3].body = keep
  note("badge %s: %d triangles removed" % [str(area.get("note", area.get("box", area.get("material", "")))), removed])
  if removed:count += 1
 return count

## The cabin steering wheel, split out of the body triangles of the jobs (in
## place: they move to a "steering_wheel" destination). Returns {"pivot":
## Transform3D} (rim centre, +Z along the column toward the driver, +Y the
## rim's top) or {} when no wheel is found.
##
## In order: the car file's explicit override; surfaces whose node, mesh or
## material name says steering wheel; otherwise geometry. The geometric search
## looks at the left half of the cabin (left-hand drive) between the front
## axle and the middle of the car, splits its triangles into connected parts
## (vertices welded at 0.5 mm) and takes the part that is most like a rim: a
## flat ring 0.28-0.48 m across, closed around its centre, facing rearward.
## The rim's plane gives the column axis (its normal). Every other part lying
## wholly inside the rim's cylinder, from 12 cm behind its plane to 9 cm in
## front (spokes, hub, airbag cover, badge), turns with it; the column shroud
## and the dashboard, which reach outside, stay in the body.
func find_steering_wheel(jobs: Array, spec, rig: Dictionary, hub: Dictionary) -> Dictionary:
 var over: Dictionary = rig.get("steering_wheel", {})
 if over.get("none", false):return {}
 var eye: Vector3 = spec.driver_eye
 var front_z: float = (hub.wheel_fl.z + hub.wheel_fr.z) * .5
 var rear_z: float = (hub.wheel_rl.z + hub.wheel_rr.z) * .5
 var hub_y: float = hub.wheel_fl.y
 var half_width: float = maxf(absf(hub.wheel_fl.x), absf(hub.wheel_fr.x)) + .2
 var ring := {}
 var how := ""
 if over.has("centre") and over.has("axis"):
  var c: Array = over.centre;var a: Array = over.axis
  ring = {"centre":Vector3(c[0], c[1], c[2]), "normal":Vector3(a[0], a[1], a[2]).normalized(), "radius":float(over.get("radius", .19))}
  how = "set in the car file"
 else:
  var pattern := RegEx.create_from_string(STEER_PATTERN)
  var named_parts := []
  for j in range(jobs.size()):
   var label: String = jobs[j][4]
   var wanted: bool = label.to_lower().contains(str(over.node).to_lower()) if over.has("node") else pattern.search(label) != null
   if wanted and jobs[j][3].has("body"):named_parts.append(j)
  if not named_parts.is_empty():
   # Every triangle of the named parts belongs to the wheel; the rim fit
   # only places the pivot.
   var parts := connected_parts(jobs, named_parts, AABB(Vector3(-50, -50, -50), Vector3(100, 100, 100)))
   var best := best_ring(parts, Vector3.INF)
   if best.is_empty():best = ring_of(merge_parts(parts))
   if not best.is_empty():
    for j in named_parts:jobs[j][3]["steering_wheel"] = jobs[j][3].body;jobs[j][3].body = PackedInt32Array()
    ring = best;how = "by name"
  if ring.is_empty():
   var region := AABB(Vector3(-half_width, hub_y + .15, front_z - .1), Vector3(half_width - .05, 1.5, (rear_z - front_z) * .6))
   var body_jobs := []
   for j in range(jobs.size()):
    if jobs[j][3].has("body"):body_jobs.append(j)
   var parts := connected_parts(jobs, body_jobs, region)
   ring = best_ring(parts, eye)
   how = "by geometry"
 if ring.is_empty():
  note("steering wheel: not found");return {}
 var n: Vector3 = ring.normal
 if n.z < 0.:n = -n   # toward the driver, who sits behind the wheel
 var centre: Vector3 = ring.centre;var r: float = ring.radius
 # Everything wholly inside the rim's cylinder turns with it.
 var near := AABB(centre - Vector3.ONE * (r + .15), Vector3.ONE * (r + .15) * 2.)
 var body_jobs := []
 for j in range(jobs.size()):
  if jobs[j][3].has("body"):body_jobs.append(j)
 var moved := 0
 for part in connected_parts(jobs, body_jobs, near):
  if part.clipped:continue
  var inside := true
  for v in part.points:
   var d: Vector3 = v - centre;var axial := d.dot(n)
   if axial < -.12 or axial > .09 or (d - n * axial).length() > r * 1.08:inside = false;break
  if not inside:continue
  for tri in part.tris:
   var j: int = tri[0];var dest: Dictionary = jobs[j][3]
   if not dest.has("steering_wheel"):dest["steering_wheel"] = PackedInt32Array()
   dest.steering_wheel.append_array(PackedInt32Array([tri[1], tri[2], tri[3]]))
   moved += 1
 # Remove the moved triangles from the body lists.
 for j in body_jobs:
  if not jobs[j][3].has("steering_wheel") or jobs[j][3].body.is_empty():continue
  var taken := {}
  var sw: PackedInt32Array = jobs[j][3].steering_wheel
  for k in range(0, sw.size(), 3):taken[Vector3i(sw[k], sw[k+1], sw[k+2])] = true
  var keep := PackedInt32Array();var list: PackedInt32Array = jobs[j][3].body
  for k in range(0, list.size(), 3):
   if not taken.has(Vector3i(list[k], list[k+1], list[k+2])):keep.append_array(PackedInt32Array([list[k], list[k+1], list[k+2]]))
  jobs[j][3].body = keep
 var up := (Vector3.UP - n * n.dot(Vector3.UP)).normalized()
 var pivot := Transform3D(Basis(up.cross(n), up, n), centre)
 note("steering wheel %s: centre %s, column %s (%.0f° from horizontal), rim radius %.3f m, %d triangles" % [how, centre.snapped(Vector3.ONE * .001), n.snapped(Vector3.ONE * .001), rad_to_deg(asin(clampf(n.y, -1., 1.))), r, moved])
 return {"pivot":pivot, "radius":r}

## Connected parts of the given jobs' body triangles lying wholly in region:
## [{"tris":[[job, a, b, c], …], "points":PackedVector3Array}], vertices
## welded by position (0.5 mm) so UV seams do not split a part.
func connected_parts(jobs: Array, which: Array, region: AABB) -> Array:
 var weld := {}           # Vector3i -> welded id
 var parent := PackedInt32Array()
 var position := PackedVector3Array()
 var tris := []
 var clipped := {}        # welded ids of triangles that leave the region
 for j in which:
  var verts: PackedVector3Array = jobs[j][1][Mesh.ARRAY_VERTEX];var t: Transform3D = jobs[j][2]
  var list: PackedInt32Array = jobs[j][3].body
  var local := {}
  for k in range(0, list.size(), 3):
   var ids := PackedInt32Array()
   var outside := 0
   for m in 3:
    var vi: int = list[k + m]
    var id: int = local.get(vi, -1)
    if id == -1:
     var w: Vector3 = t * verts[vi]
     if region.has_point(w):
      var key := Vector3i((w * 2000.).round())
      id = weld.get(key, -1)
      if id < 0:id = position.size();weld[key] = id;position.append(w);parent.append(id)
     else:id = -2
     local[vi] = id
    if id == -2:outside += 1
    else:ids.append(id)
   if outside == 3:continue
   if outside > 0:
    for id in ids:clipped[id] = true
    continue
   tris.append([j, list[k], list[k+1], list[k+2], ids[0]])
   uf_union(parent, ids[0], ids[1]);uf_union(parent, ids[0], ids[2])
 var groups := {}
 for tri in tris:
  var root := uf_find(parent, tri[4])
  if not groups.has(root):groups[root] = {"tris":[], "ids":{}, "clipped":false}
  groups[root].tris.append(tri)
 for id in position.size():
  var root := uf_find(parent, id)
  if groups.has(root):
   groups[root].ids[id] = true
   if clipped.has(id):groups[root].clipped = true
 var out := []
 for g in groups.values():
  var pts := PackedVector3Array()
  for id in g.ids:pts.append(position[id])
  out.append({"tris":g.tris, "points":pts, "clipped":g.clipped})
 return out

static func uf_find(parent: PackedInt32Array, i: int) -> int:
 while parent[i] != i:
  parent[i] = parent[parent[i]];i = parent[i]
 return i

static func uf_union(parent: PackedInt32Array, a: int, b: int) -> void:
 a = uf_find(parent, a);b = uf_find(parent, b)
 if a != b:parent[maxi(a, b)] = mini(a, b)

static func merge_parts(parts: Array) -> PackedVector3Array:
 var pts := PackedVector3Array()
 for p in parts:pts.append_array(p.points)
 return pts

## The most rim-like part, or {}. Near an expected eye point (not INF), a
## part is also kept only when it sits ahead of and below the eye.
func best_ring(parts: Array, eye: Vector3) -> Dictionary:
 var best := {};var best_score := 0.
 for part in parts:
  if part.points.size() < 24 or part.clipped:continue
  var r := ring_of(part.points)
  if r.is_empty():continue
  if eye != Vector3.INF:
   var c: Vector3 = r.centre
   if c.x > -.08 or absf(c.x - eye.x) > .45:continue
  var score: float = r.quality * pow(float(part.points.size()), .25)
  if score > best_score:best_score = score;best = r
 return best

## Ring fit of a point set: centroid, plane normal (smallest principal axis),
## radius (95th percentile in the plane) and a quality in 0..1, or {} when the
## points are not a flat closed ring of steering-wheel size facing rearward.
func ring_of(pts: PackedVector3Array) -> Dictionary:
 if pts.size() < 12:return {}
 var c := Vector3.ZERO
 for p in pts:c += p
 c /= pts.size()
 var cov := Basis(Vector3.ZERO, Vector3.ZERO, Vector3.ZERO)
 for p in pts:
  var d := p - c
  cov.x += d * d.x;cov.y += d * d.y;cov.z += d * d.z
 var n := smallest_axis(cov)
 if absf(n.z) < .55:return reject("normal %s" % n, c)
 var radii := PackedFloat32Array();var axial := 0.
 for p in pts:
  var d := p - c;var a := d.dot(n)
  axial += a * a;radii.append((d - n * a).length())
 radii.sort()
 var r: float = radii[int(radii.size() * .95) - 1] if radii.size() > 20 else radii[radii.size() - 1]
 if r < RIM_RADIUS_MIN or r > RIM_RADIUS_MAX:return reject("radius %.3f" % r, c)
 if sqrt(axial / pts.size()) > .045:return reject("thick %.3f r %.3f" % [sqrt(axial / pts.size()), r], c)
 # A rim: most points near the outer edge, all the way round. The edge is
 # taken per sixteenth of the circle, so a flat-bottomed or squared-off
 # wheel (C8) counts as well as a round one.
 var u := (Vector3.UP - n * n.dot(Vector3.UP)).normalized();var v := n.cross(u)
 var edge := PackedFloat32Array();edge.resize(16)
 var sector := PackedInt32Array();sector.resize(pts.size())
 var flat_r := PackedFloat32Array();flat_r.resize(pts.size())
 for i in pts.size():
  var d := pts[i] - c;var flat := d - n * d.dot(n)
  sector[i] = int(floor((atan2(flat.dot(v), flat.dot(u)) + PI) / TAU * 16.)) % 16
  flat_r[i] = flat.length()
  edge[sector[i]] = maxf(edge[sector[i]], flat_r[i])
 var on_rim := 0;var bins := 0
 for b in 16:
  if edge[b] >= r * .6:bins += 1
 for i in pts.size():
  if edge[sector[i]] >= r * .6 and flat_r[i] >= edge[sector[i]] * .72:on_rim += 1
 var share := float(on_rim) / pts.size()
 if share < .4 or bins < 15:return reject("share %.2f bins %d r %.3f n %s" % [share, bins, r, n], c)
 return {"centre":c, "normal":n, "radius":r, "quality":share * bins / 16.}

var debug_steer := OS.has_environment("RIG_DEBUG_STEER")
func reject(why: String, c: Vector3) -> Dictionary:
 if debug_steer:note("  ring rejected at %s: %s" % [c.snapped(Vector3.ONE * .01), why])
 return {}

## Eigenvector of the smallest eigenvalue of a symmetric 3×3 matrix (Jacobi).
static func smallest_axis(m: Basis) -> Vector3:
 var a := [[m.x.x, m.y.x, m.z.x], [m.x.y, m.y.y, m.z.y], [m.x.z, m.y.z, m.z.z]]
 var vec := [[1., 0., 0.], [0., 1., 0.], [0., 0., 1.]]
 for sweep in 12:
  for pq in [[0, 1], [0, 2], [1, 2]]:
   var p: int = pq[0];var q: int = pq[1]
   if absf(a[p][q]) < 1e-12:continue
   var theta: float = (a[q][q] - a[p][p]) / (2. * a[p][q])
   var t: float = signf(theta) / (absf(theta) + sqrt(theta * theta + 1.)) if theta != 0. else 1.
   var cs := 1. / sqrt(t * t + 1.);var sn := t * cs
   for k in 3:
    var akp: float = a[k][p];var akq: float = a[k][q]
    a[k][p] = cs * akp - sn * akq;a[k][q] = sn * akp + cs * akq
   for k in 3:
    var apk: float = a[p][k];var aqk: float = a[q][k]
    a[p][k] = cs * apk - sn * aqk;a[q][k] = sn * apk + cs * aqk
   for k in 3:
    var vkp: float = vec[k][p];var vkq: float = vec[k][q]
    vec[k][p] = cs * vkp - sn * vkq;vec[k][q] = sn * vkp + cs * vkq
 var i := 0
 for k in 3:
  if a[k][k] < a[i][i]:i = k
 return Vector3(vec[0][i], vec[1][i], vec[2][i]).normalized()

## Material roles CarBody.gd looks for, from the author's names.
func role_material(mat: Material) -> Material:
 if mat == null:return mat
 var n := mat.resource_name.to_lower()
 if n.contains("brake_light") or n.contains("brake light") or n.contains("taillight") or n.contains("tail_light") or n.contains("rear_light"):
  var m: Material = mat.duplicate();m.resource_name = "lamp_rear";return m
 if n.contains("windshield") or n.contains("glass") or n.contains("window"):
  var g: Material = mat.duplicate();g.resource_name = "glass";return g
 return mat

## Ground-contact patches → four wheel centres (model space).
func contact_wheels(parts: Array, lo: Vector3, hi: Vector3, long_x: bool, spec) -> Dictionary:
 var length := (hi.x - lo.x) if long_x else (hi.z - lo.z)
 # A first scale guess from overall length vs. wheelbase (cars: WB ≈ 0.58 L).
 var guess_r: float = spec.tire_radius_m() / (spec.wheelbase_m / (length * .58))
 var band := lo.y + guess_r * .12
 var pts := PackedVector3Array()
 for p in parts:
  for v in p[0][Mesh.ARRAY_VERTEX]:
   var w: Vector3 = p[2] * v
   if w.y <= band:pts.append(w)
 if pts.size() < 8:return {}
 var mid := (lo + hi) * .5
 var groups := {"n-":[],"n+":[],"p-":[],"p+":[]}
 for w in pts:
  var along := (w.x - mid.x) if long_x else (w.z - mid.z)
  var across := (w.z - mid.z) if long_x else (w.x - mid.x)
  groups[("n" if along < 0. else "p") + ("-" if across < 0. else "+")].append(w)
 var centres := {}
 for g in groups:
  if groups[g].is_empty():return {}
  var c := Vector3.ZERO
  for w in groups[g]:c += w
  c /= groups[g].size()
  c.y = lo.y + guess_r
  centres[g] = c
 # Refine the radius: wheelbase from the patches gives the true scale.
 var wb: float = ((centres["n-"] + centres["n+"]) * .5).distance_to((centres["p-"] + centres["p+"]) * .5)
 var r_model: float = spec.tire_radius_m() * wb / spec.wheelbase_m
 for g in centres:centres[g].y = lo.y + r_model
 note("contact patches: %s, tyre radius %.3f units" % [groups.keys().map(func(k): return groups[k].size()), r_model])
 # Provisional names (n = negative end): build() relabels in the vehicle frame,
 # and the front is decided by spec.model_rig.flip (default: positive end).
 return {"wheel_fl":centres["p-"],"wheel_fr":centres["p+"],"wheel_rl":centres["n-"],"wheel_rr":centres["n+"]}

## Triangle index lists per destination ("body" or a wheel key).
func split_triangles(arrays: Array, t: Transform3D, hub: Dictionary, radius: float, half_width: float) -> Dictionary:
 var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
 var idx = arrays[Mesh.ARRAY_INDEX]
 var count: int = idx.size() if idx != null else verts.size()
 var inside := PackedStringArray();inside.resize(verts.size())
 for i in verts.size():
  var w := t * verts[i]
  var hit := ""
  for k in hub:
   var d: Vector3 = w - hub[k]
   if absf(d.x) <= half_width and Vector2(d.y, d.z).length() <= radius:hit = k;break
  inside[i] = hit
 var out := {"body":PackedInt32Array()}
 for k in hub:out[k] = PackedInt32Array()
 var tri := 0
 while tri + 2 < count:
  var a: int = idx[tri] if idx != null else tri
  var b: int = idx[tri+1] if idx != null else tri+1
  var c: int = idx[tri+2] if idx != null else tri+2
  var k := inside[a]
  var dest := k if k != "" and inside[b] == k and inside[c] == k else "body"
  out[dest].append(a);out[dest].append(b);out[dest].append(c)
  tri += 3
 return out

## Append a surface (or a triangle subset) transformed by t, then shifted.
func add_surface(target: Dictionary, mat: Material, arrays: Array, t: Transform3D, shift: Vector3, subset) -> void:
 var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
 var normals = arrays[Mesh.ARRAY_NORMAL]
 var tangents = arrays[Mesh.ARRAY_TANGENT]
 var idx = arrays[Mesh.ARRAY_INDEX]
 var indices: PackedInt32Array
 if subset is PackedInt32Array and subset.size() > 0:indices = subset
 elif subset is PackedInt32Array:return
 elif idx != null:indices = idx
 else:
  indices = PackedInt32Array();indices.resize(verts.size())
  for i in verts.size():indices[i] = i
 # Compact the vertex set to what these triangles use.
 var remap := {}
 var nv := PackedVector3Array();var nn := PackedVector3Array();var nt := PackedFloat32Array()
 var nuv := PackedVector2Array();var nuv2 := PackedVector2Array();var ncol := PackedColorArray()
 var uv = arrays[Mesh.ARRAY_TEX_UV];var uv2 = arrays[Mesh.ARRAY_TEX_UV2];var col = arrays[Mesh.ARRAY_COLOR]
 var basis := t.basis
 var nbasis := basis.inverse().transposed()
 var out_idx := PackedInt32Array();out_idx.resize(indices.size())
 for j in indices.size():
  var i: int = indices[j]
  if not remap.has(i):
   remap[i] = nv.size()
   nv.append(t * verts[i] + shift)
   if normals != null:nn.append((nbasis * normals[i]).normalized())
   if tangents != null:
    var tv: Vector3 = (basis * Vector3(tangents[i*4], tangents[i*4+1], tangents[i*4+2])).normalized()
    nt.append_array([tv.x, tv.y, tv.z, tangents[i*4+3]])
   if uv != null:nuv.append(uv[i])
   if uv2 != null:nuv2.append(uv2[i])
   if col != null:ncol.append(col[i])
  out_idx[j] = remap[i]
 # A mirroring transform flips winding; keep faces outward.
 if basis.determinant() < 0.:
  for j in range(0, out_idx.size(), 3):
   var tmp := out_idx[j+1];out_idx[j+1] = out_idx[j+2];out_idx[j+2] = tmp
 var a := [];a.resize(Mesh.ARRAY_MAX)
 a[Mesh.ARRAY_VERTEX] = nv;a[Mesh.ARRAY_INDEX] = out_idx
 if nn.size():a[Mesh.ARRAY_NORMAL] = nn
 if nt.size():a[Mesh.ARRAY_TANGENT] = nt
 if nuv.size():a[Mesh.ARRAY_TEX_UV] = nuv
 if nuv2.size():a[Mesh.ARRAY_TEX_UV2] = nuv2
 if ncol.size():a[Mesh.ARRAY_COLOR] = ncol
 if target.has("body"):
  var mesh: ArrayMesh = target.body
  if mesh.get_surface_count() >= 250:return
  mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, a)
  mesh.surface_set_material(mesh.get_surface_count()-1, mat)
 else:
  # Wheels: one SurfaceTool per material.
  var key := str(mat.get_instance_id()) if mat else "none"
  if not target.has(key):
   var st := SurfaceTool.new();st.begin(Mesh.PRIMITIVE_TRIANGLES)
   target[key] = [mat, st]
  var m := ArrayMesh.new();m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, a)
  target[key][1].append_from(m, 0, Transform3D.IDENTITY)
