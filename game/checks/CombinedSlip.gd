extends SceneTree
## Combined slip: braking while cornering, for every car in res://cars,
## compared with main (before the slip-ratio tyre curve).
##
##   godot --headless --path game --fixed-fps 60 --script res://checks/CombinedSlip.gd
##   ... -- --car=nb1999         one car (repeatable)
##   COMBINED_TRACE=1            prints every trial
##
## Each trial holds 20 m/s on a steady turn (ramp steer, then the steering is
## held) at 35 % or 70 % of the car's skidpad target, lifts off and brakes at a
## fixed pedal with the steering still held, as a driver trail-braking into a
## bend does. The pedal is swept from 0.2 to 1.0 in steps of 0.05, until two
## pedals in a row lock a wheel. Measured per trial: the peak 0.25 s mean of the
## total horizontal acceleration (the friction circle the car actually uses)
## while every loaded wheel turns at 85 % or more of the road speed (less than 15 %
## slip: past any tyre's peak, short of locking) and the body slip angle is
## under 15°. A car's combined grip at each turn is the largest of these over
## the sweep: the most a driver can brake while cornering without a wheel
## sliding.
##
## MAIN holds the same numbers measured on main with this file. The check
## fails when a car whose ABS never acted gains more than 3 % combined grip
## over main at either turn: a gain there comes from the tyre parameters alone
## and needs a published measurement behind it (the slip-ratio branch's first
## roadster parameters, peak 1.07 sliding to 0.963, gained 5.3 % here and fail). Cars
## with ABS (abs_enabled, cars that had it) are reported against main but do
## not fail: ABS holds each tyre at its peak slip where main's tyres locked,
## which is what the system is for.
const CAR_SPEC=preload("res://CarSpec.gd")
const SPEED=20.
const TURNS=[.35,.7]
const PEDAL_FROM=.2
const PEDAL_STEP=.05
const ROLLING=.85
const ALLOWED_GAIN=1.03
# Combined grip (g) at each turn, on main fe0f9db (before the slip-ratio
# curve), measured with this file. 0: spun or locked at every pedal.
const MAIN={
 "meridian_z":[.746,.926],
 "mustang65":[.662,.729],
 "nb1999":[.808,.955],
}
var queue=[];var spec;var car;var pad
var turn_i=0;var pedal=0.;var state=0;var t=0.;var tick=0;var locks_in_row=0
var speed_i=0.;var turn=0.;var lat_window=[];var acc_window=[]
var prev_v=Vector3.ZERO;var peak=0.;var locked=false;var spun=false;var abs_acted=false
var best=[0.,0.];var best_pedal=[0.,0.];var first_lock=[-1.,-1.];var car_abs=false;var car_notes=""
var rows=[];var failed=0

func _initialize():
 var args=OS.get_cmdline_user_args()
 for a in args:
  if a.begins_with("--car="):queue.append(a.trim_prefix("--car="))
 if queue.is_empty():queue=CAR_SPEC.available()
 pad=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(5000,1,5000);col.shape=box;pad.position.y=-.5;pad.add_child(col);root.add_child(pad)
 next_car()

func next_car():
 if car:root.remove_child(car);car.free();car=null
 if queue.is_empty():finish();return
 spec=CAR_SPEC.load_id(queue.pop_front())
 turn_i=0;pedal=PEDAL_FROM;locks_in_row=0
 best=[0.,0.];best_pedal=[0.,0.];first_lock=[-1.,-1.];car_abs=false;car_notes=""
 start_trial()

func start_trial():
 if car:root.remove_child(car);car.free();car=null
 car=load("res://Powertrain.gd").new();car.use_spec(spec);car.setup();root.add_child(car);car.restart(Vector3(0,.18,0),0.)
 state=0;t=0.;tick=0;speed_i=0.;turn=0.;lat_window=[];acc_window=[]
 peak=0.;locked=false;spun=false;abs_acted=false;prev_v=Vector3.ZERO

