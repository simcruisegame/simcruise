extends RefCounted
## The drivable corridor of a tile: every place a car may drive, so that nothing
## that collides is ever built there (GuardRails.gd, BridgeDeck.gd).
##
## The 2026-11-10 field test crashed every run at two real places because a
## collider stood on a through road: inferred guard rails of Jerome's side
## streets ran diagonally across the AZ-89A junction (each side street's ribbon
## reaches the main road's centreline, and so did the rail along its shoulder),
## and at Moab the parapet of one US-191 carriageway's deck stood inside the
## other carriageway's deck (twin one-way decks 17 m apart, each 16 m wide).
##
## The corridor is the union of, all with their surface height:
##   * every road ribbon (RoadSurface.gd): its carriageway half width plus
##     MARGIN_M, station by station, at the ribbon's height;
##   * every mapped bridge deck (BridgeDeck.gd): the deck with its shoulders, at
##     the deck height;
##   * every other mapped road (no ribbon, not on a deck: driveways, service
##     roads, tracks): its mapped width plus MARGIN_M along the centreline, at
##     height_at();
##   * every junction: a disc around each road end that meets another road
##     (not a continuation of the same carriageway), radius the wider road's
##     half width plus JUNCTION_PAD_M, so rails stop where turning cars sweep.
## A point blocks when it is inside a piece of the corridor and the collider's
## height span overlaps the road's envelope (BELOW_M under to ABOVE_M over the
## surface): a rail under a bridge, or beside a road far below, is not on it.
## A rail's own road is tested with its carriageway only (the rail stands on
## that road's shoulder), and a parapet ignores its own deck.

const MARGIN_M := .3
const JUNCTION_PAD_M := 3.
const BELOW_M := 1.5
const ABOVE_M := 4.5
const BIN := 16.
const REACH_M := 2.         # widest margin any query may add
const CAPSULE_STEP_M := 10.
const DECK_SHOULDER_M := .6  # BridgeDeck.SHOULDER_M

# Each piece: [a: Vector3, b: Vector3, half: float, owner: String, station: int]
var pieces: Array = []
var bins := {}

## --without-road-clearance builds rails and parapets unclipped (A/B, and the
## before measurement of checks/RoadClearance.gd).
static func enabled() -> bool:
 return not "--without-road-clearance" in OS.get_cmdline_user_args()

static func for_tile(tile) -> RefCounted:
 if tile.has_meta("drivable_corridor"):return tile.get_meta("drivable_corridor")
 var c = new()
 c.build(tile)
 tile.set_meta("drivable_corridor", c)
 return c

func add(a: Vector3, b: Vector3, half: float, owner: String, station := -1) -> void:
 var idx := pieces.size()
 pieces.append([a, b, half, owner, station])
 var r := half + REACH_M
 var lo := Vector2(minf(a.x, b.x) - r, minf(a.z, b.z) - r);var hi := Vector2(maxf(a.x, b.x) + r, maxf(a.z, b.z) + r)
 for x in range(floori(lo.x / BIN), floori(hi.x / BIN) + 1):
  for y in range(floori(lo.y / BIN), floori(hi.y / BIN) + 1):
   var key := Vector2i(x, y)
   if not bins.has(key):bins[key] = []
   bins[key].append(idx)

func build(tile) -> void:
 var side: float = tile.side
 # Ribbons: one piece per station pair, at the ribbon's own height.
 if tile.road_surface != null:
  var lines: Array = tile.road_surface.lines
  for li in lines.size():
   var sections: Array = lines[li].sections
   var half: float = lines[li].half
   for i in range(1, sections.size()):
    var a: Vector3 = sections[i-1][2];var b: Vector3 = sections[i][2]
    if Vector2(a.x, a.z).distance_to(Vector2(b.x, b.z)) >= 8.:continue
    add(a, b, half + MARGIN_M, "L%d" % li, i)
 # Decks, with their shoulders, at the deck height.
 var bridges: Array = tile.info.get("bridges", [])
 for bi in bridges.size():
  var row = bridges[bi]
  if not row is Array or row.size() < 5:continue
  var pts: Array = row[2];var heights: Array = row[4]
  if pts.size() < 2 or pts.size() != heights.size():continue
  var half := float(row[3]) * .5 + DECK_SHOULDER_M
  for k in range(1, pts.size()):
   var a := Vector3(float(pts[k-1][0]) * side, float(heights[k-1]), float(pts[k-1][1]) * side)
   var b := Vector3(float(pts[k][0]) * side, float(heights[k]), float(pts[k][1]) * side)
   add(a, b, half, "B%d" % bi)
  extend_at_edges(tile, pts, heights, half, "B%d" % bi)
 # Mapped roads the ribbons do not cover (and that are not on a deck).
 var roads: Array = tile.info.get("roads", [])
 var polylines := []        # [pts tile metres, half, bridge-on-deck]
 for road in roads:
  var pts := PackedVector2Array()
  for p in road[3]:pts.append(Vector2(float(p[0]), float(p[1])) * side)
  var half := float(road[4]) * .5
  var on_deck: bool = road.size() > 6 and str(road[6]) == "bridge" and not bridges.is_empty()
  polylines.append([pts, half, on_deck])
  if on_deck:continue
  for k in range(1, pts.size()):
   var a: Vector2 = pts[k-1];var b: Vector2 = pts[k]
   var length := a.distance_to(b)
   if length < .05 or covered(tile, a, b):continue
   var steps := maxi(1, ceili(length / CAPSULE_STEP_M))
   for s in steps:
    var p0 := a.lerp(b, float(s) / steps);var p1 := a.lerp(b, float(s + 1) / steps)
    add(Vector3(p0.x, tile.height_at(p0), p0.y), Vector3(p1.x, tile.height_at(p1), p1.y), half + MARGIN_M, "R")
  if pts.size() >= 2:
   var raw := []
   var heights := []
   for p in pts:raw.append([p.x / side, p.y / side]);heights.append(tile.height_at(p))
   extend_at_edges(tile, raw, heights, half + MARGIN_M, "R")
 # Junctions: a road end that meets another road, unless it simply continues it.
 for i in polylines.size():
  var pts: PackedVector2Array = polylines[i][0]
  if pts.size() < 2:continue
  for end in [0, pts.size() - 1]:
   var e: Vector2 = pts[end]
   var away: Vector2 = (pts[1] - pts[0]) if end == 0 else (pts[end - 1] - pts[end])
   if away.length_squared() < 1e-6:continue
   away = away.normalized()
   var radius := 0.
   for j in polylines.size():
    if j == i:continue
    var other: PackedVector2Array = polylines[j][0]
    var other_half: float = polylines[j][1]
    if other.size() < 2:continue
    var near := INF
    for k in range(1, other.size()):
     near = minf(near, e.distance_to(Geometry2D.get_closest_point_to_segment(e, other[k-1], other[k])))
    if near > other_half + 1.:continue
    if continues(e, away, other):continue
    radius = maxf(radius, maxf(polylines[i][1], other_half) + JUNCTION_PAD_M)
   if radius <= 0.:continue
   var h := NAN
   if polylines[i][2]:h = tile.deck_at(e)
   if is_nan(h):h = tile.height_at(e)
   var c := Vector3(e.x, h, e.y)
   add(c, c, radius, "J")

