extends SceneTree
## The cabin steering wheel of every car turns like the real one.
##
##   godot --headless --path game --fixed-fps 60 --script res://checks/SteeringWheelCheck.gd [-- --car=<id>]
##
## Builds each car's cabin wheel the way the game does (Cockpit.gd for the 1999
## roadster; for the others the model's "steering_wheel" node, which
## ModelRig.gd splits out of a downloaded model) and sweeps the steering at a
## standstill from the centre to full left lock, to full right lock and back.
## Every tick the wheel node's actual rotation, accumulated tick by tick about
## its column, must equal the front wheels' mean angle times the car's
## steering_ratio within 0.5°. Also gated: the node exists; it is a wheel in
## front of the driver (rim radius 0.12-0.24 m, centred on its pivot within
## 3 cm, within 0.3 m sideways of the driver's eye, 0.15-0.95 m ahead of it and
## below it); its column leans back toward the driver 5-50° from horizontal;
## a left turn moves the top of the rim to the left; and the wheel turns
## only about its column (the axis moves under 0.1° through the sweep).
const CAR_SPEC := preload("res://CarSpec.gd")
const CAR_BODY := preload("res://CarBody.gd")
var queue := [];var rows := [];var failed := 0

func _initialize():
 var only := ""
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--car="):only = a.trim_prefix("--car=")
 for id in CAR_SPEC.available():
  if only == "" or id == only:queue.append(id)
 var pad := StaticBody3D.new();var col := CollisionShape3D.new();var box := BoxShape3D.new();box.size = Vector3(200, 1, 200);col.shape = box;col.position.y = -.5;pad.add_child(col);root.add_child(pad)
 run.call_deferred()

func fail(id: String, why: String) -> void:
 rows.append("%-12s FAIL %s" % [id, why]);failed += 1

