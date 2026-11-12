extends RefCounted
# A generic 17 x 7 ten-spoke alloy on a 205/40R17 tyre for the tuned roadster
# (cars/nb1999_turbo.json, finish_wheel "multispoke17"), built procedurally once
# per run and shared by the four hubs, in the same frame as SportWheel.gd: one
# right-hand wheel in metres, axle along +X (outboard), x = 0 on the rim
# centreline. Offset +40 like the factory 15 x 6, so the centreline stays where
# the 15 in wheel's was and the extra inch of width splits half inboard, half
# outboard.
#
# The design is generic and original: ten straight spokes in five close pairs
# running from a recessed hub out to the outer lip (a concave face, because the
# mounting face sits 40 mm outboard of the centreline and the lip 88 mm), four
# lugs on the 100 mm circle and a plain cap. It copies no maker's wheel and
# carries no marks. Rim: 17 in bead seat (215.9 mm radius), 7 in between the
# flanges, a machined outer lip and a dark-grey painted face. The 269 mm
# vented front disc (Mazda 2004 turbo-car specification) shows through the
# spokes; the caliper is SportWheel.caliper() on the hub.
# Tyre: SportWheel's 195/50R15 section rescaled to 205/40R17 (82 mm sidewall,
# 298 mm free radius), same rim protector, shoulder and grooves.
const SPORT_WHEEL := preload("res://SportWheel.gd")
const BEAD_R := .2159
const SIDEWALL_M := .082
const HALF_WIDTH := .0889          # 7 in between the flanges
const SPOKE_PAIRS := 5

static var _wheel: ArrayMesh

static func wheel() -> ArrayMesh:
 if _wheel == null:
  _wheel = build_wheel()
 return _wheel

static func radial(a: float) -> Vector3:
 return Vector3(0, cos(a), sin(a))

# Axial position of the spoke face at a radius: recessed at the hub, curving
# out to the lip (concave).
static func face_x(r: float) -> float:
 var t := clampf((r - .075) / (.205 - .075), 0., 1.)
 return .050 + .034 * (t * t * (3. - 2. * t)) + .006 * t

static func spoke(st: SurfaceTool, centre: float) -> void:
 # One straight spoke as a lofted section: a crowned top, two flanks and a
 # chamfer, from r 0.072 to 0.212 (into the lip), 16 mm deep.
 var rows := 18
 var prev := []
 for row in rows + 1:
  var r := lerpf(.072, .212, float(row) / rows)
  var half_w := lerpf(.0135, .0085, float(row) / rows) # tapering
  var depth := .016
  var fx := face_x(r)
  # Section across the spoke: (lateral metres, axial offset from the face).
  var section := [Vector2(-half_w, -depth), Vector2(-half_w, -.003), Vector2(-half_w * .7, 0.), Vector2(0., .0015), Vector2(half_w * .7, 0.), Vector2(half_w, -.003), Vector2(half_w, -depth)]
  var pts := []
  for s in section:
   var ang: float = centre + s.x / r
   pts.append(radial(ang) * r + Vector3.RIGHT * (fx + s.y))
  if row > 0:
   for j in section.size() - 1:
    var a: Vector3 = prev[j]; var b: Vector3 = pts[j]; var c: Vector3 = pts[j + 1]; var d: Vector3 = prev[j + 1]
    st.add_vertex(a); st.add_vertex(c); st.add_vertex(b)
    st.add_vertex(a); st.add_vertex(d); st.add_vertex(c)
  prev = pts

