extends SceneTree
## Pad rumble (Haptics.gd) against the tyres it reads. Headless, no pad needed:
## Haptics.sample() and motors() are pure reads of car state; nothing here
## calls Input.
##   godot --headless --path game --fixed-fps 60 --script res://checks/HapticsCheck.gd
##   ... -- --car=nb1999         one car (repeatable; default nb1999 and meridian_z)
##   HAPTICS_TRACE=1             prints every 10th tick of the live runs
##
## Part 1, the law: slip_feel and both motors never fall as any input grows,
## are exactly zero with every input zero, and the ABS pulse alternates at
## Haptics.ABS_HZ only while ABS acts.
## Part 2, each car on a flat pad with the force model unchanged:
##   coast    25 m/s, no pedal, no steering for 3 s: both motors exactly 0
##   cruise   hold 25 m/s on part throttle: only the engine buzz (≤ ENGINE_GAIN)
##   0.5 g    steady turn at 20 m/s: weak ≤ 0.05
##   ramp     steering wound on at 20 m/s past the front tyres' limit: the weak
##            motor, binned by the front tyres' normalised slip, never falls
##            as slip grows, and reaches ≥ 0.5 once the fronts slide (s ≥ 1.5)
##   plateau  through the 0.5 g turn and the ramp, the front tyres' lateral
##            force as Powertrain produced it (contact row 8, over Fz·mu·scale),
##            binned by slip angle over Haptics' alpha*: within 5 % of its
##            largest bin at alpha* (s 1.0-1.2) and at most 93 % of it at half
##            alpha*. Haptics.plateau_angle() restates the lateral law, so a new
##            tyre law that moves the force peak fails here instead of silently
##            shifting every rumble and squeal threshold.
##   stop    full brake from 25 m/s: on a car with ABS the strong motor pulses
##            (≥ 6 on/off cycles per second); without ABS the locked tyres
##            give a steady strong rumble instead
const CAR_SPEC=preload("res://CarSpec.gd")
const HAPTICS=preload("res://Haptics.gd")
const G=9.80665
var failures=[]
var queue=[]
var spec;var car;var feel;var pad
var phase="";var t=0.;var tick=0
var speed_i=0.;var turn=0.;var lat_window=[]
var stats={}
var trace=OS.has_environment("HAPTICS_TRACE")

func expect(ok:bool,what:String):
 if not ok:failures.append(what)

func _initialize():
 law()
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--car="):queue.append(a.trim_prefix("--car="))
 if queue.is_empty():queue=["nb1999","meridian_z"]
 pad=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(5000,1,5000);col.shape=box;pad.position.y=-.5;pad.add_child(col);root.add_child(pad)
 next_car()

# Part 1: monotonic in every input, zero at rest.
func law():
 var nb=CAR_SPEC.load_id("nb1999")
 var alpha_star:float=HAPTICS.plateau_angle(nb)
 print("HAPTICS nb1999 lateral plateau %.3f rad (%.1f°), longitudinal peak %.3f"%[alpha_star,rad_to_deg(alpha_star),nb.slip_ratio_peak])
 expect(alpha_star>.12 and alpha_star<.25,"nb1999 lateral plateau %.3f rad outside 0.12-0.25"%alpha_star)
 for id in CAR_SPEC.available():
  var a:float=HAPTICS.plateau_angle(CAR_SPEC.load_id(id))
  expect(is_finite(a) and a>0. and a<1.,"%s lateral plateau %.3f is not a usable angle"%[id,a])
 expect(HAPTICS.motors(0.,0.,0.,0.,0.,30.,0.)==Vector2.ZERO,"motors are not zero with every input zero")
 expect(HAPTICS.slip_feel(HAPTICS.S_ONSET)==0.,"slip_feel is not zero at the onset")
 var previous=-1.
 for n in 401:
  var f:float=HAPTICS.slip_feel(n*.01)
  expect(f>=previous,"slip_feel falls at s = %.2f"%(n*.01));previous=f
 # Sweep each input with the others at zero and at a mid value.
 for base in [[0.,0.,0.,0.,0.,0.,false],[.9,.9,.3,.3,.3,.3,false],[.9,.9,.3,.3,.3,.3,true]]:
  for k in 6:
   var last=Vector2(-1.,-1.)
   for n in 301:
    var x:Array=base.duplicate();x[k]=n*(.01 if k<2 else .0034)
    var m:Vector2=HAPTICS.motors(x[0],x[1],x[2],x[3],x[4],20.,x[5],x[6])
    expect(m.x>=last.x-1e-6 and m.y>=last.y-1e-6,"motors fall as input %d grows (base %s, value %.3f)"%[k,str(base),x[k]])
    expect(m.x>=0. and m.x<=1. and m.y>=0. and m.y<=1.,"motors leave 0..1")
    last=m
 var flips=0;var level=HAPTICS.abs_pulse(true,0.)
 for n in 600:
  var p:float=HAPTICS.abs_pulse(true,n/600.)
  if p!=level:flips+=1;level=p
 expect(flips>=2*int(HAPTICS.ABS_HZ)-1 and flips<=2*int(HAPTICS.ABS_HZ)+1,"ABS pulse flips %d times in 1 s, not %d"%[flips,2*int(HAPTICS.ABS_HZ)])
 expect(HAPTICS.abs_pulse(false,.01)==0.,"ABS pulse runs without ABS")
 print("HAPTICS law: slip_feel(1.0)=%.2f slip_feel(1.5)=%.2f, ABS pulse %d flips/s"%[HAPTICS.slip_feel(1.),HAPTICS.slip_feel(1.5),flips])

