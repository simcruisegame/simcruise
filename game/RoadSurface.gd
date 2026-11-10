extends RefCounted
## The engineered road surface: what the tyres drive on and what is drawn for
## every paved road on a streamed tile.
##
## Roads are built objects. Their profile changes grade through designed
## vertical curves and their cross slope follows the curve (superelevation),
## so a real road is smooth at the scale a tyre feels. The terrain grid cannot
## represent that: its cells are ~3.9 m, wider than half a mountain road, and on
## a banked hairpin the planar triangles under the wheels tilt against each
## other. On the Tail of the Dragon that grid surface had 35 slope kinks per km
## above 0.5°/m and up to 3.9°/m (checks/RoadProfile.gd); the profile the
## helper fits along the road changes by at most ~0.17°/m.
##
## So the helper exports each road's fitted profile (helper/road-grade.mjs,
## road_surface: stations every 2 m with height, cross slope and a smoothed
## tangent) and this builds, per road, a ribbon from it:
##   * the carriageway at the profile height with its superelevation, plus a
##     1 m shoulder that drops 4 cm, like a paved edge;
##   * drawn with the tile's own ground material and UVs (position / side), so
##     it looks exactly like the ground around it;
##   * a collider of its own, so wheel rays meet the ribbon, not the grid;
##   * terrain grid nodes under the footprint pushed 0.3 m below it before the
##     ground mesh is built, so faceted ground can never poke through;
##   * height() answers for any point on the ribbon, so WorldTile.height_at()
##     (camera, paint, props, placing the car) agrees with the tyres.
## Every input is the helper's world-anchored profile, so neighbouring tiles
## build matching ribbons; a 6 m apron makes them overlap at the seam.
## --grid-roads keeps the old grid-only surface (A/B and RoadProfile before).

const SHOULDER_M := 1.2
const SHOULDER_DROP_M := .05
const UNDER_M := .06
const BIN := 8.

var quads: Array = []         # [a, b, c, d] Vector3, tile metres (a,b at station i; c,d at i+1)
var bins := {}                # Vector2i -> [quad index]
var active := false
var lines: Array = []         # per road: cls, half width, sections (5 cross-section points per station; GuardRails.gd)
# Lane frame for the drawn surface (GroundFinish.gdshaderinc lane_wear): per
# built chain its half width, lane width, cross-section offsets, distance
# along (m) and centre per station; per quad [chain, station, offset index].
var lane_chains: Array = []
var quad_lane: Array = []
const LANE_FLAG := 1000.      # added to the lateral offset in UV2; the ground mesh has no UV2
const LANE_M := 3.6           # a US travel lane (AASHTO 12 ft)
const LANE_MIN_M := 3.0       # narrowest lane a carriageway is split into (10 ft)
const CONDITION_STEP := 5     # stations between street-condition samples (refined where they differ)
var mesh_stats := {}          # timing of the last mesh() (checks/RoadProfile.gd prints it)

## Lanes per side and lane width that fit inside a carriageway of half width
## `half`: 3.6 m lanes, as many per side as fit with up to 0.9 m to spare
## (narrowed to share the width, never below 3.0 m), so lanes never overhang
## the carriageway. A road under 5.4 m is one centred lane: Vector2(0, 2 half).
static func lane_fit(half: float) -> Vector2:
 if half < LANE_M * .75:return Vector2(0., half * 2.)
 var n := maxf(floorf(half / LANE_M + .25), 1.)
 while n > 1. and half / n < LANE_MIN_M:n -= 1.
 return Vector2(n, minf(LANE_M, half / n))

static func enabled() -> bool:
 return not "--grid-roads" in OS.get_cmdline_user_args()

static func for_tile(tile) -> RefCounted:
 var rows: Array = tile.info.get("road_surface", [])
 if rows.is_empty() or not enabled():return null
 var s = new()
 s.build(rows, tile.side)
 return s if s.active else null

# Major roads first: at an intersection the minor road meets the major road's
# surface (its ribbon snaps where they overlap, and the correction fades out
# along the minor road over JOIN_FADE_M, so there is no step at the edge).
const RANK := {"motorway":7,"trunk":6,"primary":5,"secondary":4,"tertiary":3,"minor":2,"residential":2,"unclassified":2}
const JOIN_FADE_M := 15.

