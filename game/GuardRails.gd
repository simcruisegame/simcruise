extends RefCounted
## Guard rails: mapped where someone mapped them, otherwise where a road
## engineer would put them.
##
## Mapped rails come first. The helper (helper/guard-rails.mjs) fetches OSM
## barrier=guard_rail and barrier=jersey_barrier ways for the tile's z14
## neighbourhood at runtime (never bundled: ODbL) and sends them as
## info.guard_rails rows [kind, [[x, y], ...]] in tile units. OSM positions are
## good to a few metres, the road ribbon (RoadSurface.gd) to centimetres, so a
## mapped rail is fitted to the ribbon it runs beside: each mapped way picks the
## road and side most of its points lie along, and points within MAPPED_SNAP_M
## of that shoulder are moved onto it (RAIL_OFFSET_M inside the edge) at the
## shoulder's height; farther points blend back to their mapped position on the
## ground over another MAPPED_SNAP_M. A rail never stands on the carriageway.
## W-beam faces the road; jersey barriers are concrete F-shape (810 mm).
##
## Elsewhere, and on roads nobody mapped, rails are placed by the warrant highway
## departments use (AASHTO Roadside Design Guide): a barrier where the ground
## falls away steeply beside the travelled way. For every road ribbon each
## station's outer shoulder edge is compared with the natural ground PROBE_M
## farther out (8 m); a drop of more than DROP_M (3 m, steeper than the 1:3 slope
## treated as non-recoverable) is a hazard. Hazard stretches shorter than
## MIN_RUN_M are ignored, runs are extended by LEAD_M at each end (rails start
## before the hazard and end after it), and close runs merge. An inferred rail
## gives way within MAPPED_CLEAR_M (15 m) of a mapped one.
##
## Each W-beam run is a galvanised rail (310 mm deep, top 0.79 m above the
## shoulder, the US standard height) on posts every 1.905 m (6 ft 3 in), with a
## collider so a car is caught rather than launched. --without-guard-rails turns
## all of them off.

const PROBE_M := 8.
const DROP_M := 3.
const MIN_RUN_M := 12.
const LEAD_M := 8.
const RAIL_OFFSET_M := .35
const POST_SPACING_M := 1.905
const RAIL_TOP_M := .79
const RAIL_DEPTH_M := .31
const MAPPED_CLEAR_M := 15.
const MAPPED_SNAP_M := 6.
const MAPPED_STEP_M := 2.
const BIN_M := 20.
const ROAD_SURFACE := preload("res://RoadSurface.gd")
const CORRIDOR := preload("res://DrivableCorridor.gd")
const CLIP_STEP_M := 1.
const MIN_PIECE_M := 6.
const TERMINAL_M := 7.6         # 25 ft turned-down end
const FLARE_M := .6
const TERMINAL_EDGE_M := 12.    # no terminal where a run is only cut by the tile edge
const POST_MIN_SCALE := .6
const JERSEY_TOP_M := .81
# Concrete F-shape safety shape: (half width, height), base to top.
const JERSEY_PROFILE := [Vector2(.305, 0.), Vector2(.305, .075), Vector2(.205, .33), Vector2(.12, JERSEY_TOP_M)]

