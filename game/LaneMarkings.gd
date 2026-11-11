extends RefCounted
## Pavement markings for the paved major roads of a streamed tile, after the
## MUTCD (2009 edition, Part 3: chapters 3A and 3B).
##
## Lane counts come from OpenStreetMap (`lanes`, `lanes:forward`,
## `lanes:backward`, `oneway`, `maxspeed`), which helper/lane-counts.mjs
## matches to every segment of the tile's roads (tile JSON `road_lanes`; the
## helper answers from its cache at once and BuildingStream.gd fetches the
## rest in the background and repaints). A segment with no mapped count takes
## the counts of the same road within INHERIT_M, then the counts mapped on
## most of the same street in the tile, then the US default: two lanes, one
## each way, on a two-way road, and on a one-way road the lanes its paved
## width was built for (helper/road-widths.mjs: two, one on a ramp). From the
## count:
##   * white edge lines on freeways, expressways and arterials (motorway, trunk,
##     primary) everywhere and on collectors (secondary, tertiary) outside
##     towns (3B.06, 3B.07); on a one-way carriageway the left edge is yellow.
##     Town or rural follows a mapped speed limit, else the share of the
##     street's length in the tile that is built up (smoothed along the road,
##     so it does not switch from block to block);
##   * a yellow centre line on two-way roads (3B.01): a double solid line on
##     roads with two or more lanes each way and in towns; on rural two-lane
##     roads a broken line (10 ft dash, 30 ft gap) where the passing sight
##     distance of Table 3B-1 is available, measured on the road surface with
##     a 3.5 ft eye and object (3B.02), solid on the side that may not pass,
##     and no passing zone shorter than 400 ft. On a rural three-lane road
##     with two lanes one way (a climbing or passing lane) the two-lane side
##     is solid and the single lane may pass where the sight distance allows;
##   * a centre two-way left-turn lane (3B.03, solid yellow outside, broken
##     yellow inside) only where OSM counts a lane in neither direction
##     (lanes = forward + backward + 1, OSM's lanes:both_ways) or gives an odd
##     total with no split. A 2 + 1 or 3 + 2 split stays as mapped;
##   * white broken lane lines between lanes in the same direction (3B.04),
##     10 ft lines on a 40 ft cycle anchored to the world, so dashes do not
##     jump at tile seams on straight roads;
##   * no markings on local streets (the `minor` class) and no centre line on a
##     road narrower than 16 ft or mapped with a single lane (3B.01).
## Lanes are LANE_M wide, narrower if the mapped count does not fit the road's
## width but never under MIN_LANE_M: a count the road cannot hold loses its
## centre lane, then lanes from the busier direction. The width is the paved
## width of the ribbon at each station (RoadSurface.gd `halves`), which
## helper/road-widths.mjs sets from the class, one-way and ramp flags alone so
## that both tiles of a seam agree, never from the mapped count. Lanes may use
## the paved shoulder of the road ribbon when the count needs it.
##
## The lines follow the engineered road ribbon: their path is the ribbon's
## 2 m station centreline and every corner sits LIFT_M above the surface the
## tyres drive on (the ribbon's own cross-sections, or WorldTile.height_at()
## where ribbons overlap or there is none), so they lie on the same smooth,
## superelevated surface; a quad ends wherever the surface leaves its
## straight chord by FIT_M, so crests do not swallow the middle of a stripe,
## and pieces across a step in the surface are left out (polyline(), emit()).
## Each tile paints only inside its own square. Roads without a ribbon
## (--grid-roads) use their map polyline.
## Lines stop where another road crosses them; the centre line carries on
## across a T junction. Measured paint keeps precedence wherever it exists:
## PaintEvidence and MeasuredSection mask the inferred lines inside their
## zones, RoadLayout removes inferred lane lines on a certified single lane,
## and a RoadSurvey road draws only its own surveyed strokes.

const DASH_M := 3.05          # 10 ft broken line
const PERIOD_M := 12.2        # 10 ft line + 30 ft gap
const LIFT_M := .048          # as WorldTile.raised_stripe()
const FIT_M := .015           # largest gap between a stripe quad and the surface under its middle
const STEP_M_OFF := .05       # a stripe end this far from the surface its fit followed is off that surface
const STEP_SLOPE := .3       # across a line, a steeper surface (16.7 deg) is a step, not a road
const MIN_PIECE_M := .25    # shortest piece a fold is split into; a step is cut out below this
const LINE_M := .12           # normal line (4-6 in)
const WIDE_M := .15           # 6 in edge lines on motorway, trunk and primary
const DOUBLE_GAP_M := .1      # between the two lines of a double line
const STEP_M := 2.            # station spacing of polyline (no-ribbon) paths
const SHOULDER_M := 1.2       # RoadSurface.SHOULDER_M
const MAX_JOIN_M := 8.        # a longer step between stations is a gap
const SKIP_SPAN_M := 10.      # kept_stations(): longest straight run drawn without its inner stations
const SKIP_HEIGHT_M := .005   # ... and how far its cross-sections may leave the straight line in height
const SKIP_EDGE_M := 12.      # ... and how near the tile edge stations are always kept
const OWN_REACH_M := 1.6      # a stripe point this near its station shares its ownership (own_station())
const SURFACE := preload("res://RoadSurface.gd")
const PROFILE := 15
const MAJOR := ["motorway","trunk","primary","secondary","tertiary"]
const ARTERIAL := ["motorway","trunk","primary"]
const LANE_M := {"motorway":3.65,"trunk":3.65,"primary":3.5,"secondary":3.35,"tertiary":3.2}
const MIN_LANE_M := 2.75
const WIDTH_LANE_M := 3.6     # helper/road-widths.mjs LANE_M
const ONE_WAY_LANES := 2      # helper/road-widths.mjs ONE_WAY_LANES
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
# Town or rural is decided per street, not per 50 m cell: a street mostly
# built up (or mostly open) in the tile is that all along; a mixed one keeps
# the majority of its stations within TOWN_RUN_M each way.
const TOWN_SHARE := .65
const TOWN_RUN_M := 150.
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
var marked_cells := {} # Vector2i -> true where a road that gets markings (markable()) has a segment
var marked_near := {}  # marked_cells grown by one cell
var covered := {}      # Vector2i(road, segment) -> true once a ribbon station used it
var deck := {}         # road index -> true for roads drawn on a bridge deck (BridgeDeck.gd)
var homes := {}        # Vector2i -> building count
var streets := {}      # Vector2i -> metres of local street and driveway
var heights := {}      # Vector2 -> stripe corner height, within one polyline
var ribbon := []       # cross-sections of the ribbon the current path follows (RoadSurface.lines)
var ribbon_index := -1 # its index in RoadSurface.lines
var profile := PackedFloat64Array()   # per station of that ribbon, PROFILE numbers (see ribbon_fit())
var own_k := PackedByteArray()   # per station of the current path: 1 where its stripes lie on its ribbon alone
var cross_k := PackedByteArray()   # kept_stations(): per station, 0 not tested, 1 no crossing road, 2 a crossing road
var fans := {}         # paint batch key -> the tile's SurfaceTool for it (stripe())
var up4 := PackedVector3Array([Vector3.UP, Vector3.UP, Vector3.UP, Vector3.UP])
var cell_ribbons := {}   # Vector2i (RoadSurface.BIN cell) -> {ribbon index: bounding box of its quads there} (cell_boxes())
var towns := {}        # Vector2i -> built up (dense())
var street_town := {}  # street -> share of its length in this tile that is built up
var street_counts := {}   # [street, class, one-way] -> the counts mapped on most of its length here
var sections_by_key := {}   # cross-section cache, see path()
var sight_us := 0
var draw_us := 0
var town_us := 0
var origin := Vector2.ZERO
var stats := {"white_solid_m":0.,"white_dash_m":0.,"yellow_solid_m":0.,"yellow_dash_m":0.,"quads":0,"stations":0,"mapped_stations":0,"street_stations":0,"fitted_stations":0,"town_flips":0,"height_calls":0,"step_pieces":0,"layouts":{}}

