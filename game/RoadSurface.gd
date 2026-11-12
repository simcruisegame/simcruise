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
var quad_station := PackedInt32Array()   # quad index -> station i of its line (the quad spans i-1 .. i)
var quad_band := PackedByteArray()       # quad index -> k: between section points k and k+1
var boxes := PackedFloat32Array()     # quad index * 4 -> plan bounding box (min x, min z, max x, max z)
var active := false
# Per road: cls, halves (paved half width at each station; a chain can join
# roads of different widths), sections (5 cross-section points per station:
# shoulder edge, paved edge, centre, paved edge, shoulder edge) and edge (per
# station, the treatment beyond the paved edge on the side of section point 0
# then on the side of point 4: EDGE_RURAL, EDGE_TOWN or EDGE_KERB, see
# classify_edges()). GuardRails.gd places rails from each station's own
# shoulder edge, never a chain-wide width.
var lines: Array = []

# What lies beyond the paved edge (classify_edges()):
const EDGE_RURAL := 0   # a gravel shoulder, then the verge (terrain.gdshader)
const EDGE_TOWN := 1    # a built-up street with no mapped sidewalk: the verge meets the asphalt
const EDGE_KERB := 2    # a mapped sidewalk runs alongside: curb and gutter (Kerbs.gd)
# A sidewalk or footway this near the paved edge and this parallel to the
# road is the sidewalk of this street (OSM maps them as separate ways); a US
# street with one has a curb.
const KERB_SIDEWALK_MIN_M := .3
const KERB_SIDEWALK_MAX_M := 12.
const KERB_PARALLEL := .93      # cos 21.6 deg
const KERB_FILL_STATIONS := 5   # a gap this short (10 m: a driveway, a gap in the mapping) keeps the kerb
const KERB_MIN_STATIONS := 5    # a run shorter than this (10 m) is a crossing path, not a sidewalk
# Built up as LaneMarkings.gd decides it without a mapped limit (dense(),
# town_share()): the 250 m square around a station holds TOWN_BUILDINGS
# buildings or TOWN_STREETS_M of local streets and driveways; a ribbon mostly
# built up (or mostly open) in the tile is that all along, a mixed one takes
# the majority of its stations within TOWN_RUN_M each way, so the shoulder
# does not flip between gravel and verge at every 50 m cell.
# checks/RoadEdgeCheck.gd fails if these drift from LaneMarkings'.
const TOWN_CELL_M := 50.        # LaneMarkings.HOME_CELL_M
const TOWN_BUILDINGS := 15      # LaneMarkings.TOWN_BUILDINGS
const TOWN_STREETS_M := 600.    # LaneMarkings.TOWN_STREETS_M
const TOWN_SHARE := .65         # LaneMarkings.TOWN_SHARE
const TOWN_RUN_M := 150.        # LaneMarkings.TOWN_RUN_M

static func edge_color(kind: int) -> Color:
 return Color(1. if kind == EDGE_KERB else 0., 1. if kind != EDGE_RURAL else 0., 0., 1.)

static func enabled() -> bool:
 return not "--grid-roads" in OS.get_cmdline_user_args()

static func for_tile(tile) -> RefCounted:
 var rows: Array = tile.info.get("road_surface", [])
 if rows.is_empty() or not enabled():return null
 var s = new()
 s.build(rows, tile.side)
 if not s.active:return null
 s.classify_edges(tile)
 return s

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
  var edge := PackedByteArray();edge.resize(sections.size() * 2)
  lines.append({"cls":str(chain.get("cls","")),"halves":halves,"sections":sections,"edge":edge})
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
    quad_station.append(i)
    quad_band.append(k)
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
## h is the tile's own height list, changed in place: an Array (from JSON) or,
## on a tile with surveyed road banks, the PackedFloat64Array RoadBankMesh.gd
## put there. (A typed Array parameter used to convert the packed one into a
## copy, so on those tiles nothing was lowered and the ground covered up to
## 1 m of the ribbon: checks/RoadEdgeCheck.gd --capture, poke_frac.)
func lower_grid(h, n: int, side: float) -> int:
 var cell := side / n
 var moved := 0
 for j in n + 1:
  for i in n + 1:
   var r := height(Vector2(i, j) * cell)
   if is_nan(r):continue
   var k := j * (n + 1) + i
   if float(h[k]) > r - UNDER_M:h[k] = r - UNDER_M;moved += 1
 return moved

## Signed lateral offset (+ toward section point 4) of section point idx at
## station i of a line.
static func offset_of(line: Dictionary, i: int, idx: int) -> float:
 var h: float = line.halves[i]
 match idx:
  0:return -(h + SHOULDER_M)
  1:return -h
  3:return h
  4:return h + SHOULDER_M
 return 0.

