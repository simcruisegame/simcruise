extends RefCounted
## Where a car is set down on a road: a start, a jump to a place, or the
## back-on-the-road key. One rule for every road in every tile:
##   * in its lane: the first lane right of the centreline (US traffic keeps
##     right), LANE_CENTRE_M from the line or a fifth of the carriageway on a
##     narrow road, so it never sits on the shoulder or the far edge;
##   * facing along the road at that point (the mapped segment's own direction;
##     a requested bearing only picks which way along a two-way road);
##   * level with the road: the body is tilted to the plane through the four
##     tyre contact points (grade and cross slope) and set at its static ride
##     height above it, so it does not drop, bounce or pitch onto one axle.
## Before this the car was set down upright 13 cm above the surface at its
## centre; on the 7.5° grade at Deals Gap one axle landed first and the car
## started rolling, and with nothing holding it, it ran 160 m back down the
## hill into the bank (checks/StartPlacement.gd measures both).
## height(p: Vector2) -> float is the surface the tyres meet (Drive.surface:
## the bridge deck where there is one, else the road ribbon or the ground).

const LANE_CENTRE_M := 1.8   # centre of a 3.6 m lane from the centreline
const LANE_SHARE := .18      # on narrow roads, this share of the paved width

## Lateral offset (to the right of the direction of travel) for a road of this
## paved width.
static func lane_offset(width_m: float) -> float:
 return minf(LANE_CENTRE_M, maxf(width_m, 0.) * LANE_SHARE)

## Direction of travel along a mapped segment [a, b, name, width, oneway, ...]:
## a one-way road's own direction (oneway -1 runs b to a), else the way nearer
## prefer (x, z; zero keeps the mapped direction): a jump's requested bearing,
## or the car's own heading when the back-on-the-road key is pressed.
static func travel_direction(segment: Array, prefer := Vector2.ZERO) -> Vector2:
 var d: Vector2 = (segment[1] - segment[0]).normalized()
 if float(segment[4]) == -1.:d = -d
 if float(segment[4]) == 0. and d.dot(prefer) < 0.:d = -d
 return d

## Unit direction (x, z) of a bearing in degrees clockwise from north.
static func bearing_direction(bearing_deg: float) -> Vector2:
 var a := deg_to_rad(bearing_deg)
 return Vector2(sin(a), -cos(a))

## Yaw (radians about +Y) of a car whose forward (-Z) points along d (x, z).
static func yaw_of(d: Vector2) -> float:
 return atan2(-d.x, -d.y)

## [origin: Vector3, yaw: float, up: Vector3] for a car centred at p facing d.
## mounts: the car's suspension mounts (CarSpec.mounts(), car frame, -Z ahead);
## ride: body origin height above the road at static load.
static func pose(p: Vector2, d: Vector2, mounts: Array, ride: float, height: Callable) -> Array:
 d = d.normalized()
 var right := Vector2(-d.y, d.x)
 var contact := []
 for m in mounts:
  var q: Vector2 = p + right * float(m.x) - d * float(m.z)
  var h = height.call(q)
  if not (h is float) or is_nan(h) or is_inf(h):h = height.call(p)
  contact.append(Vector3(q.x, float(h), q.y))
 # Mounts are front-left, front-right, rear-left, rear-right.
 var ahead: Vector3 = (contact[0] + contact[1] - contact[2] - contact[3]) * .5
 var across: Vector3 = (contact[1] + contact[3] - contact[0] - contact[2]) * .5
 var up := across.cross(ahead).normalized()
 if up.y < .5:up = Vector3.UP # a cliff edge or a missing tile: stay upright
 var centre := Vector3.ZERO
 for c in contact:centre += c
 centre /= contact.size()
 # The plane's height under the car's centre (mount centroid sits on the axis).
 var under: float = centre.y + (up.x * (centre.x - p.x) + up.z * (centre.z - p.y)) / up.y
 return [Vector3(p.x, under, p.y) + up * ride, yaw_of(d), up]