## Draws every inferred marking of the tile into its whitepaint / yellowpaint
## batches and returns statistics (also kept in tile.info.lane_markings).
static func paint(tile) -> Dictionary:
 var m = new()
 m.tile = tile
 var t0 := Time.get_ticks_usec()
 m.prepare()
 if tile.road_surface:
  for li in tile.road_surface.lines.size():m.ribbon_path(tile.road_surface.lines[li], li)
 m.polyline_paths()
 m.stats["ms"] = (Time.get_ticks_usec() - t0) / 1000.
 m.stats["sight_ms"] = m.sight_us / 1000.
 m.stats["draw_ms"] = m.draw_us / 1000.
 m.stats["town_ms"] = m.town_us / 1000.
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
  # An unmapped one-way gets the lanes its paved width was built for
  # (helper/road-widths.mjs: a one-way carriageway is two lanes plus its
  # shoulders, a ramp one), so the paint never disagrees with the surface.
  if total <= 0:total = f if f > 0 else oneway_default_lanes(width)
  f = total;b = 0
 else:
  if f > 0 and b > 0:
   # OSM: lanes = forward + backward + both_ways; the both-ways lane is the
   # centre turn lane. A 2 + 1 or 3 + 2 split with nothing left over is a
   # climbing, passing or turn lane in one direction (below).
   c = clampi(total - f - b, 0, 1)
  elif f > 0 and total > f:
   b = total - f
  elif b > 0 and total > b:
   f = total - b
  else:
   if total <= 0:total = 2 if width >= 5.5 else 1
   f = (total + 1) / 2;b = total / 2
   if total >= 3 and total % 2 == 1:f = (total - 1) / 2;b = f;c = 1
  # In town the odd lane of a 2 + 1 or 3 + 2 split is the left-turn pocket
  # of a junction approach (OSM counts it in that direction), cut from a
  # centre turn lane; outside towns it is a climbing or passing lane.
  if town and c == 0 and f > 0 and b > 0 and absi(f - b) == 1 and f + b >= 3:
   c = 1;f = mini(f, b);b = f
  if f <= 0 and b <= 0:f = 1
 var mapped_lanes: int = f + b + c
 # Every lane at least MIN_LANE_M: a count the road cannot hold loses the
 # centre lane first, then lanes from the direction with more of them.
 var fits: int = maxi(1, int(floorf((width + 2. * room - .3) / MIN_LANE_M)))
 while f + b + c > fits:
  if c > 0:c = 0
  elif f >= b and f > 1:f -= 1
  elif b > 1:b -= 1
  elif b > 0:b -= 1
  else:break
 var n: int = f + b + c
 var lane: float = clampf(width / n, MIN_LANE_M, LANE_M.get(cls, 3.3))
 var travelled: float = minf(n * lane, width + 2. * room - .3)
 lane = travelled / n
 var right := travelled * .5
 var lines := []
 var edges: bool = cls in ARTERIAL or not town
 var ew: float = WIDE_M if cls in ARTERIAL else LINE_M
 if edges:
  # The edge line is on the asphalt: the road ribbon's paved half is the
  # carriageway terrain.gdshader draws, its shoulder band is gravel, a gutter
  # or the verge. Lanes that borrow the shoulder only narrow the edge gap.
  var edge_at := minf(right, width * .5)
  lines.append([edge_at - ew * .5, "white", "solid", ew])
  lines.append([-edge_at + ew * .5, "yellow" if oneway else "white", "solid", ew])
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
  elif f + b >= 4 or town:
   kind = "double solid"
   lines.append([centre + d, "yellow", "solid", LINE_M])
   lines.append([centre - d, "yellow", "solid", LINE_M])
  elif f > 1 or b > 1:
   # Three lanes, two one way (a climbing or passing lane, 3B.02 and Figure
   # 3B-4): the two-lane direction may not pass; the single lane may where
   # the sight distance allows.
   var single_passes: bool = passing[0] if f == 1 else passing[1]
   kind = "solid and broken" if single_passes else "double solid"
   lines.append([centre + d, "yellow", "dash" if f == 1 and single_passes else "solid", LINE_M])
   lines.append([centre - d, "yellow", "dash" if b == 1 and single_passes else "solid", LINE_M])
  elif passing[0] and passing[1]:
   kind = "broken"
   lines.append([centre, "yellow", "dash", LINE_M])
  else:
   kind = "double solid" if not (passing[0] or passing[1]) else "solid and broken"
   lines.append([centre + d, "yellow", "dash" if passing[0] else "solid", LINE_M])
   lines.append([centre - d, "yellow", "dash" if passing[1] else "solid", LINE_M])
 return {"lines":lines, "lanes":n, "forward":f, "backward":b, "centre":c, "lane":lane, "travelled":travelled, "edges":edges, "centre_line":kind, "mapped_lanes":mapped_lanes}

