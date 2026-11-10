extends SceneTree
## S-bend bench: the transition that spun the scripted driver on the Tail of
## the Dragon (RoadDrive trace at 10.57 km: left curve into right at 17-19 m/s,
## off the throttle, yaw kept building against the steering until the car spun).
## On a flat slab: cruise at SPEED_MPS, lift off, steer one full sine period
## (left then right, SINE_HZ) with the rack positioned directly like a wheel,
## then hold straight. Amplitudes sweep from gentle to near the grip limit.
## Reported per case: peak lateral g, peak sideslip, peak yaw rate, heading
## REPORT_SECONDS after the input ends, and whether the car spun (sideslip past
## 20°). A road car of this kind tracks an S-bend inside its grip without
## spinning; gate: no spin while peak lateral stays under 0.8 g.
##   godot --headless --path game --fixed-fps 60 --script res://checks/SBend.gd -- [--car=nb1999] [--speed=17.9]
const SINE_HZ := .5
const SETTLE_SECONDS := 1.5
const REPORT_SECONDS := 2.5
const AMPLITUDES_DEG := [2., 3., 4., 5., 6., 7.]
var car
var case_i := 0
var state := 0
var t := 0.
var speed_i := 0.
var target := 17.9
var start_yaw := 0.
var peak := {}
var done := false
var passed := true
var rows := []

func _initialize():
 var slab=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(4000,1,8000);col.shape=box;slab.position.y=-.5;slab.add_child(col);root.add_child(slab)
 var car_id := "nb1999"
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--car="):car_id=a.trim_prefix("--car=")
  if a.begins_with("--speed="):target=float(a.trim_prefix("--speed="))
 car=load("res://Powertrain.gd").new()
 if car_id!="nb1999":car.use_spec(load("res://CarSpec.gd").load_id(car_id))
 car.setup();root.add_child(car)
 car.steering_assist=false
 begin()

func begin():
 car.restart(Vector3(0,.18,0),0.)
 state=0;t=0.;speed_i=0.
 peak={"lat":0.,"slip":0.,"yaw":0.,"roll":0.}

func yaw_of(b:Basis)->float:
 var f=-b.z;return atan2(-f.x,-f.z)

func _physics_process(dt):
 if done:return true
 t+=dt
 var b:Basis=car.global_transform.basis;var vel:Vector3=car.linear_velocity;var speed=-vel.dot(b.z)
 var gas:=0.;var turn:=0.
 var amp:float=AMPLITUDES_DEG[case_i]
 if state==0:
  gas=1.
  if speed>=target-.3:state=1;t=0.
 elif state==1:
  speed_i=clampf(speed_i+(target-speed)*dt*.12,0.,.75)
  gas=clampf((target-speed)*.65+speed_i,0.,1.)
  if t>=SETTLE_SECONDS:state=2;t=0.;start_yaw=yaw_of(b)
 elif state==2:
  # Off the throttle, one full sine of rack angle (Direct map: turn = rack / lock).
  turn=sin(TAU*SINE_HZ*t)*deg_to_rad(amp)/car.PARKING_RACK_RAD
  if t>=1./SINE_HZ:state=3;t=0.
 car.drive(dt,gas,0.,turn,false)
 if state>=2:
  var lat=absf(vel.dot(b.x)*0.+car.angular_velocity.y*speed)/9.80665
  var slip=absf(rad_to_deg(atan2(vel.dot(b.x),maxf(.5,absf(speed)))))
  peak.lat=maxf(peak.lat,lat);peak.slip=maxf(peak.slip,slip)
  peak.yaw=maxf(peak.yaw,absf(rad_to_deg(car.angular_velocity.y)))
  peak.roll=maxf(peak.roll,absf(rad_to_deg(asin(clampf(b.x.y,-1.,1.)))))
 if state==3 and t>=REPORT_SECONDS:
  var heading=rad_to_deg(angle_difference(start_yaw,yaw_of(b)))
  var spun:bool=peak.slip>20.
  var ok:bool=not spun or peak.lat>=.8
  passed=passed and ok
  print("SBEND %.1f m/s rack ±%.0f°: peak_lat_g=%.2f peak_sideslip_deg=%.1f peak_yaw_deg_s=%.1f peak_roll_deg=%.1f heading_after_deg=%.1f speed_after=%.1f spun=%s -> %s"%[target,amp,peak.lat,peak.slip,peak.yaw,peak.roll,heading,-car.linear_velocity.dot(car.global_transform.basis.z),str(spun),"PASS" if ok else "FAIL"])
  case_i+=1
  if case_i>=AMPLITUDES_DEG.size():
   done=true;print("SBEND PASSED" if passed else "SBEND FAILED");quit(0 if passed else 1);return true
  begin()
 return false
