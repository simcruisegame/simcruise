extends RefCounted
## Kerbside parking and the travel lanes it must stay out of.
##
## The map gives a road its paved width, class and one-way flag, nothing about
## lanes (helper/road-widths.mjs). This file is the one lane model the game
## uses for parked cars, delivery pull-overs and moving traffic, so a parked car
## can be tested against the same lanes the traffic drives in:
##  * a road carries two travel lanes: one per direction on a two-way road
##    (the painted centre line), two on a one-way road (the painted lane line);
##  * a travel lane is at least LANE_MIN_M wide (10 ft, the narrowest urban
##    lane in the AASHTO Green Book and NACTO's guide);
##  * whatever pavement a street (primary, secondary, tertiary) has beyond its
##    lanes, up to PARK_MAX_M (an 8 ft parking lane) per side, is a parking lane
##    when at least PARK_MIN_M is left, else the lanes take it.
## So a 12 m primary has 3.6 m lanes and 2.4 m parking lanes, a 10 m tertiary
## 3.0 m lanes and 2.0 m parking lanes, and a streamed one-way carriageway (two
## 3.6 m lanes plus shoulders, at most 9.6 m) or a minor street has none. Tiles
## built before one-way widths (the bundled Miami Beach start: one-way streets
## at the 10-12 m class width) do get parking lanes on their one-way streets.
##
## A parked car stands KERB_GAP from the kerb, fully inside the parking lane,
## and keeps clear of every other road: a junction by the cross road's half
## width plus a crosswalk and the 20 ft no-parking distance (Uniform Vehicle
## Code 11-1003), a driveway or alley by its half width plus 5 ft. Segments are
## the tile builder's [a, b, name, width, oneway, class, surface].
const LANE_MIN_M := 3.0
const PARK_MIN_M := 2.0
const PARK_MAX_M := 2.4
const KERB_GAP := .12
const PARKING_CLASSES := ["primary","secondary","tertiary"]
const STREET_CLASSES := ["motorway","trunk","primary","secondary","tertiary","minor"]
const JUNCTION_CLEAR := 3. + 6.1      # crosswalk + 20 ft
const DRIVEWAY_CLEAR := 1.5           # 5 ft
const SPOT_LENGTH := 5.9              # longest parked model (a full-size pickup)

static func is_oneway(seg: Array) -> bool:
 var o = seg[4]
 return (o is bool and o) or ((o is int or o is float) and o != 0)

## Parking lane width per side, metres (0: no parking lane).
static func parking_m(seg: Array) -> float:
 if str(seg[5]) not in PARKING_CLASSES:return 0.
 if seg.size() > 6 and seg[6] == "unpaved":return 0.
 var spare := float(seg[3]) * .5 - LANE_MIN_M
 return minf(PARK_MAX_M, spare) if spare >= PARK_MIN_M else 0.

## Distance from the centre line to the outer edge of the travel lanes.
static func travel_half(seg: Array) -> float:
 return float(seg[3]) * .5 - parking_m(seg)

## Lateral offset of a moving car's lane centre, right of the centre line:
## the middle of the right-hand travel lane (on a one-way road the right one of
## its two). A one-way road without parking lanes keeps the old offsets: the
## right lane of a wide carriageway, the middle of a narrow one.
static func lane_offset(width: float, oneway: bool, cls: String) -> float:
 var seg := [Vector2.ZERO, Vector2.ZERO, "", width, 1 if oneway else 0, cls]
 if oneway and parking_m(seg) <= 0.:return width * .2 if width > 7. else 0.
 return travel_half(seg) * .5

## Corners of a car footprint centred at c, long axis along fwd.
static func box(c: Vector2, fwd: Vector2, length: float, width: float) -> PackedVector2Array:
 var f := fwd.normalized() * length * .5
 var r := Vector2(-fwd.y, fwd.x).normalized() * width * .5
 return PackedVector2Array([c + f + r, c + f - r, c - f - r, c - f + r])

## Shortest distance from a footprint polygon to the segment a-b (0 if they touch).
static func box_distance(corners: PackedVector2Array, a: Vector2, b: Vector2) -> float:
 if Geometry2D.is_point_in_polygon(a, corners) or Geometry2D.is_point_in_polygon(b, corners):return 0.
 var best := INF
 for i in corners.size():
  var p: Vector2 = corners[i]
  var q: Vector2 = corners[(i + 1) % corners.size()]
  if Geometry2D.segment_intersects_segment(p, q, a, b) != null:return 0.
  var pair := Geometry2D.get_closest_points_between_segments(p, q, a, b)
  best = minf(best, pair[0].distance_to(pair[1]))
 return best

