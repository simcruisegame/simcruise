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
## road_surface: stations every 2 m with height, cross slope, paved width and a
## smoothed tangent) and this builds, per road, a ribbon from it:
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
var quad_line := PackedInt32Array()   # quad index -> index into lines of the ribbon it belongs to
var boxes := PackedFloat32Array()     # quad index * 4 -> plan bounding box (min x, min z, max x, max z)
var active := false
# Per road: cls, halves (paved half width at each station; a chain can join
# roads of different widths) and sections (5 cross-section points per station:
# shoulder edge, paved edge, centre, paved edge, shoulder edge). GuardRails.gd
# places rails from each station's own shoulder edge, never a chain-wide width.
var lines: Array = []

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
# Ribbons of one class meet the same way, the longer first. Where two banked
# roads overlap (a couplet splitting from its approach) the helper has already
# run their superelevation out, so their cross slopes agree there.
const RANK := {"motorway":7,"trunk":6,"primary":5,"secondary":4,"tertiary":3,"minor":2,"residential":2,"unclassified":2}
const JOIN_FADE_M := 15.

func build(rows: Array, side: float) -> void:
 var chains := []
 for chain in rows:
  var pts: Array = chain.get("pts", [])
  if pts.size() < 2:continue
  var half := float(chain.get("w", 6.)) * .5
  var sections := []
  var halves := PackedFloat32Array()
  for row in pts:
   # Per-station width (a chain can join roads of different widths); older rows have none.
   var half_i: float = float(row[6]) * .5 if row.size() > 6 else half
   halves.append(half_i)
   var offsets := [-(half_i + SHOULDER_M), -half_i, 0., half_i, half_i + SHOULDER_M]
   var c := Vector2(float(row[0]), float(row[1])) * side
   var z := float(row[2]);var e := float(row[3])
   var n := Vector2(-float(row[5]), float(row[4]))      # left normal (helper convention)
   var section := []
   for L in offsets:
    var lateral: float = clampf(L, -half_i, half_i)
    var h: float = z - e * lateral
    if absf(L) > half_i:h -= SHOULDER_DROP_M
    var p: Vector2 = c + n * L
    section.append(Vector3(p.x, h, p.y))
   sections.append(section)
  chains.append({"rank":RANK.get(str(chain.get("cls","")),1)*100000+pts.size(),"sections":sections,"line":lines.size()})
  lines.append({"cls":str(chain.get("cls","")),"halves":halves,"sections":sections})
 chains.sort_custom(func(a, b): return a.rank > b.rank)
 for chain in chains:
  var sections: Array = chain.sections
  if active:join(sections)
  for i in range(1, sections.size()):
   var prev: Array = sections[i-1];var section: Array = sections[i]
   if Vector2(prev[2].x, prev[2].z).distance_to(Vector2(section[2].x, section[2].z)) >= 8.:continue
   for k in section.size() - 1:
    add_quad(prev[k], prev[k+1], section[k+1], section[k])
    quad_line.append(int(chain.line))
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
 boxes.append(lo.x - .01);boxes.append(lo.y - .01);boxes.append(hi.x + .01);boxes.append(hi.y + .01)
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
  # Most quads in a bin are nowhere near p: reject them on their box first.
  var o: int = idx * 4
  if p.x < boxes[o] or p.y < boxes[o + 1] or p.x > boxes[o + 2] or p.y > boxes[o + 3]:continue
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

func mesh(mat: Material, side: float) -> ArrayMesh:
 var st := SurfaceTool.new()
 st.begin(Mesh.PRIMITIVE_TRIANGLES)
 for q in quads:
  # Godot front faces wind clockwise seen from the front (above).
  for v in [q[0], q[2], q[1], q[0], q[3], q[2]]:
   st.set_uv(Vector2(v.x, v.z) / side)
   st.add_vertex(v)
 st.generate_normals()
 var m := st.commit()
 m.surface_set_material(0, mat)
 return m
