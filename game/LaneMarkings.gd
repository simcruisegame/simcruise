extends RefCounted
## Pavement markings for the paved major roads of a streamed tile, after the
## MUTCD (2009 edition, Part 3: chapters 3A and 3B).
##
## Lane counts come from OpenStreetMap (`lanes`, `lanes:forward`,
## `lanes:backward`, `oneway`, `maxspeed`), which helper/lane-counts.mjs
## matches to every segment of the tile's roads (tile JSON `road_lanes`). A
## segment with no mapped count gets the US default: two lanes, one each way on
## a two-way road. From the count:
##   * white edge lines on freeways, expressways and arterials (motorway, trunk,
##     primary) everywhere and on collectors (secondary, tertiary) outside
##     towns (3B.06, 3B.07); on a one-way carriageway the left edge is yellow;
##   * a yellow centre line on two-way roads (3B.01): a double solid line on
##     roads with two or more lanes each way and in towns; on rural two-lane
##     roads a broken line (10 ft dash, 30 ft gap) where the passing sight
##     distance of Table 3B-1 is available, measured on the road surface with
##     a 3.5 ft eye and object (3B.02), solid on the side that may not pass,
##     and no passing zone shorter than 400 ft;
##   * a centre two-way left-turn lane (3B.03, solid yellow outside, broken
##     yellow inside) where the lane count is odd and only one lane short of
##     even (OSM has no `turn:lanes` here, so turn pockets read the same way);
##   * white broken lane lines between lanes in the same direction (3B.04),
##     10 ft lines on a 40 ft cycle anchored to the world, so dashes do not
##     jump at tile seams on straight roads;
##   * no markings on local streets (the `minor` class) and no centre line on a
##     road narrower than 16 ft or mapped with a single lane (3B.01).
## Lanes are LANE_M wide (narrower if the mapped count does not fit the road's
## width, never under MIN_LANE_M) and may use the paved shoulder of the road
## ribbon (RoadSurface.gd) when the count needs it.
##
## The lines follow the engineered road ribbon: their path is the ribbon's
## 2 m station centreline and every corner sits at WorldTile.height_at() plus
## LIFT_M, so they lie on the same smooth, superelevated surface the tyres
## drive on. Roads without a ribbon (--grid-roads) use their map polyline.
## Lines stop where another road crosses them; the centre line carries on
## across a T junction. Measured paint keeps precedence wherever it exists:
## PaintEvidence and MeasuredSection mask the inferred lines inside their
## zones, RoadLayout removes inferred lane lines on a certified single lane,
## and a RoadSurvey road draws only its own surveyed strokes.

const DASH_M := 3.05          # 10 ft broken line
const PERIOD_M := 12.2        # 10 ft line + 30 ft gap
const LIFT_M := .048          # as WorldTile.raised_stripe()
const LINE_M := .12           # normal line (4-6 in)
const WIDE_M := .15           # 6 in edge lines on motorway, trunk and primary
const DOUBLE_GAP_M := .1      # between the two lines of a double line
const STEP_M := 2.            # station spacing of polyline (no-ribbon) paths
const SHOULDER_M := 1.2       # RoadSurface.SHOULDER_M
const MAX_JOIN_M := 8.        # a longer step between stations is a gap
const MAJOR := ["motorway","trunk","primary","secondary","tertiary"]
const ARTERIAL := ["motorway","trunk","primary"]
const LANE_M := {"motorway":3.65,"trunk":3.65,"primary":3.5,"secondary":3.35,"tertiary":3.2}
const MIN_LANE_M := 2.75
const NARROW_M := 4.9         # 16 ft: no centre line below this travelled width
# Default posted speed where OSM has no maxspeed, rural / in town.
const RURAL_MPH := {"motorway":65,"trunk":55,"primary":55,"secondary":50,"tertiary":45}
const TOWN_MAX_MPH := 40      # a mapped limit at or under this is a town street
# Without a mapped limit a station is in town when the 250 m square around it
# holds this many buildings or this length of local streets and driveways.
const TOWN_BUILDINGS := 15
const TOWN_STREETS_M := 600.
const HOME_CELL_M := 50.
const INHERIT_M := 250.       # unmapped stations take the counts of the same road this near
# MUTCD Table 3B-1: minimum passing sight distance (ft) by speed (mph).
const PSD_FT := {25:450,30:500,35:550,40:600,45:700,50:800,55:900,60:1000,65:1100,70:1200}
const EYE_M := 1.07           # 3.5 ft eye and object height (3B.02)
const SIGHT_CLEAR_M := 3.     # roadside clear of sight obstructions beyond the travelled way
const MIN_PASSING_M := 122.   # 400 ft
const SIGHT_EVERY := 4
const BIN_M := 24.