## Where p lies across the ribbon under it: Vector3(signed lateral offset m,
## paved half width m, edge treatment on that side), or all NAN off every
## ribbon. Where ribbons overlap, the one p is most inside (smallest
## |lateral| / half width). The same numbers the ribbon's UV2 and COLOR carry
## into terrain.gdshader, so the grip (WorldTile.wheel_grip()) and the shading
## of the carriageway are one decision.
func lateral(p: Vector2, exclude_line := -1) -> Vector3:
 var best := Vector3(NAN, NAN, NAN);var best_ratio := INF
 for idx in bins.get(Vector2i(floori(p.x / BIN), floori(p.y / BIN)), []):
  var o: int = idx * 4
  if p.x < boxes[o] or p.y < boxes[o + 1] or p.x > boxes[o + 2] or p.y > boxes[o + 3]:continue
  if quad_line[idx] == exclude_line:continue
  var q: Array = quads[idx]
  var w := barycentric(p, q[0], q[1], q[2]);var tri := 0
  if is_nan(w.x):
   w = barycentric(p, q[0], q[2], q[3]);tri = 1
   if is_nan(w.x):continue
  var line: Dictionary = lines[quad_line[idx]]
  var i: int = quad_station[idx];var k: int = quad_band[idx]
  # Corners a, b at station i-1 (points k, k+1), c, d at station i (k+1, k).
  var la := offset_of(line, i - 1, k);var lb := offset_of(line, i - 1, k + 1)
  var lc := offset_of(line, i, k + 1);var ld := offset_of(line, i, k)
  var h0: float = line.halves[i - 1];var h1: float = line.halves[i]
  var lat: float;var half: float
  if tri == 0:
   lat = w.x * la + w.y * lb + w.z * lc;half = (w.x + w.y) * h0 + w.z * h1
  else:
   lat = w.x * la + w.y * lc + w.z * ld;half = w.x * h0 + (w.y + w.z) * h1
  var ratio := absf(lat) / maxf(half, .01)
  if ratio < best_ratio:
   best_ratio = ratio
   var edge: PackedByteArray = line.edge
   var station := i - 1 if (w.x + (w.y if tri == 0 else 0.)) > .5 else i
   best = Vector3(lat, half, edge[station * 2 + (1 if k >= 2 else 0)])
 return best

static func barycentric(p: Vector2, a: Vector3, b: Vector3, c: Vector3) -> Vector3:
 var ab := Vector2(b.x - a.x, b.z - a.z);var ac := Vector2(c.x - a.x, c.z - a.z);var ap := p - Vector2(a.x, a.z)
 var det := ab.cross(ac)
 if absf(det) < 1e-9:return Vector3(NAN, NAN, NAN)
 var u := ap.cross(ac) / det;var v := ab.cross(ap) / det
 if u >= -1e-4 and v >= -1e-4 and u + v <= 1. + 1e-4:return Vector3(1. - u - v, u, v)
 return Vector3(NAN, NAN, NAN)