static func build(tile) -> int:
 if "--without-guard-rails" in OS.get_cmdline_user_args():return 0
 var mapped := mapped_runs(tile)
 var inferred := inferred_runs(tile) if tile.road_surface != null else []
 # Inferred rails give way to mapped ones (including mapped rails just across
 # the tile edge, which the helper sends with a small margin).
 var clear_bins := {}
 for row in tile.info.get("guard_rails", []):
  for p in densify(row, tile.side):
   var key := Vector2i(floori(p.x / MAPPED_CLEAR_M), floori(p.y / MAPPED_CLEAR_M))
   if not clear_bins.has(key):clear_bins[key] = []
   clear_bins[key].append(p)
 var suppressed := 0.
 var runs := mapped.duplicate()
 for run in inferred:
  if clear_bins.is_empty():runs.append(run);continue
  var piece := PackedVector3Array();var piece_stations := PackedInt32Array()
  var pts: PackedVector3Array = run.pts
  for k in pts.size():
   var q := Vector2(pts[k].x, pts[k].z)
   if near_any(clear_bins, q, MAPPED_CLEAR_M):
    if k > 0:suppressed += Vector2(pts[k-1].x, pts[k-1].z).distance_to(q)
    if length(piece) >= MIN_RUN_M:runs.append({"pts":piece, "kind":"guard_rail", "face":run.face, "line":run.line, "stations":piece_stations})
    piece = PackedVector3Array();piece_stations = PackedInt32Array()
   else:piece.append(pts[k]);piece_stations.append(run.stations[k])
  if length(piece) >= MIN_RUN_M:runs.append({"pts":piece, "kind":"guard_rail", "face":run.face, "line":run.line, "stations":piece_stations})
 tile.info["guard_rail_source"] = tile.info.get("guard_rails_status", "")
 tile.info["guard_rail_mapped_runs"] = mapped.size()
 tile.info["guard_rail_suppressed_m"] = snappedf(suppressed, .1)
 # No rail on any drivable road or junction, and a terminal at each end.
 if CORRIDOR.enabled():runs = clip(tile, runs)
 if runs.is_empty():return 0
 var rail := SurfaceTool.new();rail.begin(Mesh.PRIMITIVE_TRIANGLES)
 var concrete := SurfaceTool.new();concrete.begin(Mesh.PRIMITIVE_TRIANGLES)
 var has_rail := false;var has_concrete := false
 var posts := []
 var mapped_focus := []          # Transform3D at the middle of each mapped run (checks/RailView.gd)
 var faces := PackedVector3Array()
 # W-beam cross-section: (offset toward traffic, height below the top) — two ridges.
 var profile := [Vector2(0., 0.), Vector2(.06, .05), Vector2(.02, .155), Vector2(.06, .26), Vector2(0., RAIL_DEPTH_M)]
 var mapped_m := 0.;var inferred_m := 0.
 for run in runs:
  var pts: PackedVector3Array = run.pts
  var jersey: bool = run.kind == "jersey_barrier"
  var face: float = run.face
  var top := JERSEY_TOP_M if jersey else RAIL_TOP_M
  # Height share per point: 1 along the run, falling to 0 over a terminal.
  var scale: PackedFloat32Array = run.get("scale", PackedFloat32Array())
  if run.has("mapped"):
   var mid := pts.size() / 2;var dir := pts[mini(mid + 1, pts.size() - 1)] - pts[maxi(mid - 1, 0)];dir.y = 0.
   if dir.length() > 1e-3:mapped_focus.append(Transform3D(Basis.looking_at(dir.normalized(), Vector3.UP), pts[mid]))
   mapped_m += length(pts)
  else:inferred_m += length(pts)
  var along := 0.;var next_post := 0.
  for k in range(1, pts.size()):
   var p: Vector3 = pts[k];var p0: Vector3 = pts[k-1]
   var seg := Vector2(p.x - p0.x, p.z - p0.z).length()
   if seg < 1e-4:continue
   var s0 := scale[k-1] if k - 1 < scale.size() else 1.;var s1 := scale[k] if k < scale.size() else 1.
   var t0 := (p - p0);t0.y = 0.;t0 = t0.normalized();var o0 := Vector3(-t0.z, 0., t0.x) * face
   if jersey:
    has_concrete = true
    for m in JERSEY_PROFILE.size() * 2 - 1:
     var u: Vector2 = section_point(m);var w: Vector2 = section_point(m + 1)
     var a0 := p0 + o0 * u.x + Vector3.UP * u.y * s0;var b0 := p0 + o0 * w.x + Vector3.UP * w.y * s0
     var a1 := p + o0 * u.x + Vector3.UP * u.y * s1;var b1 := p + o0 * w.x + Vector3.UP * w.y * s1
     for v in [a0, a1, b1, a0, b1, b0]:concrete.add_vertex(v)
   else:
    has_rail = true
    # Posts every POST_SPACING_M along the run, standing on the shoulder
    # (none where a terminal has turned the beam down to the ground).
    while next_post <= along + seg:
     var f := clampf((next_post - along) / seg, 0., 1.)
     var q: Vector3 = p0.lerp(p, f)
     if lerpf(s0, s1, f) >= POST_MIN_SCALE:posts.append(Transform3D(Basis.looking_at(t0, Vector3.UP), q + Vector3.UP * (RAIL_TOP_M * .5 - .05)))
     next_post += POST_SPACING_M
    for m in profile.size() - 1:
     var a0: Vector3 = p0 + o0 * profile[m].x + Vector3.UP * (RAIL_TOP_M * s0 - profile[m].y)
     var b0: Vector3 = p0 + o0 * profile[m+1].x + Vector3.UP * (RAIL_TOP_M * s0 - profile[m+1].y)
     var a1: Vector3 = p + o0 * profile[m].x + Vector3.UP * (RAIL_TOP_M * s1 - profile[m].y)
     var b1: Vector3 = p + o0 * profile[m+1].x + Vector3.UP * (RAIL_TOP_M * s1 - profile[m+1].y)
     for v in [a0, a1, b1, a0, b1, b0]:rail.add_vertex(v)
   along += seg
   # Collider: a wall from the ground to the rail top along the run; over a
   # terminal it ramps down with the beam, so a car meeting the end rides up
   # it instead of stopping on a blunt face.
   if maxf(s0, s1) * top < .1:continue
   var g0 := p0 + Vector3.UP * .05;var g1 := p + Vector3.UP * .05
   var h0 := p0 + Vector3.UP * maxf(.05, top * s0);var h1 := p + Vector3.UP * maxf(.05, top * s1)
   faces.append_array(PackedVector3Array([g0, h0, h1, g0, h1, g1]))
 var mesh := ArrayMesh.new()
 if has_rail:
  rail.generate_normals()
  var steel := StandardMaterial3D.new()
  steel.albedo_color = Color("#a4a9ab");steel.metallic = .65;steel.roughness = .42
  steel.cull_mode = BaseMaterial3D.CULL_DISABLED
  rail.commit(mesh);mesh.surface_set_material(mesh.get_surface_count() - 1, steel)
 if has_concrete:
  concrete.generate_normals()
  var cement := StandardMaterial3D.new()
  cement.albedo_color = Color("#b4afa5");cement.roughness = .9
  cement.cull_mode = BaseMaterial3D.CULL_DISABLED
  concrete.commit(mesh);mesh.surface_set_material(mesh.get_surface_count() - 1, cement)
 var node := MeshInstance3D.new();node.mesh = mesh;node.name = "GuardRails";tile.add_child(node)
 if not mapped_focus.is_empty():node.set_meta("mapped_focus", mapped_focus)
 if not posts.is_empty():
  var post_mesh := BoxMesh.new();post_mesh.size = Vector3(.15, RAIL_TOP_M + .1, .15)
  var wood := StandardMaterial3D.new();wood.albedo_color = Color("#6f6a62");wood.roughness = .8
  post_mesh.material = wood
  var mm := MultiMesh.new();mm.transform_format = MultiMesh.TRANSFORM_3D;mm.mesh = post_mesh;mm.instance_count = posts.size()
  for i in posts.size():mm.set_instance_transform(i, posts[i])
  var post_node := MultiMeshInstance3D.new();post_node.multimesh = mm;post_node.name = "GuardRailPosts";tile.add_child(post_node)
 if not faces.is_empty():
  var shape := ConcavePolygonShape3D.new();shape.backface_collision = true;shape.set_faces(faces)
  tile.collider(shape,"RailCollider")
 tile.info["guard_rail_runs"] = runs.size()
 tile.info["guard_rail_mapped_m"] = snappedf(mapped_m, .1)
 tile.info["guard_rail_inferred_m"] = snappedf(inferred_m, .1)
 return runs.size()

