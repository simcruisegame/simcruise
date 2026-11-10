extends SceneTree
## ABS in both directions, for every car in res://cars that has it.
##
##   godot --headless --path game --fixed-fps 60 --script res://checks/AbsReverse.gd
##
## Each car reaches 6 m/s forwards, then (from rest) 6 m/s in reverse, and
## stops with a full service brake. Braking slip is measured in the direction
## of travel, so ABS must keep every loaded wheel turning in reverse as it
## does forwards. Fails when a loaded wheel locks (turns at under 30 % of the
## road speed) for more than 0.15 s while the car still moves faster than
## 2 m/s, or when the reverse stop is more than 15 % longer than the forward one.
const CAR_SPEC=preload("res://CarSpec.gd")
const SPEED=6.
var queue=[];var spec;var car;var pad
var run=0;var state=0;var t=0.;var tick=0;var start=Vector3.ZERO
var locked_s=0.;var worst_lock=0.;var distance=[0.,0.]
var rows=[];var failed=0;var tested=0

func _initialize():
 for c in CAR_SPEC.available():queue.append(c)
 pad=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(5000,1,5000);col.shape=box;pad.position.y=-.5;pad.add_child(col);root.add_child(pad)
 next_car()

func next_car():
 if car:root.remove_child(car);car.free();car=null
 spec=null
 while not queue.is_empty():
  spec=CAR_SPEC.load_id(queue.pop_front())
  if spec.abs_enabled:break
  spec=null
 if spec==null:finish();return
 tested+=1;run=0;worst_lock=0.;start_run()

func start_run():
 if car:root.remove_child(car);car.free();car=null
 car=load("res://Powertrain.gd").new();car.use_spec(spec);car.setup();root.add_child(car);car.restart(Vector3(0,.18,0),0.)
 state=0;t=0.;tick=0;locked_s=0.

func _physics_process(dt):
 if car==null:return false
 tick+=1;t+=dt
 var speed=-car.linear_velocity.dot(car.transform.basis.z)
 if tick>60*60:end_run("timeout in state %d"%state);return false
 match state:
  0:
   car.drive(dt,0.,0.,0.,false)
   if t>1.:state=1;t=0.
  1:
   # Forwards on the accelerator, backwards on the brake key from rest
   # (automatic: it engages reverse), as a player does.
   var push=clampf((SPEED-absf(speed))*.5,.15,1.)
   if run==0:car.drive(dt,push,0.,0.,false)
   else:car.drive(dt,0.,push,0.,false)
   if absf(speed)>=SPEED:state=2;t=0.;start=car.position
   if t>20.:end_run("did not reach %.0f m/s"%SPEED);return false
  2:
   car.drive(dt,0.,0.,0.,true)
   var stuck=false
   for i in 4:
    if car.spring_load[i]>.25*spec.mass_kg*9.80665/4. and absf(car.omega[i]*car.R)<.3*absf(speed):stuck=true
   locked_s=locked_s+dt if stuck and absf(speed)>2. else 0.
   worst_lock=maxf(worst_lock,locked_s)
   if absf(speed)<.05:end_run("");return false
 return false

func end_run(note:String):
 distance[run]=car.position.distance_to(start)
 if note!="":rows.append("%-12s %s: %s"%[spec.id,"forward" if run==0 else "reverse",note]);failed+=1
 run+=1
 if run<2 and note=="":call_deferred("start_run");return
 if note=="":
  var ratio=distance[1]/maxf(distance[0],.01)
  var ok=worst_lock<=.15 and ratio<=1.15
  if not ok:failed+=1
  rows.append("%-12s stop from %.0f m/s: forward %.2f m, reverse %.2f m (%.2fx)  longest lock above 2 m/s %.2f s  %s"%[spec.id,SPEED,distance[0],distance[1],ratio,worst_lock,"PASS" if ok else "FAIL"])
 call_deferred("next_car")

func finish():
 for r in rows:print("RESULT ",r)
 print("ABS REVERSE %s: %d cars with ABS, %d failed"%["PASSED" if failed==0 else "FAILED",tested,failed])
 quit(0 if failed==0 else 1)
