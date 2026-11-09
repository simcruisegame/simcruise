extends SceneTree

const VehicleSpec = preload("res://NB1999Sport.gd")
const REAR_RATIO = 0.77
const LBF_TO_NEWTON = 4.4482216153
const INCH_TO_METRE = 0.0254

func _initialize():
 var static_load := VehicleSpec.contact_load(1, VehicleSpec.STATIC_LENGTH_M, 0.0, 1043.0)
 if absf(static_load - 0.485 * 1043.0 * 9.80665 * 0.5) > 0.001:
  push_error("Rear damper changed static support");quit(1);return
 # Independent readings from the published graph, plus near-zero, interpolation
 # and endpoint continuity checks. Values here are shaft in/s and shaft lbf.
 var cases = [
  Vector3(0.0001, 0.0042, 0.0073), Vector3(1.0, 42, 73), Vector3(1.1, 45, 80),
  Vector3(2.0, 56, 127), Vector3(3.0, 66, 146), Vector3(4.0, 72, 160),
  Vector3(5.0, 79.5, 171.5), Vector3(6.0, 87, 183), Vector3(8.0, 100, 206),
  Vector3(9.9799, 112, 224), Vector3(9.98, 112, 224), Vector3(9.9801, 112, 224),
  Vector3(14.0, 112, 224),
 ]
 for reading: Vector3 in cases:
  var wheel_speed := reading.x * INCH_TO_METRE / REAR_RATIO
  for direction: int in [-1, 1]:
   var expected := (reading.y if direction < 0 else -reading.z) * LBF_TO_NEWTON * REAR_RATIO
   var load := VehicleSpec.contact_load(1, VehicleSpec.STATIC_LENGTH_M, direction * wheel_speed, 1043.0)
   if absf((load-static_load) - expected) > 0.005:
    push_error("Rear damper at %s in/s, direction %s: got %.4f N, expected %.4f N" % [reading.x, direction, load-static_load, expected])
    quit(1);return
 print("REAR HARD S DAMPER PASSED: static load and ", cases.size()*2, " signed wheel-load checks.")
 quit()
