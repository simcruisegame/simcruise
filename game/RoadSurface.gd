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
##   * terrain grid nodes under the footprint, and the ground triangles its
##     edges pass over, pushed 6 cm below it before the ground mesh is built,
##     so faceted ground can never poke through (lower_grid; bridge decks use
##     the same index and the same pass, BridgeDeck.gd);
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
const JOIN_FADE_MAX_M := 60.
const JOIN_GRADE := .04

func build(rows: Array, side: float) -> void:
 _side = side
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
  chains.append({"rank":RANK.get(str(chain.get("cls","")),1)*100000+pts.size(),"sections":sections})
  lines.append({"cls":str(chain.get("cls","")),"halves":halves,"sections":sections})
 chains.sort_custom(func(a, b): return a.rank > b.rank)
 for chain in chains:
  var sections: Array = chain.sections
  if active:join(sections)
  # Distance along the chain, so lower_grid leaves the ground alone at its
  # open ends (where a road without a ribbon, or the ground, carries on).
  var along := PackedFloat32Array([0.])
  for i in range(1, sections.size()):along.append(along[i-1] + Vector2(sections[i][2].x, sections[i][2].z).distance_to(Vector2(sections[i-1][2].x, sections[i-1][2].z)))
  for i in range(1, sections.size()):
   var prev: Array = sections[i-1];var section: Array = sections[i]
   if Vector2(prev[2].x, prev[2].z).distance_to(Vector2(section[2].x, section[2].z)) >= 8.:continue
   var near_end := minf(along[i], along[-1] - along[i-1]) < END_CLEAR_M
   # Edges lower_grid samples: the station edge a-b once per grid cell (not
   # at the chain's first section), the long edges b-c and d-a densely.
   var mask := (0 if near_end else EDGE_LONG << 2 | EDGE_LONG << 6) | (0 if i == 1 or near_end else EDGE_STATION)
   for k in section.size() - 1:
    add_quad(prev[k], prev[k+1], section[k+1], section[k], mask, Vector2(prev[2].x, prev[2].z))
  active = not quads.is_empty()

## Snap a lower-ranked ribbon onto the ribbons already built where they
## overlap; the station-wise correction fades out over JOIN_FADE_M.
func join(sections: Array) -> void:
 var delta := PackedFloat32Array();delta.resize(sections.size())
 var covered := []
 var crossing := []
 for i in sections.size():
  var sum := 0.;var count := 0
  # The centreline on another ribbon: a junction, joined whatever the
  # difference. Only the edges overlapping: side by side (twin carriageways,
  # a ramp merging) when the heights are close, or a road on another level
  # (a street ending at the abutment of a bridge over a motorway) when not.
  var c := Vector2(sections[i][2].x, sections[i][2].z)
  var centre := not is_nan(height(c)) and not unshared(c)
  crossing.append(centre)
  for v in sections[i]:
   var p := Vector2(v.x, v.z)
   var hi := height(p) if centre else level(p, v.y)
   if not is_nan(hi) and not unshared(p):sum += hi - v.y;count += 1
  covered.append(count > 0)
  delta[i] = sum / count if count > 0 else 0.
 # Station spacing along this chain, for the fade, and each station's distance
 # inside the tile edge.
 var station := PackedFloat32Array();station.resize(sections.size())
 var edge := PackedFloat32Array()
 for sec in sections:edge.append(minf(minf(sec[2].x, sec[2].z), minf(_side - sec[2].x, _side - sec[2].z)))
 for i in range(1, sections.size()):
  station[i] = station[i-1] + Vector2(sections[i][2].x, sections[i][2].z).distance_to(Vector2(sections[i-1][2].x, sections[i-1][2].z))
 for i in sections.size():
  var best := 0.;var weight := 0.;var sum_w := 0.
  for j in sections.size():
   if not covered[j]:continue
   # The fade grows with the height it absorbs, so its steepest grade stays
   # near 6 % (smoothstep: 1.5 · delta / length). A fixed 15 m fade took a
   # 1.8 m snap onto a bridge approach in 12 m (Western Ave, Allston MA: 7-8 g).
   var fade := clampf(absf(delta[j]) / JOIN_GRADE, JOIN_FADE_M, JOIN_FADE_MAX_M)
   # A junction the neighbouring tile cannot see (more than the 6 m apron
   # inside this tile) fades out before the tile edge: the neighbour builds
   # the same road from the same profile but cannot join it there, so a fade
   # reaching the seam left the two tiles' overlapping ribbons up to 0.6 m
   # apart (a motorway ramp leaving Cambridge St 14 m from a seam, Allston MA:
   # a dead stop at 20 m/s). A junction nearer the edge is in both tiles.
   if edge[j] > SEAM_SHARED_M:fade = minf(fade, edge[j])
   var w := clampf(1. - absf(station[i] - station[j]) / fade, 0., 1.)
   w = w * w * (3. - 2. * w)
   # The correction is the weighted mean of the overlaps in reach, not the
   # nearest one's: between two overlaps of different heights the nearest
   # switched from one to the other in a single station (0.9 m in 2 m on
   # Cambridge St beside a motorway ramp, Allston MA).
   best += w * delta[j];sum_w += w
   weight = maxf(weight, w)
  if weight <= 0.:continue
  best /= sum_w
  for k in sections[i].size():
   var v: Vector3 = sections[i][k]
   var y := v.y + best * weight
   # At a junction the vertices on the other ribbon take its height. Side by
   # side the whole section moves by the faded correction: snapping only the
   # vertices that overlap tears a section between two levels.
   var p := Vector2(v.x, v.z)
   var hi := height(p) if crossing[i] and not unshared(p) else NAN
   sections[i][k] = Vector3(v.x, hi if not is_nan(hi) else y, v.z)
