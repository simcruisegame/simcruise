extends SceneTree
## Calibration bench for any car in res://cars: 0-60 mph, 70-0 mph and the
## 300 ft skidpad, compared with the car's published targets.
##
##   godot --headless --path game --fixed-fps 60 --script res://checks/CarTrack.gd -- --car=kestrel_s
##   ... -- --all          every car in res://cars, one table
##
## The acceleration and braking runs repeat checks/Track.gd's procedure (settle
## 1.5 s, full throttle in automatic, full service brake from 70 mph). Skidpad
## grip is measured by a ramp steer at the speed a 300 ft circle needs for the
## target: steady speed, steering rising 0.6 %/s, peak 1 s mean of lateral
## acceleration before the body slip angle passes 10°. It needs no path
## driver, so the result does not depend on controller tuning; on the NB it
## reads 0.884 g where Track.gd's tuned circle driver demonstrates 0.885 g.
## A car tested 60-0 instead (target_60_0_ft) is measured from the moment the
## same full-pedal stop passes 60 mph. Bands: 0-60 ±0.15 s, stop ±5 ft,
## skidpad ±0.02 g, unless the car file widens one to cover two published
## tests (tolerance_*). Exit 0 when every car
## passes or carries a stated calibration_open note in its file.
const CAR_SPEC=preload("res://CarSpec.gd")
const RADIUS=45.72
var queue=[]
var car
var spec
var tick=0;var state=0;var t=0.;var last=0.
var a60=0.;var b70=0.;var b60=0.;var reached=false;var start_z=0.;var start60_z=NAN
var speed_i=0.
var rows=[];var failed=0;var open_count=0
var ramp_turn=0.;var ramp_peak=0.;var ramp_window=[];var ramp_speed=0.
var pad

func _initialize():
 var args=OS.get_cmdline_user_args()
 for a in args:
  if a.begins_with("--car="):queue.append(a.trim_prefix("--car="))
 if "--all" in args:queue=CAR_SPEC.available()
 if queue.is_empty():queue=["nb1999"]
 pad=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(2500,1,5000);col.shape=box;pad.position.y=-.5;pad.add_child(col);root.add_child(pad)
 next_car()

func next_car():
 if car:root.remove_child(car);car.free();car=null
 if queue.is_empty():finish();return
 spec=CAR_SPEC.load_id(queue.pop_front())
 car=load("res://Powertrain.gd").new();car.use_spec(spec);car.setup();root.add_child(car);car.restart(Vector3(0,.18,0),0.)
 tick=0;state=0;t=0.;last=0.;a60=0.;b70=0.;b60=0.;start60_z=NAN;reached=false
 speed_i=0.

func _physics_process(dt):
 if car==null:return false
 tick+=1;t+=dt
 if tick>60*600:record("timeout in state %d"%state);return false
 var v=car.linear_velocity;var speed=-v.dot(car.transform.basis.z)
 match state:
  0:
   car.drive(dt,0.,0.,0.,false)
   if tick==90:t=0;state=1
  1:
   car.drive(dt,1.,0.,0.,false)
   if OS.has_environment("CARTRACK_TRACE") and tick%15==0 and not reached:print("TRACE t %.2f v %.1f mph gear %d rpm %d slip %.2f contacts %d front %.0f rear %.0f N"%[t,speed*2.23694,car.gear+1,car.rpm,(car.omega[2]*car.R-speed)/maxf(speed,1.),car.wheel_contact,car.spring_load[0]+car.spring_load[1],car.spring_load[2]+car.spring_load[3]])
   if speed>=26.8224 and not reached:a60=t-dt+(26.8224-last)/maxf(.0001,speed-last)*dt;reached=true
   if speed>=31.2928:start_z=car.position.z+(speed-31.2928)*dt/2;state=2;t=0
   if t>40:a60=INF;start_z=car.position.z;state=2;t=0
  2:
   car.drive(dt,0.,1.,0.,false)
   # 60-0 inside the same stop: the brakes act at once, so passing 60 mph
   # under full pedal is where a 60-0 test's stop begins.
   if is_nan(start60_z) and speed<=26.8224:start60_z=car.position.z-(26.8224-speed)*dt/2
   if speed<.1:
    b70=absf(start_z-car.position.z)*3.28084
    b60=absf(start60_z-car.position.z)*3.28084
    state=3;car.restart(Vector3(0,.18,0),0.);t=0
  3:
   # (also re-entered to restart the fine skidpad pass from rest)
   car.drive(dt,0.,0.,0.,false)
   if t>=1.:
    state=5;t=0;ramp_turn=0.;ramp_peak=0.;ramp_window=[];speed_i=0.
    ramp_speed=sqrt(spec.target_skidpad_g*9.80665*RADIUS)
  5:
   # Ramp steer at the skidpad speed: steering grows 0.6 %/s after 6 s at speed;
   # the peak 1 s mean of lateral acceleration (v·yaw rate) before the slip
   # angle at the rear passes 10° is the steady limit.
   speed_i=clampf(speed_i+(ramp_speed-speed)*dt*.12,0,.75)
   if t>6.:ramp_turn+=dt*.006
   car.drive(dt,clampf((ramp_speed-speed)*.65+speed_i,0,1),0.,ramp_turn,false)
   var lat_g=absf(speed*car.angular_velocity.y)/9.80665
   ramp_window.append(lat_g);if ramp_window.size()>60:ramp_window.pop_front()
   var mean=0.;for x in ramp_window:mean+=x
   mean/=ramp_window.size()
   var beta=absf(atan2(car.linear_velocity.dot(car.transform.basis.x),maxf(absf(speed),1.)))
   if t>6. and absf(speed-ramp_speed)<.6:ramp_peak=maxf(ramp_peak,mean)
   if beta>deg_to_rad(10.) or t>120. or ramp_turn>=1.:record("");return false
 last=speed
 return false

func record(note:String):
 var sixty:bool=spec.target_60_0_ft>0.
 var stop:float=b60 if sixty else b70
 var stop_target:float=spec.target_60_0_ft if sixty else spec.target_70_0_ft
 var ok=absf(a60-spec.target_0_60_s)<=spec.tolerance_0_60_s+1e-6 and absf(stop-stop_target)<=spec.tolerance_stop_ft and absf(ramp_peak-spec.target_skidpad_g)<=spec.tolerance_skidpad_g+1e-6
 if not ok and spec.calibration_open=="":failed+=1
 if not ok and spec.calibration_open!="":note+=" (open: %s)"%spec.calibration_open;open_count+=1
 rows.append("%-14s 0-60 %5.2f s (target %5.2f)  %s %6.1f ft (target %5.1f)  skidpad %.3f g (target %.2f)  %s %s"%[spec.id,a60,spec.target_0_60_s,"60-0" if sixty else "70-0",stop,stop_target,ramp_peak,spec.target_skidpad_g,"PASS" if ok else "OFF",note])
 call_deferred("next_car")

func finish():
 for r in rows:print("RESULT ",r)
 print("CARTRACK %d of %d cars inside their bands, %d with a stated open item, %d failed"%[rows.size()-failed-open_count,rows.size(),open_count,failed])
 quit(0 if failed==0 else 1)