func build(rows: Array, side: float) -> void:
 var chains := []
 for chain in rows:
  var pts: Array = chain.get("pts", [])
  if pts.size() < 2:continue
  var half := float(chain.get("w", 6.)) * .5
  var offsets := [-(half + SHOULDER_M), -half, 0., half, half + SHOULDER_M]
  var sections := []
  var centres := []
  for row in pts:
   var c := Vector2(float(row[0]), float(row[1])) * side
   centres.append(c)
   var z := float(row[2]);var e := float(row[3])
   var n := Vector2(-float(row[5]), float(row[4]))      # left normal (helper convention)
   var section := []
   for L in offsets:
    var lateral: float = clampf(L, -half, half)
    var h: float = z - e * lateral
    if absf(L) > half:h -= SHOULDER_DROP_M
    var p: Vector2 = c + n * L
    section.append(Vector3(p.x, h, p.y))
   sections.append(section)
  chains.append({"rank":RANK.get(str(chain.get("cls","")),1)*100000+pts.size(),"sections":sections,"half":half,"offsets":offsets,"centres":centres,"cls":str(chain.get("cls",""))})
  lines.append({"cls":str(chain.get("cls","")),"half":half,"sections":sections})
 chains.sort_custom(func(a, b): return a.rank > b.rank)
 for chain in chains:
  var sections: Array = chain.sections
  if active:join(sections)
  var along := PackedFloat32Array();along.resize(sections.size())
  for i in range(1, sections.size()):
   along[i] = along[i-1] + Vector2(sections[i][2].x, sections[i][2].z).distance_to(Vector2(sections[i-1][2].x, sections[i-1][2].z))
  # Lanes are laid out from the centreline, fitted inside the carriageway
  # (lane_fit); a road narrower than two lanes is one centred lane.
  var ci := lane_chains.size()
  var fit := lane_fit(chain.half)
  lane_chains.append({"half":chain.half,"lane_w":fit.y,"per_side":fit.x,"offsets":chain.offsets,"along":along,"centres":chain.centres,"cls":chain.cls})
  for i in range(1, sections.size()):
   var prev: Array = sections[i-1];var section: Array = sections[i]
   if Vector2(prev[2].x, prev[2].z).distance_to(Vector2(section[2].x, section[2].z)) >= 8.:continue
   for k in section.size() - 1:
    add_quad(prev[k], prev[k+1], section[k+1], section[k])
    quad_lane.append([ci, i, k])
  active = not quads.is_empty()

## Snap a lower-ranked ribbon onto the ribbons already built where they
## overlap; the station-wise correction fades out over JOIN_FADE_M.
func join(sections: Array) -> void:
 var delta := PackedFloat32Array();delta.resize(sections.size())
 var covered := []
 for i in sections.size():
  var sum := 0.;var count := 0
  for v in sections[i]:
   var hi := height(Vector2(v.x, v.z))
   if not is_nan(hi):sum += hi - v.y;count += 1
  covered.append(count > 0)
  delta[i] = sum / count if count > 0 else 0.
 # Station spacing along this chain, for the fade.
 var station := PackedFloat32Array();station.resize(sections.size())
 for i in range(1, sections.size()):
  station[i] = station[i-1] + Vector2(sections[i][2].x, sections[i][2].z).distance_to(Vector2(sections[i-1][2].x, sections[i-1][2].z))
 for i in sections.size():
  var best := 0.;var weight := 0.
  for j in sections.size():
   if not covered[j]:continue
   var w := clampf(1. - absf(station[i] - station[j]) / JOIN_FADE_M, 0., 1.)
   w = w * w * (3. - 2. * w)
   if w > weight:weight = w;best = delta[j]
  if weight <= 0.:continue
  for k in sections[i].size():
   var v: Vector3 = sections[i][k]
   var hi := height(Vector2(v.x, v.z))
   sections[i][k] = Vector3(v.x, hi if not is_nan(hi) else v.y + best * weight, v.z)

func add_quad(a: Vector3, b: Vector3, c: Vector3, d: Vector3) -> void:
 var idx := quads.size()
 quads.append([a, b, c, d])
 var lo := Vector2(minf(minf(a.x, b.x), minf(c.x, d.x)), minf(minf(a.z, b.z), minf(c.z, d.z)))
 var hi := Vector2(maxf(maxf(a.x, b.x), maxf(c.x, d.x)), maxf(maxf(a.z, b.z), maxf(c.z, d.z)))
 for x in range(floori(lo.x / BIN), floori(hi.x / BIN) + 1):
  for y in range(floori(lo.y / BIN), floori(hi.y / BIN) + 1):
   var key := Vector2i(x, y)
   if not bins.has(key):bins[key] = []
   bins[key].append(idx)

