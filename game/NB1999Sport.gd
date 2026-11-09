extends RefCounted
# US 1999 five-speed Sport, 195/50R15. Spring and bar dimensions are the
# NB Sport row in Fat Cat Motorsports' NB Ride Harmonizer. Forces use SI.
const POUNDS_PER_INCH_TO_N_M = 175.126835
const COIL_LB_IN = [168.0, 130.0]
const LEVER_RATIO = [0.68, 0.77]
const BAR_DIAMETER_M = [0.022, 0.011]
const BAR_TORSION_LENGTH_M = [0.822, 0.813]
const BAR_ARM_M = [0.216, 0.122]
const BAR_LEVER_RATIO = [0.548, 0.586]
# Working on-throttle Torsen bias cap for development. JTEKT confirms the
# 1996–2002 Miata T-2 family; this ratio has not been measured on this car.
const DRIVE_BIAS_RATIO = 2.0

static func differential_torque(axle_torque: float, left_omega: float, right_omega: float,
                                wheel_inertia: float, dt: float, drive: bool) -> Vector2:
 var half_torque := axle_torque * 0.5
 if not drive or dt <= 0.0:
  return Vector2(half_torque, half_torque)
 # Resist differentiation with friction proportional to transmitted torque.
 # The impulse cannot swap the wheel speeds or increase wheel-pair energy.
 var limit := absf(half_torque) * (DRIVE_BIAS_RATIO-1.) / (DRIVE_BIAS_RATIO+1.)
 var equalizing := (right_omega-left_omega) * wheel_inertia / (2.*dt)
 var transfer := clampf(equalizing, -limit, limit)
 return Vector2(half_torque+transfer, half_torque-transfer)

const FRONT_WEIGHT = 0.515
const STATIC_LENGTH_M = 0.217
const STATIC_CAMBER_DEG = [-0.53, -1.10]
const CAMBER_PER_M = [23.0, 25.0] # estimated local wishbone camber gain
const CASTER_DEG = 6.0
const TOE_PER_WHEEL_DEG = 0.15
const MIN_TRAVEL_M = 0.074
const MAX_TRAVEL_M = 0.315

# Fat Cat Motorsports' orange “Bilstein Hard S Rear PVP” trace:
# https://www.fatcatmotorsports.com/igallery/graphics/OE_NB_Showa_and_HardS_vs_NA_OE_Rpkg_and_aftermarket_HD1.GIF
# Rounded graph readings: shaft speed (in/s), compression (+lbf), rebound magnitude (−lbf on plot).
# One published rear test; its temperature, mileage and factory tolerance are unknown.
const REAR_DYNO = [
 Vector3(0.0, 0, 0), Vector3(1.0, 42, 73), Vector3(1.1, 45, 80),
 Vector3(2.0, 56, 127), Vector3(3.0, 66, 146), Vector3(4.0, 72, 160),
 Vector3(6.0, 87, 183), Vector3(8.0, 100, 206), Vector3(9.98, 112, 224),
]

static func wheel_rate(axle: int) -> float:
 return COIL_LB_IN[axle] * POUNDS_PER_INCH_TO_N_M * LEVER_RATIO[axle] ** 2

static func roll_bar_rate(axle: int) -> float:
 var diameter: float = BAR_DIAMETER_M[axle]
 var moment := PI * diameter ** 4 / 64.0
 var arm: float = BAR_ARM_M[axle]
 var twist: float = arm ** 2 * BAR_TORSION_LENGTH_M[axle] / (79e9 * 2.0 * moment)
 var bending := 2.0 * arm ** 3 / (3.0 * 205e9 * moment)
 return 0.82 * BAR_LEVER_RATIO[axle] ** 2 / (twist + bending) # estimated bush compliance

static func rear_damper_load(wheel_velocity: float) -> float:
 # Positive wheel-to-body separation velocity is rebound and reduces the contact load.
 # Shaft travel per wheel travel scales velocity and, by virtual work, wheel force.
 var ratio: float = LEVER_RATIO[1]
 var speed_in_s := absf(wheel_velocity) * ratio / 0.0254
 var curve_axis := 2 if wheel_velocity > 0.0 else 1
 var last: Vector3 = REAR_DYNO.back()
 var force_lbf: float = last[curve_axis]
 # Interpolate to zero below the first sample. Beyond the plot, hold the last force.
 for i in range(1, REAR_DYNO.size()):
  var upper: Vector3 = REAR_DYNO[i]
  if speed_in_s <= upper.x:
   var lower: Vector3 = REAR_DYNO[i-1]
   force_lbf = lerpf(lower[curve_axis], upper[curve_axis], inverse_lerp(lower.x, upper.x, speed_in_s))
   break
 return -signf(wheel_velocity) * ratio * force_lbf * 4.4482216153

static func contact_load(axle: int, travel: float, velocity: float, mass_kg: float) -> float:
 var share: float = FRONT_WEIGHT if axle == 0 else 1.0 - FRONT_WEIGHT
 var corner_mass := mass_kg * share * 0.5
 var rate := wheel_rate(axle)
 var compression := STATIC_LENGTH_M - travel
 # Installed preload preserves the target ride height when changing coil rate.
 var spring := corner_mass * 9.80665 + compression * rate
 var bump := maxf(compression - 0.054, 0.0)
 spring += bump * bump * 800000.0
 if axle == 1:
  return clampf(spring + rear_damper_load(velocity), 0.0, 14200.0)
 # Front damping remains a wheel-side estimate pending a front Hard S dyno test.
 var damping_ratio := 0.56 if velocity > 0 else 0.33
 var critical := 2.0 * sqrt(rate * (corner_mass - 23.0))
 var damper_speed := absf(velocity)
 var effective_speed := minf(damper_speed, 0.12) + maxf(damper_speed - 0.12, 0.0) * 0.48
 return clampf(spring - signf(velocity) * critical * damping_ratio * effective_speed, 0.0, 14200.0)
