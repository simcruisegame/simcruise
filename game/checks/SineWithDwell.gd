extends SceneTree
## FMVSS 126 sine-with-dwell bench (49 CFR 571.126, S7.6 and S7.9) on a flat
## pad, for every car in res://cars.
##   1. Slowly increasing steer at 80 km/h: the road-wheel angle is ramped
##      (1 °/s, about 13.5 °/s at a 13.5:1 steering wheel) while a speed
##      driver holds 80 km/h; A is the angle at which the lateral
##      acceleration first reaches 0.3 g (S7.6), the mean of a left and a
##      right ramp.
##   2. Sine with dwell (S7.9): coasting from 80 km/h, the road wheels follow
##      a 0.7 Hz sine of amplitude k·A; at the second peak they dwell 500 ms
##      and then complete the cycle; k = 1.5, 2.0, ... up to 6.5 (S7.9.3),
##      left-first and right-first.
## The bench positions the rack itself (a steering robot), so the steering
## wheel is not modelled; amplitudes are in road-wheel angle.
## Per run: the first yaw-rate peak after the steer changes sign (S5.2.1),
## the yaw rate 1.00 and 1.75 s after the steer completes as a share of it
## (limits 35 % and 20 %), and the lateral displacement of the centre of
## gravity 1.07 s after the steer begins (S5.2.3; at least 1.83 m from 5A on
## a vehicle of 3,500 kg GVWR or less).
## Gate: every car with stability_control meets S5.2.1-S5.2.3 on every run;
## cars without it are reported.
##
##   godot --headless --path game --fixed-fps 60 --script res://checks/SineWithDwell.gd -- [--car=volta ...] [--trace] [--set=field=value ...]
## --set overrides a car-file field (e.g. --set=stability_control=false to see a car without it).
const CAR_SPEC=preload("res://CarSpec.gd")
const SPEED_MPS := 80./3.6
const RAMP_RAD_S := deg_to_rad(1.)
const FREQ := .7
const DWELL_S := .5
const AFTER_S := 2.5
var queue := []
var car
var spec
var trace := false
var overrides := {}
var state := 0
var t := 0.
var phase := 0            # 0 ramp left, 1 ramp right, 2 sine runs
var ramp_hits := []
var a_ref := 0.
var runs := []            # [k, direction]
var run_i := 0
var speed_i := 0.
var samples := []
var origin := Vector3.ZERO
var heading := Basis()
var rows := []
var failures := 0
var car_failed := false

func _initialize():
 InputMap.add_action("clutch")
 var pad=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(3000,1,3000);col.shape=box;pad.position.y=-.5;pad.add_child(col);root.add_child(pad)
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--car="):queue.append(a.trim_prefix("--car="))
  elif a=="--trace":trace=true
  elif a.begins_with("--set="):
   var kv=a.trim_prefix("--set=").split("=",true,1)
   overrides[kv[0]]=JSON.parse_string(kv[1])
 if queue.is_empty():queue=CAR_SPEC.available()
 next_car()

func next_car():
 if car:root.remove_child(car);car.free();car=null
 if queue.is_empty():finish();return
 spec=CAR_SPEC.load_id(queue.pop_front())
 if not overrides.is_empty():spec.apply(overrides)
 car=load("res://Powertrain.gd").new();car.use_spec(spec);car.setup();root.add_child(car)
 car.steering_assist=false
 phase=0;ramp_hits=[];runs=[];run_i=0;car_failed=false
 var k:=1.5
 while k<=6.5001:runs.append([k,1.]);runs.append([k,-1.]);k+=.5
 start()

func start():
 car.restart(Vector3(0,.15,0),0.)
 state=0;t=0.;speed_i=0.;samples=[]

# Road-wheel angle (rad, + = left) set straight on the rack: the bench is a robot.
func steer_to(angle:float,speed:float):
 var cap:float=car.available_steering(speed,false)
 var turn:=clampf(angle/cap,-1.,1.)
 car.steering_input=turn
 return turn