static func tri_height(p: Vector2, a: Vector3, b: Vector3, c: Vector3) -> float:
 var ab := Vector2(b.x - a.x, b.z - a.z);var ac := Vector2(c.x - a.x, c.z - a.z);var ap := p - Vector2(a.x, a.z)
 var det := ab.cross(ac)
 if absf(det) < 1e-9:return NAN
 var u := ap.cross(ac) / det;var v := ab.cross(ap) / det
 if u >= -1e-4 and v >= -1e-4 and u + v <= 1. + 1e-4:return a.y + u * (b.y - a.y) + v * (c.y - a.y)
 return NAN

## Ribbon height at p (tile metres), the highest where roads overlap, or NAN.
func height(p: Vector2) -> float:
 var best := NAN
 for idx in bins.get(Vector2i(floori(p.x / BIN), floori(p.y / BIN)), []):
  var q: Array = quads[idx]
  var h := tri_height(p, q[0], q[1], q[2])
  if is_nan(h):h = tri_height(p, q[0], q[2], q[3])
  if not is_nan(h) and (is_nan(best) or h > best):best = h
 return best

## Push terrain grid nodes that lie under the ribbon just below it (before the
## ground mesh is built). Nodes beside the ribbon keep their height: the
## shoulder slopes down to meet them, so there is no trench along the edge.
func lower_grid(h: Array, n: int, side: float) -> int:
 var cell := side / n
 var moved := 0
 for j in n + 1:
  for i in n + 1:
   var r := height(Vector2(i, j) * cell)
   if is_nan(r):continue
   var k := j * (n + 1) + i
   if float(h[k]) > r - UNDER_M:h[k] = r - UNDER_M;moved += 1
 return moved

## Street condition, 0 (new) to 1 (worn), from the IRI that RoadRoughness.gd
## gives the tyres here (class baseline × the stable per-street hash), so the
## drawn wear matches the felt road when --rough-roads is on.
## IRI 0.8 (new) maps to 0 and 3.3 (poor; FHWA "poor" starts at 2.7) to 1, so
## a typical primary road (1.6) sits near 0.3 and a typical street near 0.65.
## --road-condition=X forces X everywhere (A/B captures).
static func condition(tile, p: Vector2, cls: String) -> float:
 var value: float = ROUGHNESS.CLASS_IRI.get(cls, 2.4)
 if tile != null and not tile.road_grid.is_empty():value = ROUGHNESS.iri(tile, p, 1.)
 return clampf((value - .8) / 2.5, 0., 1.)
const ROUGHNESS := preload("res://RoadRoughness.gd")

## Ribbons carry UV2 and vertex colour, which the plain ground mesh does not,
## so the ground shader needs pipelines for a second vertex format. Show one
## sub-pixel triangle of that format 1 m in front of the camera (also casting
## a shadow) for the first two seconds, so those pipelines are compiled while
## the start place loads instead of when the first ribbon comes into view.
static func prewarm(camera: Node3D, shader: Shader) -> void:
 if not enabled() or "--without-lane-wear" in OS.get_cmdline_user_args():return
 var arrays := []
 arrays.resize(Mesh.ARRAY_MAX)
 arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3.ZERO, Vector3(.0002, 0., 0.), Vector3(0., .0002, 0.)])
 arrays[Mesh.ARRAY_NORMAL] = PackedVector3Array([Vector3.BACK, Vector3.BACK, Vector3.BACK])
 arrays[Mesh.ARRAY_TEX_UV] = PackedVector2Array([Vector2.ZERO, Vector2.ZERO, Vector2.ZERO])
 arrays[Mesh.ARRAY_TEX_UV2] = PackedVector2Array([Vector2(LANE_FLAG, 0.), Vector2(LANE_FLAG, 0.), Vector2(LANE_FLAG, 0.)])
 arrays[Mesh.ARRAY_COLOR] = PackedColorArray([Color(.45, .5, .2, .25), Color(.45, .5, .2, .25), Color(.45, .5, .2, .25)])
 var m := ArrayMesh.new()
 m.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
 var mat := ShaderMaterial.new()
 mat.shader = shader
 m.surface_set_material(0, mat)
 var warm := MeshInstance3D.new()
 warm.name = "RibbonPipelineWarm"
 warm.mesh = m
 warm.position = Vector3(0., 0., -1.)
 camera.add_child(warm)
 camera.get_tree().create_timer(2.).timeout.connect(warm.queue_free)

