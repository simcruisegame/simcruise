extends SceneTree
## Lift-off and trail-brake bench on a flat pad, after ISO 9816 (power-off
## reaction in a turn) and ISO 7975 (braking in a turn), both open-loop tests.
## The car is held on a RADIUS circle at a steady lateral acceleration by a
## path and speed driver, then the steering is frozen where it was and the
## driver either
##   (a) lift: closes the throttle over 0.1 s, or
##   (b) brake: closes the throttle over 0.1 s, moves to the brake and from
##       0.2 s presses it over 0.3 s to a pedal that would give TRAIL_BRAKE_G
##       of deceleration (through the lightest pedal positions, which once
##       put an automatic into reverse at speed),
## and keeps the pedals and the wheel still for WINDOW_S seconds.
## Reported per case (t = 0 at the start of the lift):
##   yaw_ratio_1s   yaw rate at 1 s over the steady yaw rate (ISO 9816's
##                  "yaw rate ratio"; above 1 the car tucks in)
##   yaw_overshoot  peak yaw rate over the steady yaw rate, minus 1
##   beta_1s        body sideslip at the CG at 1 s, tail out positive (deg)
##   peak_beta      largest tail-out sideslip in the window (deg)
##   settle_s       last time the sideslip was more than 1° from its final value
##   decel_g        mean deceleration from 0.5 to 1.5 s
##   esc            peak stability-control pressure at a front wheel (share
##                  of a full service brake), cars with stability_control
## A car the driver cannot hold at the asked level (it runs wide, or cannot
## reach the speed) is not left unmeasured: if it has not settled after
## settle_limit() (longer on the larger circle, where the path driver
## converges more slowly) the run starts again EASE_G lower, as a test driver finds a
## car's highest steady state, and the case runs there ("asked 0.7 g, held
## 0.66 g" then shows in the row); after MAX_EASES it counts as unsteady.
## A car spins when the sideslip passes SPIN_DEG. Gate, lift or brake, on
## both circles (40 m and ISO 7975's 100 m): no car spins at 0.3 or 0.5 g,
## and no car with stability control spins at 0.7 g (its highest steady
## state up to 0.7 g); 0.7 g on a car without it is reported. A gated case
## that finds no steady state fails. Any case where the automatic selects
## reverse while rolling forward fails.
##
##   godot --headless --path game --fixed-fps 60 --script res://checks/LiftOff.gd -- \
##     [--car=nb1999 ...] [--g=0.3,0.5,0.7] [--radius=40] [--trace] [--set=field=value ...]
## --radius picks one circle (default: both); --set overrides a car-file field
## for an experiment (number or JSON array).
const CAR_SPEC=preload("res://CarSpec.gd")
const SPIN_DEG := 8.
const WINDOW_S := 3.
const TRAIL_BRAKE_G := .15
const GATE_G := .5
const ESC_GATE_G := .7
const EASE_G := .02
const MAX_EASES := 10
var radii := [40.,100.]
var radius := 40.
var levels := [.3,.5,.7]
var queue := []
var overrides := {}
var trace := false
var car
var spec
var cases := []
var case_i := 0
var state := 0
var t := 0.
var speed_i := 0.
var radial_i := 0.
var target_speed := 0.
var eases := 0
var steady := {}
var hold_turn := 0.
var hold_gas := 0.
var pedal := 0.
var samples := []
var wrong_gear := false
var esc_peak := 0.
var rows := []
var failures := 0
var history := []

func _initialize():
 InputMap.add_action("clutch")
 var pad=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(3000,1,3000);col.shape=box;pad.position.y=-.5;pad.add_child(col);root.add_child(pad)
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--car="):queue.append(a.trim_prefix("--car="))
  elif a.begins_with("--g="):levels=Array(a.trim_prefix("--g=").split(",",false)).map(func(x):return float(x))
  elif a.begins_with("--radius="):radii=[float(a.trim_prefix("--radius="))]
  elif a=="--trace":trace=true
  elif a.begins_with("--set="):
   var kv=a.trim_prefix("--set=").split("=",true,1)
   overrides[kv[0]]=JSON.parse_string(kv[1])
 if queue.is_empty():queue=CAR_SPEC.available()
 for r in radii:
  for id in queue:
   for g in levels:
    for kind in ["lift","brake"]:cases.append([id,g,kind,r])
 begin()

func begin():
 if case_i>=cases.size():finish();return
 var c=cases[case_i]
 if car==null or spec.id!=c[0]:
  if car:root.remove_child(car);car.free()
  spec=CAR_SPEC.load_id(c[0])
  if not overrides.is_empty():spec.apply(overrides)
  car=load("res://Powertrain.gd").new();car.use_spec(spec);car.setup();root.add_child(car)
  car.steering_assist=false
 eases=0
 launch()