# The helper clips decks and roads at the tile edge, but the road goes on in
# the neighbouring tile; a prop near the edge must still see it. Each end that
# lies on the edge is carried on straight for EDGE_EXTEND_M at its own height.
const EDGE_EXTEND_M := 12.
func extend_at_edges(tile, pts: Array, heights: Array, half: float, owner: String) -> void:
 var side: float = tile.side
 var n := pts.size()
 if n < 2:return
 for end in [0, n - 1]:
  var e := Vector2(float(pts[end][0]), float(pts[end][1])) * side
  if e.x > .5 and e.y > .5 and e.x < side - .5 and e.y < side - .5:continue
  # The helper often repeats the edge point; walk in to the first distinct one.
  var dir := Vector2.ZERO
  var inner_i: int = 1 if end == 0 else n - 2
  while inner_i >= 0 and inner_i < n:
   dir = e - Vector2(float(pts[inner_i][0]), float(pts[inner_i][1])) * side
   if dir.length_squared() >= .01:break
   inner_i += 1 if end == 0 else -1
  if dir.length_squared() < .01:continue
  var far := e + dir.normalized() * EDGE_EXTEND_M
  var h := float(heights[end])
  add(Vector3(e.x, h, e.y), Vector3(far.x, h, far.y), half, owner)

# True when segment a-b lies on a ribbon (both ends and the middle).
static func covered(tile, a: Vector2, b: Vector2) -> bool:
 if tile.road_surface == null:return false
 for p in [a, (a + b) * .5, b]:
  if is_nan(tile.road_surface.height(p)):return false
 return true

# Another piece of the same carriageway: it starts or ends at e and leaves e
# in the opposite direction to this road (within 35 degrees).
static func continues(e: Vector2, away: Vector2, other: PackedVector2Array) -> bool:
 for end in [0, other.size() - 1]:
  if other[end].distance_to(e) > 1.5:continue
  var dir: Vector2 = (other[1] - other[0]) if end == 0 else (other[end - 1] - other[end])
  if dir.length_squared() < 1e-6:continue
  if dir.normalized().dot(away) < -cos(deg_to_rad(35.)):return true
 return false

## Does a collider standing at p (tile metres) from y_lo to y_hi intrude on the
## corridor? skip_owner ignores one owner entirely (a parapet's own deck);
## own_line/own_station test the rail's own ribbon with the bare carriageway
## within OWN_REACH stations of where the rail came from.
const OWN_REACH := 12
func blocked(p: Vector2, y_lo: float, y_hi: float, skip_owner := "", own_line := "", own_station := -1) -> bool:
 for idx in bins.get(Vector2i(floori(p.x / BIN), floori(p.y / BIN)), []):
  var piece: Array = pieces[idx]
  var owner: String = piece[3]
  if owner == skip_owner:continue
  var a: Vector3 = piece[0];var b: Vector3 = piece[1]
  var a2 := Vector2(a.x, a.z);var b2 := Vector2(b.x, b.z)
  var ab := b2 - a2
  var t := 0.
  if ab.length_squared() > 1e-8:t = clampf((p - a2).dot(ab) / ab.length_squared(), 0., 1.)
  var half: float = piece[2]
  if owner == own_line and own_station >= 0 and absi(int(piece[4]) - own_station) <= OWN_REACH:half -= MARGIN_M
  if p.distance_to(a2 + ab * t) >= half:continue
  var h := lerpf(a.y, b.y, t)
  if y_hi >= h - BELOW_M and y_lo <= h + ABOVE_M:return true
 return false