static func same_road(s: Array, k: Array) -> bool:
 if str(s[2]) != str(k[2]) or str(s[5]) != str(k[5]):return false
 var ts: Vector2 = (s[1] - s[0]).normalized()
 var tk: Vector2 = (k[1] - k[0]).normalized()
 return absf(ts.dot(tk)) > .8

## Why a parked footprint is not allowed here ("" when it is): it reaches into
## a travel lane of any road, or into a junction or driveway mouth of a road
## other than its own. own is the segment it parks on (null to test them all
## as foreign roads, e.g. for a car whose road is unknown).
static func conflict(corners: PackedVector2Array, segments: Array, own = null) -> String:
 var c := (corners[0] + corners[2]) * .5
 var radius := corners[0].distance_to(c)
 for k in segments:
  var a: Vector2 = k[0];var b: Vector2 = k[1]
  var hw := float(k[3]) * .5
  var foreign: bool = own == null or (not is_same(k, own) and not same_road(own, k))
  var keep := travel_half(k)
  if foreign:keep = hw + (JUNCTION_CLEAR if str(k[5]) in STREET_CLASSES else DRIVEWAY_CLEAR)
  if Geometry2D.get_closest_point_to_segment(c, a, b).distance_to(c) > keep + radius:continue
  var d := box_distance(corners, a, b)
  if d < travel_half(k) - .01:return "travel lane of %s %s" % [k[5], k[2]]
  if foreign and d < keep:return ("junction with " if str(k[5]) in STREET_CLASSES else "driveway ") + "%s %s" % [k[5], k[2]]
 return ""

## Broad phase: segments by CELL, each in every cell within its widest
## clearance, so the cell of a footprint's centre lists every road it can touch.
const CELL := 32.
static func index(segments: Array) -> Dictionary:
 var grid := {}
 for k in segments:
  var reach := Vector2.ONE * (float(k[3]) * .5 + JUNCTION_CLEAR + SPOT_LENGTH)
  var lo := Vector2i(((k[0].min(k[1]) - reach) / CELL).floor())
  var hi := Vector2i(((k[0].max(k[1]) + reach) / CELL).floor())
  for x in range(lo.x, hi.x + 1):
   for y in range(lo.y, hi.y + 1):
    var key := Vector2i(x, y)
    if not grid.has(key):grid[key] = []
    grid[key].append(k)
 return grid

static func nearby(grid: Dictionary, p: Vector2) -> Array:
 return grid.get(Vector2i((p / CELL).floor()), [])

## Kerbside spots along a tile's streets: [kerb point, yaw, variant, room,
## inward normal, segment index]. A car of width w stands at kerb + inward * (KERB_GAP + w / 2)
## and fits when w + KERB_GAP <= room (Drive.gd update_parked). occupied(p) is
## true inside a building footprint. A tile's roads stop at its edge, so a cross
## street just over the edge is unknown here: with side > 0 (the tile's size)
## no spot is made within EDGE_MARGIN of the edge.
const EDGE_MARGIN := JUNCTION_CLEAR + 8. + SPOT_LENGTH * .5   # a 16 m cross road's junction
static func spots(segments: Array, tile_id: Array, occupied: Callable, side := 0.) -> Array:
 var out := []
 var grid := index(segments)
 for idx in segments.size():
  var seg: Array = segments[idx]
  var room := parking_m(seg)
  if room <= 0.:continue
  var a: Vector2 = seg[0];var b: Vector2 = seg[1];var length := a.distance_to(b)
  if length < 36:continue
  var t := (b - a) / length;var n := Vector2(-t.y, t.x);var half := float(seg[3]) * .5
  for signum in [-1, 1]:
   var outward: Vector2 = n * signum
   for j in range(16 + idx % 9, int(length) - 10, 27 + (idx % 3) * 5):
    var kerb := a + t * j + outward * half
    var centre := kerb - outward * (KERB_GAP + (room - KERB_GAP) * .5)
    if side > 0. and (minf(centre.x, centre.y) < EDGE_MARGIN or maxf(centre.x, centre.y) > side - EDGE_MARGIN):continue
    if occupied.call(centre):continue
    var corners := box(centre, t, SPOT_LENGTH, room - KERB_GAP)
    if conflict(corners, nearby(grid, centre), seg) != "":continue
    var rnd := absi(hash(Vector3i(int(tile_id[0]) + idx, int(tile_id[1]) + j, signum)))
    # Detail cars are pooled around the driver. More distant streets use the ground texture.
    # Parked with the traffic on its side: both kerbs of a one-way street face its way.
    var facing := t
    if is_oneway(seg):facing = t * (-1. if float(seg[4]) < 0. else 1.)
    elif signum < 0:facing = -t
    out.append([kerb, atan2(facing.x, facing.y), rnd % 5, room, -outward, idx])
 return out