const SEAM_SHARED_M := 6.
const SEAM_CLEAR_M := 15.
var _side := 0.
var axes := PackedVector2Array()   # per quad: its road's centreline at that station

## Whether the ribbons covering p, near the tile edge, are ones the
## neighbouring tile may not hold: each tile holds the helper's chains to 6 m
## past its edges, so a ribbon whose centreline is further inside than that is
## missing from the neighbour, even when its edge reaches the seam (a 19 m
## motorway ramp beside Cambridge St, Allston MA). A ribbon joined onto it there
## would part from the neighbour's copy at the seam (0.4-0.6 m, a dead stop at
## 20 m/s), so near the edge only ribbons both tiles hold are joined.
func unshared(p: Vector2) -> bool:
 if minf(minf(p.x, p.y), minf(_side - p.x, _side - p.y)) >= SEAM_CLEAR_M:return false
 var any := false
 for idx in bins.get(Vector2i(floori(p.x / BIN), floori(p.y / BIN)), []):
  var q: Array = quads[idx]
  if is_nan(tri_height(p, q[0], q[1], q[2])) and is_nan(tri_height(p, q[0], q[2], q[3])):continue
  any = true
  var a := axes[idx]
  if minf(minf(a.x, a.y), minf(_side - a.x, _side - a.y)) <= SEAM_SHARED_M:return false
 return any

## The ribbon height at p closest to y, when it is within JOIN_REACH_M of it
## (else NAN): a ribbon further above or below is another level.
const JOIN_REACH_M := 1.
func level(p: Vector2, y: float) -> float:
 var best := NAN
 for h in heights(p):
  if absf(h - y) <= JOIN_REACH_M and (is_nan(best) or absf(h - y) < absf(best - y)):best = h
 return best

## mask: which edges lower_grid samples, two bits per edge (a-b, b-c, c-d,
## d-a from the low bits): 0 none, EDGE_LONG every ~1 m, EDGE_STATION once per
## grid cell.
const EDGE_LONG := 1
const EDGE_STATION := 2
const END_CLEAR_M := 4.
var masks := PackedInt32Array()
func add_quad(a: Vector3, b: Vector3, c: Vector3, d: Vector3, mask := EDGE_STATION | EDGE_LONG << 2 | EDGE_LONG << 6, axis := Vector2(NAN, NAN)) -> void:
 var idx := quads.size()
 quads.append([a, b, c, d])
 masks.append(mask)
 axes.append(Vector2((a.x + c.x) * .5, (a.z + c.z) * .5) if is_nan(axis.x) else axis)
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