## What lies beyond the paved edge, per station and side (lines[].edge):
## EDGE_KERB where a mapped sidewalk or footway runs alongside within
## KERB_SIDEWALK_MAX_M of the paved edge (a gap of a driveway or two keeps it,
## a lone crossing path does not make one), else EDGE_TOWN on a built-up
## street (LaneMarkings.gd's rule without a mapped limit: buildings or local
## streets in the 250 m square, by the ribbon's share or its majority within
## TOWN_RUN_M), else EDGE_RURAL. Only mapped data decides; nothing is read
## from imagery.
func classify_edges(tile) -> void:
 var side: float = tile.side
 # Buildings (first coordinate of each footprint) and local street length
 # per TOWN_CELL_M cell, counted as LaneMarkings.prepare() counts them.
 var homes := {}
 for b in tile.info.get("buildings", []):
  var p = b
  for depth in 3:
   if p is Array and not p.is_empty() and p[0] is Array:p = p[0]
  if p is Array and p.size() >= 2 and typeof(p[0]) in [TYPE_FLOAT, TYPE_INT]:
   var key := Vector2i((Vector2(float(p[0]), float(p[1])) * side / TOWN_CELL_M).floor())
   homes[key] = homes.get(key, 0) + 1
 var streets := {}
 for road in tile.info.get("roads", []):
  if not road is Array or road.size() < 4 or not str(road[0]) in ["minor", "service"] or not road[3] is Array:continue
  var route: Array = road[3]
  for j in range(1, route.size()):
   var a := Vector2(float(route[j-1][0]), float(route[j-1][1])) * side;var b := Vector2(float(route[j][0]), float(route[j][1])) * side
   var pieces := ceili(a.distance_to(b) / 10.)
   for k in pieces:
    var key := Vector2i((a.lerp(b, (k + .5) / pieces) / TOWN_CELL_M).floor())
    streets[key] = streets.get(key, 0.) + a.distance_to(b) / pieces
 var cells := ceili(side / TOWN_CELL_M)
 var towns := {}
 var dense := func(c: Vector2) -> bool:
  var cell := Vector2i((c / TOWN_CELL_M).floor())
  if not towns.has(cell):
   var count := 0.;var length := 0.;var inside := 0
   for dx in range(-2, 3):
    for dy in range(-2, 3):
     var k := cell + Vector2i(dx, dy)
     if k.x < 0 or k.y < 0 or k.x >= cells or k.y >= cells:continue
     inside += 1;count += homes.get(k, 0);length += streets.get(k, 0.)
   var scale := 25. / maxi(inside, 1)
   towns[cell] = count * scale >= TOWN_BUILDINGS or length * scale >= TOWN_STREETS_M
  return towns[cell]
 # Sidewalk segments in BIN cells.
 var walks := {}
 for row in tile.info.get("pedestrian", []):
  if not row is Array or row.size() < 3 or not str(row[0]) in ["sidewalk", "footway"] or not row[2] is Array:continue
  var pts: Array = row[2]
  for j in range(1, pts.size()):
   var a := Vector2(float(pts[j-1][0]), float(pts[j-1][1])) * side;var b := Vector2(float(pts[j][0]), float(pts[j][1])) * side
   if a.distance_squared_to(b) < .01:continue
   for x in range(floori(minf(a.x, b.x) / BIN), floori(maxf(a.x, b.x) / BIN) + 1):
    for y in range(floori(minf(a.y, b.y) / BIN), floori(maxf(a.y, b.y) / BIN) + 1):
     var key := Vector2i(x, y)
     if not walks.has(key):walks[key] = []
     walks[key].append([a, b])
 var reach := ceili((KERB_SIDEWALK_MAX_M + 12.) / BIN)
 for line in lines:
  var sections: Array = line.sections
  var n := sections.size()
  var kerb := [PackedByteArray(), PackedByteArray()]
  kerb[0].resize(n);kerb[1].resize(n)
  var edge: PackedByteArray = line.edge
  # Built up: the ribbon's share of built-up stations, or on a mixed ribbon
  # the majority within TOWN_RUN_M (LaneMarkings.path()).
  var raw := PackedByteArray();raw.resize(n)
  var arc := PackedFloat64Array();arc.resize(n)
  var built := 0
  for i in n:
   var c := Vector2(sections[i][2].x, sections[i][2].z)
   if i > 0:arc[i] = arc[i - 1] + c.distance_to(Vector2(sections[i - 1][2].x, sections[i - 1][2].z))
   raw[i] = 1 if dense.call(c) else 0
   built += raw[i]
  var town_at := majority(raw, arc, TOWN_RUN_M)
  var share := float(built) / maxi(n, 1)
  if share >= TOWN_SHARE or share <= 1. - TOWN_SHARE:town_at.fill(1 if share >= TOWN_SHARE else 0)
  for i in n:
   edge[i * 2] = EDGE_TOWN if town_at[i] == 1 else EDGE_RURAL;edge[i * 2 + 1] = edge[i * 2]
  for i in n:
   var c := Vector2(sections[i][2].x, sections[i][2].z)
   var nrm := Vector2(sections[i][4].x - c.x, sections[i][4].z - c.y)
   if nrm.length_squared() < 1e-6:continue
   nrm = nrm.normalized()
   var t := Vector2(nrm.y, -nrm.x)
   var half: float = line.halves[i]
   if walks.is_empty():continue
   var home := Vector2i(floori(c.x / BIN), floori(c.y / BIN))
   for dx in range(-reach, reach + 1):
    for dy in range(-reach, reach + 1):
     for seg in walks.get(home + Vector2i(dx, dy), []):
      var a: Vector2 = seg[0];var b: Vector2 = seg[1]
      var d := (b - a).normalized()
      if absf(d.dot(t)) < KERB_PARALLEL:continue
      # Where the station's cross line meets the sidewalk's line.
      var denom := d.dot(t)
      var s := (c - a).dot(t) / denom
      if s < -.5 or s > a.distance_to(b) + .5:continue
      var lat := (a + d * s - c).dot(nrm)
      var beyond := absf(lat) - half
      if beyond < KERB_SIDEWALK_MIN_M or beyond > KERB_SIDEWALK_MAX_M:continue
      kerb[1 if lat > 0. else 0][i] = 1
  for s in 2:
   var run: PackedByteArray = kerb[s]
   fill_gaps(run, KERB_FILL_STATIONS)
   drop_short(run, KERB_MIN_STATIONS)
   for i in n:
    if run[i] == 1:edge[i * 2 + s] = EDGE_KERB