func _physics_process(dt):
 if car==null:return false
 tick+=1;t+=dt
 var v=car.linear_velocity;var fwd=-car.transform.basis.z;var speed=v.dot(fwd)
 var lat_g=absf(speed*car.angular_velocity.y)/9.80665
 var accel=(v-prev_v)/dt;prev_v=v
 var beta=absf(atan2(v.dot(car.transform.basis.x),maxf(absf(speed),1.)))
 if tick>60*120:end_trial("timeout in state %d"%state);return false
 match state:
  0:
   car.drive(dt,0.,0.,0.,false)
   if t>1.5:state=1;t=0.
  1:
   # Accelerate to the test speed and ramp the steering until the car holds
   # the turn's share of its skidpad target.
   speed_i=clampf(speed_i+(SPEED-speed)*dt*.12,0,.75)
   if speed>SPEED-1.:turn+=dt*.03
   car.drive(dt,clampf((SPEED-speed)*.65+speed_i,0,1),0.,turn,false)
   lat_window.append(lat_g);if lat_window.size()>60:lat_window.pop_front()
   var m=0.;for x in lat_window:m+=x
   m/=lat_window.size()
   if speed>SPEED-1. and m>=float(TURNS[turn_i])*spec.target_skidpad_g:state=2;t=0.
   if t>60.:end_trial("could not reach the turn");return false
  2:
   speed_i=clampf(speed_i+(SPEED-speed)*dt*.12,0,.75)
   car.drive(dt,clampf((SPEED-speed)*.65+speed_i,0,1),0.,turn,false)
   if t>1.:state=3;t=0.
  3:
   car.drive(dt,0.,pedal,turn,false)
   # A wheel the turn has nearly unloaded (inside rear, under a quarter of
   # its static load) carries little force either way and is not counted.
   for i in 4:
    if car.spring_load[i]>.25*spec.mass_kg*9.80665/4. and absf(car.omega[i]*car.R)<ROLLING*absf(speed):locked=true
   if "abs_release" in car:
    for r in car.abs_release:
     if float(r)<.999:abs_acted=true
   if beta>deg_to_rad(15.):spun=true
   var horiz=Vector2(accel.x,accel.z).length()/9.80665
   acc_window.append(horiz);if acc_window.size()>15:acc_window.pop_front()
   var m=0.;for x in acc_window:m+=x
   m/=acc_window.size()
   if t>.1 and acc_window.size()==15 and not locked and not spun:peak=maxf(peak,m)
   if t>=1.5 or speed<5. or spun:end_trial("");return false
 return false

func end_trial(note:String):
 if note!="":car_notes+=" (turn %.2f pedal %.2f: %s)"%[TURNS[turn_i],pedal,note]
 if peak>float(best[turn_i]):best[turn_i]=peak;best_pedal[turn_i]=pedal
 if locked and float(first_lock[turn_i])<0.:first_lock[turn_i]=pedal
 car_abs=car_abs or abs_acted
 locks_in_row=locks_in_row+1 if locked else 0
 if OS.has_environment("COMBINED_TRACE"):print("TRIAL %s turn %.2f pedal %.2f peak %.3f g%s%s%s"%[spec.id,TURNS[turn_i],pedal,peak," locked" if locked else ""," spun" if spun else ""," abs" if abs_acted else ""])
 pedal+=PEDAL_STEP
 if pedal<=1.0001 and locks_in_row<2 and note=="":call_deferred("start_trial");return
 turn_i+=1;pedal=PEDAL_FROM;locks_in_row=0
 if turn_i<TURNS.size():call_deferred("start_trial");return
 var ref:Array=MAIN.get(spec.id,[])
 var parts=[];var verdict="REPORT"
 for k in TURNS.size():
  var against="main -"
  if not ref.is_empty() and float(ref[k])<=0.:against="main: spun or locked at every pedal"
  elif not ref.is_empty():
   against="main %.3f g, %+.1f %%"%[float(ref[k]),(float(best[k])/float(ref[k])-1.)*100.]
   if not car_abs and float(best[k])>float(ref[k])*ALLOWED_GAIN:verdict="FAIL"
  parts.append("turn %.2f: %.3f g at pedal %.2f (%s, first lock %s)"%[TURNS[k],best[k],best_pedal[k],against,("%.2f"%first_lock[k]) if float(first_lock[k])>0. else "none"])
 if verdict=="REPORT" and not ref.is_empty() and not car_abs:verdict="PASS"
 if verdict=="FAIL":failed+=1
 rows.append("%-12s %s  ABS %s  %s%s"%[spec.id,"  ".join(parts),"acted" if car_abs else "no",verdict,car_notes])
 call_deferred("next_car")

func finish():
 for r in rows:print("RESULT ",r)
 print("COMBINED SLIP %s: %d cars, %d without ABS gained more than %d %% combined grip over main"%["PASSED" if failed==0 else "FAILED",rows.size(),failed,roundi((ALLOWED_GAIN-1.)*100.)])
 quit(0 if failed==0 else 1)