var tile
var roads: Array = []
var mapped := {}       # road index -> {segment index: [lanes, forward, backward, mph]}
var segs: Array = []   # [a, b, road index, segment index, half width, class, name]
var grid := {}         # Vector2i -> [index into segs]
var covered := {}      # Vector2i(road, segment) -> true once a ribbon station used it
var deck := {}         # road index -> true for roads drawn on a bridge deck (BridgeDeck.gd)
var homes := {}        # Vector2i -> building count
var streets := {}      # Vector2i -> metres of local street and driveway
var heights := {}      # Vector2 -> stripe corner height, within one polyline
var towns := {}        # Vector2i -> in town (no mapped limit)
var sections_by_key := {}   # cross-section cache, see path()
var sight_us := 0
var draw_us := 0
var origin := Vector2.ZERO
var stats := {"white_solid_m":0.,"white_dash_m":0.,"yellow_solid_m":0.,"yellow_dash_m":0.,"quads":0,"stations":0,"mapped_stations":0,"layouts":{}}

## Draws every inferred marking of the tile into its whitepaint / yellowpaint
## batches and returns statistics (also kept in tile.info.lane_markings).
static func paint(tile) -> Dictionary:
 var m = new()
 m.tile = tile
 var t0 := Time.get_ticks_usec()
 m.prepare()
 if tile.road_surface:
  for line in tile.road_surface.lines:m.ribbon_path(line)
 m.polyline_paths()
 m.stats["ms"] = (Time.get_ticks_usec() - t0) / 1000.
 m.stats["sight_ms"] = m.sight_us / 1000.
 return m.stats