# Put the car on the pad and start the circle at the asked level, less any eases.
func launch():
 var c=cases[case_i]
 car.restart(Vector3(0,.15,0),0.)
 radius=float(c[3])
 target_speed=sqrt((float(c[1])-EASE_G*eases)*9.80665*radius)
 state=0;t=0.;speed_i=0.;radial_i=0.;samples=[];history=[];wrong_gear=false;esc_peak=0.
 pedal=TRAIL_BRAKE_G*spec.mass_kg*9.80665*car.R/(2.*(spec.brake_front_nm+spec.brake_rear_nm))

func settle_limit()->float:
 return 10.+.15*radius

func gated(c:Array)->bool:
 var limit:=ESC_GATE_G if spec.stability_control else GATE_G
 return float(c[1])<=limit+.001

# Lowest gear that keeps the engine below about 4000 rpm (a brisk road gear).
func road_gear(speed:float)->int:
 for g in car.GEARS.size():
  if speed/car.R*car.GEARS[g]*car.FINAL_DRIVE*60./TAU<4000.:return g
 return car.GEARS.size()-1

func yaw_of(b:Basis)->float:
 var f=-b.z;return atan2(-f.x,-f.z)

func _physics_process(dt):
 if case_i>=cases.size():return true
 t+=dt
 var b:Basis=car.global_transform.basis;var vel:Vector3=car.linear_velocity;var speed=-vel.dot(b.z)
 var yaw_rate:float=car.angular_velocity.y
 var beta:float=rad_to_deg(atan2(vel.dot(b.x),maxf(absf(speed),.5)))*signf(yaw_rate if absf(yaw_rate)>.01 else 1.)
 var gas:=0.;var brake:=0.;var turn:=hold_turn
 match state:
  0:
   if t>=.5:
    car.linear_velocity=-b.z*target_speed;car.omega.fill(target_speed/car.R)
    car.gear=road_gear(target_speed);car.engine_omega=target_speed/car.R*car.GEARS[car.gear]*car.FINAL_DRIVE;car.rpm=car.engine_omega*60./TAU
    state=1;t=0.
  1:
   # Circle driver (as Track.gd): aim 10 m ahead on the circle, trim the
   # radius error with an integrator; speed by proportional-integral throttle.
   # It starts from the circle's Ackermann angle (a driver turns the wheel
   # to the bend before correcting), so the integrator only has to find the
   # car's understeer and a large circle settles as quickly as a small one.
   var p=Vector2(car.position.x+radius,car.position.z);var current=p.length()
   var ahead=p.rotated(-10./radius).normalized()*radius-p
   var desired=atan2(-ahead.x,-ahead.y)
   radial_i=clampf(radial_i+(current-radius)*dt*.08,-.3,.6)
   var feed:float=atan(spec.wheelbase_m/radius)/car.available_steering(speed,false)
   turn=clampf(feed+angle_difference(yaw_of(b),desired)*1.9+radial_i,-1.,1.)
   speed_i=clampf(speed_i+(target_speed-speed)*dt*.3,0.,.9)
   gas=clampf((target_speed-speed)*.65+speed_i,0.,1.)
   history.append([absf(current-radius),absf(speed-target_speed),turn,gas,yaw_rate,beta,speed,car.esc_level])
   if history.size()>120:history.pop_front()
   var settled=history.size()==120 and t>6.
   var mean_yaw:=0.
   for h in history:settled=settled and h[0]<.6 and h[1]<.15;mean_yaw+=h[4]/120.
   # Steady also means the heading turns with the path: a car still yawing
   # faster or slower than v/R is sliding or recovering, not in a steady state.
   settled=settled and absf(absf(mean_yaw)*radius/target_speed-1.)<.03
   for h in history:settled=settled and absf(h[4]-mean_yaw)<maxf(.05*absf(mean_yaw),.01)
   if not settled and t>settle_limit():
    # The car cannot hold this level here: start again a little lower.
    if eases<MAX_EASES:eases+=1;launch();return false
    record_unsteady(current,speed);return false
   if settled:
    var n=float(history.size());hold_turn=0.;hold_gas=0.;var r0=0.;var b0=0.;var v0=0.;var e0=0.
    for h in history:hold_turn+=h[2]/n;hold_gas+=h[3]/n;r0+=h[4]/n;b0+=h[5]/n;v0+=h[6]/n;e0=maxf(e0,h[7])
    steady={"yaw":r0,"beta":b0,"speed":v0,"lat_g":absf(v0*r0)/9.80665,"turn":hold_turn,"gas":hold_gas,"gear":car.gear+1,"esc":e0}
    turn=hold_turn;gas=hold_gas;state=2;t=0.
  2:
   gas=hold_gas*clampf(1.-t/.1,0.,1.)
   # The right foot moves across (0.1 s) and squeezes the pedal over 0.3 s,
   # passing through the lightest pedal positions on the way.
   if cases[case_i][2]=="brake":brake=pedal*clampf((t-.2)/.3,0.,1.)
   samples.append([t,yaw_rate,beta,speed]);wrong_gear=wrong_gear or (car.in_reverse and speed>1.)
   esc_peak=maxf(esc_peak,maxf(car.esc_brake[0],car.esc_brake[1])/maxf(spec.brake_front_nm,1.))
   if trace and int(round(t/dt))%3==0:print_trace(beta,yaw_rate,speed)
   if t>=WINDOW_S or absf(beta)>45.:report();return false
 car.drive(dt,gas,brake,turn,false)
 return false