static func build_wheel() -> ArrayMesh:
 var mesh := ArrayMesh.new()
 # Tyre: the 15 in section rescaled about the bead.
 var rubber := SPORT_WHEEL.material(Color("#1b1c1c"), 0., .86)
 rubber.normal_enabled = true; rubber.normal_texture = preload("res://art/tyre_sidewall_normal.png"); rubber.normal_scale = 1.
 rubber.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
 var tyre := SPORT_WHEEL.surface(rubber)
 var section := []
 for p in SPORT_WHEEL.tyre_profile():
  section.append(Vector3(p.x * (.1025 / .0975), BEAD_R + (p.y - .1905) * (SIDEWALL_M / .0973), p.z))
 SPORT_WHEEL.lathe(tyre, section, 112)
 tyre.generate_tangents()
 tyre.commit(mesh)
 # Rim barrel and machined lip.
 var lip_mat := SPORT_WHEEL.material(Color("#c4c7c8"), .9, .22)
 lip_mat.clearcoat_enabled = true; lip_mat.clearcoat = .7; lip_mat.clearcoat_roughness = .08
 var lip := SPORT_WHEEL.surface(lip_mat)
 SPORT_WHEEL.ring(lip, [Vector2(HALF_WIDTH - .004, BEAD_R - .004), Vector2(HALF_WIDTH + .002, BEAD_R + .004), Vector2(HALF_WIDTH + .0035, BEAD_R + .0095), Vector2(HALF_WIDTH + .0005, BEAD_R + .0125), Vector2(HALF_WIDTH - .004, BEAD_R + .0105), Vector2(HALF_WIDTH - .006, BEAD_R + .002)], Vector3.ZERO, 112)
 lip.commit(mesh)
 var face_mat := SPORT_WHEEL.material(Color("#3b3e41"), .75, .32)
 face_mat.clearcoat_enabled = true; face_mat.clearcoat = .6; face_mat.clearcoat_roughness = .1
 var face := SPORT_WHEEL.surface(face_mat)
 # Inner barrel from the lip back to the inboard flange.
 SPORT_WHEEL.ring(face, [Vector2(HALF_WIDTH - .006, BEAD_R + .002), Vector2(HALF_WIDTH - .010, BEAD_R - .008), Vector2(.02, BEAD_R - .012), Vector2(-.03, BEAD_R - .018), Vector2(-HALF_WIDTH + .004, BEAD_R - .012), Vector2(-HALF_WIDTH - .002, BEAD_R + .006), Vector2(-HALF_WIDTH - .004, BEAD_R + .010)], Vector3.ZERO, 112)
 # Ten spokes in five pairs, 12 degrees apart inside a pair.
 for k in SPOKE_PAIRS:
  for side in [-1., 1.]:
   spoke(face, k * TAU / SPOKE_PAIRS + side * deg_to_rad(6.))
 # Hub: recessed mounting pad with lug bosses.
 SPORT_WHEEL.ring(face, [Vector2(.040, .001), Vector2(.046, .030), Vector2(.050, .062), Vector2(.050, .080), Vector2(.030, .080)], Vector3.ZERO, 60)
 face.generate_normals()
 face.commit(mesh)
 var dark := SPORT_WHEEL.surface(SPORT_WHEEL.material(Color("#2a2b2b"), .6, .4))
 for i in 4:
  var c := radial((i + .5) * PI * .5) * .05
  SPORT_WHEEL.ring(dark, [Vector2(.040, .0105), Vector2(.058, .0105), Vector2(.060, .0085), Vector2(.0605, .0002)], c, 6)
 dark.commit(mesh)
 var cap := SPORT_WHEEL.surface(SPORT_WHEEL.material(Color("#2f3133"), .7, .3))
 SPORT_WHEEL.ring(cap, [Vector2(.047, .030), Vector2(.052, .027), Vector2(.0555, .018), Vector2(.0565, .006), Vector2(.0566, .0005)], Vector3.ZERO, 48)
 cap.commit(mesh)
 # 269 mm vented disc with its hat, inside the spokes.
 var disc := SPORT_WHEEL.surface(SPORT_WHEEL.material(Color("#6d6f6d"), .7, .45))
 SPORT_WHEEL.ring(disc, [Vector2(.040, .045), Vector2(.040, .080), Vector2(.022, .080), Vector2(.022, .1345), Vector2(.000, .1345), Vector2(.000, .080)], Vector3.ZERO, 72)
 disc.commit(mesh)
 return mesh
