extends RefCounted
## Carriageway widths for one-way roads and twin bridge decks, applied to a
## tile's data before anything is built from it (WorldTile.build).
##
## The helper gives every road its class width (helper/verified-footprints.mjs:
## trunk 16 m, primary 12 m, secondary 11 m, tertiary 10 m). Those are two-way
## widths: four lanes on a trunk. A divided highway is mapped as two one-way
## ways, one per carriageway, and each got the full two-way width, so the two
## carriageways of US-191 at Moab UT, whose centrelines are 12.8-17 m apart,
## were each 16 m wide and overlapped by 3-5 m: on the twin bridge decks over
## the Colorado River each deck's parapet stood in the other deck, and at the
## south end the 16 m ribbon of the one-way link to Lions Park reached across
## the northbound approach and stood 0.35 m proud of it where the approach
## meets the deck, which rolled the 2026-11-10 field-test car (RoadDrive,
## station 118).
##
##   * One-way trunk, primary, secondary and tertiary roads (OSM oneway, the
##     carriageways of divided roads, one-way streets and links) are at most
##     ONEWAY_MAX_W wide: two 3.6 m lanes and their edge lines, the common US
##     carriageway (AASHTO Green Book: 12 ft lanes). Motorways are always
##     mapped one way per carriageway and keep their class width. The same
##     limit applies to the road rows, their bridge decks and their road
##     surface ribbons, so ribbons, decks, paint and the drivable corridor
##     agree.
##   * Twin decks: where two decks still overlap side by side (parallel within
##     TWIN_ANGLE_DEG, deck levels within TWIN_LEVEL_M), both are narrowed in
##     proportion so that their outer parapet faces meet at most at the
##     smallest centreline offset; a deck is never narrower than MIN_DECK_W.
##     Each narrowed deck is counted in info.bridge_twin_narrowed.
## The helper's own widths are kept in info.road_class_width (per road row)
## for data validated against them (RoadCrossfallGround.gd).
## --without-road-clearance leaves the data as the helper wrote it.

const ONEWAY_MAX_W := 8.
const ONEWAY_CLASSES := ["trunk", "primary", "secondary", "tertiary"]
const TWIN_ANGLE_DEG := 20.
const TWIN_LEVEL_M := 3.
const MIN_DECK_W := 3.6
const DECK_SHOULDER_M := .6     # BridgeDeck.SHOULDER_M
const DECK_PARAPET_W := .45     # BridgeDeck.PARAPET_W

static func apply(data: Dictionary) -> void:
 if "--without-road-clearance" in OS.get_cmdline_user_args():return
 if data.get("road_widths_applied", false):return
 data["road_widths_applied"] = true
 var roads: Array = data.get("roads", [])
 var original := []
 var narrowed := 0
 for road in roads:
  original.append(float(road[4]) if road.size() > 4 else 0.)
  if road.size() > 4 and one_way(road[0], road[1]) and float(road[4]) > ONEWAY_MAX_W:
   road[4] = ONEWAY_MAX_W;narrowed += 1
 if narrowed > 0:data["road_class_width"] = original
 data["road_oneway_narrowed"] = narrowed
 for row in data.get("bridges", []):
  if row is Array and row.size() > 6 and one_way(row[0], row[6]) and float(row[3]) > ONEWAY_MAX_W:row[3] = ONEWAY_MAX_W
 # Ribbon chains carry no one-way flag: a chain is one-way when most of its
 # sampled stations lie nearest to a one-way road of its class.
 for chain in data.get("road_surface", []):
  if not chain is Dictionary or not str(chain.get("cls", "")) in ONEWAY_CLASSES:continue
  if float(chain.get("w", 6.)) <= ONEWAY_MAX_W:continue
  if chain_one_way(chain, roads):chain["w"] = ONEWAY_MAX_W
 separate_twins(data)

static func one_way(cls, oneway) -> bool:
 if not str(cls) in ONEWAY_CLASSES:return false
 if oneway is bool:return oneway
 if oneway is int or oneway is float:return oneway != 0
 return str(oneway) in ["1", "-1", "yes", "true"]

