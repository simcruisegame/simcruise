extends SceneTree
## Traction control stability, for every car in res://cars that has it.
##
##   godot --headless --path game --fixed-fps 60 --script res://checks/TractionControlCheck.gd
##   ... -- --car=meridian_z      one car
##   ... -- --all-cars            also cars without traction control (driveline only)
##
## Two runs per car, both at full pedal with traction control doing the work:
##   launch  settle 1.5 s, full throttle from rest for 9 s (through the first
##           shifts and, on the dual clutch, the lock-up in first gear);
##   exit    hold 12 m/s on a steady turn at 60 % of the skidpad target, then
##           full throttle for 4 s with the steering held (a power-on exit).
## Signals: the torque factor traction control leaves (1 - tc_cut), read every
## force substep when Powertrain records them (trace_substeps), else every
## frame; and the driveline torque (clutch or motor torque, the frame mean, as a
## fraction of its largest value in the run, not counted during a gear change).
## A reversal is a swing of at least 0.05 (5 % torque) against the previous
## extreme. Reversals less than 0.25 s apart form a burst. Sustained
## oscillation: a burst of at least 6 reversals (three cycles) lasting 0.5 s or
## more. A settling transient of a few cycles, such as the first wheelspin of a
## launch, is shorter. The check fails on sustained oscillation of either signal.
## TC_TRACE=1 in the environment prints every frame and the reversal times.
const CAR_SPEC=preload("res://CarSpec.gd")
const HYSTERESIS=.05
const BURST_GAP_S=.25
const SUSTAINED_REVERSALS=6
const SUSTAINED_S=.5
var queue=[];var spec;var car;var pad;var all_cars=false
var run=0;var state=0;var t=0.;var tick=0;var speed_i=0.;var turn=0.;var lat_window=[]
var xs=[];var ys=[];var ts=[];var shifting=[];var cut_peak=0.;var cut_time=0.;var v60=0.
var rows=[];var failed=0;var tested=0

func _initialize():
 var args=OS.get_cmdline_user_args()
 for a in args:
  if a.begins_with("--car="):queue.append(a.trim_prefix("--car="))
 all_cars="--all-cars" in args
 if queue.is_empty():queue=CAR_SPEC.available()
 pad=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(5000,1,5000);col.shape=box;pad.position.y=-.5;pad.add_child(col);root.add_child(pad)
 next_car()

func next_car():
 if car:root.remove_child(car);car.free();car=null
 spec=null
 while not queue.is_empty():
  spec=CAR_SPEC.load_id(queue.pop_front())
  if spec.traction_control or all_cars:break
  rows.append("%-12s no traction control"%spec.id);spec=null
 if spec==null:finish();return
 tested+=1;run=0;start_run()

func start_run():
 if car:root.remove_child(car);car.free();car=null
 car=load("res://Powertrain.gd").new();car.use_spec(spec);car.setup();root.add_child(car);car.restart(Vector3(0,.18,0),0.)
 if "trace_substeps" in car:car.trace_substeps=true
 state=0;t=0.;tick=0;speed_i=0.;turn=0.;lat_window=[]
 xs=[];ys=[];ts=[];shifting=[];cut_peak=0.;cut_time=0.;v60=0.

func sample(dt:float):
 var shift:bool=car.shift_time>0.
 # Driveline torque is the frame mean of the substeps: what moves the car.
 if "substep_trace" in car and not car.substep_trace.is_empty():
  var n:int=car.substep_trace.size();var mean=0.
  for k in n:
   var s=car.substep_trace[k]
   mean+=float(s[1])/n;shift=shift or bool(s[2])
   xs.append(1.-float(s[0]));ts.append(t-dt+dt*(k+1)/n)
   if k<n-1:ys.append(null);shifting.append(true)
  ys.append(mean);shifting.append(shift)
 else:
  xs.append(1.-car.tc_cut);ys.append(car.sound_shaft_load);ts.append(t);shifting.append(shift)
 if OS.has_environment("TC_TRACE"):
  var sp=-car.linear_velocity.dot(car.transform.basis.z)
  print("TRACE %s run %d t %.2f v %.2f gear %d rpm %d cut %.2f slip/target %.2f use %s torque %.0f"%[spec.id,run,t,sp,car.gear+1,car.rpm,car.tc_cut,car.tc_slip if "tc_slip" in car else 0.,str(car.grip_use.map(func(u):return snappedf(u,.01))) if "grip_use" in car else "",ys[ys.size()-1] if ys[ys.size()-1]!=null else 0.])
 cut_peak=maxf(cut_peak,car.tc_cut)
 if car.tc_cut>.01:cut_time+=dt

