extends SceneTree
## Every car parked on its parking brake must come to rest and stay there.
##
##   godot --headless --path game --fixed-fps 60 --script res://checks/ParkBrakeHold.gd [-- --car=<id>] [--trace]
##
## Each car is set down at its ride height on a flat pad, then on a 10 % grade
## nose up and nose down, and on a 10 % cross slope, with the rear parking
## brake held (the H key, Input action "parking_brake"), the selector in drive
## and the engine idling. A real car parked like this does not move. Gate: from
## 3 s on, the body's speed stays under 0.005 m/s and it moves under 5 mm in
## the following 3 s. Reports the speed at 3 s, the worst speed and the drift.
const CAR_SPEC=preload("res://CarSpec.gd")
const GRADE=.10
const SETTLE_S=3.
const HOLD_S=3.
const MAX_SPEED=.005
const MAX_DRIFT=.005
# name, pitch grade (+ nose up), cross grade (+ right side down), initial push
# (m/s across and half that along the car: the C8 used to start rocking
# side to side at 0.07 m/s once disturbed, and never stopped)
const CASES=[["flat",0.,0.,0.],["nose up 10%",GRADE,0.,0.],["nose down 10%",-GRADE,0.,0.],["cross 10%",0.,GRADE,0.],["flat, pushed",0.,0.,.3]]
var queue=[];var spec;var car;var pad
var case_i=0;var t=0.;var anchor=Vector3.ZERO;var speed_at_settle=0.;var worst=0.
var rows=[];var failed=0;var trace=false

func _initialize():
 var only=""
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--car="):only=a.trim_prefix("--car=")
  if a=="--trace":trace=true
 for c in CAR_SPEC.available():
  if only=="" or c==only:queue.append(c)
 if not InputMap.has_action("parking_brake"):InputMap.add_action("parking_brake")
 Input.action_press("parking_brake")
 next_car()

func next_car():
 if queue.is_empty():finish();return
 spec=CAR_SPEC.load_id(queue.pop_front());case_i=0;start_case()

func start_case():
 if car:root.remove_child(car);car.free();car=null
 if pad:root.remove_child(pad);pad.free();pad=null
 var c=CASES[case_i]
 # The ground: a large box whose top face passes through the origin.
 var tilt=Basis(Vector3.RIGHT,atan(c[1]))*Basis(Vector3.BACK,-atan(c[2]))
 pad=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(400,1,400);col.shape=box;col.position.y=-.5;pad.add_child(col)
 pad.transform=Transform3D(tilt,Vector3.ZERO);root.add_child(pad)
 car=load("res://Powertrain.gd").new();car.use_spec(spec);car.setup();root.add_child(car)
 # Set down at the static ride height (the contact rays start at the mounts):
 # the hubs' height above the road is the free radius, the hubs sit
 # static_length_m below the mounts.
 var ride=spec.static_length_m+car.R-spec.mount_height_m
 car.restart(Vector3.ZERO,0.)
 car.global_transform=Transform3D(tilt,tilt.y*ride)
 car.reset_physics_interpolation()
 car.linear_velocity=tilt*Vector3(c[3],0.,-c[3]*.5)
 t=0.;worst=0.;speed_at_settle=0.

func _physics_process(dt):
 if car==null:return false
 t+=dt
 car.drive(dt,0.,0.,0.,false)
 var v:float=car.linear_velocity.length()
 if trace and (t<.2 or fmod(t,.25)<dt):print("TRACE %s %s t=%.3f v=%s w=%s omega=%s load=%s"%[spec.id,CASES[case_i][0],t,str(car.global_basis.inverse()*car.linear_velocity),str(car.angular_velocity),str(car.omega.map(func(o):return snappedf(o,.001))),str(car.spring_load.map(func(o):return roundi(o)))])
 if t<SETTLE_S:return false
 if t-dt<SETTLE_S:anchor=car.global_position;speed_at_settle=v
 worst=maxf(worst,v)
 if t>=SETTLE_S+HOLD_S:
  var drift=car.global_position.distance_to(anchor)
  var ok=worst<MAX_SPEED and drift<MAX_DRIFT
  if not ok:failed+=1
  rows.append("%-12s %-14s speed at 3 s %.4f m/s, worst 3-6 s %.4f m/s, drift %.1f mm  %s"%[spec.id,CASES[case_i][0],speed_at_settle,worst,drift*1000.,"PASS" if ok else "FAIL"])
  case_i+=1
  if case_i<CASES.size():call_deferred("start_case")
  else:call_deferred("next_car")
  car.set_physics_process(false)
  var c=car;car=null;root.remove_child(c);c.queue_free()
 return false

func finish():
 Input.action_release("parking_brake")
 for r in rows:print("RESULT ",r)
 print("PARK BRAKE HOLD %s: %d cases, %d failed"%["PASSED" if failed==0 else "FAILED",rows.size(),failed])
 quit(0 if failed==0 else 1)