func mesh(mat: Material, side: float, tile = null) -> ArrayMesh:
 var forced := -1.
 # --without-lane-wear leaves the flag off, so the shader keeps its old look (A/B).
 var plain := "--without-lane-wear" in OS.get_cmdline_user_args()
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--road-condition="):forced = clampf(a.trim_prefix("--road-condition=").to_float(), 0., 1.)
 var started := Time.get_ticks_usec()
 var wear := []
 var samples := 0
 for lc in lane_chains:
  var w := PackedFloat32Array();w.resize(lc.centres.size())
  if forced >= 0.:
   w.fill(forced)
  else:
   # The condition changes only where the ribbon passes from one street to
   # another, so sample every CONDITION_STEP stations and evaluate the
   # stations in between only where two samples differ.
   var last := w.size() - 1
   var prev_i := 0
   w[0] = condition(tile, lc.centres[0], lc.cls);samples += 1
   while prev_i < last:
    var i := mini(prev_i + CONDITION_STEP, last)
    w[i] = condition(tile, lc.centres[i], lc.cls);samples += 1
    for j in range(prev_i + 1, i):
     if w[i] == w[prev_i]:w[j] = w[i]
     else:w[j] = condition(tile, lc.centres[j], lc.cls);samples += 1
    prev_i = i
  wear.append(w)
 mesh_stats = {"condition_ms":(Time.get_ticks_usec() - started) / 1000., "condition_samples":samples}
 started = Time.get_ticks_usec()
 # Packed arrays filled in one pass and handed to SurfaceTool for the normals:
 # per-vertex SurfaceTool calls cost about twice as much once UV2 and colour
 # are set too.
 var count := quads.size() * 6
 var verts := PackedVector3Array();verts.resize(count)
 var uvs := PackedVector2Array();uvs.resize(count)
 var uv2s := PackedVector2Array();uv2s.resize(count)
 var colours := PackedColorArray();colours.resize(count)
 var flag := 0. if plain else LANE_FLAG
 var inv := 1. / side
 var n := 0
 var last_ci := -1
 var w: PackedFloat32Array;var along: PackedFloat32Array;var offsets: Array;var fixed := Color()
 for qi in quads.size():
  var q: Array = quads[qi]
  var ql: Array = quad_lane[qi]
  var ci: int = ql[0];var i: int = ql[1];var k: int = ql[2]
  if ci != last_ci:
   # Quads of one chain are consecutive: look its frame up once.
   var lc: Dictionary = lane_chains[ci]
   last_ci = ci;w = wear[ci];along = lc.along;offsets = lc.offsets
   fixed = Color(lc.lane_w / 8., 0., lc.half / 16., lc.per_side / 4.)
  # a, b at station i-1 and offsets k, k+1; c, d at station i and offsets k+1, k (see build()).
  var l0: float = float(offsets[k]) + flag;var l1: float = float(offsets[k + 1]) + flag
  var s0: float = along[i - 1];var s1: float = along[i]
  var c0 := Color(fixed.r, w[i - 1], fixed.b, fixed.a)
  var c1 := Color(fixed.r, w[i], fixed.b, fixed.a)
  var a: Vector3 = q[0];var b: Vector3 = q[1];var c: Vector3 = q[2];var d: Vector3 = q[3]
  # Godot front faces wind clockwise seen from the front (above): a c b, a d c.
  verts[n] = a;uvs[n] = Vector2(a.x, a.z) * inv;uv2s[n] = Vector2(l0, s0);colours[n] = c0
  verts[n+1] = c;uvs[n+1] = Vector2(c.x, c.z) * inv;uv2s[n+1] = Vector2(l1, s1);colours[n+1] = c1
  verts[n+2] = b;uvs[n+2] = Vector2(b.x, b.z) * inv;uv2s[n+2] = Vector2(l1, s0);colours[n+2] = c0
  verts[n+3] = a;uvs[n+3] = uvs[n];uv2s[n+3] = uv2s[n];colours[n+3] = c0
  verts[n+4] = d;uvs[n+4] = Vector2(d.x, d.z) * inv;uv2s[n+4] = Vector2(l0, s1);colours[n+4] = c1
  verts[n+5] = c;uvs[n+5] = uvs[n+1];uv2s[n+5] = uv2s[n+1];colours[n+5] = c1
  n += 6
 var arrays := []
 arrays.resize(Mesh.ARRAY_MAX)
 arrays[Mesh.ARRAY_VERTEX] = verts
 arrays[Mesh.ARRAY_TEX_UV] = uvs
 arrays[Mesh.ARRAY_TEX_UV2] = uv2s
 arrays[Mesh.ARRAY_COLOR] = colours
 mesh_stats["fill_ms"] = (Time.get_ticks_usec() - started) / 1000.
 var st := SurfaceTool.new()
 st.create_from_arrays(arrays, Mesh.PRIMITIVE_TRIANGLES)
 st.generate_normals()
 var m := st.commit()
 m.surface_set_material(0, mat)
 mesh_stats["mesh_ms"] = (Time.get_ticks_usec() - started) / 1000.
 return m
