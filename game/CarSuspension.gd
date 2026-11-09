extends RefCounted
## Springs, anti-roll bars, dampers and the limited-slip split for any car,
## parameterised by a CarSpec. With the default spec every function performs
## the same operations, in the same order, as NB1999Sport.gd, so the Miata's
## numbers do not move (checks/Track.gd, RideCheck.gd, RearDamperCheck.gd).
const POUNDS_PER_INCH_TO_N_M = 175.126835
var spec

func _init(car_spec) -> void:
 spec=car_spec

func wheel_rate(axle: int) -> float:
 if float(spec.wheel_rate_n_m[axle])>0.:return float(spec.wheel_rate_n_m[axle])
 return float(spec.coil_lb_in[axle]) * POUNDS_PER_INCH_TO_N_M * float(spec.lever_ratio[axle]) ** 2

func roll_bar_rate(axle: int) -> float:
 if float(spec.bar_rate_n_m[axle])>0.:return float(spec.bar_rate_n_m[axle])
 var diameter: float = spec.bar_diameter_m[axle]
 if diameter<=0.:return 0.
 var moment := PI * diameter ** 4 / 64.0
 var arm: float = spec.bar_arm_m[axle]
 var twist: float = arm ** 2 * float(spec.bar_torsion_length_m[axle]) / (79e9 * 2.0 * moment)
 var bending := 2.0 * arm ** 3 / (3.0 * 205e9 * moment)
 return 0.82 * float(spec.bar_lever_ratio[axle]) ** 2 / (twist + bending) # estimated bush compliance

# Damper force from a shaft-speed table, as NB1999Sport.rear_damper_load.
func dyno_load(axle: int, wheel_velocity: float) -> float:
 var table: Array = spec.damper_dyno[axle]
 var ratio: float = spec.lever_ratio[axle]
 var speed_in_s := absf(wheel_velocity) * ratio / 0.0254
 var curve_axis := 2 if wheel_velocity > 0.0 else 1
 var last: Vector3 = table.back()
 var force_lbf: float = last[curve_axis]
 for i in range(1, table.size()):
  var upper: Vector3 = table[i]
  if speed_in_s <= upper.x:
   var lower: Vector3 = table[i-1]
   force_lbf = lerpf(lower[curve_axis], upper[curve_axis], inverse_lerp(lower.x, upper.x, speed_in_s))
   break
 return -signf(wheel_velocity) * ratio * force_lbf * 4.4482216153

func contact_load(axle: int, travel: float, velocity: float, mass_kg: float) -> float:
 var share: float = spec.front_weight if axle == 0 else 1.0 - spec.front_weight
 var corner_mass := mass_kg * share * 0.5
 var rate := wheel_rate(axle)
 var compression: float = spec.static_length_m - travel
 # Installed preload preserves the target ride height when changing rates.
 var spring := corner_mass * 9.80665 + compression * rate
 var bump := maxf(compression - spec.bump_gap_m, 0.0)
 spring += bump * bump * spec.bump_stiffness
 if spec.damper[axle]=="dyno":
  return clampf(spring + dyno_load(axle, velocity), 0.0, spec.max_contact_load_n)
 var damping_ratio: float = spec.damper_ratio_rebound[axle] if velocity > 0 else spec.damper_ratio_compression[axle]
 var critical := 2.0 * sqrt(rate * (corner_mass - spec.unsprung_corner_kg))
 var damper_speed := absf(velocity)
 var knee: float = spec.damper_knee_mps[axle]
 var effective_speed := minf(damper_speed, knee) + maxf(damper_speed - knee, 0.0) * float(spec.damper_knee_slope[axle])
 return clampf(spring - signf(velocity) * critical * damping_ratio * effective_speed, 0.0, spec.max_contact_load_n)

# Torque-sensing limited slip between two wheels (bias 1 = open differential),
# as NB1999Sport.differential_torque with the bias as a parameter.
static func differential_torque(axle_torque: float, left_omega: float, right_omega: float,
                                wheel_inertia: float, dt: float, drive: bool, bias: float) -> Vector2:
 var half_torque := axle_torque * 0.5
 if not drive or dt <= 0.0:
  return Vector2(half_torque, half_torque)
 var limit := absf(half_torque) * (bias-1.) / (bias+1.)
 var equalizing := (right_omega-left_omega) * wheel_inertia / (2.*dt)
 var transfer := clampf(equalizing, -limit, limit)
 return Vector2(half_torque+transfer, half_torque-transfer)