# Jersey cross-section walked from the traffic-side base over the top to the
# far base, as (offset toward traffic, height).
static func section_point(m: int) -> Vector2:
 var n := JERSEY_PROFILE.size()
 if m < n:return JERSEY_PROFILE[m]
 var q: Vector2 = JERSEY_PROFILE[2 * n - 1 - m]
 return Vector2(-q.x, q.y)

static func length(pts: PackedVector3Array) -> float:
 var total := 0.
 for k in range(1, pts.size()):total += Vector2(pts[k].x - pts[k-1].x, pts[k].z - pts[k-1].z).length()
 return total

static func near_any(bins: Dictionary, q: Vector2, reach: float) -> bool:
 var cell := Vector2i(floori(q.x / reach), floori(q.y / reach))
 for dx in [-1, 0, 1]:
  for dy in [-1, 0, 1]:
   for p in bins.get(cell + Vector2i(dx, dy), []):
    if q.distance_to(p) <= reach:return true
 return false

# A mapped row's points in tile metres, no farther apart than MAPPED_STEP_M.
static func densify(row, side: float) -> PackedVector2Array:
 var out := PackedVector2Array()
 if not row is Array or row.size() < 2 or not row[1] is Array:return out
 var pts: Array = row[1]
 for k in pts.size():
  var p := Vector2(float(pts[k][0]), float(pts[k][1])) * side
  if k > 0:
   var a: Vector2 = out[out.size() - 1]
   var steps := ceili(a.distance_to(p) / MAPPED_STEP_M)
   for s in range(1, steps):out.append(a.lerp(p, float(s) / steps))
  out.append(p)
 return out