## The lines across one road for one direction of travel: [offset m right of
## the travel direction, "white"/"yellow", "solid"/"dash", width m].
## lanes = [total, forward, backward] from OSM (0 = not mapped); passing =
## [forward may pass, backward may pass] on a rural two-lane road.
static func cross_section(cls: String, oneway: bool, lanes: Array, width: float, town: bool, passing: Array = [false, false], room: float = SHOULDER_M) -> Dictionary:
 var total := int(lanes[0]) if lanes.size() > 0 else 0
 var f := int(lanes[1]) if lanes.size() > 1 else 0
 var b := int(lanes[2]) if lanes.size() > 2 else 0
 var c := 0
 if oneway:
  if total <= 0:total = f if f > 0 else (2 if width >= 7.3 else 1)
  f = total;b = 0
 else:
  if f > 0 and b > 0:
   c = clampi(total - f - b, 0, 1)
  elif f > 0 and total > f:
   b = total - f
  elif b > 0 and total > b:
   f = total - b
  else:
   if total <= 0:total = 2 if width >= 5.5 else 1
   f = (total + 1) / 2;b = total / 2
   if total >= 3 and total % 2 == 1:f = (total - 1) / 2;b = f;c = 1
  # A mapped odd count with one direction a lane ahead (3 = 2+1, 5 = 3+2) is
  # read as a centre turn lane; OSM counts turn pockets in lanes:forward.
  if c == 0 and f > 0 and b > 0 and absi(f - b) == 1 and (f + b) % 2 == 1 and f + b >= 3:
   c = 1;f = mini(f, b);b = f
  if f <= 0 and b <= 0:f = 1
 var n: int = f + b + c
 var lane: float = clampf(width / n, MIN_LANE_M, LANE_M.get(cls, 3.3))
 var travelled: float = minf(n * lane, width + 2. * room - .3)
 lane = travelled / n
 var right := travelled * .5
 var lines := []
 var edges: bool = cls in ARTERIAL or not town
 var ew: float = WIDE_M if cls in ARTERIAL else LINE_M
 if edges:
  lines.append([right - ew * .5, "white", "solid", ew])
  lines.append([-right + ew * .5, "yellow" if oneway else "white", "solid", ew])
 for k in range(1, f):lines.append([right - k * lane, "white", "dash", LINE_M])
 for k in range(1, b):lines.append([-right + k * lane, "white", "dash", LINE_M])
 var kind := ""
 if not oneway and n >= 2 and travelled >= NARROW_M:
  var centre := right - f * lane
  var d := (DOUBLE_GAP_M + LINE_M) * .5
  if c > 0:
   kind = "turn lane"
   for edge in [[centre, 1.], [centre - lane, -1.]]:
    lines.append([edge[0] + d * edge[1], "yellow", "solid", LINE_M])
    lines.append([edge[0] - d * edge[1], "yellow", "dash", LINE_M])
  elif f + b >= 4 or town or f > 1 or b > 1:
   kind = "double solid"
   lines.append([centre + d, "yellow", "solid", LINE_M])
   lines.append([centre - d, "yellow", "solid", LINE_M])
  elif passing[0] and passing[1]:
   kind = "broken"
   lines.append([centre, "yellow", "dash", LINE_M])
  else:
   kind = "double solid" if not (passing[0] or passing[1]) else "solid and broken"
   lines.append([centre + d, "yellow", "dash" if passing[0] else "solid", LINE_M])
   lines.append([centre - d, "yellow", "dash" if passing[1] else "solid", LINE_M])
 return {"lines":lines, "lanes":n, "forward":f, "backward":b, "centre":c, "lane":lane, "travelled":travelled, "edges":edges, "centre_line":kind}

## MUTCD Table 3B-1 passing sight distance in metres for a speed in mph.
static func passing_sight_m(mph: float) -> float:
 var v := clampi(int(roundf(mph / 5.)) * 5, 25, 70)
 return float(PSD_FT[v]) * .3048

static func right_of(t: Vector2) -> Vector2:return Vector2(-t.y, t.x)

func prepare() -> void:
 roads = tile.info.get("roads", [])
 var id: Array = tile.info.get("id", [0, 0])
 if id.size() == 2:origin = Vector2(float(id[0]), float(id[1])) * tile.side
 var doc = tile.info.get("road_lanes")
 if doc is Dictionary and int(doc.get("version", 0)) == 1 and doc.get("status") == "checked" and doc.get("roads") is Array:
  for row in doc.roads:
   if not row is Array or row.size() != 2 or not row[1] is Array:continue
   var i := int(row[0])
   if i < 0 or i >= roads.size():continue
   var per := {}
   for run in row[1]:
    if not run is Array or run.size() != 6:continue
    for j in range(int(run[0]), mini(int(run[1]), 4096) + 1):per[j] = [int(run[2]), int(run[3]), int(run[4]), int(run[5])]
   mapped[i] = per
 var decks: bool = not tile.info.get("bridges", []).is_empty()
 for i in roads.size():
  var road: Array = roads[i]
  if decks and road.size() > 6 and road[6] == "bridge":
   deck[i] = true
   continue
  var cls := str(road[0])
  var route: Array = road[3]
  if cls in ["minor", "service"]:
   for j in range(1, route.size()):
    var a: Vector2 = tile.point(route[j-1]);var b: Vector2 = tile.point(route[j])
    var pieces := ceili(a.distance_to(b) / 10.)
    for k in pieces:
     var key := Vector2i((a.lerp(b, (k + .5) / pieces) / HOME_CELL_M).floor())
     streets[key] = streets.get(key, 0.) + a.distance_to(b) / pieces
  if cls in ["service", "track"]:continue
  var half := float(road[4]) * .5
  for j in range(1, route.size()):
   var a: Vector2 = tile.point(route[j-1]);var b: Vector2 = tile.point(route[j])
   if a.distance_to(b) < .1:continue
   var index := segs.size()
   segs.append([a, b, i, j - 1, half, cls, str(road[2])])
   var reach := Vector2.ONE * (half + 2.)
   var lo := Vector2i(((a.min(b) - reach) / BIN_M).floor());var hi := Vector2i(((a.max(b) + reach) / BIN_M).floor())
   for x in range(lo.x, hi.x + 1):
    for y in range(lo.y, hi.y + 1):
     var key := Vector2i(x, y)
     if not grid.has(key):grid[key] = []
     grid[key].append(index)
 for b in tile.info.get("buildings", []):
  var p = b
  for depth in 3:
   if p is Array and not p.is_empty() and p[0] is Array:p = p[0]
  if p is Array and p.size() >= 2 and typeof(p[0]) in [TYPE_FLOAT, TYPE_INT]:
   var key := Vector2i((Vector2(float(p[0]), float(p[1])) * tile.side / HOME_CELL_M).floor())
   homes[key] = homes.get(key, 0) + 1