func print_trace(beta:float,yaw_rate:float,speed:float):
 var s="TRACE %s %.2f t %.2f v %.2f r %.3f beta %.2f gear %d rpm %d esc %.2f excess %.3f"%[cases[case_i][2],cases[case_i][1],t,speed,yaw_rate,beta,car.gear+1,car.rpm,car.esc_level,car.esc_excess]
 for i in 4:
  var fwd=speed;var w=car.omega[i]*car.R
  s+=" | w%d fz %.0f a %.2f k %.3f"%[i,car.spring_load[i],rad_to_deg(car.rolling_slip[i]),(w-fwd)/maxf(absf(fwd),1.)]
 print(s)

func label(c:Array)->String:
 return "%-11s %3.0f m %.1f g %-5s"%[c[0],c[3],c[1],c[2]]

func record_unsteady(current:float,speed:float):
 var c=cases[case_i]
 var g:=gated(c)
 rows.append("%s no steady state (radius %.1f m, speed %.1f of %.1f m/s)%s"%[label(c),current,speed,target_speed,"" if g else " (reported)"])
 print("LIFTOFF ",rows.back())
 if g:failures+=1
 case_i+=1;begin()

func report():
 var c=cases[case_i]
 var r0:float=absf(steady.yaw);var r1:=0.;var b1:=0.;var peak_r:=0.;var peak_b:=-INF;var v0:=0.;var v1:=0.
 for s in samples:
  peak_r=maxf(peak_r,absf(s[1])*signf(s[1]*steady.yaw));peak_b=maxf(peak_b,s[2])
  if s[0]<=1.:r1=absf(s[1])*signf(s[1]*steady.yaw);b1=s[2]
  if s[0]<=.5:v0=s[3]
  if s[0]<=1.5:v1=s[3]
 var final_b:float=samples.back()[2];var settle:=0.
 for s in samples:
  if absf(s[2]-final_b)>1.:settle=s[0]
 var decel=(v0-v1)/9.80665
 var spun:bool=peak_b>SPIN_DEG
 var g:=gated(c)
 if spun and g:failures+=1
 # Any car: the gearbox must never select the opposite direction at speed.
 if wrong_gear:failures+=1
 var eased:String=" (asked %.1f g, held %.2f g)"%[c[1],float(c[1])-EASE_G*eases] if eases>0 else ""
 var esc:String="  esc %.2f%s"%[esc_peak," (also while steady %.2f)"%steady.esc if steady.esc>.01 else ""] if spec.stability_control else ""
 rows.append("%s steady %.2f g (gear %d, beta %+.1f)%s  yaw_ratio_1s %.2f  yaw_overshoot %+.2f  beta_1s %+.1f  peak_beta %+.1f  settle_s %.2f  decel_g %.2f%s  %s"%[
  label(c),steady.lat_g,steady.gear,steady.beta,eased,r1/maxf(r0,1e-4),peak_r/maxf(r0,1e-4)-1.,b1,peak_b,settle,decel,esc,
  ("SPIN" if spun else "ok")+("" if g else " (reported)")+(" REVERSE SELECTED AT SPEED" if wrong_gear else "")])
 print("LIFTOFF ",rows.back())
 case_i+=1;begin()

func finish():
 case_i=cases.size()
 print("")
 for r in rows:print("RESULT ",r)
 print("LIFTOFF %s: %d gated case(s) spun, failed to settle or selected reverse (radius %s m, spin = sideslip past %.0f°)"%["PASSED" if failures==0 else "FAILED",failures,"/".join(radii.map(func(x):return "%.0f"%x)),SPIN_DEG])
 quit(0 if failures==0 else 1)
