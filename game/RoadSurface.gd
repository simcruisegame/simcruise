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
##   * terrain grid nodes under the footprint pushed 6 cm below it before the
##     ground mesh is built, so faceted ground can never poke through
##     (lower_grid; bridge decks use the same index, and also push down every
##     ground triangle their edges pass over, BridgeDeck.gd);
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
 # Bridge decks (centreline, half width, heights) and their real ends (not
 # cuts at the tile edge): the abutments of join().
 var abutments := PackedVector2Array()
 var decks := []
 for row in tile.info.get("bridges", []):
  if not row is Array or row.size() < 5 or row[2].size() < 2 or row[2].size() != row[4].size():continue
  var line := PackedVector3Array()
  for k in row[2].size():line.append(Vector3(float(row[2][k][0]) * tile.side, float(row[4][k]), float(row[2][k][1]) * tile.side))
  decks.append([line, float(row[3]) * .5 + DECK_SHOULDER_M])
  for p3 in [line[0], line[-1]]:
   var p := Vector2(p3.x, p3.z)
   if p.x > .05 and p.y > .05 and p.x < tile.side - .05 and p.y < tile.side - .05:abutments.append(p)
 s.build(rows, tile.side, abutments, decks)
 return s if s.active else null

# Major roads first: at an intersection the minor road meets the major road's
# surface (its ribbon snaps where they overlap, and the correction fades out
# along the minor road over JOIN_FADE_M, so there is no step at the edge).
# Ribbons of one class meet the same way, the longer first. Where two banked
# roads overlap (a couplet splitting from its approach) the helper has already
# run their superelevation out, so their cross slopes agree there.
const RANK := {"motorway":7,"trunk":6,"primary":5,"secondary":4,"tertiary":3,"minor":2,"residential":2,"unclassified":2}
const JOIN_FADE_M := 15.

func build(rows: Array, side: float, abutments := PackedVector2Array(), decks := []) -> void:
 _abutments = abutments
 _decks = decks
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
  add_chain(sections, passes_under_deck(sections), chain.rank / 100000)
  active = not quads.is_empty()

## One chain's quads (build, and again after join_ends_to_decks).
func add_chain(sections: Array, under: bool, rank := 0) -> void:
 if not _abutments.is_empty():_built.append([sections, under, rank])
 for i in range(1, sections.size()):
  var prev: Array = sections[i-1];var section: Array = sections[i]
  if Vector2(prev[2].x, prev[2].z).distance_to(Vector2(section[2].x, section[2].z)) >= 8.:continue
  for k in section.size() - 1:
   add_quad(prev[k], prev[k+1], section[k+1], section[k])
   if not _abutments.is_empty():under_deck.resize(quads.size());under_deck[-1] = 1 if under else 0
var _built := []   # [sections, under, class rank] in build order, on a tile with bridges