func _physics_process(dt):
 if car==null:return true
 t+=dt
 var b:Basis=car.global_transform.basis;var vel:Vector3=car.linear_velocity;var speed:float=-vel.dot(b.z)
 var yaw_rate:float=car.angular_velocity.y
 var gas:=0.;var turn:=0.
 match state:
  0:
   if t>=.5:
    car.linear_velocity=-b.z*SPEED_MPS;car.omega.fill(SPEED_MPS/car.R)
    car.gear=road_gear(SPEED_MPS);car.engine_omega=SPEED_MPS/car.R*car.GEARS[car.gear]*car.FINAL_DRIVE;car.rpm=car.engine_omega*60./TAU
    state=1;t=0.
  1:
   # Hold 80 km/h straight for 2 s, then ramp or start the sine.
   speed_i=clampf(speed_i+(SPEED_MPS-speed)*dt*.3,0.,.9)
   gas=clampf((SPEED_MPS-speed)*.65+speed_i,0.,1.)
   turn=steer_to(0.,speed)
   if t>=2.:
    state=2 if phase<2 else 3;t=0.;origin=car.global_position;heading=b
  2:
   # Slowly increasing steer at a held speed.
   speed_i=clampf(speed_i+(SPEED_MPS-speed)*dt*.3,0.,.9)
   gas=clampf((SPEED_MPS-speed)*.65+speed_i,0.,1.)
   var dir:=1. if phase==0 else -1.
   turn=steer_to(dir*RAMP_RAD_S*t,speed)
   var lat:float=absf(speed*yaw_rate)/9.80665
   if lat>=.3 or t>20.:
    ramp_hits.append(RAMP_RAD_S*t)
    phase+=1
    if phase==2:a_ref=(ramp_hits[0]+ramp_hits[1])*.5
    start();return false
  3:
   var run=runs[run_i]
   var amp:float=run[0]*a_ref*run[1]
   var angle:=sine_with_dwell(t)*amp
   turn=steer_to(angle,speed)
   var local:Vector3=heading.inverse()*(car.global_position-origin)
   samples.append([t,yaw_rate,-local.x*run[1],angle])
   if trace and int(round(t/dt))%3==0:print("TRACE k %.1f dir %+d t %.2f v %.2f r %.3f esc %.2f excess %.3f angle %.2f"%[run[0],run[1],t,speed,yaw_rate,car.esc_level,car.esc_excess,rad_to_deg(angle)])
   if t>=end_of_steer()+AFTER_S:report_run();return false
 car.drive(dt,gas,0.,turn,false)
 return false

func road_gear(speed:float)->int:
 for g in car.GEARS.size():
  if speed/car.R*car.GEARS[g]*car.FINAL_DRIVE*60./TAU<4000.:return g
 return car.GEARS.size()-1

func end_of_steer()->float:
 return 1./FREQ+DWELL_S

# Unit steer profile: three quarters of a 0.7 Hz sine, a 500 ms dwell at the
# second peak, then the last quarter back to zero (S7.9).
func sine_with_dwell(time:float)->float:
 var w:=TAU*FREQ;var peak2:=.75/FREQ
 if time<=peak2:return sin(w*time)
 if time<=peak2+DWELL_S:return -1.
 if time<=end_of_steer():return sin(w*(time-DWELL_S))
 return 0.

func report_run():
 var run=runs[run_i]
 var cos_t:=end_of_steer()
 var sign_change:=.5/FREQ
 # First yaw-rate peak after the steer changes sign, in the direction it then has.
 var peak:=0.;var r1:=0.;var r175:=0.;var lateral:=0.
 var want:float=-run[1]
 for s in samples:
  if s[0]>=sign_change and s[0]<=cos_t+.5:peak=maxf(peak,s[1]*want)
  if s[0]<=cos_t+1.:r1=s[1]*want
  if s[0]<=cos_t+1.75:r175=s[1]*want
  if s[0]<=1.07:lateral=s[2]
 # A car that keeps rotating the first way (it spun) never reaches a second
 # peak: that fails, as does any yaw rate left either way at 1.00 or 1.75 s.
 var reversed:=peak>=.05
 var ratio1:=absf(r1)/maxf(peak,1e-4);var ratio175:=absf(r175)/maxf(peak,1e-4)
 var bad:bool=not reversed or ratio1>.35 or ratio175>.20 or (run[0]>=5. and lateral<1.83)
 var gated:bool=spec.stability_control
 if bad and gated:failures+=1;car_failed=true
 rows.append("%-11s %.1fA (%.2f°) %s  first yaw peak %.3f rad/s  1.00 s %5.1f %%  1.75 s %5.1f %%  lateral 1.07 s %.2f m  %s"%[
  spec.id,run[0],rad_to_deg(run[0]*a_ref),"L-R" if run[1]>0 else "R-L",peak,100.*minf(ratio1,9.99),100.*minf(ratio175,9.99),lateral,
  ("FAIL" if bad else "ok")+("" if reversed else " (no second yaw peak: spun)")+("" if gated else " (reported, no ESC)")])
 print("SWD ",rows.back())
 run_i+=1
 if run_i>=runs.size():next_car()
 else:start()

func finish():
 print("")
 for r in rows:print("RESULT ",r)
 print("SINE WITH DWELL %s: %d run(s) on cars with stability control missed FMVSS 126 S5.2"%["PASSED" if failures==0 else "FAILED",failures])
 quit(0 if failures==0 else 1)
