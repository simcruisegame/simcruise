extends SceneTree
## Headless: drives the Cockpit driver head model with synthetic car motion at
## 60 Hz and checks the constants it promises: the eye settles opposite to the
## felt acceleration (3 cm/g lateral, 2 cm/g longitudinal, capped at 3 and
## 2 cm) without overshoot, braking dips the gaze, the gaze leads 4° into a
## corner at speed, the view keeps 70 % of the body roll, and head_motion 0 is
## an exact weld to the chassis. Prints once, then quits.
##   godot --headless --path game --script res://checks/HeadCameraCheck.gd
const G := 9.80665
var failures := []

class FakeCar:
 var linear_velocity := Vector3.ZERO
 var steering := 0.0
 var connected_wheels := []
 func _init():
  for i in 4:
   var wheel := Node3D.new();wheel.position = Vector3(0.,.305,0.);connected_wheels.append(wheel)
 func release():
  for wheel in connected_wheels:wheel.free()

func expect(ok: bool, message: String):
 if not ok:failures.append(message)

# Integrate a constant world acceleration for a number of seconds.
func run(cockpit, car: FakeCar, body: Transform3D, seconds: float, accel: Vector3, speed: float) -> Dictionary:
 var dt := 1./60.;var peak_x := 0.;var view := [];var settle := -1.
 var steady: float = clampf(-(body.basis.inverse()*accel).x/G*cockpit.HEAD_LATERAL_M_PER_G,-cockpit.HEAD_LATERAL_MAX_M,cockpit.HEAD_LATERAL_MAX_M)
 for i in int(seconds*60.):
  car.linear_velocity += accel*dt
  view = cockpit.head_view(body,car,speed,dt)
  peak_x = maxf(peak_x,absf(cockpit.head_offset.x))
  if settle < 0. and absf(steady) > 0. and absf(cockpit.head_offset.x-steady) < .02*absf(steady):settle = (i+1)*dt
 return {"view":view,"peak_x":peak_x,"settle_s":settle}

func _initialize():
 var cockpit = preload("res://Cockpit.gd").new()
 var car := FakeCar.new()
 var body := Transform3D.IDENTITY
 # 1. Cruising straight: nothing moves the eye off EYE.
 car.linear_velocity = Vector3(0.,0.,-20.)
 var cruise := run(cockpit,car,body,2.,Vector3.ZERO,20.)
 var eye: Vector3 = cruise.view[0]
 expect(eye.distance_to(cockpit.EYE) < .0015,"cruise eye drifted %.4f m"%eye.distance_to(cockpit.EYE))
 # 2. A right-hand corner at 0.5 g: eye 1.5 cm to the left, no overshoot, gaze 4° right.
 var corner := run(cockpit,car,body,3.,Vector3(.5*G,0.,0.),20.)
 var steady: float = -.5*cockpit.HEAD_LATERAL_M_PER_G
 expect(absf(cockpit.head_offset.x-steady) < .0012,"lateral steady offset %.4f m, expected %.4f"%[cockpit.head_offset.x,steady])
 expect(corner.peak_x <= absf(steady)*1.02+.0005,"lateral overshoot: peak %.4f m against steady %.4f"%[corner.peak_x,absf(steady)])
 expect(absf(cockpit.gaze_deg-cockpit.GAZE_LEAD_DEG) < .25,"gaze lead %.2f°, expected %.2f"%[cockpit.gaze_deg,cockpit.GAZE_LEAD_DEG])
 var direction: Vector3 = (corner.view[1]-corner.view[0]).normalized()
 expect(direction.x > .05,"gaze did not turn right into a right-hand corner")
 print("HEADCAM corner 0.5 g: eye x=%.4f m (steady %.4f), overshoot peak %.4f, settled 2%% at %.2f s, gaze %.2f°"%[cockpit.head_offset.x,steady,corner.peak_x,corner.settle_s,cockpit.gaze_deg])
 # 3. Two g sideways stays at the 3 cm cap.
 cockpit.reset_head();car.linear_velocity = Vector3(0.,0.,-20.)
 run(cockpit,car,body,2.,Vector3(2.*G,0.,0.),20.)
 expect(absf(cockpit.head_offset.x+cockpit.HEAD_LATERAL_MAX_M) < .0005,"lateral cap %.4f m"%cockpit.head_offset.x)
 # 4. Braking at 0.8 g: eye 1.6 cm forward, gaze 1.2° down.
 cockpit.reset_head();car.linear_velocity = Vector3(0.,0.,-25.)
 var braking := run(cockpit,car,body,2.,Vector3(0.,0.,.8*G),25.)
 expect(absf(cockpit.head_offset.z+.8*cockpit.HEAD_LONGITUDINAL_M_PER_G) < .001,"braking offset %.4f m"%cockpit.head_offset.z)
 expect(absf(cockpit.brake_pitch_deg-.8*cockpit.BRAKE_PITCH_DEG) < .1,"brake pitch %.2f°"%cockpit.brake_pitch_deg)
 var brake_dir: Vector3 = (braking.view[1]-braking.view[0]).normalized()
 var level_dir: Vector3 = (cockpit.SIGHT-cockpit.EYE).normalized()
 expect(brake_dir.y < level_dir.y,"braking did not dip the gaze")
 print("HEADCAM braking 0.8 g: eye z=%.4f m, pitch %.2f°"%[cockpit.head_offset.z,cockpit.brake_pitch_deg])
 # 5. Body rolled 10°: the view up vector keeps 70 % of it.
 cockpit.reset_head();car.linear_velocity = Vector3(0.,0.,-20.)
 var rolled := Transform3D(Basis(Vector3(0.,0.,-1.),deg_to_rad(10.)),Vector3.ZERO)
 var roll_view: Array = run(cockpit,car,rolled,1.,Vector3.ZERO,20.).view
 var up_angle := rad_to_deg((roll_view[2] as Vector3).angle_to(Vector3.UP))
 expect(absf(up_angle-10.*cockpit.HORIZON_FOLLOW) < .3,"horizon follow %.2f°, expected %.2f"%[up_angle,10.*cockpit.HORIZON_FOLLOW])
 print("HEADCAM roll 10°: view up %.2f° from vertical"%up_angle)
 # 6. head_motion 0 welds the camera to the chassis exactly.
 cockpit.head_motion = 0.
 car.linear_velocity = Vector3(0.,0.,-20.)
 var welded: Array = run(cockpit,car,body,.5,Vector3(.5*G,0.,0.),20.).view
 expect(welded[0].distance_to(body*cockpit.EYE) < 1e-6 and welded[1].distance_to(body*cockpit.SIGHT) < 1e-5,"head_motion 0 is not a weld")
 car.release();cockpit.free()
 if failures.is_empty():print("HEADCAM PASSED")
 else:
  for f in failures:print("HEADCAM FAILED ",f)
 quit(0 if failures.is_empty() else 1)