## Per station, 1 where at least half of the stations within `reach` metres
## of arc each way are 1 (a tie is 1, as in LaneMarkings.path()).
static func majority(run: PackedByteArray, arc: PackedFloat64Array, reach: float) -> PackedByteArray:
 var out := PackedByteArray();out.resize(run.size())
 var lo := 0;var hi := 0;var votes := 0
 for k in run.size():
  while hi < run.size() and arc[hi] - arc[k] <= reach:votes += run[hi];hi += 1
  while arc[k] - arc[lo] > reach:votes -= run[lo];lo += 1
  out[k] = 1 if votes * 2 >= hi - lo else 0
 return out

## Set the gaps of at most `gap` zeros between ones.
static func fill_gaps(run: PackedByteArray, gap: int) -> void:
 var last := -1
 for i in run.size():
  if run[i] != 1:continue
  if last >= 0 and i - last - 1 <= gap:
   for j in range(last + 1, i):run[j] = 1
  last = i

## Clear runs of ones shorter than `shortest`.
static func drop_short(run: PackedByteArray, shortest: int) -> void:
 var i := 0
 while i < run.size():
  if run[i] != 1:i += 1;continue
  var j := i
  while j < run.size() and run[j] == 1:j += 1
  if j - i < shortest:
   for k in range(i, j):run[k] = 0
  i = j

## Whether two or more ribbons cover p: where shoulder bands overlap (a
## divided street, a junction corner) the ground is paved (paved_elsewhere()).
func shared(p: Vector2) -> bool:
 var first := -1
 for idx in bins.get(Vector2i(floori(p.x / BIN), floori(p.y / BIN)), []):
  var o: int = idx * 4
  if p.x < boxes[o] or p.y < boxes[o + 1] or p.x > boxes[o + 2] or p.y > boxes[o + 3]:continue
  if quad_line[idx] == first:continue
  var q: Array = quads[idx]
  if is_nan(barycentric(p, q[0], q[1], q[2]).x) and is_nan(barycentric(p, q[0], q[2], q[3]).x):continue
  if first >= 0:return true
  first = quad_line[idx]
 return false

var paved_cache := {}
## 1 where section point idx of station i of line li lies on another ribbon
## (its carriageway, or its shoulder band beside this one: two carriageways of
## a divided street a metre apart, a junction corner), else 0 (cached:
## neighbouring quads share the point). Paved there, as on a real road.
func paved_elsewhere(li: int, i: int, idx: int) -> float:
 var key := Vector3i(li, i, idx)
 if paved_cache.has(key):return paved_cache[key]
 var v: Vector3 = lines[li].sections[i][idx]
 var other := lateral(Vector2(v.x, v.z), li)
 var flag := 1. if not is_nan(other.x) and absf(other.x) < other.y + SHOULDER_M - .05 else 0.
 paved_cache[key] = flag
 return flag

func mesh(mat: Material, side: float) -> ArrayMesh:
 var st := SurfaceTool.new()
 st.begin(Mesh.PRIMITIVE_TRIANGLES)
 for qi in quads.size():
  var q: Array = quads[qi]
  var line: Dictionary = lines[quad_line[qi]]
  var i: int = quad_station[qi];var k: int = quad_band[qi]
  var s := 1 if k >= 2 else 0
  var edge: PackedByteArray = line.edge
  # UV2: signed lateral offset and paved half width (terrain.gdshader reads
  # the ribbon from it); COLOR: kerb, built up on this side.
  var uv2 := [Vector2(offset_of(line, i - 1, k), line.halves[i - 1]), Vector2(offset_of(line, i - 1, k + 1), line.halves[i - 1]),
   Vector2(offset_of(line, i, k + 1), line.halves[i]), Vector2(offset_of(line, i, k), line.halves[i])]
  var e0: int = edge[(i - 1) * 2 + s];var e1: int = edge[i * 2 + s]
  var tint := [edge_color(e0), edge_color(e0), edge_color(e1), edge_color(e1)]
  # COLOR.b: a corner of this ribbon's shoulder band that lies on another
  # ribbon's carriageway (a junction, a merge) is asphalt there too, so a
  # minor road's shoulder never paints gravel across the road it joins.
  if k == 0 or k == 3:
   var li: int = quad_line[qi]
   var corners := [Vector2i(i - 1, k), Vector2i(i - 1, k + 1), Vector2i(i, k + 1), Vector2i(i, k)]
   for c in 4:
    tint[c].b = paved_elsewhere(li, corners[c].x, corners[c].y)
  # Godot front faces wind clockwise seen from the front (above).
  for corner in [0, 2, 1, 0, 3, 2]:
   var v: Vector3 = q[corner]
   st.set_uv(Vector2(v.x, v.z) / side)
   st.set_uv2(uv2[corner])
   st.set_color(tint[corner])
   st.add_vertex(v)
 st.generate_normals()
 var m := st.commit()
 m.surface_set_material(0, mat)
 return m