# Where point p sits relative to one ribbon line near station i: the centre and
# left normal interpolated along the nearer segment, lateral offset (+ left) and
# the shoulder-edge height on p's side.
static func frame(line: Dictionary, i: int, p: Vector2) -> Dictionary:
 var sections: Array = line.sections
 var best := {}
 for j in [i - 1, i]:
  if j < 0 or j + 1 >= sections.size():continue
  var a := Vector2(sections[j][2].x, sections[j][2].z);var b := Vector2(sections[j+1][2].x, sections[j+1][2].z)
  var ab := b - a
  if ab.length_squared() < 1e-6:continue
  var t := clampf((p - a).dot(ab) / ab.length_squared(), 0., 1.)
  var c := a.lerp(b, t)
  var d := p.distance_to(c)
  if not best.is_empty() and d >= best.d:continue
  var na := Vector2(sections[j][4].x - sections[j][2].x, sections[j][4].z - sections[j][2].z)
  var nb := Vector2(sections[j+1][4].x - sections[j+1][2].x, sections[j+1][4].z - sections[j+1][2].z)
  var outer := lerpf(na.length(), nb.length(), t)
  var n := na.lerp(nb, t).normalized()
  var lateral := (p - c).dot(n)
  var e := 4 if lateral >= 0. else 0
  best = {"d":d, "c":c, "n":n, "lateral":lateral, "outer":outer, "sign":1. if lateral >= 0. else -1., "i":j if t < .5 else j + 1,
   "edge_h":lerpf(sections[j][e].y, sections[j+1][e].y, t)}
 return best