func markable(i: int) -> bool:
 var road: Array = roads[i]
 if deck.has(i) or str(road[0]) not in MAJOR:return false
 return not (road.size() > 5 and road[5] == "unpaved")

func in_town(p: Vector2, mph: int) -> bool:
 if mph > 0:return mph <= TOWN_MAX_MPH
 var c := Vector2i((p / HOME_CELL_M).floor())
 if towns.has(c):return towns[c]
 var count := 0;var length := 0.
 for dx in range(-2, 3):
  for dy in range(-2, 3):
   count += homes.get(c + Vector2i(dx, dy), 0)
   length += streets.get(c + Vector2i(dx, dy), 0.)
 towns[c] = count >= TOWN_BUILDINGS or length >= TOWN_STREETS_M
 return towns[c]

## The road segment a station of a ribbon belongs to: [segment, +1/-1 along it].
func match(p: Vector2, t: Vector2, cls: String, half: float) -> Array:
 var best := [];var best_score := INF
 for index in grid.get(Vector2i((p / BIN_M).floor()), []):
  var s: Array = segs[index]
  var d: Vector2 = s[1] - s[0];var along := d.normalized().dot(t)
  if absf(along) < .85:continue
  var dist := Geometry2D.get_closest_point_to_segment(p, s[0], s[1]).distance_to(p)
  if dist > half + 1.5:continue
  var score := dist + (0. if s[5] == cls else 4.)
  if score < best_score:best_score = score;best = [s, 1 if along > 0. else -1]
 return best

func ribbon_path(line: Dictionary) -> void:
 var cls := str(line.get("cls", ""))
 if cls not in MAJOR:return
 var pts := PackedVector2Array()
 for section in line.sections:pts.append(Vector2(section[2].x, section[2].z))
 path(pts, cls, float(line.half), [])

## Major-road segments no ribbon station used (no ribbon, or --grid-roads).
func polyline_paths() -> void:
 for i in roads.size():
  if not markable(i):continue
  var route: Array = roads[i][3]
  var pts := PackedVector2Array();var forced := []
  for j in range(1, route.size() + 1):
   var a: Vector2 = tile.point(route[j-1])
   var b: Vector2 = tile.point(route[j]) if j < route.size() else a
   var length := a.distance_to(b)
   if j < route.size() and length < .1:continue
   if j == route.size() or covered.has(Vector2i(i, j - 1)):
    if pts.size() >= 2 and pts[0].distance_to(pts[pts.size() - 1]) >= 4.:path(pts, str(roads[i][0]), float(roads[i][4]) * .5, forced)
    pts = PackedVector2Array();forced = []
    continue
   var steps := maxi(1, ceili(length / STEP_M))
   for k in range(0 if pts.is_empty() else 1, steps + 1):
    pts.append(a.lerp(b, float(k) / steps))
    forced.append([i, j - 1])