## After the decks are fitted (BridgeDeck.prepare), a ribbon meets a deck as a
## minor road meets a major one: where it lies on a deck within
## DECK_JOIN_REACH_M of its own height, its vertices take the deck's height and
## the correction fades out along it over JOIN_FADE_M. Two cases, both only on a
## tile with bridges:
##   * a ribbon ending where its own deck was left out (no deck built has a
##     real end there) meets the deck it runs onto: Linden St ending on the
##     Cambridge St bridge, Allston MA, stood 0.77 m above it once its few
##     metres of deck were left out (BridgeDeck.without_slivers);
##   * a ribbon of a lower class than a deck it crosses at nearly the deck's
##     level joins it, as at an at-grade junction: Frazier Ave, Chattanooga TN,
##     rose 0.19 m through the Veterans Bridge deck crossing it.
## Every other approach already has its deck fitted to it (BridgeDeck.fit), and
## a ribbon is never moved onto a deck of its own class or lower (moving an
## approach onto the twin carriageway's deck beside it left a 1.2 m step).
## Returns the chains changed; the quads are rebuilt when any was.
const DECK_JOIN_REACH_M := 1.
const POKE_M := .3
## deck_ends: per deck built, its real end points; deck_ranks: its RANK.
func join_ends_to_decks(decks, deck_ends: Array, deck_ranks := PackedInt32Array()) -> int:
 if _built.is_empty() or decks == null:return 0
 var owner: PackedInt32Array = decks.get_meta("owner", PackedInt32Array())
 var changed := 0
 for entry in _built:
  var sections: Array = entry[0];var rank: int = entry[2]
  var orphan := []
  for c in [Vector2(sections[0][2].x, sections[0][2].z), Vector2(sections[-1][2].x, sections[-1][2].z)]:
   var abut := false;var built := false
   for p in _abutments:abut = abut or p.distance_to(c) <= ABUTMENT_SNAP_M
   for ends in deck_ends:
    for e in ends:built = built or e.distance_to(c) <= ABUTMENT_SNAP_M
   orphan.append(abut and not built)
  # Per section: 2 any deck (an orphan end's zone), 1 only decks of a higher
  # class than this road, 0 none.
  var at := []
  var along := 0.;var total := 0.
  for i in range(1, sections.size()):total += Vector2(sections[i][2].x, sections[i][2].z).distance_to(Vector2(sections[i-1][2].x, sections[i-1][2].z))
  for i in sections.size():
   if i > 0:along += Vector2(sections[i][2].x, sections[i][2].z).distance_to(Vector2(sections[i-1][2].x, sections[i-1][2].z))
   at.append(2 if (orphan[0] and along < ABUTMENT_M) or (orphan[1] and total - along < ABUTMENT_M) else 1)
  var deck_at := func(p: Vector2, y: float, mode: int) -> float:
   var best := NAN
   for idx in decks.bins.get(Vector2i(floori(p.x / BIN), floori(p.y / BIN)), []):
    if mode == 1 and (idx >= owner.size() or owner[idx] >= deck_ranks.size() or deck_ranks[owner[idx]] <= rank):continue
    var q: Array = decks.quads[idx]
    var h := tri_height(p, q[0], q[1], q[2])
    if is_nan(h):h = tri_height(p, q[0], q[2], q[3])
    if is_nan(h) or absf(h - y) > DECK_JOIN_REACH_M:continue
    # A road crossing a deck of a higher class joins it only where it would
    # stand above the deck or within POKE_M below it (it pokes through), and
    # not at an abutment, where the junction is the approaches' (join()).
    if mode == 1 and h - y > POKE_M:continue
    if mode == 1 and near_abutment(p):continue
    if is_nan(best) or absf(h - y) < absf(best - y):best = h
   return best
  var delta := PackedFloat32Array();delta.resize(sections.size())
  var covered := []
  for i in sections.size():
   var sum := 0.;var count := 0
   for v in sections[i]:
    var hd: float = deck_at.call(Vector2(v.x, v.z), v.y, at[i])
    if not is_nan(hd):sum += hd - v.y;count += 1
   covered.append(count > 0)
   delta[i] = sum / count if count > 0 else 0.
  if not covered.has(true):continue
  var station := PackedFloat32Array();station.resize(sections.size())
  for i in range(1, sections.size()):
   station[i] = station[i-1] + Vector2(sections[i][2].x, sections[i][2].z).distance_to(Vector2(sections[i-1][2].x, sections[i-1][2].z))
  var moved := false
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
    var hd: float = deck_at.call(Vector2(v.x, v.z), v.y, at[i])
    var y := hd if not is_nan(hd) else v.y + best * weight
    if absf(y - v.y) > .005:moved = true
    sections[i][k] = Vector3(v.x, y, v.z)
  if moved:changed += 1
 if changed > 0:
  var built := _built.duplicate()
  quads.clear();bins.clear();masks.clear();under_deck.clear();_built.clear()
  for entry in built:add_chain(entry[0], entry[1], entry[2])
 return changed