func next_car():
 if car:root.remove_child(car);car.free();car=null
 if queue.is_empty():finish();return
 spec=CAR_SPEC.load_id(queue.pop_front())
 stats={"coast_max":Vector2.ZERO,"cruise_max":Vector2.ZERO,"half_g_weak":0.,"half_g":0.,"ramp":[],"ramp_peak_weak":0.,"ramp_peak_s":0.,"stop_on":0,"stop_strong_max":0.,"stop_abs":false,"stop_s":0.,"onset_g":-1.,"lateral":{}}
 car=load("res://Powertrain.gd").new();car.use_spec(spec);car.setup();root.add_child(car);car.restart(Vector3(0,.18,0),0.)
 feel=HAPTICS.new(spec)
 phase="settle";t=0.;tick=0;speed_i=0.;turn=0.;lat_window=[]

func hold(target:float)->float:
 var speed=-car.linear_velocity.dot(car.transform.basis.z)
 speed_i=clampf(speed_i+(target-speed)*1./60.*.12,0,.75)
 return clampf((target-speed)*.65+speed_i,0,1)

func _physics_process(dt):
 if car==null:return false
 tick+=1;t+=dt
 var v=car.linear_velocity;var fwd=-car.transform.basis.z;var speed=v.dot(fwd)
 var lat_g=absf(speed*car.angular_velocity.y)/G
 var m:Vector2
 match phase:
  "settle":
   car.drive(dt,0.,0.,0.,false);m=feel.sample(car,0.,dt)
   if t>1.5:phase="speed";t=0.
  "speed":
   car.drive(dt,hold(25.),0.,0.,false);m=feel.sample(car,0.,dt)
   if speed>24.8 or t>30.:phase="cruise";t=0.
  "cruise":
   car.drive(dt,hold(25.),0.,0.,false);m=feel.sample(car,0.,dt)
   if t>1.:stats.cruise_max=Vector2(maxf(stats.cruise_max.x,m.x),maxf(stats.cruise_max.y,m.y))
   if t>3.:phase="coast";t=0.
  "coast":
   car.drive(dt,0.,0.,0.,false);m=feel.sample(car,0.,dt)
   # The pedal's last tick still carries load; judge the coast from 0.2 s on.
   if t>.2:stats.coast_max=Vector2(maxf(stats.coast_max.x,m.x),maxf(stats.coast_max.y,m.y))
   if t>3.:phase="turn_speed";t=0.;speed_i=0.
  "turn_speed":
   car.drive(dt,hold(20.),0.,0.,false);m=feel.sample(car,0.,dt)
   if absf(speed-20.)<.3 and t>2.:phase="half_g";t=0.
   if t>30.:failures.append(spec.id+": could not settle at 20 m/s");phase="stop_speed";t=0.
  "half_g":
   # Wind the rack on until the car holds 0.5 g, then hold it.
   car.drive(dt,hold(20.),0.,turn,false);m=feel.sample(car,0.,dt)
   lat_window.append(lat_g);if lat_window.size()>30:lat_window.pop_front()
   var mean=0.;for x in lat_window:mean+=x
   mean/=lat_window.size()
   if mean<.5 and stats.half_g==0.:turn+=dt*.02
   elif stats.half_g==0.:stats.half_g=-t
   if stats.half_g<0. and t>-stats.half_g+1.5:
    stats.half_g_weak=maxf(stats.half_g_weak,m.x);lateral_sample()
    if t>-stats.half_g+3.:stats.half_g=mean;phase="ramp";t=0.
   if t>40.:failures.append(spec.id+": could not reach 0.5 g");phase="ramp";t=0.
  "ramp":
   # Past 0.5 g the rack keeps winding on to full lock, holding the speed.
   turn=minf(1.,turn+dt*.08)
   car.drive(dt,hold(20.),0.,turn,false);m=feel.sample(car,0.,dt)
   var front=maxf(feel.slip[0],feel.slip[1])
   stats.ramp.append([front,m.x]);lateral_sample()
   if m.x>.1 and stats.onset_g<0.:stats.onset_g=lat_g
   if m.x>stats.ramp_peak_weak:stats.ramp_peak_weak=m.x;stats.ramp_peak_s=front
   if (turn>=1. and t>8.) or t>20.:phase="stop_speed";t=0.;turn=0.;car.restart(Vector3(0,.18,0),0.);speed_i=0.
  "stop_speed":
   car.drive(dt,hold(25.),0.,0.,false);m=feel.sample(car,0.,dt)
   if speed>24.8 or t>30.:phase="stop";t=0.
  "stop":
   car.drive(dt,0.,1.,0.,false);m=feel.sample(car,0.,dt)
   if speed>3.:
    if m.y>=.5:stats.stop_on+=1
    stats.stop_strong_max=maxf(stats.stop_strong_max,m.y)
    stats.stop_abs=stats.stop_abs or feel.abs_active
    stats.stop_s+=dt
    if not stats.has("stop_flips"):stats.stop_flips=0;stats.stop_last=m.y>=.5
    if (m.y>=.5)!=stats.stop_last:stats.stop_flips+=1;stats.stop_last=m.y>=.5
   if speed<1. or t>10.:report();call_deferred("next_car");car_done();return false
 if trace and tick%10==0:print("TRACE %s %-10s t=%.2f v=%.1f lat=%.2fg s=[%.2f %.2f %.2f %.2f] weak=%.3f strong=%.3f abs=%s"%[spec.id,phase,t,speed,lat_g,feel.slip[0],feel.slip[1],feel.slip[2],feel.slip[3],m.x,m.y,str(feel.abs_active)])
 return false