## Lays the markings along one path of stations (2 m apart).
func path(pts: PackedVector2Array, cls: String, half: float, forced: Array) -> void:
 var n := pts.size()
 if n < 2:return
 var st := []
 var s := 0.
 var z := PackedFloat64Array();z.resize(n)
 var arc := PackedFloat64Array();arc.resize(n)
 for k in n:
  if k > 0:s += pts[k].distance_to(pts[k - 1])
  arc[k] = s
  var t := (pts[mini(k + 1, n - 1)] - pts[maxi(k - 1, 0)]).normalized()
  var i := -1;var j := -1;var sign := 1
  if forced.is_empty():
   var hit := match(pts[k], t, cls, half)
   if not hit.is_empty():i = hit[0][2];j = hit[0][3];sign = hit[1]
  else:
   i = forced[k][0];j = forced[k][1]
   var seg_dir: Vector2 = tile.point(roads[i][3][j + 1]) - tile.point(roads[i][3][j])
   sign = 1 if seg_dir.dot(t) >= 0. else -1
  if i < 0 or not markable(i):
   st.append(null)
   continue
  if forced.is_empty():covered[Vector2i(i, j)] = true
  var road: Array = roads[i]
  var oneway := int(road[1]) != 0
  # Travel direction of a one-way road; the mapped forward of a two-way road.
  var road_sign := sign * (-1 if int(road[1]) < 0 else 1)
  var counts: Array = mapped.get(i, {}).get(j, [])
  # Two-way counts are kept along the path: forward is the path direction.
  if not oneway and counts.size() == 4 and road_sign < 0:counts = [counts[0], counts[2], counts[1], counts[3]]
  st.append({"p":pts[k], "t":t, "road":i, "sign":road_sign if oneway else 1, "oneway":oneway, "counts":counts,
   "cls":str(road[0]), "width":float(road[4]), "name":str(road[2])})
 stats.stations += n
 # An unmapped station takes the counts of the nearest mapped station of the
 # same road within INHERIT_M (a map segment can miss its way at a junction).
 var last := -1
 var before := PackedInt32Array();before.resize(n)
 for k in n:
  if st[k] != null and not st[k].counts.is_empty():last = k
  before[k] = last
 last = -1
 for k in range(n - 1, -1, -1):
  if st[k] != null and not st[k].counts.is_empty():last = k
  if st[k] == null or not st[k].counts.is_empty():continue
  var best := -1
  for cand in [before[k], last]:
   if cand < 0 or absf(arc[cand] - arc[k]) > INHERIT_M:continue
   var other: Dictionary = st[cand]
   if other.name != st[k].name or other.cls != st[k].cls or other.oneway != st[k].oneway:continue
   if best < 0 or absf(arc[cand] - arc[k]) < absf(arc[best] - arc[k]):best = cand
  if best >= 0:st[k].counts = st[best].counts;st[k]["inherited"] = true
 for k in n:
  if st[k] == null:continue
  var mph: int = int(st[k].counts[3]) if st[k].counts.size() == 4 else 0
  st[k]["mph"] = mph
  st[k]["town"] = in_town(pts[k], mph)
 # Passing sight along the path, both ways (rural two-lane two-way stretches).
 var ahead := PackedByteArray();ahead.resize(n)
 var behind := PackedByteArray();behind.resize(n)
 var any_rural := false
 for k in n:
  if st[k] != null and not st[k].oneway and not st[k].town:any_rural = true;break
 var ts := Time.get_ticks_usec()
 if any_rural:
  for k in n:z[k] = tile.height_at(pts[k])
  for k in n:
   if st[k] == null or st[k].oneway or st[k].town:continue
   # Every SIGHT_EVERY-th station is measured; the ones between share it.
   if k % SIGHT_EVERY != 0 and k > 0 and st[k - 1] != null and not st[k - 1].oneway and not st[k - 1].town:
    ahead[k] = ahead[k - 1];behind[k] = behind[k - 1]
    continue
   var mph: float = float(st[k].mph) if st[k].mph > 0 else float(RURAL_MPH.get(st[k].cls, 50))
   var need := passing_sight_m(mph)
   var clear: float = minf(float(st[k].width), 2. * LANE_M.get(st[k].cls, 3.5)) * .5 + SIGHT_CLEAR_M
   ahead[k] = 1 if sight(pts, z, arc, k, 1, need, clear) else 0
   behind[k] = 1 if sight(pts, z, arc, k, -1, need, clear) else 0
  short_zones(ahead, arc);short_zones(behind, arc)
 sight_us += Time.get_ticks_usec() - ts
 # Cross-section of every station, in the path frame (offset right of the path tangent).
 var sections := []
 for k in n:
  if st[k] == null:sections.append(null);continue
  var r: Dictionary = st[k]
  var pass_path := [ahead[k] == 1, behind[k] == 1]
  var passing: Array = pass_path if r.sign > 0 else [pass_path[1], pass_path[0]]
  var counts: Array = r.counts
  if counts.size() == 4:stats.mapped_stations += 1
  # Neighbouring stations nearly always share a cross-section: build each once.
  var key := [r.cls, r.oneway, counts, r.width, r.town, passing, r.sign, r.name]
  var cached = sections_by_key.get(key)
  if cached == null:
   var x := cross_section(r.cls, r.oneway, [counts[0], counts[1], counts[2]] if counts.size() == 4 else [0, 0, 0], r.width, r.town, passing)
   var lines := []
   for line in x.lines:lines.append([line[0] * r.sign, line[1], line[2], line[3]])
   var label := "%s %s: %d lanes %d+%s%d %s%s" % [r.cls, r.name, x.lanes, x.forward, "TWLTL+" if x.centre > 0 else "", x.backward, x.centre_line if x.centre_line != "" else ("one-way" if r.oneway else "no centre line"), ", edge lines" if x.edges else ""]
   cached = {"lines":lines, "label":label}
   sections_by_key[key] = cached
  sections.append(cached.lines)
  stats.layouts[cached.label] = stats.layouts.get(cached.label, 0) + 1
 # A world-anchored dash phase for the path.
 var t0 := (pts[1] - pts[0]).normalized()
 var phase := fposmod((origin + pts[0]).dot(t0), PERIOD_M)
 # Each line is followed from station to station as a track (a polyline on
 # the ribbon) and drawn when it ends, so straight runs become long quads.
 var td := Time.get_ticks_usec()
 var open := {}   # line index at the current station -> track
 for k in n - 1:
  var a = st[k];var b = st[k + 1]
  var mid: Vector2 = (pts[k] + pts[k + 1]) * .5
  var usable: bool = a != null and b != null and not (a.road != b.road and a.cls != b.cls)
  usable = usable and mid.x >= 0. and mid.y >= 0. and mid.x < tile.side and mid.y < tile.side and pts[k].distance_to(pts[k + 1]) <= MAX_JOIN_M
  usable = usable and not (tile.road_survey and tile.road_survey.observed(mid, a.t))
  if not usable:
   for track in open.values():close(track, phase)
   open = {}
   continue
  var ra := right_of(a.t);var rb := right_of(b.t)
  var crossing := near_crossing(mid, a.road, a.t, maxf(a.width, b.width) * .5 + 1.)
  var next := {}
  var same: bool = is_same(sections[k], sections[k + 1])
  for li in sections[k].size():
   var line: Array = sections[k][li]
   var oi: int = li if same else -1;var gap := .35
   if not same:
    for ci in sections[k + 1].size():
     var cand: Array = sections[k + 1][ci]
     if cand[1] == line[1] and absf(cand[0] - line[0]) < gap:gap = absf(cand[0] - line[0]);oi = ci
   var track = open.get(li)
   var pa: Vector2 = pts[k] + ra * line[0]
   var pb: Vector2 = pts[k + 1] + rb * sections[k + 1][oi][0] if oi >= 0 else pa
   var stop: bool = oi < 0 or next.has(oi)
   if not stop and crossing:stop = blocked((pa + pb) * .5, a.road, a.t, line[1] == "yellow", mid, ra, float(a.width) * .5)
   if track != null and (stop or track.line[2] != line[2]):
    close(track, phase);track = null
   if stop:continue
   if track == null:
    track = {"pts":PackedVector2Array([pa]), "r":PackedVector2Array([ra]), "arc":PackedFloat64Array([arc[k]]), "line":line, "station":a}
   track.pts.append(pb);track.r.append(rb);track.arc.append(arc[k + 1])
   track["next"] = true
   next[oi] = track
  for track in open.values():
   if not track.get("next", false):close(track, phase)
  for track in next.values():track.erase("next")
  open = next
 for track in open.values():close(track, phase)
 draw_us += Time.get_ticks_usec() - td