## Lanes of an unmapped one-way road of this paved width: as many WIDTH_LANE_M
## lanes as helper/road-widths.mjs built it for, 1 or ONE_WAY_LANES. Every
## two-lane one-way width there (two 3.6 m lanes plus shoulders, 9.0-11.4 m)
## holds two; a ramp (one lane plus shoulders, 6 m) holds one.
static func oneway_default_lanes(width: float) -> int:
 return ONE_WAY_LANES if width >= ONE_WAY_LANES * WIDTH_LANE_M - .05 else 1

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
     if markable(i):marked_cells[key] = true
 for key in marked_cells:
  for dx in range(-1, 2):
   for dy in range(-1, 2):marked_near[key + Vector2i(dx, dy)] = true
 for b in tile.info.get("buildings", []):
  var p = b
  for depth in 3:
   if p is Array and not p.is_empty() and p[0] is Array:p = p[0]
  if p is Array and p.size() >= 2 and typeof(p[0]) in [TYPE_FLOAT, TYPE_INT]:
   var key := Vector2i((Vector2(float(p[0]), float(p[1])) * tile.side / HOME_CELL_M).floor())
   homes[key] = homes.get(key, 0) + 1
 # The counts mapped on most of each named street's length in the tile. A
 # two-way split is only carried when it is symmetric: another row of the
 # street can run the other way, and a 2 + 1 split would turn round with it.
 var votes := {}
 for s in segs:
  var i: int = s[2]
  var counts: Array = mapped.get(i, {}).get(s[3], [])
  if counts.size() != 4 or str(roads[i][2]) == "":continue
  var oneway := int(roads[i][1]) != 0
  var carried: Array = counts if oneway or counts[1] == counts[2] else [0, 0, 0, counts[3]]
  var key := [str(roads[i][2]), s[5], oneway]
  if not votes.has(key):votes[key] = {}
  votes[key][carried] = votes[key].get(carried, 0.) + s[0].distance_to(s[1])
 for key in votes:
  var best := [];var most := 0.
  for counts in votes[key]:
   if votes[key][counts] > most:most = votes[key][counts];best = counts
  street_counts[key] = best

func markable(i: int) -> bool:
 var road: Array = roads[i]
 if deck.has(i) or str(road[0]) not in MAJOR:return false
 return not (road.size() > 5 and road[5] == "unpaved")

## Whether the 250 m square around p is built up (no mapped limit). Near the
## tile edge only part of the square is known; its counts are scaled up to
## the whole square, so a town street does not turn rural at a tile seam.
func dense(p: Vector2) -> bool:
 var c := Vector2i((p / HOME_CELL_M).floor())
 if towns.has(c):return towns[c]
 var count := 0.;var length := 0.;var inside := 0
 var cells := ceili(tile.side / HOME_CELL_M)
 for dx in range(-2, 3):
  for dy in range(-2, 3):
   var k := c + Vector2i(dx, dy)
   if k.x < 0 or k.y < 0 or k.x >= cells or k.y >= cells:continue
   inside += 1
   count += homes.get(k, 0)
   length += streets.get(k, 0.)
 var scale := 25. / maxi(inside, 1)
 towns[c] = count * scale >= TOWN_BUILDINGS or length * scale >= TOWN_STREETS_M
 return towns[c]

## Street of a road row: its name, or the row itself when it has none.
func street_of(i: int) -> String:
 var name := str(roads[i][2])
 return name if name != "" else "#%d" % i

## Share of a street's length in this tile that is built up (dense()).
func town_share(street: String) -> float:
 if street_town.has(street):return street_town[street]
 var town := 0.;var total := 0.
 for s in segs:
  if street_of(s[2]) != street:continue
  var a: Vector2 = s[0];var b: Vector2 = s[1]
  var pieces := maxi(1, ceili(a.distance_to(b) / 10.))
  for k in pieces:
   var w: float = a.distance_to(b) / pieces
   total += w
   if dense(a.lerp(b, (k + .5) / pieces)):town += w
 street_town[street] = town / total if total > 0. else 0.
 return street_town[street]

## The road segment a station of a ribbon belongs to: [segment, +1/-1 along it].
## A ribbon chain can join roads of several classes (road-grade.mjs links the
## straight-on continuation whatever its class, and names the chain after its
## first piece), so the station's own paved half width, which is its road's,
## picks between roads that meet there; the chain's class only breaks ties.
func match(p: Vector2, t: Vector2, cls: String, half: float) -> Array:
 var best := [];var best_score := INF
 for index in grid.get(Vector2i((p / BIN_M).floor()), []):
  var s: Array = segs[index]
  var d: Vector2 = s[1] - s[0];var along := d.normalized().dot(t)
  if absf(along) < .85:continue
  var dist := Geometry2D.get_closest_point_to_segment(p, s[0], s[1]).distance_to(p)
  if dist > half + 1.5:continue
  var score := dist + (0. if absf(s[4] - half) < .05 else 4.) + (0. if s[5] == cls else .5)
  if score < best_score:best_score = score;best = [s, 1 if along > 0. else -1]
 return best

func ribbon_path(line: Dictionary, index: int) -> void:
 # Every ribbon, whatever its class: a chain named after a local street can
 # carry on along a major road (match()), and its stations must follow the
 # ribbon, not the map polyline beside it.
 var cls := str(line.get("cls", ""))
 var pts := PackedVector2Array()
 var near := false
 for k in line.sections.size():
  var section: Array = line.sections[k]
  pts.append(Vector2(section[2].x, section[2].z))
  # A ribbon that never comes near a marked road is skipped. Every third
  # station is tested against the marked cells grown by one cell, so any
  # station in a marked cell has a tested one within a cell of it.
  if not near and (k % 3 == 0 or k == line.sections.size() - 1 or (k > 0 and pts[k].distance_to(pts[k - 1]) > MAX_JOIN_M)):
   near = marked_near.has(Vector2i((pts[k] / BIN_M).floor()))
 if not near:return
 # Each station's own paved half width (RoadSurface.gd `halves`): a chain can
 # join roads of different widths, and the lines must fit the surface under them.
 var halves = line.get("halves")
 if not halves is PackedFloat32Array or halves.size() != pts.size():
  push_error("LaneMarkings: road surface line %d has no per-station halves" % index)
  return
 path(pts, cls, halves, [], line.sections, index)

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
    if pts.size() >= 2 and pts[0].distance_to(pts[pts.size() - 1]) >= 4.:
     var halves := PackedFloat32Array();halves.resize(pts.size());halves.fill(float(roads[i][4]) * .5)
     path(pts, str(roads[i][0]), halves, forced)
    pts = PackedVector2Array();forced = []
    continue
   var steps := maxi(1, ceili(length / STEP_M))
   for k in range(0 if pts.is_empty() else 1, steps + 1):
    pts.append(a.lerp(b, float(k) / steps))
    forced.append([i, j - 1])