## Snap a lower-ranked ribbon onto the ribbons already built where they
## overlap; the station-wise correction fades out over JOIN_FADE_M.
##
## One exception, and only beside a bridge: on a road with an end within
## ABUTMENT_M of a real deck end (an abutment the helper gave this tile), a
## vertex within ABUTMENT_M of the abutment is never joined
## onto a ribbon that passes under a deck of this tile (passes_under_deck) and
## lies more than UNDER_JOIN_M from it: the two overlap there only because one
## runs beneath the abutment, on another level. River St, Allston MA, was
## pulled 1.7 m down onto Soldiers Field Road under its own abutment, and
## Cambridge St 2.5 m down onto the Mass Pike's shoulder, and the fitted deck
## ends followed them (a 2.3 m step; the Miata rolled over); the Cambridge St
## junction 7 m from River St's abutment was torn into a 2 m V where half its
## vertices took the parkway's height. A junction there with a road that does
## not pass under the bridge is joined as anywhere else. Away from abutments,
## and on a tile without a bridge, ribbons join exactly as they always did.
func join(sections: Array) -> void:
 var delta := PackedFloat32Array();delta.resize(sections.size())
 var covered := []
 # Only a road ending within ABUTMENT_M of an abutment (an approach, or a
 # street meeting it there); a road passing by joins as it always did.
 var ends_near := not _abutments.is_empty() and (near_abutment(Vector2(sections[0][2].x, sections[0][2].z), ABUTMENT_M) or near_abutment(Vector2(sections[-1][2].x, sections[-1][2].z), ABUTMENT_M))
 for i in sections.size():
  var sum := 0.;var count := 0
  for v in sections[i]:
   var hi := level_height(Vector2(v.x, v.z), v.y) if ends_near else height(Vector2(v.x, v.z))
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
   var hi := level_height(Vector2(v.x, v.z), v.y) if ends_near else height(Vector2(v.x, v.z))
   sections[i][k] = Vector3(v.x, hi if not is_nan(hi) else v.y + best * weight, v.z)
const ABUTMENT_M := 20.
const ABUTMENT_SNAP_M := 3.
const DECK_SHOULDER_M := .6      # BridgeDeck.SHOULDER_M
const UNDER_CLEARANCE_M := .5    # a ribbon crossing this far below a deck runs under it
const UNDER_ALIGN := .866        # cos 30°: closer to the deck's direction is alongside, not across
const UNDER_END_CLEAR_M := 6.    # ... away from the deck's real ends (its own approaches)
var _abutments := PackedVector2Array()
var _decks := []
var under_deck := PackedByteArray()   # per ribbon quad: its road passes under a deck (only with abutments)

## Whether a road runs under a bridge deck of this tile: a centreline station
## inside a deck's footprint, at least UNDER_CLEARANCE_M below the deck and
## more than UNDER_END_CLEAR_M from every real deck end (where the bridge's own
## approaches meet it).
func passes_under_deck(sections: Array) -> bool:
 if _abutments.is_empty():return false
 for i in sections.size():
  var sec: Array = sections[i]
  var c := Vector2(sec[2].x, sec[2].z)
  var near_end := false
  for p in _abutments:near_end = near_end or p.distance_to(c) <= UNDER_END_CLEAR_M
  if near_end:continue
  var o: Array = sections[maxi(i - 1, 0)] if i > 0 else sections[mini(1, sections.size() - 1)]
  var along := (c - Vector2(o[2].x, o[2].z)).normalized()
  for d in _decks:
   var line: PackedVector3Array = d[0]
   for k in range(1, line.size()):
    var a := Vector2(line[k-1].x, line[k-1].z);var b := Vector2(line[k].x, line[k].z)
    var q := Geometry2D.get_closest_point_to_segment(c, a, b)
    if q.distance_to(c) > d[1]:continue
    # Crossing the deck (not running alongside it at its level, a carriageway
    # the deck shares a footprint with) and below it.
    if absf(along.dot((b - a).normalized())) > UNDER_ALIGN:continue
    var t := clampf(a.distance_to(q) / maxf(a.distance_to(b), 1e-4), 0., 1.)
    if lerpf(line[k-1].y, line[k].y, t) - sec[2].y >= UNDER_CLEARANCE_M:return true
 return false

func near_abutment(p: Vector2, reach := UNDER_END_CLEAR_M) -> bool:
 for a in _abutments:
  if a.distance_to(p) <= reach:return true
 return false