## Draws a finished track: solid, or 10 ft dashes on the world-anchored cycle.
func close(track: Dictionary, phase: float) -> void:
 if track.get("drawn", false):return
 track["drawn"] = true
 var line: Array = track.line
 var key := "yellowpaint" if line[1] == "yellow" else "whitepaint"
 var P: PackedVector2Array = track.pts;var R: PackedVector2Array = track.r;var A: PackedFloat64Array = track.arc
 if P.size() < 2:return
 if line[2] == "solid":
  polyline(key, P, R, A, line, track.station)
  return
 var start := floorf((A[0] + phase) / PERIOD_M) * PERIOD_M - phase
 while start < A[A.size() - 1]:
  var d0 := maxf(start, A[0]);var d1 := minf(start + DASH_M, A[A.size() - 1])
  if d1 - d0 > .05:
   var p := PackedVector2Array();var r := PackedVector2Array();var a := PackedFloat64Array()
   for i in A.size() - 1:
    if A[i + 1] <= d0 or A[i] >= d1:continue
    var span := maxf(A[i + 1] - A[i], 1e-6)
    var f0 := clampf((d0 - A[i]) / span, 0., 1.);var f1 := clampf((d1 - A[i]) / span, 0., 1.)
    if p.is_empty():p.append(P[i].lerp(P[i + 1], f0));r.append(R[i].lerp(R[i + 1], f0).normalized());a.append(lerpf(A[i], A[i + 1], f0))
    p.append(P[i].lerp(P[i + 1], f1));r.append(R[i].lerp(R[i + 1], f1).normalized());a.append(lerpf(A[i], A[i + 1], f1))
   if p.size() >= 2:polyline(key, p, r, a, line, track.station)
  start += PERIOD_M