static func mapped_runs(tile) -> Array:
 var rows: Array = tile.info.get("guard_rails", [])
 var runs := []
 if rows.is_empty():return runs
 var lines: Array = tile.road_surface.lines if tile.road_surface != null else []
 var bins := {}
 for li in lines.size():
  var sections: Array = lines[li].sections
  for i in sections.size():
   var c: Vector3 = sections[i][2]
   var key := Vector2i(floori(c.x / BIN_M), floori(c.z / BIN_M))
   if not bins.has(key):bins[key] = []
   bins[key].append(Vector2i(li, i))
 var reach_extra := 2. * MAPPED_SNAP_M
 for row in rows:
  var kind := str(row[0]) if row is Array and row.size() > 0 else ""
  if kind != "guard_rail" and kind != "jersey_barrier":continue
  var pts := densify(row, tile.side)
  if pts.size() < 2:continue
  # Nearest ribbon centreline for each point, and which road and side the
  # way as a whole runs along (votes from points within snapping reach).
  var nearest := []
  var votes := {}
  for p in pts:
   var cell := Vector2i(floori(p.x / BIN_M), floori(p.y / BIN_M))
   var found := {};var found_line := -1
   for dx in [-1, 0, 1]:
    for dy in [-1, 0, 1]:
     for hit in bins.get(cell + Vector2i(dx, dy), []):
      var f := frame(lines[hit.x], hit.y, p)
      if f.is_empty():continue
      if found.is_empty() or f.d < found.d:found = f;found_line = hit.x
   nearest.append([found_line, found])
   if found_line >= 0 and absf(found.lateral) - found.outer <= reach_extra:
    var vote := Vector2i(found_line, int(found.sign))
    votes[vote] = votes.get(vote, 0) + 1
  var chosen := Vector2i(-1, 0);var most := 0
  for v in votes:
   if votes[v] > most:most = votes[v];chosen = v
  var placed := PackedVector3Array()
  var placed_stations := PackedInt32Array()    # station on the chosen road (DrivableCorridor own-road test)
  var clear := PackedByteArray();clear.resize(pts.size())
  var face_sum := 0.
  for k in pts.size():
   var p: Vector2 = pts[k]
   var ground := Vector3(p.x, tile.height_at(p), p.y)
   var f: Dictionary = {}
   if chosen.x >= 0:
    if nearest[k][0] == chosen.x:f = nearest[k][1]
    else:
     # Nearest station on the chosen road only.
     var cell := Vector2i(floori(p.x / BIN_M), floori(p.y / BIN_M))
     for dx in [-1, 0, 1]:
      for dy in [-1, 0, 1]:
       for hit in bins.get(cell + Vector2i(dx, dy), []):
        if hit.x != chosen.x:continue
        var g := frame(lines[hit.x], hit.y, p)
        if not g.is_empty() and (f.is_empty() or g.d < f.d):f = g
   var q := ground
   if not f.is_empty() and int(f.sign) == chosen.y:
    var beyond: float = absf(f.lateral) - f.outer
    var w := 1. - smoothstep(MAPPED_SNAP_M, 2. * MAPPED_SNAP_M, beyond)
    var on_shoulder: Vector2 = f.c + f.n * f.sign * (f.outer - RAIL_OFFSET_M)
    var snapped := Vector3(on_shoulder.x, f.edge_h, on_shoulder.y)
    q = ground.lerp(snapped, w)
    # Never on the carriageway, whatever the blend.
    if absf((Vector2(q.x, q.z) - f.c).dot(f.n)) < f.outer - RAIL_OFFSET_M:q = snapped
   # A point left on any carriageway (bad geometry, the far side of the
   # road, a junction) is dropped; the run breaks there.
   var any: Dictionary = nearest[k][1]
   clear[k] = 1
   if not any.is_empty():
    var lat := absf((Vector2(q.x, q.z) - any.c).dot(any.n))
    if lat < any.outer - ROAD_SURFACE.SHOULDER_M:clear[k] = 0
   placed.append(q)
   placed_stations.append(int(f.i) if not f.is_empty() else -1)
  # W-beam faces the road it runs along.
  for k in range(1, placed.size()):
   var t := Vector2(placed[k].x - placed[k-1].x, placed[k].z - placed[k-1].z)
   if t.length_squared() < 1e-8:continue
   var o := Vector2(-t.y, t.x).normalized()
   var f2: Dictionary = nearest[k][1]
   if f2.is_empty():continue
   face_sum += o.dot(f2.c - Vector2(placed[k].x, placed[k].z))
  var face := 1. if face_sum >= 0. else -1.
  # Keep only segments this tile owns (midpoint inside), so a rail crossing a
  # tile edge is drawn once and its two halves meet.
  var line := "L%d" % chosen.x if chosen.x >= 0 else ""
  var piece := PackedVector3Array();var piece_stations := PackedInt32Array()
  for k in range(1, placed.size()):
   var mid := Vector2(placed[k].x + placed[k-1].x, placed[k].z + placed[k-1].z) * .5
   var owned: bool = mid.x >= 0. and mid.y >= 0. and mid.x < tile.side and mid.y < tile.side and clear[k-1] == 1 and clear[k] == 1
   if owned:
    if piece.is_empty():piece.append(placed[k-1]);piece_stations.append(placed_stations[k-1])
    piece.append(placed[k]);piece_stations.append(placed_stations[k])
   elif piece.size() >= 2:
    runs.append({"pts":piece, "kind":kind, "face":face, "mapped":true, "line":line, "stations":piece_stations})
    piece = PackedVector3Array();piece_stations = PackedInt32Array()
   else:piece = PackedVector3Array();piece_stations = PackedInt32Array()
  if piece.size() >= 2:runs.append({"pts":piece, "kind":kind, "face":face, "mapped":true, "line":line, "stations":piece_stations})
 return runs