## Lays the markings along one path of stations (2 m apart).
func path(pts: PackedVector2Array, cls: String, halves: PackedFloat32Array, forced: Array, ribbon_sections: Array = [], index: int = -1) -> void:
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
   # Most stations of a local street's ribbon are nowhere near a marked road.
   if not marked_cells.has(Vector2i((pts[k] / BIN_M).floor())):
    st.append(null)
    continue
   var hit := match(pts[k], t, cls, halves[k])
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
   "cls":str(road[0]), "width":2. * halves[k], "name":str(road[2])})
 var markable_stations := n - st.count(null)
 if markable_stations == 0:return
 stats.stations += markable_stations
 # The ribbon's own profile, for heights on it (ribbon_fit()).
 ribbon = ribbon_sections;ribbon_index = index
 profile.resize(ribbon.size() * PROFILE)
 for k in ribbon.size():
  if k <= 1 or k >= n - 2 or st[k] != null or st[k - 1] != null or st[k + 1] != null:profile_station(k)
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
  # Farther from any mapped part of this path: what the street carries on
  # most of its mapped length in the tile.
  elif st[k].name != "":
   var carried: Array = street_counts.get([st[k].name, st[k].cls, st[k].oneway], [])
   if not carried.is_empty():st[k].counts = carried;st[k]["street"] = true
 # Town or rural: a mapped limit decides; otherwise the street's share of
 # built-up length, and on a mixed street the majority within TOWN_RUN_M.
 var raw := PackedByteArray();raw.resize(n)
 for k in n:
  if st[k] == null:continue
  var mph: int = int(st[k].counts[3]) if st[k].counts.size() == 4 else 0
  st[k]["mph"] = mph
  if mph > 0:
   st[k]["town"] = mph <= TOWN_MAX_MPH
   continue
  var tt := Time.get_ticks_usec()
  var share := town_share(street_of(st[k].road))
  town_us += Time.get_ticks_usec() - tt
  if share >= TOWN_SHARE or share <= 1. - TOWN_SHARE:
   st[k]["town"] = share >= TOWN_SHARE
   continue
  raw[k] = 2 + (1 if dense(pts[k]) else 0)
 var lo := 0;var hi := 0;var votes := 0;var seen := 0
 for k in n:
  if raw[k] < 2:continue
  while hi < n and arc[hi] - arc[k] <= TOWN_RUN_M:
   if raw[hi] >= 2:seen += 1;votes += raw[hi] - 2
   hi += 1
  while arc[k] - arc[lo] > TOWN_RUN_M:
   if raw[lo] >= 2:seen -= 1;votes -= raw[lo] - 2
   lo += 1
  st[k]["town"] = votes * 2 >= seen
 var prev = null
 for k in n:
  if st[k] == null:continue
  if prev != null and prev.name == st[k].name and prev.town != st[k].town:stats.town_flips += 1
  prev = st[k]
 # Passing sight along the path, both ways (rural two-lane two-way stretches).
 var ahead := PackedByteArray();ahead.resize(n)
 var behind := PackedByteArray();behind.resize(n)
 var any_rural := false
 for k in n:
  if st[k] != null and not st[k].oneway and not st[k].town:any_rural = true;break
 var ts := Time.get_ticks_usec()
 if any_rural:
  for k in n:z[k] = surface(pts[k])
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
 var prev_key := [];var prev_cached = null
 own_k.resize(n);own_k.fill(0)
 var outer := PackedFloat32Array();outer.resize(n)   # outermost line offset per station
 for k in n:
  if st[k] == null:sections.append(null);continue
  var r: Dictionary = st[k]
  var pass_path := [ahead[k] == 1, behind[k] == 1]
  var passing: Array = pass_path if r.sign > 0 else [pass_path[1], pass_path[0]]
  var counts: Array = r.counts
  if counts.size() == 4:stats.mapped_stations += 1
  if r.get("street", false):stats.street_stations += 1
  # Neighbouring stations nearly always share a cross-section: build each once,
  # and reuse the previous station's without hashing when nothing changed.
  var cached = null
  var prev_r = st[k - 1] if k > 0 else null
  if prev_r != null and prev_key.size() == 6 and prev_key[0] == r.road and prev_key[1] == r.width and prev_key[2] == r.town and prev_key[3] == passing and prev_key[4] == r.sign and prev_key[5] == counts:
   cached = prev_cached
  var key := []
  if cached == null:
   key = [r.cls, r.oneway, counts, r.width, r.town, passing, r.sign, r.name]
   cached = sections_by_key.get(key)
   prev_key = [r.road, r.width, r.town, passing, r.sign, counts]
  if cached == null:
   var x := cross_section(r.cls, r.oneway, [counts[0], counts[1], counts[2]] if counts.size() == 4 else [0, 0, 0], r.width, r.town, passing)
   var lines := []
   for line in x.lines:lines.append([line[0] * r.sign, line[1], line[2], line[3]])
   var label := "%s %s: %d lanes %d+%s%d %s%s" % [r.cls, r.name, x.lanes, x.forward, "TWLTL+" if x.centre > 0 else "", x.backward, x.centre_line if x.centre_line != "" else ("one-way" if r.oneway else "no centre line"), ", edge lines" if x.edges else ""]
   if x.mapped_lanes != x.lanes:label += " (%d mapped, too narrow)" % x.mapped_lanes
   var widest := 0.
   for line in lines:widest = maxf(widest, absf(line[0]) + line[3] * .5)
   cached = {"lines":lines, "label":label, "fitted":x.mapped_lanes != x.lanes, "outer":widest}
   sections_by_key[key] = cached
  prev_cached = cached
  sections.append(cached.lines)
  stats.layouts[cached.label] = stats.layouts.get(cached.label, 0) + 1
  if cached.fitted:stats.fitted_stations += 1
  outer[k] = cached.outer
 # Whether every stripe point at station k lies on this ribbon alone: no other
 # ribbon within its outermost line plus OWN_REACH_M. Tested for three
 # neighbouring stations at once, around the middle one.
 var k0 := 0
 while k0 < n and not ribbon.is_empty():
  if sections[k0] == null:k0 += 1;continue
  var k1 := k0;var reach: float = outer[k0]
  while k1 + 1 < n and k1 - k0 < 2 and sections[k1 + 1] != null:k1 += 1;reach = maxf(reach, outer[k1])
  var c: Vector2 = pts[(k0 + k1) / 2]
  var spread := maxf(c.distance_to(pts[k0]), c.distance_to(pts[k1]))
  var clear := 1 if alone(c, reach + OWN_REACH_M + spread) else 0
  for k in range(k0, k1 + 1):own_k[k] = clear
  k0 = k1 + 1
 # A world-anchored dash phase for the path.
 var t0 := (pts[1] - pts[0]).normalized()
 var phase := fposmod((origin + pts[0]).dot(t0), PERIOD_M)
 # Each line is followed from station to station as a track (a polyline on
 # the ribbon) and drawn when it ends, so straight runs become long quads.
 var td := Time.get_ticks_usec()
 var kept := kept_stations(pts, st, sections, n)
 var open := {}   # line index at the current station -> track
 for m in kept.size() - 1:
  var k: int = kept[m];var k2: int = kept[m + 1]
  # Stations left out between k and k2 lie on the straight run between them.
  var skipped: bool = k2 > k + 1
  var a = st[k];var b = st[k2]
  var mid: Vector2 = (pts[k] + pts[k2]) * .5
  var usable: bool = a != null and b != null and not (a.road != b.road and a.cls != b.cls)
  usable = usable and mid.x >= 0. and mid.y >= 0. and mid.x < tile.side and mid.y < tile.side and (skipped or pts[k].distance_to(pts[k2]) <= MAX_JOIN_M)
  usable = usable and not (tile.road_survey and tile.road_survey.observed(mid, a.t))
  if not usable:
   for track in open.values():close(track, phase)
   open = {}
   continue
  var ra := right_of(a.t);var rb := right_of(b.t)
  var crossing := false if skipped else near_crossing(mid, a.road, a.t, maxf(a.width, b.width) * .5 + 1.)
  var next := {}
  var same: bool = is_same(sections[k], sections[k2])
  for li in sections[k].size():
   var line: Array = sections[k][li]
   var oi: int = li if same else -1;var gap := .35
   if not same:
    for ci in sections[k2].size():
     var cand: Array = sections[k2][ci]
     if cand[1] == line[1] and absf(cand[0] - line[0]) < gap:gap = absf(cand[0] - line[0]);oi = ci
   var track = open.get(li)
   var pa: Vector2 = pts[k] + ra * line[0]
   var pb: Vector2 = pts[k2] + rb * sections[k2][oi][0] if oi >= 0 else pa
   var stop: bool = oi < 0 or next.has(oi)
   if not stop and crossing:stop = blocked((pa + pb) * .5, a.road, a.t, line[1] == "yellow", mid, ra, float(a.width) * .5)
   if track != null and (stop or track.line[2] != line[2]):
    close(track, phase);track = null
   if stop:continue
   # On a ribbon path each point keeps its station: its height comes from
   # the ribbon's own cross-sections, unless another ribbon overlaps it there.
   if track == null:
    track = {"pts":PackedVector2Array([pa]), "r":PackedVector2Array([ra]), "arc":PackedFloat64Array([arc[k]]), "k":PackedFloat64Array([own_station(k)]), "c":PackedByteArray([0]), "line":line, "station":a}
   track.pts.append(pb);track.r.append(rb);track.arc.append(arc[k2]);track.k.append(own_station(k2));track.c.append(1 if skipped else 0)
   track["next"] = true
   next[oi] = track
  for track in open.values():
   if not track.get("next", false):close(track, phase)
  for track in next.values():track.erase("next")
  open = next
 for track in open.values():close(track, phase)
 draw_us += Time.get_ticks_usec() - td

