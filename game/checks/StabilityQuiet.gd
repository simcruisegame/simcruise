extends SceneTree
## Stability control must stay out of ordinary driving. A production ESC does
## not brake a wheel or cut power while the car follows its steering; this
## bench drives every car with stability_control on a flat pad through:
##   weave   a straight-line weave: the road wheels follow a sine about
##           straight ahead (0.2-2 deg at 20 m/s, 0.2-0.5 deg at 30 m/s, 0.5
##           and 1 Hz) while a speed driver holds the speed: lane keeping and
##           the left-right transitions in which yaw lags the steering;
##   taps    keyboard lane keeping: alternate left and right key taps of 0.1 s
##           every second on the assisted keyboard steering, at 25 m/s;
##   road    a steady 0.4 g road drive: a pure-pursuit driver at constant speed
##           follows a course of straights, clothoid transitions, left and right
##           arcs and a direct S-bend whose arcs need 0.4 g (radius 60 m at
##           15.3 m/s and 150 m at 24.3 m/s).
## Gate: no stability-control activity at all (any propulsion cut or ESC brake
## torque on any tick) on a car that has it, and the road driver stays within
## 2.5 m of its line. Each row reports the peak excess over the allowed yaw rate
## (negative: margin left inside the deadband), peak lateral acceleration,
## peak body sideslip, and the peaks of the ESC's sideslip estimate and of the
## heading it has gained over the reference ("overturn"; 1.7° marks a
## slide). Cars without stability control are listed, not run.
##
##   godot --headless --path game --fixed-fps 60 --script res://checks/StabilityQuiet.gd -- [--car=volta ...] [--trace] [--set=field=value ...]
## --trace prints the yaw rate, the lagged reference, a_y/v, the sideslip estimate and the ESC state every 3 ticks.
const CAR_SPEC=preload("res://CarSpec.gd")
const G:=9.80665
# [kind, speed m/s, amplitude deg (weave), frequency Hz (weave) or radius m (road)]
const CASES:=[["weave",20.,.2,.5],["weave",20.,.5,.5],["weave",20.,1.,.5],["weave",20.,2.,.5],["weave",20.,.5,1.],
 ["weave",30.,.2,.5],["weave",30.,.5,.5],["weave",30.,.5,1.],["taps",25.,0.,0.],["road",0.,0.,60.],["road",0.,0.,150.]]
const ROAD_G:=.4
var queue:=[]
var overrides:={}
var car
var spec
var case_i:=0
var state:=0
var t:=0.
var speed_i:=0.
var target:=0.
var route:=PackedVector3Array()
var station:=0
var active_ticks:=0
var events:=0
var was_active:=false
var peak_excess:=-1.
var peak_lat:=0.
var peak_beta:=0.
var peak_estimate:=0.
var peak_overturn:=0.
var peak_cross:=0.
var peak_brake:=0.
var rows:=[]
var failures:=0
var trace:=false

func _initialize():
 InputMap.add_action("clutch")
 var pad=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(4000,1,4000);col.shape=box;pad.position.y=-.5;pad.add_child(col);root.add_child(pad)
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
 spec=null
 while not queue.is_empty():
  spec=CAR_SPEC.load_id(queue.pop_front())
  if not overrides.is_empty():spec.apply(overrides)
  if spec.stability_control:break
  rows.append("%-11s no stability control (not run)"%spec.id);spec=null
 if spec==null:finish();return
 car=load("res://Powertrain.gd").new();car.use_spec(spec);car.setup();root.add_child(car)
 case_i=0
 start()

func start():
 var c=CASES[case_i]
 car.steering_assist=c[0]=="taps"
 if c[0]=="road":
  target=sqrt(ROAD_G*G*float(c[3]))
  build_route(float(c[3]))
 else:target=float(c[1])
 car.restart(Vector3(0,.15,0),0.)
 state=0;t=0.;speed_i=0.;station=0
 active_ticks=0;events=0;was_active=false;peak_excess=-1.;peak_lat=0.;peak_beta=0.;peak_estimate=0.;peak_overturn=0.;peak_cross=0.;peak_brake=0.

# Course for a road case: integrate a piecewise-linear curvature profile at
# 1 m steps (clothoid transitions between straights and arcs). Points are
# (x, station, z) as RoadDrive.gd keeps them; the car starts at the origin
# heading -Z.
func build_route(radius:float):
 var k:=1./radius
 var arc:=radius*deg_to_rad(70.)
 # [length m, curvature at the end]; curvature runs linearly along each piece.
 var plan:=[[80.,0.],[40.,k],[arc,k],[40.,0.],[60.,0.],[40.,-k],[arc,-k],[40.,0.],[60.,0.],
  [40.,k],[arc*.6,k],[60.,-k],[arc*.6,-k],[40.,0.],[120.,0.]]
 route=PackedVector3Array()
 var x:=0.;var z:=0.;var heading:=0.;var kappa:=0.;var s:=0.
 route.append(Vector3(x,0.,z))
 for piece in plan:
  var n:=int(ceil(float(piece[0])))
  var k0:=kappa
  for j in n:
   kappa=lerpf(k0,float(piece[1]),float(j+1)/n)
   heading+=kappa
   # Heading 0 is -Z; a positive curvature turns left (toward -X).
   x-=sin(heading);z-=cos(heading);s+=1.
   route.append(Vector3(x,s,z))