## Every surface height at p (overlapping ribbons, or stacked bridge decks),
## for picking the layer a wheel at a known height is on (WorldTile.height_at).
func heights(p: Vector2) -> PackedFloat64Array:
 var out := PackedFloat64Array()
 for idx in bins.get(Vector2i(floori(p.x / BIN), floori(p.y / BIN)), []):
  var q: Array = quads[idx]
  var h := tri_height(p, q[0], q[1], q[2])
  if is_nan(h):h = tri_height(p, q[0], q[2], q[3])
  if not is_nan(h):out.append(h)
 return out

## Push the terrain grid just below the surface (before the ground mesh is
## built), so faceted ground can never poke through it: first the nodes under
## it, then every ground triangle the surface's edges pass over. Before, only
## nodes under the ribbon moved, and a triangle reaching out to a higher node
## beside a road in a cut stood up to 0.2 m above the shoulder and the lane
## edge: a bump for the tyres, and the collider disagreed with height_at()
## (checks/RoadProfile.gd). Where the ground triangle under an edge sample
## stands above the surface, its corners drop by the least that brings it
## UNDER_M below (weighted by how close each corner is to the sample), like the
## ditch beside a cut; ground that is already below keeps its height. bilinear:
## the grid is read bilinearly (base_height on a tile without a measured grade
## source), so the four corners of the cell share the correction.
const EDGE_SAMPLE_M := .5
## keep: roads without a ribbon (service roads, tracks) as [a, b, reach]
## segments in tile metres (keep_segments): the ground they drive on is not
## pushed down where they meet a ribbon's side, or a car on them would drop off
## the ribbon edge into the dip (Anabel's Way and the fire station drive at
## Stowe VT met VT-108 with 0.1-0.3 m steps).
static func keep_segments(roads: Array, side: float) -> Dictionary:
 var out := {}
 for r in roads:
  if not r is Array or r.size() < 5:continue
  var ribbon: bool = RANK.has(str(r[0])) and not (r.size() > 5 and str(r[5]) == "unpaved")
  if ribbon or (r.size() > 6 and str(r[6]) == "bridge"):continue
  var reach := float(r[4]) * .5 + KEEP_MARGIN_M
  var pts: Array = r[3]
  for k in range(1, pts.size()):
   var a := Vector2(float(pts[k-1][0]), float(pts[k-1][1])) * side;var b := Vector2(float(pts[k][0]), float(pts[k][1])) * side
   var lo := a.min(b) - Vector2.ONE * reach;var hi := a.max(b) + Vector2.ONE * reach
   for x in range(floori(lo.x / BIN), floori(hi.x / BIN) + 1):
    for y in range(floori(lo.y / BIN), floori(hi.y / BIN) + 1):
     var key := Vector2i(x, y)
     if not out.has(key):out[key] = []
     out[key].append([a, b, reach])
 return out
const KEEP_MARGIN_M := 1.5
static func _kept(keep: Dictionary, p: Vector2) -> bool:
 for seg in keep.get(Vector2i(floori(p.x / BIN), floori(p.y / BIN)), []):
  if Geometry2D.get_closest_point_to_segment(p, seg[0], seg[1]).distance_to(p) <= seg[2]:return true
 return false