## Emits a polyline as few quads as keep every point within 2 cm of a chord (10 m at most).
func polyline(key: String, P: PackedVector2Array, R: PackedVector2Array, A: PackedFloat64Array, line: Array, station: Dictionary) -> void:
 var s := 0
 var m := P.size()
 heights.clear()
 while s < m - 1:
  var e := s + 1
  while e + 1 < m and A[e + 1] - A[s] <= 10.:
   var straight := true
   for i in range(s + 1, e + 1):
    if Geometry2D.get_closest_point_to_segment(P[i], P[s], P[e + 1]).distance_to(P[i]) > .02:straight = false;break
   if not straight:break
   e += 1
  emit(key, P[s], P[e], R[s], R[e], line[3], station, line)
  s = e

## Clear sight from station k over `need` metres one way (eye and object 3.5 ft).
func sight(pts: PackedVector2Array, z: PackedFloat64Array, arc: PackedFloat64Array, k: int, step: int, need: float, clear: float) -> bool:
 var j := k
 while j + step >= 0 and j + step < pts.size() and absf(arc[j + step] - arc[k]) <= need:j += step
 if j == k:return true
 var length := absf(arc[j] - arc[k])
 var m := k + step
 while m != j:
  if Geometry2D.get_closest_point_to_segment(pts[m], pts[k], pts[j]).distance_to(pts[m]) > clear:return false
  var f := absf(arc[m] - arc[k]) / length
  if z[m] > lerpf(z[k], z[j], f) + EYE_M:return false
  m += step
 return true