func car_done():
 phase="done"

# Front tyres' lateral force over its ceiling, binned by |alpha| / alpha* (0.1 wide).
func lateral_sample():
 for i in 2:
  if i>=car.contact_rows.size():continue
  var row:Array=car.contact_rows[i]
  if row.size()<9 or float(row[2])<=0.:continue
  var ceiling:float=float(row[2])*float(row[7])*car.LATERAL_GRIP_SCALE
  if ceiling<=1.:continue
  var b:=int(floor(absf(float(car.rolling_slip[i]))/feel.lateral_peak*10.))
  if not stats.lateral.has(b):stats.lateral[b]=[0.,0]
  stats.lateral[b][0]+=absf(float(row[8]))/ceiling;stats.lateral[b][1]+=1

# Mean of the lateral bins from lo to hi (inclusive) with at least 3 samples; -1 if none.
func lateral_mean(lo:int,hi:int)->float:
 var sum=0.;var n=0
 for b in range(lo,hi+1):
  if stats.lateral.has(b) and int(stats.lateral[b][1])>=3:sum+=float(stats.lateral[b][0])/int(stats.lateral[b][1]);n+=1
 return sum/n if n>0 else -1.

func report():
 var id:String=spec.id
 expect(stats.coast_max==Vector2.ZERO,"%s: rumble while coasting straight (%s)"%[id,str(stats.coast_max)])
 expect(stats.cruise_max.y==0. and stats.cruise_max.x<=HAPTICS.ENGINE_GAIN+1e-6,"%s: cruising rumble %s beyond the engine buzz"%[id,str(stats.cruise_max)])
 expect(stats.half_g>.45 and stats.half_g_weak<=.05,"%s: weak motor %.3f at %.2f g steady (want ≤ 0.05 at 0.5 g)"%[id,stats.half_g_weak,stats.half_g])
 # Bin the ramp by front slip (0.1 wide); the mean weak level must not fall.
 var bins={}
 for p in stats.ramp:
  var b=int(floor(float(p[0])*10.))
  if not bins.has(b):bins[b]=[0.,0]
  bins[b][0]+=float(p[1]);bins[b][1]+=1
 var keys=bins.keys();keys.sort()
 var last=-1.;var curve=[]
 for b in keys:
  if int(bins[b][1])<3:continue
  var mean=float(bins[b][0])/int(bins[b][1])
  curve.append("%.1f:%.2f"%[b/10.,mean])
  expect(mean>=last-.02,"%s: weak motor falls from %.3f to %.3f as front slip reaches %.1f"%[id,last,mean,b/10.])
  last=maxf(last,mean)
 expect(stats.ramp_peak_s<1.5 or stats.ramp_peak_weak>=.5,"%s: front tyres at s %.2f but weak motor only %.3f"%[id,stats.ramp_peak_s,stats.ramp_peak_weak])
 expect(stats.ramp_peak_s>=1.5,"%s: the ramp never slid the front tyres (peak s %.2f)"%[id,stats.ramp_peak_s])
 # alpha* must sit at the physics' own lateral force plateau.
 var lateral_peak=0.;var lateral_curve=[]
 var lateral_keys=stats.lateral.keys();lateral_keys.sort()
 for b in lateral_keys:
  if int(stats.lateral[b][1])<3:continue
  var f=float(stats.lateral[b][0])/int(stats.lateral[b][1]);lateral_peak=maxf(lateral_peak,f)
  lateral_curve.append("%.1f:%.2f"%[b/10.,f])
 var at_plateau=lateral_mean(10,11);var at_half=lateral_mean(5,5)
 expect(at_plateau>=.95*lateral_peak,"%s: front lateral force at alpha* (s 1.0-1.2) is %.3f, below 95 %% of its peak %.3f: alpha* no longer marks the tyre law's plateau"%[id,at_plateau,lateral_peak])
 expect(at_half>=0. and at_half<=.93*lateral_peak,"%s: front lateral force at half alpha* is %.3f of peak %.3f (want ≤ 93 %%, measured): alpha* lies past the tyre law's plateau"%[id,at_half,lateral_peak])
 var flips_per_s=float(stats.get("stop_flips",0))/maxf(stats.stop_s,.01)
 if spec.abs_enabled:
  expect(stats.stop_abs and flips_per_s>=12.,"%s: ABS stop strong motor flips %.1f/s (want ≥ 12, six on/off cycles)"%[id,flips_per_s])
 else:
  expect(stats.stop_strong_max>=.5,"%s: locked-wheel stop gives strong %.2f"%[id,stats.stop_strong_max])
 print("RESULT %s coast=%s cruise=%s | %.2f g steady weak=%.3f | ramp weak>0.1 from %.2f g, peak weak %.2f at front s %.2f, weak by front s %s | stop %s strong max %.2f, %.1f flips/s"%[id,str(stats.coast_max),str(stats.cruise_max),stats.half_g,stats.half_g_weak,stats.onset_g,stats.ramp_peak_weak,stats.ramp_peak_s," ".join(curve),"ABS" if stats.stop_abs else "no ABS",stats.stop_strong_max,flips_per_s])
 print("LATERAL %s front force / ceiling by |alpha|/alpha* %s (peak %.3f, at alpha* %.3f, at half %.3f)"%[id," ".join(lateral_curve),lateral_peak,at_plateau,at_half])

func finish():
 if failures.is_empty():print("HAPTICS PASSED")
 else:
  for f in failures:print("HAPTICS FAILED ",f)
 quit(0 if failures.is_empty() else 1)