func lower_grid(h_in: Array, n: int, side: float, bilinear := false, keep := {}) -> int:
 var cell := side / n
 var moved := {}
 # A packed copy: the edge pass reads the grid hundreds of thousands of times.
 var h := PackedFloat64Array(h_in)
 for j in n + 1:
  for i in n + 1:
   var r := height(Vector2(i, j) * cell)
   # A surface clipped at the tile edge (a deck row) ends exactly on the edge
   # nodes; read it just inside, as the neighbour does from its side.
   if is_nan(r) and (i == 0 or j == 0 or i == n or j == n):
    r = height((Vector2(i, j) * cell).clamp(Vector2.ONE * .05, Vector2.ONE * (side - .05)))
   if is_nan(r):continue
   var k := j * (n + 1) + i
   if h[k] > r - UNDER_M:h[k] = r - UNDER_M;moved[k] = true
 # Long edges (where the ground beside a road in a cut rises) every ~1 m,
 # station edges once per grid cell: the samples a ground triangle can peak at.
 # The quad's mask says which of its edges to sample (add_quad): never the
 # open end of a ribbon or a deck, where the road carries on over the ground
 # and a ground lowered there is a step down off the end.
 var spacing := [0., clampf(cell * .3, EDGE_SAMPLE_M, 1.25), cell]
 if not quads.is_empty():
  for idx in quads.size():
   var q: Array = quads[idx];var mask := masks[idx]
   for e in 4:
    var kind := (mask >> (2 * e)) & 3
    if kind == 0:continue
    var a: Vector3 = q[e];var b: Vector3 = q[(e + 1) % 4]
    # Most edges lie wholly above the ground: skip them unless a grid node in
    # the cells under the edge stands above its lower end.
    var i0 := clampi(floori(minf(a.x, b.x) / cell), 0, n - 1);var i1 := clampi(floori(maxf(a.x, b.x) / cell), 0, n - 1)
    var j0 := clampi(floori(minf(a.z, b.z) / cell), 0, n - 1);var j1 := clampi(floori(maxf(a.z, b.z) / cell), 0, n - 1)
    var low := minf(a.y, b.y) - UNDER_M;var above := false
    for j in range(j0, j1 + 2):
     for i in range(i0, i1 + 2):
      if h[j * (n + 1) + i] > low:above = true;break
     if above:break
    if not above:continue
    var steps := maxi(1, ceili(Vector2(b.x - a.x, b.z - a.z).length() / spacing[kind]))
    for s in steps + 1:
     var p := a.lerp(b, float(s) / steps)
     _lower_cell(h, n, cell, p.x, p.z, p.y - UNDER_M, bilinear, moved, keep)
 for k in moved:h_in[k] = h[k]
 return moved.size()

func _lower_cell(h: PackedFloat64Array, n: int, cell: float, x: float, z: float, limit: float, bilinear: bool, moved: Dictionary, keep: Dictionary) -> void:
 var gx := x / cell;var gz := z / cell
 if gx < 0. or gz < 0. or gx > n or gz > n:return
 var i := mini(int(gx), n - 1);var j := mini(int(gz), n - 1)
 var u := gx - i;var v := gz - j
 var k := j * (n + 1) + i
 # Fast reject: the highest corner of the cell is already below the limit.
 if maxf(maxf(h[k], h[k + 1]), maxf(h[k + n + 1], h[k + n + 2])) <= limit:return
 # The grid's triangles split each cell on its NE/SW diagonal (u + v = 1);
 # corners and their interpolation weights at the sample.
 var corners: Array;var weights: Array
 if bilinear:
  corners = [k, k + 1, k + n + 1, k + n + 2];weights = [(1. - u) * (1. - v), u * (1. - v), (1. - u) * v, u * v]
 elif u + v <= 1.:
  corners = [k, k + 1, k + n + 1];weights = [1. - u - v, u, v]
 else:
  corners = [k + n + 2, k + 1, k + n + 1];weights = [u + v - 1., 1. - v, 1. - u]
 var g := 0.
 for c in corners.size():g += float(h[corners[c]]) * weights[c]
 var excess := g - limit
 if excess <= 1e-4:return
 if not keep.is_empty() and _kept(keep, Vector2(x, z)):return
 # Tile-edge nodes are shared with the neighbour, which cannot see this tile's
 # samples: only the node pass above moves them, as both tiles do. The others
 # take the least-squares correction that brings g to the limit.
 var norm := 0.
 for c in corners.size():
  var ci: int = corners[c] % (n + 1);var cj: int = corners[c] / (n + 1)
  if ci == 0 or cj == 0 or ci == n or cj == n:weights[c] = 0.
  norm += weights[c] * weights[c]
 if norm < .05:return   # the sample is at a tile-edge node
 # No node drops by more than the triangle stands above the limit: beside a
 # tile edge, where the edge node cannot move, the least-squares answer took
 # the interior nodes down by up to three times that and dug a hole beside the
 # road (a 0.35 m step where a service road meets VT-108, Stowe VT).
 for c in corners.size():
  if weights[c] <= 0.:continue
  h[corners[c]] = float(h[corners[c]]) - minf(excess * weights[c] / norm, excess);moved[corners[c]] = true

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