## The stations the lines are drawn through. On a straight, uniform stretch of
## the path's own ribbon a station adds nothing: the stripe quads would run
## straight through it anyway (polyline() merges up to 10 m). A station is
## left out when it and every station between the kept ones around it (at most
## SKIP_SPAN_M apart) lie on the ribbon alone with the same cross-section, no
## crossing road and no measured paint, its centre lies within 1 cm of the
## straight line between them in plan, its direction within 0.06 deg, and every
## point of its ribbon cross-section within SKIP_HEIGHT_M of the straight line
## in height, so each line's height there is within SKIP_HEIGHT_M of its chord
## too (well inside FIT_M). Stations near the tile edge are always kept.
func kept_stations(pts: PackedVector2Array, st: Array, sections: Array, n: int) -> PackedInt32Array:
 var kept := PackedInt32Array()
 if n == 0:return kept
 kept.append(0)
 cross_k.resize(n);cross_k.fill(0)
 if ribbon.is_empty() or tile.road_survey or tile.paint_evidence != null or tile.measured_section or tile.road_layout:
  for k in range(1, n):kept.append(k)
  return kept
 var a := 0
 while a < n - 1:
  var b := a + 1
  # Extend the run a..b while every station strictly inside may be left out.
  while b + 1 < n and skippable(pts, st, sections, a, b + 1):b += 1
  if b > a + 1 and not skippable(pts, st, sections, a, b):b = a + 1
  kept.append(b)
  a = b
 return kept

## Whether every station strictly between a and b may be left out (kept_stations()).
func skippable(pts: PackedVector2Array, st: Array, sections: Array, a: int, b: int) -> bool:
 if pts[a].distance_to(pts[b]) > SKIP_SPAN_M:return false
 if st[a] == null or st[b] == null or own_k[a] == 0 or own_k[b] == 0:return false
 var sa: Array = ribbon[a];var sb: Array = ribbon[b]
 var ta: Vector2 = st[a].t
 for k in range(a + 1, b):
  var r = st[k]
  if r == null or own_k[k] == 0 or not is_same(sections[k], sections[a]) or r.road != st[a].road:return false
  if pts[k].distance_to(pts[k - 1]) > MAX_JOIN_M:return false
  var p := pts[k]
  if p.x < SKIP_EDGE_M or p.y < SKIP_EDGE_M or p.x > tile.side - SKIP_EDGE_M or p.y > tile.side - SKIP_EDGE_M:return false
  if Geometry2D.get_closest_point_to_segment(p, pts[a], pts[b]).distance_to(p) > .01:return false
  if r.t.dot(ta) < .9999995:return false
  var f := float(k - a) / (b - a)
  var sk: Array = ribbon[k]
  for j in 5:
   if absf(sk[j].y - lerpf(sa[j].y, sb[j].y, f)) > SKIP_HEIGHT_M:return false
  if cross_k[k] == 0:cross_k[k] = 2 if near_crossing(p, r.road, r.t, float(r.width) * .5 + 1.) else 1
  if cross_k[k] == 2:return false
 return true