## Passing zones shorter than 400 ft become no-passing. A zone cut by the end
## of the path (a tile seam or a junction) needs half that within the path.
static func short_zones(flags: PackedByteArray, arc: PackedFloat64Array) -> void:
 var n := flags.size();var k := 0
 while k < n:
  if flags[k] == 0:k += 1;continue
  var e := k
  while e + 1 < n and flags[e + 1] == 1:e += 1
  var need := MIN_PASSING_M if k > 0 and e < n - 1 else MIN_PASSING_M * .5
  if arc[e] - arc[k] < need:
   for m in range(k, e + 1):flags[m] = 0
  k = e + 1

## Whether another road (not a driveway) crosses near p.
func near_crossing(p: Vector2, road: int, t: Vector2, reach: float) -> bool:
 for index in grid.get(Vector2i((p / BIN_M).floor()), []):
  var s: Array = segs[index]
  if s[2] == road or s[6] == str(roads[road][2]) and s[6] != "":continue
  if absf((s[1] - s[0]).normalized().dot(t)) > .866:continue
  if Geometry2D.get_closest_point_to_segment(p, s[0], s[1]).distance_to(p) < s[4] + reach:return true
 return false

## A line point inside a crossing road. The centre line only stops where the
## other road runs through on both sides (not at a T junction).
func blocked(p: Vector2, road: int, t: Vector2, centre: bool, c: Vector2, right: Vector2, half: float) -> bool:
 if not inside_other(p, road, t):return false
 if not centre:return true
 return inside_other(c + right * (half + 1.), road, t) and inside_other(c - right * (half + 1.), road, t)

func inside_other(p: Vector2, road: int, t: Vector2) -> bool:
 for index in grid.get(Vector2i((p / BIN_M).floor()), []):
  var s: Array = segs[index]
  if s[2] == road or s[6] == str(roads[road][2]) and s[6] != "":continue
  if absf((s[1] - s[0]).normalized().dot(t)) > .866:continue
  if Geometry2D.get_closest_point_to_segment(p, s[0], s[1]).distance_to(p) < s[4] + .3:return true
 return false

## A stripe corner on the road surface; consecutive quads share their corners.
func corner(p: Vector2) -> Vector3:
 var h = heights.get(p)
 if h == null:
  h = tile.height_at(p) + LIFT_M
  heights[p] = h
 return tile.vert(p, h)

## One raised stripe piece, after the measured-paint masks of its road.
func emit(key: String, p: Vector2, q: Vector2, rp: Vector2, rq: Vector2, width: float, station: Dictionary, line: Array) -> void:
 var parts := [[p, q]]
 var name: String = station.name
 if tile.paint_evidence != null and name == tile.paint_evidence.road_name:
  parts = []
  for part in tile.paint_evidence.remaining(p, q):
   if part.size() >= 2:parts.append([part[0], part[part.size() - 1]])
 elif tile.measured_section and name == tile.measured_section.road_name:
  parts = []
  for part in tile.measured_section.remaining(p, q):
   if part.size() >= 2:parts.append([part[0], part[part.size() - 1]])
 elif tile.road_layout and station.oneway and line[1] == "white" and line[2] == "dash" and tile.road_layout.applies(name):
  parts = tile.road_layout.remaining(p, q, name)
 var length := p.distance_to(q)
 if length < .01:return
 for part in parts:
  var x: Vector2 = part[0];var y: Vector2 = part[1]
  var l := x.distance_to(y)
  if l < .05:continue
  var fx := clampf((x - p).dot(q - p) / (length * length), 0., 1.);var fy := clampf((y - p).dot(q - p) / (length * length), 0., 1.)
  var wx: Vector2 = rp.lerp(rq, fx).normalized() * width * .5;var wy: Vector2 = rp.lerp(rq, fy).normalized() * width * .5
  tile.quad(key, corner(x - wx), corner(x + wx), corner(y + wy), corner(y - wy), Vector3.UP)
  stats.quads += 1
  stats[line[1] + "_" + line[2] + "_m"] += l