func _physics_process(dt):
 if car==null:return false
 tick+=1;t+=dt
 var v=car.linear_velocity;var speed=-v.dot(car.transform.basis.z)
 var lat_g=absf(speed*car.angular_velocity.y)/9.80665
 if tick>60*120:finish_run("timeout in state %d"%state);return false
 if run==0:
  match state:
   0:
    car.drive(dt,0.,0.,0.,false)
    if t>1.5:state=1;t=0.
   1:
    car.drive(dt,1.,0.,0.,false);sample(dt)
    if v60==0. and speed>=26.8224:v60=t
    if t>=9.:finish_run("");return false
 else:
  match state:
   0:
    car.drive(dt,0.,0.,0.,false)
    if t>1.5:state=1;t=0.
   1:
    speed_i=clampf(speed_i+(12.-speed)*dt*.12,0,.75)
    if speed>11.:turn+=dt*.03
    car.drive(dt,clampf((12.-speed)*.65+speed_i,0,1),0.,turn,false)
    lat_window.append(lat_g);if lat_window.size()>60:lat_window.pop_front()
    var m=0.;for x in lat_window:m+=x
    m/=lat_window.size()
    if speed>11. and m>=.6*spec.target_skidpad_g:state=2;t=0.
    if t>60.:finish_run("could not reach the turn");return false
   2:
    speed_i=clampf(speed_i+(12.-speed)*dt*.12,0,.75)
    car.drive(dt,clampf((12.-speed)*.65+speed_i,0,1),0.,turn,false)
    if t>1.:state=3;t=0.
   3:
    car.drive(dt,1.,0.,turn,false);sample(dt)
    if t>=4.:finish_run("");return false
 return false

# Reversals of a signal with hysteresis; returns the times they happen.
static func reversals(values:Array,times:Array,skip:Array,h:float)->Array:
 var out=[];var dir=0;var extreme=0.;var lo=INF;var hi=-INF
 for k in values.size():
  if skip[k]:continue
  var x:float=values[k]
  if dir==0:
   # No direction yet: wait for the first swing of h either way.
   lo=minf(lo,x);hi=maxf(hi,x)
   if hi-x>=h:dir=-1;extreme=x
   elif x-lo>=h:dir=1;extreme=x
  elif dir==1:
   if x>extreme:extreme=x
   elif extreme-x>=h:out.append(times[k]);dir=-1;extreme=x
  else:
   if x<extreme:extreme=x
   elif x-extreme>=h:out.append(times[k]);dir=1;extreme=x
 return out

# The longest burst of reversals: [count, duration in s].
static func longest_burst(times:Array)->Array:
 var best=[0,0.];var start=0
 for i in times.size():
  if i>0 and float(times[i])-float(times[i-1])>BURST_GAP_S:start=i
  var count=i-start+1;var span=float(times[i])-float(times[start])
  if span>float(best[1]) or (span==float(best[1]) and count>int(best[0])):best=[count,span]
 return best

static func sustained(burst:Array)->bool:
 return int(burst[0])>=SUSTAINED_REVERSALS and float(burst[1])>=SUSTAINED_S

func finish_run(note:String):
 var none=[];for k in xs.size():none.append(false)
 var top=0.
 for y in ys:if y!=null:top=maxf(top,absf(y))
 var yn=[];for y in ys:yn.append(0. if y==null else y/maxf(top,.0001))
 var rx=reversals(xs,ts,none,HYSTERESIS);var ry=reversals(yn,ts,shifting,HYSTERESIS)
 var bx=longest_burst(rx);var by=longest_burst(ry)
 if OS.has_environment("TC_TRACE"):print("REVERSALS x ",rx.map(func(q):return snappedf(q,.01))," y ",ry.map(func(q):return snappedf(q,.01)))
 var ok=not sustained(bx) and not sustained(by) and note==""
 if not ok:failed+=1
 var name="launch" if run==0 else "exit  "
 var extra=("0-60 %.2f s"%v60) if run==0 and v60>0. else ""
 rows.append("%-12s %s  torque factor: %3d reversals, longest burst %2d over %.2f s  driveline: %3d reversals, longest burst %2d over %.2f s  peak cut %.2f  cut active %.2f s  %s %s %s"%[spec.id,name,rx.size(),bx[0],bx[1],ry.size(),by[0],by[1],cut_peak,cut_time,extra,"PASS" if ok else "FAIL",note])
 run+=1
 if run<2:call_deferred("start_run")
 else:call_deferred("next_car")

func finish():
 for r in rows:print("RESULT ",r)
 print("TRACTION CONTROL %s: %d cars, %d runs with sustained oscillation (a burst of %d or more reversals lasting %.1f s or more)"%["PASSED" if failed==0 else "FAILED",tested,failed,SUSTAINED_REVERSALS,SUSTAINED_S])
 quit(0 if failed==0 else 1)