## Draws a finished track: solid, or 10 ft dashes on the world-anchored cycle.
func close(track: Dictionary, phase: float) -> void:
 if track.get("drawn", false):return
 track["drawn"] = true
 var line: Array = track.line
 var key := "yellowpaint" if line[1] == "yellow" else "whitepaint"
 var P: PackedVector2Array = track.pts;var R: PackedVector2Array = track.r;var A: PackedFloat64Array = track.arc;var K: PackedFloat64Array = track.k;var C: PackedByteArray = track.c
 if P.size() < 2:return
 if line[2] == "solid":
  polyline(key, P, R, A, K, line, track.station, C)
  return
 var start := floorf((A[0] + phase) / PERIOD_M) * PERIOD_M - phase
 var first := 0   # dashes run forward along the track: no point before this one matters again
 while start < A[A.size() - 1]:
  var d0 := maxf(start, A[0]);var d1 := minf(start + DASH_M, A[A.size() - 1])
  if d1 - d0 > .05:
   var p := PackedVector2Array();var r := PackedVector2Array();var a := PackedFloat64Array();var k := PackedFloat64Array();var c := PackedByteArray()
   for i in range(first, A.size() - 1):
    if A[i + 1] <= d0:first = i + 1;continue
    if A[i] >= d1:break
    var span := maxf(A[i + 1] - A[i], 1e-6)
    var f0 := clampf((d0 - A[i]) / span, 0., 1.);var f1 := clampf((d1 - A[i]) / span, 0., 1.)
    if p.is_empty():p.append(P[i].lerp(P[i + 1], f0));r.append(R[i].lerp(R[i + 1], f0).normalized());a.append(lerpf(A[i], A[i + 1], f0));k.append(lerpf(K[i], K[i + 1], f0));c.append(0)
    p.append(P[i].lerp(P[i + 1], f1));r.append(R[i].lerp(R[i + 1], f1).normalized());a.append(lerpf(A[i], A[i + 1], f1));k.append(lerpf(K[i], K[i + 1], f1));c.append(C[i + 1])
   if p.size() >= 2:polyline(key, p, r, a, k, line, track.station, c)
  start += PERIOD_M

## Emits a polyline as few quads as keep every point within 2 cm of a chord
## (10 m at most), in plan and in height: over a crest or a sag the quad ends
## where the surface leaves the straight chord by more than FIT_M, so the
## middle of a stripe never sinks into the road or floats above it. Between
## two points the surface may also fold (a terrain grid crease, a ribbon
## quad's diagonal, where two ribbons overlap) or step (where a ribbon ends
## above the ground): such a step is halved until its middle is within FIT_M
## of the straight line, and a piece still off at MIN_PIECE_M is a step the
## paint cannot follow, so the line breaks there. Heights are the highest
## surface, what the tyres meet: WorldTile.height_at(), or where only the
## path's own ribbon is there (K, a fractional station index, else NAN) the
## ribbon's own profile (ribbon_fit(), for the fit and the corners),
## which runs straight from station to station, so no step is
## searched between two such points. C[i] = 1 marks a piece from point i - 1
## to i that spans stations left out by kept_stations(), all on the ribbon
## alone, however long it is.
func polyline(key: String, P: PackedVector2Array, R: PackedVector2Array, A: PackedFloat64Array, K: PackedFloat64Array, line: Array, station: Dictionary, C: PackedByteArray = PackedByteArray()) -> void:
 heights.clear()
 var P2 := PackedVector2Array([P[0]]);var R2 := PackedVector2Array([R[0]]);var A2 := PackedFloat64Array([A[0]]);var K2 := PackedFloat64Array([K[0]])
 var own := PackedByteArray();own.resize(P.size())
 var top := PackedFloat64Array();top.resize(P.size())
 for i in P.size():
  own[i] = 0 if is_nan(K[i]) else 1
  top[i] = ribbon_fit(P[i], K[i]) if own[i] == 1 else NAN
  if is_nan(top[i]):own[i] = 0;top[i] = surface(P[i])
 if own[0] == 0:K2[0] = NAN
 var z := PackedFloat64Array([top[0]])
 var cut := PackedByteArray()   # cut[i] = 1: no paint from point i to i + 1
 for i in range(1, P.size()):
  if own[i] == 1 and own[i - 1] == 1 and ((i < C.size() and C[i] == 1) or P[i].distance_to(P[i - 1]) <= 2. * OWN_REACH_M - .2):
   P2.append(P[i]);R2.append(R[i]);A2.append(A[i]);K2.append(K[i]);z.append(top[i]);cut.append(0)
   continue
  var ends := PackedFloat64Array([z[z.size() - 1], top[i]])
  var off := PackedByteArray([0])
  while true:
   var pieces := ends.size() - 1
   var refined := PackedFloat64Array([ends[0]])
   var folded := false
   for k in pieces:
    var mid: float = surface(P[i - 1].lerp(P[i], (k + .5) / pieces))
    off[k] = 1 if absf(mid - (ends[k] + ends[k + 1]) * .5) > FIT_M else 0
    folded = folded or off[k] == 1
    refined.append(mid);refined.append(ends[k + 1])
   if not folded or (A[i] - A[i - 1]) / pieces <= MIN_PIECE_M * 1.5:break
   ends = refined
   off.resize(ends.size() - 1)
  var pieces := ends.size() - 1
  for k in pieces:
   if k > 0:
    var f := float(k) / pieces
    P2.append(P[i - 1].lerp(P[i], f));R2.append(R[i - 1].lerp(R[i], f).normalized());A2.append(lerpf(A[i - 1], A[i], f));K2.append(NAN)
    z.append(ends[k])
   cut.append(off[k])
  P2.append(P[i]);R2.append(R[i]);A2.append(A[i]);K2.append(K[i] if own[i] == 1 else NAN);z.append(ends[pieces])
 P = P2;R = R2;A = A2;K = K2
 var s := 0
 var m := P.size()
 while s < m - 1:
  if cut[s] == 1:
   s += 1
   continue
  var e := s + 1
  while e + 1 < m and A[e + 1] - A[s] <= 10. and cut[e] == 0:
   var straight := true
   var run := maxf(A[e + 1] - A[s], 1e-6)
   for i in range(s + 1, e + 1):
    if Geometry2D.get_closest_point_to_segment(P[i], P[s], P[e + 1]).distance_to(P[i]) > .02:straight = false;break
    if absf(z[i] - lerpf(z[s], z[e + 1], (A[i] - A[s]) / run)) > FIT_M:straight = false;break
   if not straight:break
   e += 1
  emit(key, P[s], P[e], R[s], R[e], K[s], K[e], z[s], z[e], line[3], station, line)
  s = e