## The ribbon height join() meets at p: height(), or near an abutment the
## height leaving out roads under a deck on another level.
func level_height(p: Vector2, y: float) -> float:
 if not _abutments.is_empty() and near_abutment(p, ABUTMENT_M):return height_above_deck_roads(p, y)
 return height(p)

## height(), leaving out ribbons that pass under a deck (join() at an abutment)
## where they are on another level than y: within UNDER_JOIN_M of this road
## such a ribbon meets it at the abutment, a junction like any other (a
## street along the bank at Veterans Bridge, Chattanooga TN, stood 0.15 m
## above the approach it meets).
const UNDER_JOIN_M := 1.
func height_above_deck_roads(p: Vector2, y: float) -> float:
 var best := NAN
 for idx in bins.get(Vector2i(floori(p.x / BIN), floori(p.y / BIN)), []):
  var q: Array = quads[idx]
  var h := tri_height(p, q[0], q[1], q[2])
  if is_nan(h):h = tri_height(p, q[0], q[2], q[3])
  if is_nan(h):continue
  if idx < under_deck.size() and under_deck[idx] == 1 and absf(h - y) > UNDER_JOIN_M:continue
  if is_nan(best) or h > best:best = h
 return best

## mask: which edges lower_grid's edge pass samples, two bits per edge (a-b,
## b-c, c-d, d-a from the low bits): 0 none, EDGE_LONG every ~1 m,
## EDGE_STATION once per grid cell. Only bridge decks are given the edge pass
## (BridgeDeck.gd edge_masks); a road ribbon pushes down only the grid nodes
## under it, as it always has.
const EDGE_LONG := 1
const EDGE_STATION := 2
const END_CLEAR_M := 4.
var masks := PackedInt32Array()
func add_quad(a: Vector3, b: Vector3, c: Vector3, d: Vector3, mask := 0) -> void:
 var idx := quads.size()
 quads.append([a, b, c, d])
 masks.append(mask)
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
## built), so faceted ground can never poke through it. For road ribbons
## (deck false) only the grid nodes under them move, exactly as before bridges
## changed anything. For bridge decks (deck true, BridgeDeck.prepare) the edge
## nodes read a deck clipped at the tile edge just inside, and every ground
## triangle the deck's masked edges pass over is pushed down too: a triangle
## reaching out to a higher node beside an abutment stood up to 1.5 m above
## the deck (Cambridge St, Allston MA). This edge pass once ran under every
## ribbon too, and it roughened at-grade roads with no bridge (Commonwealth
## Ave, Allston MA, 2.4 -> 9.7 deg/m; the Miata left the road), so it is
## confined to decks. Where the ground triangle under an edge sample
## stands above the surface, its corners drop by the least that brings it
## UNDER_M below (weighted by how close each corner is to the sample), like the
## ditch beside a cut; ground that is already below keeps its height. bilinear:
## the grid is read bilinearly (base_height on a tile without a measured grade
## source), so the four corners of the cell share the correction.
const EDGE_SAMPLE_M := .5
## keep: roads without a ribbon (service roads, tracks) as [a, b, reach]
## segments in tile metres (keep_segments): the ground they drive on is not
## pushed down by the deck edge pass where they meet a deck's side, or a car on## them would drop off its edge into the dip (when the pass ran under ribbons## too, Anabel's Way and the fire station drive at Stowe VT met VT-108 with## 0.1-0.3 m steps).
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

func lower_grid(h_in: Array, n: int, side: float, bilinear := false, keep := {}, deck := false) -> int:
 var cell := side / n
 var moved := {}
 # A packed copy: the edge pass reads the grid hundreds of thousands of times.
 var h := PackedFloat64Array(h_in)
 for j in n + 1:
  for i in n + 1:
   var r := height(Vector2(i, j) * cell)
   # A surface clipped at the tile edge (a deck row) ends exactly on the edge
   # nodes; read it just inside, as the neighbour does from its side.
   if deck and is_nan(r) and (i == 0 or j == 0 or i == n or j == n):
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
 if deck and not quads.is_empty():
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
