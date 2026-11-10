extends RefCounted
## Road roughness felt through the suspension, not drawn.
##
## Roads are rated by the International Roughness Index (IRI, m/km), the
## number state DOTs report to FHWA's HPMS for every federal-aid road: new
## interstate ~0.9, typical arterial ~1.8, "poor" above 2.7, gravel 4–8.
## Until the helper fetches HPMS IRI per section (docs/research/
## real-world-detail.md), each mapped road gets a baseline from its class and
## surface, scaled by a condition that is stable per street (some streets are
## freshly paved, others are worn) and by a slow regional field, so a whole
## part of town can be rough.
##
## The profile itself is band-limited noise in absolute map coordinates, so it
## stays put across world rebases and both front wheels feel the same patch a
## wheelbase before the rear wheels do. Its shape follows the ISO 8608 road
## spectrum (displacement density ∝ n⁻²): each octave's amplitude scales with
## its wavelength. RMS height is IRI × 2.5 mm, which puts a good highway near
## 3 mm and a worn residential street near 1 cm.
##
## Off by default; --rough-roads turns it on. It stays off until real measured
## roughness (HPMS IRI per section) replaces the class estimate.

const CLASS_IRI := {"motorway":1.0,"trunk":1.2,"motorway_link":1.3,"trunk_link":1.4,"primary":1.6,"primary_link":1.7,"secondary":1.9,"secondary_link":1.9,"tertiary":2.2,"tertiary_link":2.2,"unclassified":2.8,"residential":2.6,"living_street":2.6,"service":3.2,"track":6.5}
const UNPAVED_IRI := 6.0
const VERGE_IRI := 9.0       # grass and field beside the road
const RMS_PER_IRI := .0025   # metres of RMS height per m/km of IRI
# Wavelengths (m) of the four octaves. Long waves heave the body, short ones are
# the sharp patches and cracks felt through the seat.
const WAVES := [18., 7., 2.6, .9]

var enabled := true
var noise := FastNoiseLite.new()
var region := FastNoiseLite.new()
var norm := 0.

func _init() -> void:
 # Off by default: the profile is estimated from road class, not measured,
 # and real roads must drive smooth. --rough-roads turns it on (for HPMS work).
 enabled = "--rough-roads" in OS.get_cmdline_user_args()
 noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
 noise.seed = 8608
 noise.frequency = 1.
 noise.fractal_type = FastNoiseLite.FRACTAL_NONE
 region.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
 region.seed = 27
 region.frequency = 1. / 2500.
 region.fractal_type = FastNoiseLite.FRACTAL_NONE
 var total := 0.
 for w in WAVES:total += w * w
 # Godot's smooth simplex has an RMS near 0.53 per octave (measured by
 # checks/AmbientCheck.gd over 4000 samples); scale the sum to unit RMS.
 norm = 1. / (sqrt(total) * .53)

## IRI at a point of a tile (tile-local metres).
static func iri(tile, p: Vector2, grip: float) -> float:
 var best := INF
 var value := VERGE_IRI if grip < .7 else (UNPAVED_IRI if grip < .9 else 2.4)
 for seg in tile.road_grid.get(Vector2i((p / 24.).floor()), []):
  var q := Geometry2D.get_closest_point_to_segment(p, seg[0], seg[1])
  var d := q.distance_to(p)
  if d > float(seg[3]) * .5 + .5 or d > best:continue
  best = d
  var base: float = CLASS_IRI.get(str(seg[5]), 2.4)
  if seg.size() > 6 and seg[6] == "unpaved":base = UNPAVED_IRI
  # Street condition: stable per name and class, 0.6× (new) to 1.9× (worn).
  var h := absi(hash(str(seg[2]) + "|" + str(seg[5]))) % 1000 / 1000.
  value = base * lerpf(.6, 1.9, h * h)
 return minf(value, 10.)   # IRI 10: a rutted track; beyond that is off-road, not road

## Surface height (m) at absolute map coordinates for a given IRI.
func height(world: Vector2, value: float) -> float:
 if not enabled:return 0.
 var sum := 0.
 for i in WAVES.size():
  var w: float = WAVES[i]
  sum += noise.get_noise_2d(world.x / w + i * 101.3, world.y / w - i * 57.1) * w
 var regional := 1. + .45 * region.get_noise_2d(world.x, world.y)
 return sum * norm * value * RMS_PER_IRI * regional
