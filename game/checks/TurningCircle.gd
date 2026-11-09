extends SceneTree

# Measure the path of the outer front tire sidewall through a full turn under
# power. Its swept diameter differs from the path of the body or the rear axle.
const SPEEDS = [1.0, 1.8]
const SportCar = preload("res://Powertrain.gd")
const TIRE_HALF_WIDTH_M = 0.195 / 2.0
const SPEC_DIAMETER_M = 30.2 * 0.3048
var car
var trial := 0
var ticks := 0
var last_yaw := 0.0
var sweep := 0.0
var speed_trim := 0.0
var speed_sum := 0.0
var samples := 0
var first := Vector2.ZERO
var low := Vector2.INF
var high := -Vector2.INF
var biggest_error := 0.0
var all_stable := true

func _initialize():
 var pad := StaticBody3D.new()
 var collider := CollisionShape3D.new()
 var box := BoxShape3D.new()
 box.size = Vector3(80, 1, 80)
 collider.shape = box
 pad.position.y = -0.5
 pad.add_child(collider)
 root.add_child(pad)
 car = SportCar.new()
 car.setup()
 root.add_child(car)
 start_trial()

func start_trial():
 ticks = 0
 speed_trim = 0.0
 sweep = 0.0
 last_yaw = 0.0
 samples = 0
 speed_sum = 0.0
 low = Vector2.INF
 high = -Vector2.INF
 car.restart(Vector3(0, 0.18, 0), 0.0)

func _physics_process(dt):
 ticks += 1
 if ticks > 6400:
  push_error("Turning circle did not finish")
  quit(1)
  return false
 var direction := 1.0 if trial % 2 == 0 else -1.0
 var speed_goal: float = SPEEDS[floori(trial / 2.0)]
 var speed: float = -car.linear_velocity.dot(car.basis.z)
 if ticks > 90:
  speed_trim = clampf(speed_trim + 0.18 * (speed_goal - speed) * dt, 0.0, 0.50)
 var throttle := clampf(speed_trim + 0.42 * (speed_goal - speed), 0.0, 0.65)
 car.drive(dt, throttle if ticks > 90 else 0.0, 0.0, direction, false)
 var yaw: float = atan2(car.basis.z.x,car.basis.z.z)
 var prev := sweep
 sweep += direction * angle_difference(last_yaw, yaw)
 last_yaw = yaw
 # Allow a half turn to establish speed, body roll and tire forces.
 if sweep < PI: return false
 var outside := 1 if direction > 0 else 0
 var outward: Vector3 = car.basis.x.rotated(car.basis.y, car.wheel_heading[outside]) * direction
 var edge: Vector3 = car.connected_wheels[outside].global_position + outward * TIRE_HALF_WIDTH_M
 var point := Vector2(edge.x,edge.z)
 if prev < PI: first = point
 low = low.min(point)
 high = high.max(point)
 speed_sum += speed
 samples += 1
 if car.wheel_contact != 4: all_stable = false
 if sweep < 3.0 * PI: return false
 var span := high - low
 var diameter := (span.x + span.y) * 0.5
 var mean_speed := speed_sum / samples
 var outer_angle := absf(rad_to_deg(car.wheel_heading[outside]))
 var inner_angle := absf(rad_to_deg(car.wheel_heading[1 - outside]))
 if absf(inner_angle - 38.0) > 3.0 or absf(outer_angle - 33.0) > 3.0: all_stable = false
 var closure := point.distance_to(first)
 if absf(span.x - span.y) > 0.04 or closure > 0.12 or absf(mean_speed - speed_goal) > 0.04:
  all_stable = false
 biggest_error = maxf(biggest_error, absf(diameter - SPEC_DIAMETER_M))
 print("TURN_CIRCLE ", JSON.stringify({"direction": "left" if direction > 0 else "right", "speed_mps": mean_speed, "diameter_m": diameter, "diameter_ft": diameter / 0.3048, "closure_m": closure, "span_m": [span.x,span.y], "front_wheel_degrees": [rad_to_deg(car.wheel_heading[0]),rad_to_deg(car.wheel_heading[1])], "samples": samples}))
 trial += 1
 if trial < SPEEDS.size() * 2:
  start_trial()
 else:
  print("TURN_SUMMARY ", JSON.stringify({"max_error_m": biggest_error, "stable": all_stable}))
  quit(0 if all_stable and biggest_error <= 0.20 else 1)
 return false