static func inferred_runs(tile) -> Array:
 var runs := []
 var lines: Array = tile.road_surface.lines
 for li in lines.size():
  var line: Dictionary = lines[li]
  if str(line.cls) in ["service","track"]:continue
  var sections: Array = line.sections
  if sections.size() < 3:continue
  for side in [0, 4]:            # outer shoulder edge: 0 right, 4 left of travel
   var hazard := PackedByteArray();hazard.resize(sections.size())
   var station := PackedFloat32Array();station.resize(sections.size())
   for i in sections.size():
    var centre: Vector3 = sections[i][2];var edge: Vector3 = sections[i][side]
    if i > 0:station[i] = station[i-1] + Vector2(centre.x, centre.z).distance_to(Vector2(sections[i-1][2].x, sections[i-1][2].z))
    var out := Vector2(edge.x - centre.x, edge.z - centre.z).normalized()
    var probe := Vector2(edge.x, edge.z) + out * PROBE_M
    if not Rect2(0, 0, tile.side, tile.side).has_point(probe):continue
    if not is_nan(tile.road_surface.height(probe)):continue      # another road, not a drop
    hazard[i] = 1 if edge.y - tile.base_height(probe) > DROP_M else 0
   # Runs of hazard by station, lengthened at both ends, short ones dropped.
   var i := 0
   while i < sections.size():
    if hazard[i] == 0:i += 1;continue
    var j := i
    while j + 1 < sections.size() and (hazard[j+1] == 1 or (j + 3 < sections.size() and hazard[j+3] == 1)):j += 1
    if station[j] - station[i] >= MIN_RUN_M:
     var a := i;var b := j
     while a > 0 and station[i] - station[a-1] <= LEAD_M:a -= 1
     while b + 1 < sections.size() and station[b+1] - station[j] <= LEAD_M:b += 1
     var pts := PackedVector3Array();var stations := PackedInt32Array()
     for k in range(a, b + 1):
      stations.append(k)
      var centre: Vector3 = sections[k][2];var edge: Vector3 = sections[k][side]
      var inward := (centre - edge);inward.y = 0.;inward = inward.normalized()
      pts.append(edge + inward * RAIL_OFFSET_M)
     # The road is on the travel-left of a right-side rail (0) and the
     # travel-right of a left-side rail (4).
     runs.append({"pts":pts, "kind":"guard_rail", "face":1. if side == 0 else -1., "line":"L%d" % li, "stations":stations})
    i = j + 1
 return runs