## Station k of the current path for a stripe point at that station when the
## point lies on the path's own ribbon alone, or NAN where another ribbon may
## overlap it (own_k, from alone()). Every stripe point at station k is within
## the station's outermost line of the station; polyline() treats two
## neighbouring own points as own only when they are at most 2 OWN_REACH_M
## - 0.2 m apart (ribbon stations are 2 m apart), so every point between them
## is within OWN_REACH_M of one of them: the whole piece lies on this ribbon
## alone, and polyline() may follow the ribbon's own profile there without
## looking for a step.
func own_station(k: int) -> float:
 if ribbon.is_empty() or own_k[k] == 0:return NAN
 return float(k)

## Whether no ribbon but the current one has a quad within reach of p: no
## other ribbon's quads in a RoadSurface bin near p come within the square of
## half side `reach` around p (their bounding box in that bin, cell_boxes()).
func alone(p: Vector2, reach: float) -> bool:
 var lo := Vector2i(floori((p.x - reach) / SURFACE.BIN), floori((p.y - reach) / SURFACE.BIN))
 var hi := Vector2i(floori((p.x + reach) / SURFACE.BIN), floori((p.y + reach) / SURFACE.BIN))
 for x in range(lo.x, hi.x + 1):
  for y in range(lo.y, hi.y + 1):
   var boxes: Dictionary = cell_boxes(Vector2i(x, y))
   for li in boxes:
    if li == ribbon_index:continue
    var b: PackedFloat32Array = boxes[li]
    if p.x + reach >= b[0] and p.y + reach >= b[1] and p.x - reach <= b[2] and p.y - reach <= b[3]:return false
 return true

## Per ribbon (RoadSurface.lines index) with quads in a RoadSurface bin, the
## bounding box of those quads (min x, min z, max x, max z), cached per bin.
func cell_boxes(cell: Vector2i) -> Dictionary:
 var found = cell_ribbons.get(cell)
 if found != null:return found
 var boxes := {}
 var lines: PackedInt32Array = tile.road_surface.quad_line
 var quad_boxes: PackedFloat32Array = tile.road_surface.boxes
 for q in tile.road_surface.bins.get(cell, []):
  var li: int = lines[q]
  var o: int = q * 4
  if not boxes.has(li):
   boxes[li] = PackedFloat32Array([quad_boxes[o], quad_boxes[o + 1], quad_boxes[o + 2], quad_boxes[o + 3]])
   continue
  var b: PackedFloat32Array = boxes[li]
  b[0] = minf(b[0], quad_boxes[o]);b[1] = minf(b[1], quad_boxes[o + 1]);b[2] = maxf(b[2], quad_boxes[o + 2]);b[3] = maxf(b[3], quad_boxes[o + 3])
  boxes[li] = b
 cell_ribbons[cell] = boxes
 return boxes

## The ribbon's height at p near its station k, from its cross-sections:
## RoadSurface.gd builds each one from five points (shoulder, edge, centre,
## edge, shoulder) and joins neighbouring
## stations with straight edges. So the height is the station's centre
## height, plus the cross-section to the side of p, plus the grade
## times how far p lies along the road from the cross-section (sections can
## be skewed to the path), blended between the two stations around k: within
## millimetres of the ribbon's triangles, and only arithmetic.
## NAN where p lies beyond the ribbon (past its shoulders or its end).
func ribbon_fit(p: Vector2, k: float) -> float:
 var n := ribbon.size()
 var i := clampi(int(floorf(k)), 0, n - 1)
 var f := k - i
 if n > 1 and (i == 0 and f < 1e-6 or i == n - 1):
  var e := 0 if i == 0 else n - 1
  var c := Vector2(profile[e * PROFILE], profile[e * PROFILE + 1])
  var inner := 1 if e == 0 else n - 2
  # Past the end cross-section itself (it can be skewed to the path).
  var out := c - Vector2(profile[inner * PROFILE], profile[inner * PROFILE + 1])
  var along := Vector2(-profile[e * PROFILE + 4], profile[e * PROFILE + 3])
  if along.dot(out) < 0.:along = -along
  if (p - c).dot(along) > .02:return NAN
 var h := section_fit(i, p)
 if f > 1e-6 and i + 1 < ribbon.size():h = lerpf(h, section_fit(i + 1, p), f)
 return h

## Station k's numbers for section_fit(): centre (x, z, height), the unit
## vector across (left to right), the crossfall per metre across, the grade
## per metre along, and per side the distance and height of the carriageway
## edge and of the shoulder edge.
func profile_station(k: int) -> void:
 var sec: Array = ribbon[k]
 var c := Vector2(sec[2].x, sec[2].z)
 var across := Vector2(sec[4].x - sec[0].x, sec[4].z - sec[0].z).normalized()
 var along := Vector2(-across.y, across.x)
 var a: Array = ribbon[maxi(k - 1, 0)];var b: Array = ribbon[mini(k + 1, ribbon.size() - 1)]
 var run := (Vector2(b[2].x, b[2].z) - Vector2(a[2].x, a[2].z)).dot(along)
 var u1 := (Vector2(sec[1].x, sec[1].z) - c).dot(across);var u3 := (Vector2(sec[3].x, sec[3].z) - c).dot(across)
 var o := k * PROFILE
 profile[o] = c.x;profile[o + 1] = c.y;profile[o + 2] = sec[2].y
 profile[o + 3] = across.x;profile[o + 4] = across.y
 profile[o + 5] = (sec[3].y - sec[1].y) / (u3 - u1) if absf(u3 - u1) > .01 else 0.
 profile[o + 6] = (b[2].y - a[2].y) / run if absf(run) > .1 else 0.
 profile[o + 7] = u1;profile[o + 8] = sec[1].y
 profile[o + 9] = (Vector2(sec[0].x, sec[0].z) - c).dot(across);profile[o + 10] = sec[0].y
 profile[o + 11] = u3;profile[o + 12] = sec[3].y
 profile[o + 13] = (Vector2(sec[4].x, sec[4].z) - c).dot(across);profile[o + 14] = sec[4].y

