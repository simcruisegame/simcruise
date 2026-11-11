extends RefCounted
## Curb and gutter along the road ribbon wherever a sidewalk is mapped
## alongside (RoadSurface.gd classify_edges(): EDGE_KERB).
##
## A US town street is curbed: the asphalt meets an 18 in concrete gutter pan
## (shaded on the ribbon's shoulder band by terrain.gdshader), then a 6 in
## curb face, a 6 in curb top and the tree lawn or sidewalk behind it
## (AASHTO Green Book, curbs; most cities' standard "curb and gutter" detail).
## OpenStreetMap rarely tags curbs, but where a footway or sidewalk is mapped
## as its own way beside a street, the street has one, so that is the
## evidence; nothing is guessed from imagery and rural roads get none.
##
## The curb stands on the ribbon's shoulder band, its face KERB_GUTTER_M
## beyond the paved edge, and its back slopes down to the band's outer edge,
## where the terrain carries on. It is drawn with the tile's "curb" material
## and joins the tile's collider (a wheel climbs it, as on a real curb). It
## stops where another road's carriageway crosses (a junction, RoadSurface
## lateral()) and where the ribbon has a gap. Each tile draws only its own
## stations, so neighbours do not double the curb at a seam.
const ROAD_SURFACE := preload("res://RoadSurface.gd")
const KERB_GUTTER_M := .45      # gutter pan width in front of the face
const KERB_HEIGHT_M := .15      # exposed face
const KERB_TOP_M := .15         # top width
const KERB_BATTER_M := .02      # the face leans back this much

static func build(tile) -> Dictionary:
 var stats := {"kerb_m":0., "runs":0, "junction_cuts":0}
 var surface = tile.road_surface
 if surface == null or "--without-kerbs" in OS.get_cmdline_user_args():return stats
 var drawable: bool = tile.materials.has("curb")
 var t := KERB_GUTTER_M / ROAD_SURFACE.SHOULDER_M
 for line in surface.lines:
  var sections: Array = line.sections
  var edge: PackedByteArray = line.edge
  for s in 2:
   var outer_k := 4 if s == 1 else 0
   var edge_k := 3 if s == 1 else 1
   var prev := {}
   for i in sections.size():
    var here := {}
    if edge[i * 2 + s] == ROAD_SURFACE.EDGE_KERB:
     here = profile(sections[i], edge_k, outer_k, t)
     # A crossing carriageway (a junction) or a station outside the tile cuts it.
     var probe: Vector2 = here.probe
     var across: Vector3 = surface.lateral(probe)
     if not is_nan(across.x) and absf(across.x) <= across.y:here = {};stats.junction_cuts += 1
     elif probe.x < 0. or probe.y < 0. or probe.x > tile.side or probe.y > tile.side:here = {}
    if not here.is_empty() and not prev.is_empty():
     var gap := Vector2(prev.pts[0].x, prev.pts[0].z).distance_to(Vector2(here.pts[0].x, here.pts[0].z))
     if gap < 8.:
      if drawable:draw(tile, prev.pts, here.pts, s)
      stats.kerb_m += gap
    elif not here.is_empty():stats.runs += 1
    prev = here
 stats.kerb_m = snappedf(stats.kerb_m, .1)
 return stats

## The curb's cross-section at one station: face foot, face top, top back,
## band edge (where the back slope meets the ribbon's outer edge).
static func profile(section: Array, edge_k: int, outer_k: int, t: float) -> Dictionary:
 var paved: Vector3 = section[edge_k];var outer: Vector3 = section[outer_k]
 var across := Vector3(outer.x - paved.x, 0., outer.z - paved.z)
 var u := across.normalized()
 var foot := paved.lerp(outer, t)
 var top := foot + u * KERB_BATTER_M + Vector3.UP * KERB_HEIGHT_M
 var back := top + u * KERB_TOP_M + Vector3.UP * .005
 return {"pts":[foot, top, back, outer], "probe":Vector2(back.x, back.z)}

static func draw(tile, a: Array, b: Array, s: int) -> void:
 for m in 3:
  var p0: Vector3 = a[m];var p1: Vector3 = a[m + 1];var q0: Vector3 = b[m];var q1: Vector3 = b[m + 1]
  var n := (q0 - p0).cross(p1 - p0).normalized()
  if n.y < 0. and m != 0:n = -n
  if m == 0:
   # The face looks back at the road.
   var toward := Vector3(a[0].x - a[3].x, 0., a[0].z - a[3].z).normalized()
   if n.dot(toward) < 0.:n = -n
  tile.quad("curb", p0, q0, q1, p1, n)
  tile.collision_faces.append_array(PackedVector3Array([p0, q0, q1, p0, q1, p1]))