## Cut every run where it would stand on the drivable corridor (another road,
## a junction, a deck; DrivableCorridor.gd), drop the short leftovers, and give
## each remaining end a terminal. Runs are tested every CLIP_STEP_M.
static func clip(tile, runs: Array) -> Array:
 var corridor = CORRIDOR.for_tile(tile)
 var out := []
 var before := 0.;var after := 0.;var ends := 0
 for run in runs:
  var pts: PackedVector3Array = run.pts
  var stations: PackedInt32Array = run.get("stations", PackedInt32Array())
  var line: String = run.get("line", "")
  var top := JERSEY_TOP_M if run.kind == "jersey_barrier" else RAIL_TOP_M
  var dense := PackedVector3Array();var dense_stations := PackedInt32Array()
  for k in pts.size():
   var st := stations[k] if k < stations.size() else -1
   if k > 0:
    var a: Vector3 = pts[k-1]
    var steps := ceili(Vector2(pts[k].x - a.x, pts[k].z - a.z).length() / CLIP_STEP_M)
    var previous := stations[k-1] if k - 1 < stations.size() else -1
    for s in range(1, steps):
     dense.append(a.lerp(pts[k], float(s) / steps));dense_stations.append(previous if s * 2 < steps else st)
   dense.append(pts[k]);dense_stations.append(st)
  before += length(dense)
  var keep := PackedByteArray();keep.resize(dense.size())
  for k in dense.size():
   var p: Vector3 = dense[k]
   keep[k] = 0 if corridor.blocked(Vector2(p.x, p.z), p.y, p.y + top, "", line, dense_stations[k]) else 1
  var k := 0
  while k < dense.size():
   if keep[k] == 0:k += 1;continue
   var j := k
   while j + 1 < dense.size() and keep[j+1] == 1:j += 1
   var piece := dense.slice(k, j + 1)
   if length(piece) >= MIN_PIECE_M:
    var r: Dictionary = run.duplicate()
    r["pts"] = piece;r["stations"] = dense_stations.slice(k, j + 1)
    ends += terminals(tile, r, corridor)
    after += length(r.pts)
    out.append(r)
   k = j + 1
 tile.info["guard_rail_clipped_m"] = snappedf(before - after, .1)
 tile.info["guard_rail_terminals"] = ends
 return out

## Turned-down terminals: over the last TERMINAL_M (or half a short run) the
## beam and its collider fall to the ground and flare FLARE_M away from the
## road, so the end is a ramp, not a blunt face. An end the tile edge made
## (the run goes on in the neighbouring tile) keeps full height. Returns the
## number of terminals made.
static func terminals(tile, run: Dictionary, corridor) -> int:
 var pts: PackedVector3Array = run.pts
 var n := pts.size()
 var scale := PackedFloat32Array();scale.resize(n);scale.fill(1.)
 run["scale"] = scale
 if n < 2:return 0
 var along := PackedFloat32Array();along.resize(n)
 for k in range(1, n):along[k] = along[k-1] + Vector2(pts[k].x - pts[k-1].x, pts[k].z - pts[k-1].z).length()
 var total := along[n-1]
 var reach := minf(TERMINAL_M, total * .5)
 var inner := Rect2(0, 0, tile.side, tile.side).grow(-TERMINAL_EDGE_M)
 var top := JERSEY_TOP_M if run.kind == "jersey_barrier" else RAIL_TOP_M
 var made := 0
 for end in [0, n - 1]:
  if not inner.has_point(Vector2(pts[end].x, pts[end].z)):continue
  made += 1
  for k in n:
   var d: float = along[k] if end == 0 else total - along[k]
   if d >= reach:continue
   var u := d / reach
   scale[k] = minf(scale[k], u)
   var a: Vector3 = pts[maxi(k - 1, 0)];var b: Vector3 = pts[mini(k + 1, n - 1)]
   var t := Vector2(b.x - a.x, b.z - a.z)
   if t.length_squared() < 1e-8:continue
   t = t.normalized()
   # (−t.y, t.x)·face points toward traffic; the flare goes the other way.
   var away := -Vector2(-t.y, t.x) * float(run.face)
   var flared: Vector3 = pts[k] + Vector3(away.x, 0., away.y) * FLARE_M * (1. - u) * (1. - u)
   if not corridor.blocked(Vector2(flared.x, flared.z), flared.y, flared.y + top * u, "", run.get("line", ""), -1):pts[k] = flared
 run["pts"] = pts
 run["scale"] = scale
 return made