func section_fit(i: int, p: Vector2) -> float:
 var o := i * PROFILE
 var dx := p.x - profile[o];var dz := p.y - profile[o + 1]
 var ax := profile[o + 3];var az := profile[o + 4]
 var u := dx * ax + dz * az
 if u < profile[o + 9] - .05 or u > profile[o + 13] + .05:return NAN
 var h: float
 if u < profile[o + 7]:
  h = lerpf(profile[o + 8], profile[o + 10], clampf((u - profile[o + 7]) / minf(profile[o + 9] - profile[o + 7], -.01), 0., 1.))
 elif u > profile[o + 11]:
  h = lerpf(profile[o + 12], profile[o + 14], clampf((u - profile[o + 11]) / maxf(profile[o + 13] - profile[o + 11], .01), 0., 1.))
 elif u < 0.:
  h = lerpf(profile[o + 2], profile[o + 8], u / minf(profile[o + 7], -.01))
 else:
  h = lerpf(profile[o + 2], profile[o + 12], u / maxf(profile[o + 11], .01))
 return h + (dx * -az + dz * ax) * profile[o + 6]

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

## The part of segment a-b inside the tile square [0, side], or [] when none.
static func clip(a: Vector2, b: Vector2, side: float) -> Array:
 var t0 := 0.;var t1 := 1.;var d := b - a
 for edge in [[-d.x, a.x], [d.x, side - a.x], [-d.y, a.y], [d.y, side - a.y]]:
  if absf(edge[0]) < 1e-9:
   if edge[1] < 0.:return []
   continue
  var t: float = edge[1] / edge[0]
  if edge[0] < 0.:t0 = maxf(t0, t)
  else:t1 = minf(t1, t)
  if t0 >= t1:return []
 return [a + d * t0, a + d * t1]

## WorldTile.height_at(), counted (the costliest call here).
func surface(p: Vector2) -> float:
 stats.height_calls += 1
 return tile.height_at(p)

## A stripe corner on the road surface (k: its place on the path's own
## ribbon, or NAN); consecutive quads share their corners.
func corner(p: Vector2, k: float) -> Vector3:
 var h = heights.get(p)
 if h == null:
  var own := ribbon_fit(p, k) if not is_nan(k) else NAN
  h = (own if not is_nan(own) else surface(p)) + LIFT_M
  heights[p] = h
 return tile.vert(p, h)

## One stripe quad (a, b at one end, c, d at the other) into the tile's paint
## batch, as WorldTile.quad() draws it (facing up, the same UVs), in one
## SurfaceTool call instead of eighteen.
func stripe(key: String, a: Vector3, b: Vector3, c: Vector3, d: Vector3) -> void:
 var st = fans.get(key)
 if st == null:
  if not tile.has_method("tool"):
   tile.quad(key, a, b, c, d, Vector3.UP)
   return
  st = tile.tool(key);fans[key] = st
 if (b - a).cross(c - a).y > 0.:
  st.add_triangle_fan(PackedVector3Array([a, d, c, b]), PackedVector2Array([Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), Vector2(0, 1)]), PackedColorArray(), PackedVector2Array(), up4)
 else:
  st.add_triangle_fan(PackedVector3Array([a, b, c, d]), PackedVector2Array([Vector2(1, 1), Vector2(0, 1), Vector2(0, 0), Vector2(1, 0)]), PackedColorArray(), PackedVector2Array(), up4)
 tile.drawn_triangles += 2

## One raised stripe piece, after the measured-paint masks of its road.
func emit(key: String, p: Vector2, q: Vector2, rp: Vector2, rq: Vector2, kp: float, kq: float, zp: float, zq: float, width: float, station: Dictionary, line: Array) -> void:
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
  # Each tile paints only inside its own square: the neighbour paints the
  # rest, and overlapping stripes from two tiles would flicker.
  var inside := clip(part[0], part[1], tile.side)
  if inside.is_empty():continue
  var x: Vector2 = inside[0];var y: Vector2 = inside[1]
  var l := x.distance_to(y)
  if l < .05:continue
  var fx := clampf((x - p).dot(q - p) / (length * length), 0., 1.);var fy := clampf((y - p).dot(q - p) / (length * length), 0., 1.)
  var wx: Vector2 = rp.lerp(rq, fx).normalized() * width * .5;var wy: Vector2 = rp.lerp(rq, fy).normalized() * width * .5
  var kx := lerpf(kp, kq, fx);var ky := lerpf(kp, kq, fy)
  if not is_nan(kx) and not is_nan(ky):
   # On the path's own ribbon alone (own_station()): one smooth surface, so
   # the stripe lies flat across its width at the height its fit found on
   # the line. The helper banks a ribbon at most 6 % (road-grade.mjs), so
   # across 15 cm that is within 5 mm of it, and no other surface can step
   # under it.
   var hx := lerpf(zp, zq, fx) + LIFT_M;var hy := lerpf(zp, zq, fy) + LIFT_M
   stripe(key, tile.vert(x - wx, hx), tile.vert(x + wx, hx), tile.vert(y + wy, hy), tile.vert(y - wy, hy))
   stats.quads += 1
   stats[line[1] + "_" + line[2] + "_m"] += l
   continue
  var a := corner(x - wx, kx);var b := corner(x + wx, kx);var c := corner(y + wy, ky);var d := corner(y - wy, ky)
  # A line across the edge of a higher ribbon (a step in the road surface
  # where two roads overlap), or with an end off the surface its fit
  # followed (past the end of a ribbon), would hang in the air or dive under
  # the road: leave that piece out.
  var off_x := absf((a.y + b.y) * .5 - LIFT_M - lerpf(zp, zq, fx));var off_y := absf((c.y + d.y) * .5 - LIFT_M - lerpf(zp, zq, fy))
  if absf(a.y - b.y) > width * STEP_SLOPE or absf(c.y - d.y) > width * STEP_SLOPE or off_x > STEP_M_OFF or off_y > STEP_M_OFF:
   stats.step_pieces += 1
   continue
  stripe(key, a, b, c, d)
  stats.quads += 1
  stats[line[1] + "_" + line[2] + "_m"] += l
