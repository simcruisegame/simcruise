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
## Returns a Node3D with children body and wheel_fl/fr/rl/rr, or null.
## The spec's model_rig {"flip":bool} turns a model that faces backwards.

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
 var packed = load(scene_path)
 if not packed is PackedScene:return null
 var root: Node = packed.instantiate()
 var parts := collect(root)
 root.free()
 if parts.is_empty():return null

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
 if rig.get("flip", false):fwd = -fwd
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
 var hide := RegEx.create_from_string("(?i)badge|logo|emblem|invisible|unimportant")
 for p in parts:
  var arrays: Array = p[0];var mat: Material = p[1];var t: Transform3D = to_vehicle * p[2]
  var mat_name := mat.resource_name if mat else ""
  if hide.search(mat_name) != null:continue
  mat = role_material(mat)
  var key := named_wheel(p[3]) if named else ""
  if named and key != "":
   add_surface(wheel_tools[key], mat, arrays, t, -hub[key], [])
   continue
  if named:
   add_surface({"body":body_mesh}, mat, arrays, t, Vector3.ZERO, [])
   continue
  # Geometric split: a triangle belongs to a wheel when all three corners lie
  # inside the tyre cylinder (radius + 4 %) and within a tyre width of the hub.
  var split := split_triangles(arrays, t, hub, radius_m * 1.04, spec.tire_width_m * .75)
  for k in split:
   if k == "body":add_surface({"body":body_mesh}, mat, arrays, t, Vector3.ZERO, split[k])
   else:add_surface(wheel_tools[k], mat, arrays, t, -hub[k], split[k])
 var out := Node3D.new();out.name = "rigged"
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
