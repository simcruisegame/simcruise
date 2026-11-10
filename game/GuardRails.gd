extends RefCounted
## Guard rails where a road engineer would put them.
##
## Mapped rails (OSM barrier=guard_rail) are sparse outside interstates, and the
## public Overpass servers that serve them are often saturated. So rails are
## placed by the warrant highway departments use (AASHTO Roadside Design Guide):
## a barrier where the ground falls away steeply beside the travelled way. For
## every road ribbon (RoadSurface.gd) each station's outer shoulder edge is
## compared with the natural ground PROBE_M farther out (8 m); a drop of more than
## DROP_M (3 m, steeper than the 1:3 slope treated as non-recoverable) is a
## hazard. Hazard stretches shorter than MIN_RUN_M are
## ignored, runs are extended by LEAD_M at each end (rails start before the
## hazard and end after it), and close runs merge.
##
## Each run is a galvanised W-beam rail (310 mm deep, top 0.79 m above the
## shoulder, the US standard height) on posts every 1.905 m (6 ft 3 in), set
## RAIL_OFFSET_M inside the shoulder edge, with a collider so a car is caught
## rather than launched. --without-guard-rails turns them off.

const PROBE_M := 8.
const DROP_M := 3.
const MIN_RUN_M := 12.
const LEAD_M := 8.
const RAIL_OFFSET_M := .35
const POST_SPACING_M := 1.905
const RAIL_TOP_M := .79
const RAIL_DEPTH_M := .31

static func build(tile) -> int:
 if tile.road_surface == null or "--without-guard-rails" in OS.get_cmdline_user_args():return 0
 var runs := []
 for line in tile.road_surface.lines:
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
     var pts := PackedVector3Array()
     for k in range(a, b + 1):
      var centre: Vector3 = sections[k][2];var edge: Vector3 = sections[k][side]
      var inward := (centre - edge);inward.y = 0.;inward = inward.normalized()
      pts.append(edge + inward * RAIL_OFFSET_M)
     runs.append(pts)
    i = j + 1
 if runs.is_empty():return 0
 var rail := SurfaceTool.new();rail.begin(Mesh.PRIMITIVE_TRIANGLES)
 var posts := []
 var faces := PackedVector3Array()
 # W-beam cross-section: (outward offset, height below the top) — two ridges.
 var profile := [Vector2(0., 0.), Vector2(.06, .05), Vector2(.02, .155), Vector2(.06, .26), Vector2(0., RAIL_DEPTH_M)]
 for pts in runs:
  var along := 0.;var next_post := 0.
  for k in pts.size():
   var p: Vector3 = pts[k]
   if k == 0:continue
   var seg_start: Vector3 = pts[k-1]
   var seg := Vector2(p.x - seg_start.x, p.z - seg_start.z).length()
   var dir := (p - seg_start);dir.y = 0.;dir = dir.normalized() if seg > 1e-4 else Vector3.FORWARD
   # Posts every POST_SPACING_M along the run, standing on the shoulder.
   while next_post <= along + seg:
    var q: Vector3 = seg_start.lerp(p, clampf((next_post - along) / maxf(seg, 1e-4), 0., 1.))
    posts.append(Transform3D(Basis.looking_at(dir, Vector3.UP), q + Vector3.UP * (RAIL_TOP_M * .5 - .05)))
    next_post += POST_SPACING_M
   along += seg
   var p0: Vector3 = pts[k-1]
   var t0 := (p - p0);t0.y = 0.;t0 = t0.normalized();var o0 := Vector3(-t0.z, 0., t0.x)
   for m in profile.size() - 1:
    var a0: Vector3 = p0 + o0 * profile[m].x + Vector3.UP * (RAIL_TOP_M - profile[m].y)
    var b0: Vector3 = p0 + o0 * profile[m+1].x + Vector3.UP * (RAIL_TOP_M - profile[m+1].y)
    var a1: Vector3 = p + o0 * profile[m].x + Vector3.UP * (RAIL_TOP_M - profile[m].y)
    var b1: Vector3 = p + o0 * profile[m+1].x + Vector3.UP * (RAIL_TOP_M - profile[m+1].y)
    for v in [a0, a1, b1, a0, b1, b0]:rail.add_vertex(v)
   # Collider: a wall from the ground to the rail top along the run.
   var g0 := p0 + Vector3.UP * .05;var g1 := p + Vector3.UP * .05
   var h0 := p0 + Vector3.UP * RAIL_TOP_M;var h1 := p + Vector3.UP * RAIL_TOP_M
   faces.append_array(PackedVector3Array([g0, h0, h1, g0, h1, g1]))
 rail.generate_normals()
 var steel := StandardMaterial3D.new()
 steel.albedo_color = Color("#a4a9ab");steel.metallic = .65;steel.roughness = .42
 steel.cull_mode = BaseMaterial3D.CULL_DISABLED
 var mesh := rail.commit();mesh.surface_set_material(0, steel)
 var node := MeshInstance3D.new();node.mesh = mesh;node.name = "GuardRails";tile.add_child(node)
 var post_mesh := BoxMesh.new();post_mesh.size = Vector3(.15, RAIL_TOP_M + .1, .15)
 var wood := StandardMaterial3D.new();wood.albedo_color = Color("#6f6a62");wood.roughness = .8
 post_mesh.material = wood
 var mm := MultiMesh.new();mm.transform_format = MultiMesh.TRANSFORM_3D;mm.mesh = post_mesh;mm.instance_count = posts.size()
 for i in posts.size():mm.set_instance_transform(i, posts[i])
 var post_node := MultiMeshInstance3D.new();post_node.multimesh = mm;post_node.name = "GuardRailPosts";tile.add_child(post_node)
 var shape := ConcavePolygonShape3D.new();shape.backface_collision = true;shape.set_faces(faces)
 tile.collider(shape)
 tile.info["guard_rail_runs"] = runs.size()
 return runs.size()
