extends SceneTree
## Understeer gradient of every car in res://cars, measured as a vehicle
## dynamics engineer measures it (SAE J266 constant-speed method) and as a
## production ESC is calibrated: on a flat pad a speed driver holds 15, 25 and
## 35 m/s while the road wheels turn slowly (0.5 °/s, no steering assist); at
## each 0.1 g step of lateral acceleration the bench records the road-wheel
## angle δ and the yaw rate r, and K = (δ − L·r/v) / (a_y/g) in degrees per g.
## Gate: on a car with stability control, its esc_understeer_deg_per_g (the
## understeer gradient of the ESC's yaw-rate reference) must not exceed the
## smallest K measured from 0.2 to 0.5 g at any speed; a reference that
## understeers more than the car would read every steady turn as oversteer.
## Cars without stability control are reported.
##
##   godot --headless --path game --fixed-fps 60 --script res://checks/UndersteerGradient.gd -- [--car=volta ...]
const CAR_SPEC=preload("res://CarSpec.gd")
const SPEEDS:=[15.,25.,35.]
const RAMP_RAD_S:=deg_to_rad(.5)
var queue:=[]
var car
var spec
var speed_i:=0
var t:=0.
var state:=0
var hold:=0.
var target:=0.
var marks:=[]
var rows:=[]
var lowest:=INF
var failures:=0

func _initialize():
 InputMap.add_action("clutch")
 var pad=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(6000,1,6000);col.shape=box;pad.position.y=-.5;pad.add_child(col);root.add_child(pad)
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--car="):queue.append(a.trim_prefix("--car="))
 if queue.is_empty():queue=CAR_SPEC.available()
 next_car()

func next_car():
 if car:root.remove_child(car);car.free();car=null
 if queue.is_empty():finish();return
 spec=CAR_SPEC.load_id(queue.pop_front())
 car=load("res://Powertrain.gd").new();car.use_spec(spec);car.setup();root.add_child(car)
 car.steering_assist=false
 speed_i=0;lowest=INF
 start()

func start():
 target=SPEEDS[speed_i]
 car.restart(Vector3(0,.15,0),0.)
 state=0;t=0.;hold=0.;marks=[.1,.2,.3,.4,.5,.6,.7]

func _physics_process(dt):
 if car==null:return true
 t+=dt
 var b:Basis=car.global_transform.basis;var v:float=-car.linear_velocity.dot(b.z);var r:float=car.angular_velocity.y
 var gas:=0.;var turn:=0.
 if state==0:
  if t>=.5:car.linear_velocity=-b.z*target;car.omega.fill(target/car.R);state=1;t=0.
 else:
  hold=clampf(hold+(target-v)*dt*.3,0.,.9);gas=clampf((target-v)*.65+hold,0.,1.)
  turn=RAMP_RAD_S*maxf(t-1.,0.)/car.available_steering(v,false)
  var lat:float=absf(v*r)/9.80665
  if not marks.is_empty() and lat>=marks[0]:
   var k:float=rad_to_deg(car.steering-car.WHEELBASE_M*absf(r)/v)/lat
   rows.append("%-11s %4.1f m/s  %.2f g  road wheel %.3f°  K %.2f °/g"%[spec.id,v,lat,rad_to_deg(car.steering),k])
   if marks[0]>=.199 and marks[0]<=.501:lowest=minf(lowest,k)
   marks.pop_front()
  if marks.is_empty() or t>60. or absf(turn)>=1.:
   speed_i+=1
   if speed_i<SPEEDS.size():start();return false
   report();next_car();return false
 car.drive(dt,gas,0.,turn,false)
 return false

func report():
 var set_to:float=spec.esc_understeer_deg_per_g
 var bad:bool=spec.stability_control and set_to>lowest
 if bad:failures+=1
 rows.append("%-11s lowest K 0.2-0.5 g %.2f °/g, ESC reference %.2f °/g  %s"%[spec.id,lowest,set_to,("FAIL" if bad else "ok") if spec.stability_control else "(no stability control)"])
 print("US ",rows.back())

func finish():
 print("")
 for r in rows:print("RESULT ",r)
 print("UNDERSTEER GRADIENT %s: %d car(s) whose ESC reference understeers more than the car"%["PASSED" if failures==0 else "FAILED",failures])
 quit(0 if failures==0 else 1)