func _physics_process(dt):
 if car==null:return true
 t+=dt
 var c=CASES[case_i]
 var b:Basis=car.global_transform.basis;var vel:Vector3=car.linear_velocity;var speed:float=-vel.dot(b.z)
 var gas:=0.;var turn:=0.
 match state:
  0:
   if t>=.5:
    car.linear_velocity=-b.z*target;car.omega.fill(target/car.R)
    car.gear=road_gear(target);car.engine_omega=target/car.R*car.GEARS[car.gear]*car.FINAL_DRIVE;car.rpm=car.engine_omega*60./TAU
    state=1;t=0.
  1:
   speed_i=clampf(speed_i+(target-speed)*dt*.3,0.,.9)
   gas=clampf((target-speed)*.65+speed_i,0.,1.)
   var done:=false
   match c[0]:
    "weave":
     var angle:=deg_to_rad(float(c[2]))*sin(TAU*float(c[3])*maxf(t-1.,0.))
     turn=clampf(angle/car.available_steering(speed,false),-1.,1.)
     done=t>=1.+3./float(c[3])+1.
    "taps":
     # Left at 1.0 s, right at 2.0 s, ... for 0.1 s each.
     var phase:=fmod(t,1.)
     if t>=1. and phase<.1:turn=-1. if int(t)%2==1 else 1.
     done=t>=13.
    "road":
     var r:=follow(b,speed)
     turn=r.x
     peak_cross=maxf(peak_cross,r.y)
     done=station>=route.size()-40 or t>200.
   observe(b,vel,speed,t>=.5)
   if trace and int(round(t/dt))%3==0:print("TRACE %s t %.2f v %.2f r %+.3f ref %+.3f ay/v %+.3f beta %+.2f° excess %+.3f esc %.2f brake %.0f %.0f"%[c,t,speed,car.angular_velocity.dot(b.y),car.esc_reference,car.esc_lateral/maxf(speed,1.),rad_to_deg(car.esc_beta),car.esc_excess,car.esc_level,car.esc_brake[0],car.esc_brake[1]])
   if done:report();return false
 car.drive(dt,gas,0.,turn,false)
 return false

# Pure pursuit on the course, as RoadDrive.gd steers. Returns (turn, cross-track m).
func follow(b:Basis,speed:float)->Vector2:
 var pos:=Vector2(car.global_position.x,car.global_position.z)
 var best:=station;var best_d:=INF
 for s in range(station,mini(route.size(),station+40)):
  var d:=pos.distance_squared_to(Vector2(route[s].x,route[s].z))
  if d<best_d:best_d=d;best=s
 station=best
 var look:=clampf(speed*.9,6.,30.)
 var target_i:=mini(route.size()-1,int(station+look))
 var aim:=Vector2(route[target_i].x,route[target_i].z)
 var to:=aim-pos;var forward:=Vector2(-b.z.x,-b.z.z).normalized()
 var alpha:=forward.angle_to(to)
 var delta:=atan(2.*car.WHEELBASE_M*sin(alpha)/maxf(to.length(),1.))
 return Vector2(clampf(-delta/maxf(car.available_steering(speed,car.steering_assist),.01),-1.,1.),sqrt(best_d))

func observe(b:Basis,vel:Vector3,speed:float,counting:bool):
 if not counting:return
 var brake:float=0.
 for i in 4:brake=maxf(brake,float(car.esc_brake[i]))
 var active:bool=car.esc_level>0. or brake>0.
 if active:active_ticks+=1
 if active and not was_active:events+=1
 was_active=active
 peak_excess=maxf(peak_excess,car.esc_excess)
 peak_brake=maxf(peak_brake,brake/maxf(spec.brake_front_nm,1.))
 peak_lat=maxf(peak_lat,absf(car.angular_velocity.dot(b.y)*speed)/G)
 if speed>1.:peak_beta=maxf(peak_beta,absf(rad_to_deg(atan2(vel.dot(b.x),speed))))
 peak_estimate=maxf(peak_estimate,absf(rad_to_deg(car.esc_beta)))
 peak_overturn=maxf(peak_overturn,rad_to_deg(car.esc_overturn))

func road_gear(speed:float)->int:
 for g in car.GEARS.size():
  if speed/car.R*car.GEARS[g]*car.FINAL_DRIVE*60./TAU<4000.:return g
 return car.GEARS.size()-1

func report():
 var c=CASES[case_i]
 var what:String
 match c[0]:
  "weave":what="weave %2.0f m/s %.1f° %.1f Hz"%[c[1],c[2],c[3]]
  "taps":what="key taps %2.0f m/s 0.1 s"%c[1]
  _:what="road %.1f g R %3.0f m %4.1f m/s"%[ROAD_G,c[3],target]
 var bad:bool=active_ticks>0 or (c[0]=="road" and (peak_cross>2.5 or station<route.size()-40))
 if bad:failures+=1
 rows.append("%-11s %-26s esc events %d active %.2f s brake %.2f  peak excess %+.3f rad/s  lat %.2f g  sideslip %.2f° (estimate %.2f°, overturn %.2f°)%s  %s"%[
  spec.id,what,events,active_ticks/60.,peak_brake,peak_excess,peak_lat,peak_beta,peak_estimate,peak_overturn," cross %.2f m"%peak_cross if c[0]=="road" else "","FAIL" if bad else "ok"])
 print("QUIET ",rows.back())
 case_i+=1
 if case_i>=CASES.size():next_car()
 else:start()

func finish():
 print("")
 for r in rows:print("RESULT ",r)
 print("STABILITY QUIET %s: %d case(s) with stability-control activity or off the line"%["PASSED" if failures==0 else "FAILED",failures])
 quit(0 if failures==0 else 1)