static func chain_one_way(chain: Dictionary, roads: Array) -> bool:
 var pts: Array = chain.get("pts", [])
 if pts.is_empty():return false
 var cls := str(chain.get("cls", ""))
 var votes := 0;var samples := 0
 var stride := maxi(1, pts.size() / 9)
 for k in range(0, pts.size(), stride):
  var p := Vector2(float(pts[k][0]), float(pts[k][1]))
  var best := INF;var best_oneway := false
  for road in roads:
   if str(road[0]) != cls:continue
   var line: Array = road[3]
   for j in range(1, line.size()):
    var a := Vector2(float(line[j-1][0]), float(line[j-1][1]));var b := Vector2(float(line[j][0]), float(line[j][1]))
    var d := p.distance_squared_to(Geometry2D.get_closest_point_to_segment(p, a, b))
    if d < best:best = d;best_oneway = one_way(road[0], road[1])
  samples += 1
  if best_oneway:votes += 1
 return votes * 2 > samples

## Narrow side-by-side decks that overlap so they meet instead (see header).
static func separate_twins(data: Dictionary) -> void:
 var rows: Array = data.get("bridges", [])
 if rows.size() < 2:return
 var side := tile_side(data)
 if side <= 0.:return
 var paths := []
 for row in rows:
  var path := []
  if row is Array and row.size() > 4 and row[2] is Array and row[4] is Array and row[2].size() == row[4].size():
   for k in row[2].size():path.append(Vector3(float(row[2][k][0]) * side, float(row[4][k]), float(row[2][k][1]) * side))
  paths.append(path)
 var outer := []          # half width to the outer parapet face, per deck
 for row in rows:outer.append(float(row[3]) * .5 + DECK_SHOULDER_M + DECK_PARAPET_W if row is Array and row.size() > 4 else 0.)
 var limit := outer.duplicate()
 for i in rows.size():
  for j in range(i + 1, rows.size()):
   if paths[i].size() < 2 or paths[j].size() < 2:continue
   var gap := minf(twin_gap(paths[i], paths[j]), twin_gap(paths[j], paths[i]))
   if gap >= outer[i] + outer[j]:continue
   var share_i: float = gap * outer[i] / (outer[i] + outer[j])
   limit[i] = minf(limit[i], share_i);limit[j] = minf(limit[j], gap - share_i)
 var count := 0
 for i in rows.size():
  if limit[i] >= outer[i] - 1e-3:continue
  var width := maxf(MIN_DECK_W, 2. * (limit[i] - DECK_SHOULDER_M - DECK_PARAPET_W))
  if width < float(rows[i][3]):
   rows[i][3] = snappedf(width, .01);count += 1
 data["bridge_twin_narrowed"] = count

## Smallest centreline offset from the vertices and midpoints of a to deck b,
## counting only points that face b side by side: b's nearest segment runs
## within TWIN_ANGLE_DEG of a's direction (either way), the foot of the
## perpendicular falls inside b (not past its end) and the decks are within
## TWIN_LEVEL_M in height. INF when they never run side by side.
static func twin_gap(a: Array, b: Array) -> float:
 var gap := INF
 for k in range(1, a.size()):
  var p0: Vector3 = a[k-1];var p1: Vector3 = a[k]
  var ta := Vector2(p1.x - p0.x, p1.z - p0.z)
  if ta.length_squared() < 1e-4:continue
  ta = ta.normalized()
  for f in [0., .5, 1.]:
   var p: Vector3 = p0.lerp(p1, f)
   var q := Vector2(p.x, p.z)
   for m in range(1, b.size()):
    var b0: Vector3 = b[m-1];var b1: Vector3 = b[m]
    var tb := Vector2(b1.x - b0.x, b1.z - b0.z)
    var l2 := tb.length_squared()
    if l2 < 1e-4:continue
    var t := (q - Vector2(b0.x, b0.z)).dot(tb) / l2
    if t < 0. or t > 1.:continue
    if absf(ta.dot(tb / sqrt(l2))) < cos(deg_to_rad(TWIN_ANGLE_DEG)):continue
    var foot: Vector3 = b0.lerp(b1, t)
    if absf(foot.y - p.y) > TWIN_LEVEL_M:continue
    # Closer than a lane is one deck continued (rows meet end to end at a
    # tile edge or a joint), not a twin.
    var d := q.distance_to(Vector2(foot.x, foot.z))
    if d >= MIN_DECK_W:gap = minf(gap, d)
 return gap

## Metres per tile unit from the tile id (web-mercator z16), as WorldTile's
## callers compute it; 0 when the data has no id.
static func tile_side(data: Dictionary) -> float:
 var id = data.get("id", null)
 if not id is Array or id.size() < 2:return 0.
 return 40075016.68557849 / 65536. / cosh(PI * (1. - 2. * (float(id[1]) + .5) / 65536.))