func run():
 for id in queue:
  var spec = CAR_SPEC.load_id(id)
  var car = load("res://Powertrain.gd").new();car.use_spec(spec);car.setup();root.add_child(car);car.restart(Vector3(0, .2, 0), 0.)
  var wheel: Node3D = null
  var cockpit = null
  var model: Node3D = null
  if spec.model_scene == "":
   cockpit = load("res://Cockpit.gd").new();car.add_child(cockpit)
   wheel = cockpit.wheel
  else:
   if spec.model_rig.get("fit", false):
    var rig = load("res://ModelRig.gd").new()
    model = rig.build(spec.model_scene, spec, spec.model_rig)
   else:
    model = load(spec.model_scene).instantiate()
   if model:car.add_child(model);wheel = model.find_child("steering_wheel", true, false)
  if wheel == null:
   fail(id, "no steering wheel node in %s" % (spec.model_scene if spec.model_scene != "" else "the cockpit"))
   car.queue_free();await process_frame;continue
  if cockpit:cockpit.turn_wheel(car)   # the cockpit sets the column's lean on the first turn
  var entry: Array = [wheel, wheel.transform.basis, 1.] if cockpit else CAR_BODY.steering_wheel_entry(wheel, car)
  for i in 30:
   car.drive(1. / 60., 0., 0., 0., false);await physics_frame   # settle on the pad
  var to_car := func() -> Transform3D:
   var t := Transform3D.IDENTITY;var n: Node = wheel
   while n != null and n != car:
    if n is Node3D:t = (n as Node3D).transform * t
    n = n.get_parent()
   return t
  # Geometry at rest, in the wheel's own frame and the car's.
  var rest: Transform3D = to_car.call()
  var mesh_box := AABB();var first := true
  for mi in wheel.find_children("*", "MeshInstance3D", true, false):
   var t := Transform3D.IDENTITY;var n: Node = mi
   while n != wheel:t = (n as Node3D).transform * t;n = n.get_parent()
   var b: AABB = t * mi.mesh.get_aabb()
   mesh_box = b if first else mesh_box.merge(b);first = false
  if wheel is MeshInstance3D:mesh_box = wheel.mesh.get_aabb() if first else mesh_box.merge(wheel.mesh.get_aabb())
  var radius := maxf(mesh_box.size.x, mesh_box.size.y) * .5
  var hub_off := Vector2(mesh_box.get_center().x, mesh_box.get_center().y).length()
  var column: Vector3 = rest.basis.z.normalized() * float(entry[2])   # toward the driver
  var tilt := rad_to_deg(asin(clampf(column.y, -1., 1.)))
  var centre: Vector3 = rest * mesh_box.get_center()
  var eye: Vector3 = spec.driver_eye
  var problems := []
  if radius < .12 or radius > .24:problems.append("rim radius %.3f m" % radius)
  if hub_off > .03:problems.append("rim %.3f m off its pivot" % hub_off)
  if absf(rest.origin.x - eye.x) > .3:problems.append("%.2f m sideways of the eye" % (rest.origin.x - eye.x))
  var ahead := eye.z - rest.origin.z
  if ahead < .15 or ahead > .95:problems.append("%.2f m ahead of the eye" % ahead)
  if rest.origin.y > eye.y:problems.append("above the eye")
  if column.z <= 0. or tilt < 5. or tilt > 50.:problems.append("column %s (%.0f° from horizontal)" % [column.snapped(Vector3.ONE * .01), tilt])
  # The sweep: centre, full left, full right, centre (Direct steering, standstill).
  var dt := 1. / 60.
  var plan := [[0., .3], [1., 1.], [-1., 1.6], [0., 1.]]
  var turned := 0.;var worst := 0.;var drift := 0.;var top_ok := true;var tested_top := false
  var prev: Basis = wheel.transform.basis
  var max_left := 0.;var max_right := 0.
  for step in plan:
   for k in int(step[1] / dt):
    car.drive(dt, 0., 0., step[0], false);await physics_frame
    var road: float = (car.wheel_heading[0] + car.wheel_heading[1]) * .5
    if cockpit:cockpit.turn_wheel(car)
    else:CAR_BODY.turn_steering_wheel(entry, road, spec.steering_ratio)
    var now: Basis = wheel.transform.basis
    var d: Basis = prev.inverse() * now
    turned += atan2(d.x.y, d.x.x);prev = now
    var expected: float = road * spec.steering_ratio
    # turned is about the node's local +Z; expected is about the column toward the driver.
    worst = maxf(worst, absf(turned * float(entry[2]) - expected))
    max_left = maxf(max_left, expected);max_right = minf(max_right, expected)
    var t: Transform3D = to_car.call()
    drift = maxf(drift, t.basis.z.normalized().angle_to(rest.basis.z.normalized()))
    if not tested_top and road > deg_to_rad(3.):
     tested_top = true
     top_ok = (t.basis * Vector3.UP).x < (rest.basis * Vector3.UP).x
  if rad_to_deg(worst) > .5:problems.append("turned %.2f° away from ratio × road-wheel angle" % rad_to_deg(worst))
  if not tested_top or not top_ok:problems.append("a left turn does not move the top of the rim left")
  if rad_to_deg(drift) > .1:problems.append("the column axis moved %.2f° while turning" % rad_to_deg(drift))
  var line := "%-12s %s: rim radius %.3f m, column %.0f° from horizontal, %.2f m ahead of the eye; ratio %.2f, lock %.0f° left / %.0f° right (%.2f turns lock to lock), worst error %.3f°" % [id, "cockpit" if cockpit else ("fitted model" if spec.model_rig.get("fit", false) else "generated model"), radius, tilt, ahead, spec.steering_ratio, rad_to_deg(max_left), rad_to_deg(-max_right), rad_to_deg(max_left - max_right) / 360., rad_to_deg(worst)]
  if problems.is_empty():rows.append(line + "  PASS")
  else:rows.append(line + "  FAIL: " + "; ".join(problems));failed += 1
  car.queue_free()
  await process_frame
 for r in rows:print("RESULT ", r)
 print("STEERING WHEEL %s: %d cars, %d failed" % ["PASSED" if failed == 0 else "FAILED", queue.size(), failed])
 quit(0 if failed == 0 else 1)
