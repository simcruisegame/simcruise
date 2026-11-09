extends RigidBody3D
# SI units, +X right, -Z forwards. Springs, dampers, tires and brakes act at each contact.
const GEARS=[3.136,1.888,1.330,1.0,.814]
const FINAL_DRIVE=4.30
# 195/50R15 nominal free radius. Effective loaded rolling circumference still needs measurement.
const R=(15.*.0254*.5)+(.195*.50)
const SPORT=preload("res://NB1999Sport.gd")
const WHEELBASE_M=2.266
const PARKING_RACK_RAD = 36.6 * PI / 180.0
const ROAD_ASSIST_REFERENCE_RAD = 35.5 * PI / 180.0
# Steering map. Assisted lets a full key or stick ask for the rack angle at the
# front axle's grip limit: the Ackermann angle for ASSIST_LATERAL_G plus the
# front-minus-rear slip this tyre model carries at the limit, never more than
# the rack lock. Direct keeps the previous 35.5° map and constant ramp rates:
# the map for a device that positions the rack itself, a real wheel or a bench
# servo such as checks/Track.gd. A car mounted as a scene's `vehicle` is driven
# by a person, so it enters the tree Assisted; --direct-steering keeps Direct.
var steering_assist := false
const ASSIST_LATERAL_G = 0.95
# Rack beyond Ackermann this tyre model needs at the limit: Track.gd's 20.0 m/s
# lap holds 0.885 g with about 0.40 × 18.9° = 7.6° of rack, 2.9° of it Ackermann.
# A held key at 45 mph then reaches 0.86-0.97 g with 10-11° of front slip (the
# curve peaks near 14°). A tyre curve with a sharper peak would lower this.
const LIMIT_SLIP_SURPLUS_RAD = 5.0 * PI / 180.0
const LOCK_BLEND_RAD = 3.0 * PI / 180.0
# A held key moves the rack at a hand's pace: the previous 2.75/s of lock while
# parking, easing to 12°/s (about 160°/s at the wheel) by highway speed, so a
# half-second tap at 60 mph reaches 6° instead of the lock. Release is quicker.
const KEY_RACK_RATE_PARKING_RAD_S = 2.75 * PARKING_RACK_RAD
const KEY_RACK_RATE_HIGHWAY_RAD_S = 12.0 * PI / 180.0
const KEY_RETURN_RATE_MIN_RAD_S = 24.0 * PI / 180.0
const KEY_RETURN_FACTOR = 1.6
const STEER_DEAD_BAND = 0.01
# Roof-down or stranded timers for Drive's recovery prompt.
var roof_down := false
var flipped_seconds := 0.
var stuck_seconds := 0.
var stranded_seconds := 0.
const REST=SPORT.MAX_TRAVEL_M
var wheel_camber=[0.,0.,0.,0.]
var wheel_heading=[0.,0.,0.,0.]
const MOUNTS=[Vector3(-.707,.51,-1.133),Vector3(.707,.51,-1.133),Vector3(-.720,.51,1.133),Vector3(.720,.51,1.133)]
const WHEEL_I=1.05
const G=9.80665
var connected_wheels=[]
var omega=[0.,0.,0.,0.]
var rpm=850.
# Acoustics read these results after the six force substeps; the sound observer
# does not participate in torque, gear selection or contact integration.
var sound_throttle := 0.
var sound_combustion := 0.
var sound_shaft_load := 0.
var engine_omega=89.0
var gear=0
var in_reverse=false
var manual=false
var selected_gear=1  # -1 reverse, 0 neutral, 1..5 forward
var clutch_pedal=0.0
var stalled=false
func set_manual(enabled):
 manual=enabled;clutch_pedal=0.;stalled=false
 selected_gear=-1 if in_reverse else gear+1
 engine_omega=maxf(engine_omega,89.)
 if manual and linear_velocity.length()<1.:select_ratio(0)
func select_ratio(ratio):
 selected_gear=ratio
 in_reverse=ratio==-1
 gear=clampi(ratio-1,0,4)
func change_gear(step):
 if not manual:return ""
 var proposed=clampi(selected_gear+step,-1,5)
 if proposed==selected_gear:return ""
 if clutch_pedal<.75:return "Hold the clutch to change gear"
 var forward_speed=-linear_velocity.dot(global_transform.basis.z)
 if (proposed==-1 and forward_speed>1.) or (proposed>0 and forward_speed< -1.):return "Stop before changing direction"
 if proposed>0 and absf(forward_speed)/R*GEARS[proposed-1]*FINAL_DRIVE*60./TAU>7200.:return "Speed is too high for that gear"
 select_ratio(proposed)
 return ""
func start_engine():
 if manual and stalled and (selected_gear==0 or clutch_pedal>.75):
  engine_omega=89.;rpm=850.;stalled=false
func gear_text():
 if manual:
  if selected_gear==0:return "N"
  return "R" if selected_gear==-1 else str(selected_gear)
 return "R" if in_reverse else str(gear+1)
var shift_time=0.
var steering_input=0.
var steering=0.
var wheel_contact=0
var service_brake=false
const LATERAL_GRIP_SCALE=1.105
var static_mu=.963
var surface_grip=[1.,1.,1.,1.]
var spring_load=[0.,0.,0.,0.]
# Rolling distance to 63% of steady small-slip response. This is a representative
# passenger radial estimate; no transient force records for the OE 195/50R15.
const SIDE_RELAXATION_M = 0.35
var rolling_slip = [0.,0.,0.,0.]

func contact_slip(i: int, longitudinal: float, transverse: float, dt: float) -> float:
 var speed := absf(longitudinal)
 var target := atan2(transverse,maxf(1.8,speed))
 # Below walking speed the existing regularized contact also handles static
 # friction. Blend continuously into the rolling-tire model as travel builds.
 var length_m := SIDE_RELAXATION_M*smoothstep(.8,3.5,speed)
 if length_m < .0001:
  rolling_slip[i]=target
  return target
 var travelled := speed*dt/length_m
 var fraction := 1.-exp(-travelled)
 var change: float = target-rolling_slip[i]
 rolling_slip[i]+=change*fraction
 # Interval mean of d(alpha)/ds = (target-alpha)/length. Using the final
 # value for an entire Jolt tick would advance the lateral impulse in time.
 return target-change*fraction/travelled
func setup():
 mass=1043.;freeze=true;can_sleep=false;continuous_cd=true
 center_of_mass_mode=RigidBody3D.CENTER_OF_MASS_MODE_CUSTOM;center_of_mass=Vector3(0,.435,(.5-SPORT.FRONT_WEIGHT)*2.266)
 inertia=Vector3(1130,1260,370)
 linear_damp_mode=RigidBody3D.DAMP_MODE_REPLACE;linear_damp=0
 angular_damp_mode=RigidBody3D.DAMP_MODE_REPLACE;angular_damp=.02
 var box=BoxShape3D.new();box.size=Vector3(1.56,.49,3.65);var c=CollisionShape3D.new();c.shape=box;c.position.y=.46;add_child(c)
 for pos in MOUNTS:
  var wh=Node3D.new();wh.position=pos-Vector3.UP*.205;add_child(wh);connected_wheels.append(wh)
func _notification(what):
 # Decided at add_child time (a SceneTree bench's root is not in the tree yet
 # during _initialize, so _enter_tree would land after the bench picked a mode).
 if what==NOTIFICATION_PARENTED:
  var mounted=get_parent().get("vehicle")==self
  steering_assist=mounted and not "--direct-steering" in OS.get_cmdline_user_args()
func restart(pos:Vector3,head:float):
 # Teleport while static, then clear the live body's velocity: Jolt treats a
 # velocity set on a frozen body as surface velocity, and deactivation already
 # zeroed the body's own, so the order used to rely on that side effect.
 freeze=true;global_transform=Transform3D(Basis(Vector3.UP,head),pos)
 freeze=false;linear_velocity=Vector3.ZERO;angular_velocity=Vector3.ZERO;sleeping=false
 rolling_slip.fill(0.)
 sound_throttle=0.;sound_combustion=0.;sound_shaft_load=0.
 selected_gear=0 if manual else 1;clutch_pedal=0.;stalled=false
 roof_down=false;flipped_seconds=0.;stuck_seconds=0.;stranded_seconds=0.
 omega=[0.,0.,0.,0.];gear=0;in_reverse=false;shift_time=0.;steering=0;steering_input=0;service_brake=false;rpm=850;engine_omega=rpm*TAU/60.;reset_physics_interpolation()
func crank_torque(n):
 var curve=[Vector2(800,94),Vector2(1750,127),Vector2(3000,146),Vector2(4150,151),Vector2(5000,161),Vector2(5900,156),Vector2(6500,153),Vector2(7000,137)]
 for i in range(1,curve.size()):
  if n<=curve[i].x:return lerpf(curve[i-1].y,curve[i].y,clampf(inverse_lerp(curve[i-1].x,curve[i].x,n),0,1))
 return 120.
# Polynomial smooth minimum (Quilez): equals the smaller value once the two
# differ by more than k, and rounds the corner where they cross.
static func smooth_min(a:float, b:float, k:float) -> float:
 var h := clampf(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
 return lerpf(b, a, h) - k * h * (1.0 - h)

# Rack angle a full press asks for at this speed. Assisted: the Ackermann angle
# for ASSIST_LATERAL_G plus the slip surplus at the limit, blended into the
# parking lock where the two meet (about 6 m/s). Direct: the previous map.
static func available_steering(speed_mps:float, assisted:bool=true) -> float:
 var speed := absf(speed_mps)
 if not assisted:
  var road_angle := ROAD_ASSIST_REFERENCE_RAD / (1.0 + speed * 0.044)
  return lerpf(PARKING_RACK_RAD, road_angle, smoothstep(2.0, 6.0, speed))
 var limit_angle := atan(WHEELBASE_M * ASSIST_LATERAL_G * G / maxf(speed * speed, 0.01)) + LIMIT_SLIP_SURPLUS_RAD
 return smooth_min(PARKING_RACK_RAD, limit_angle, LOCK_BLEND_RAD)

# How fast a held key moves the rack at this speed, in radians per second.
static func key_rack_rate(speed_mps:float) -> float:
 return lerpf(KEY_RACK_RATE_PARKING_RAD_S, KEY_RACK_RATE_HIGHWAY_RAD_S, smoothstep(5.0, 25.0, absf(speed_mps)))

# A stick has a few millimetres of thumb travel. Allocate its center to ordinary
# lane corrections and preserve full available steering at either end. This maps
# the driver's input without changing the contact forces or damping the chassis.
func thumbstick_command(stick:float,speed_mps:float) -> float:
 stick=clampf(stick,-1.,1.)
 var speed_squared=maxf(speed_mps*speed_mps,.01)
 var angle_for_cornering=atan(WHEELBASE_M*.89*G/speed_squared)
 var center_gain=clampf(angle_for_cornering/available_steering(speed_mps,steering_assist),.16,1.)
 return stick*(center_gain+(1.-center_gain)*stick*stick)

# Spend the actual fraction of this tick returning through center. A correction
# beyond center should release the existing lock as promptly as letting go.
# Rates are fractions of the available rack per second.
static func advance_steering(current:float, target:float, dt:float, apply_rate:float, return_rate:float)->float:
 var unwind_target := 0.0 if current*target<0.0 else target
 if absf(unwind_target)<absf(current):
  var unwind_seconds := absf(current-unwind_target)/return_rate
  current=move_toward(current,unwind_target,dt*return_rate)
  dt=maxf(0.0,dt-unwind_seconds)
 return move_toward(current,target,dt*apply_rate)

static func advance_thumbstick(current:float, target:float, dt:float)->float:
 return advance_steering(current,target,dt,2.75,4.4)

func drive(dt,gas,back,turn,foot,thumbstick:=false):
 var b=global_transform.basis;var vel=linear_velocity;var speed=-vel.dot(b.z);var throttle=0.
 service_brake=foot or (back>.01 and speed>.45) or (gas>.01 and speed<-.45)
 if not service_brake and not manual:
  if gas>0:in_reverse=false;throttle=gas
  elif back>0:in_reverse=true;throttle=back
 var brake_amount=1.0 if foot else (maxf(back,gas) if service_brake else 0.)
 if manual:
  throttle=gas;service_brake=foot or back>.01;brake_amount=maxf(back,1. if foot else 0.)
  in_reverse=selected_gear==-1
  var pedal=Input.get_action_strength("clutch")
  clutch_pedal=move_toward(clutch_pedal,pedal,dt*(14. if pedal>clutch_pedal else 2.8))
 var cap=available_steering(speed,steering_assist)
 if absf(turn)<STEER_DEAD_BAND:turn=0.
 if thumbstick:
  steering_input=advance_thumbstick(steering_input,turn,dt)
 elif steering_assist:
  # Key rates are set in rack angle, so the same key moves the wheel more
  # slowly as the cap closes with speed. Releasing or countersteering unwinds first.
  var apply=key_rack_rate(speed)/cap
  var back_rate=maxf(apply*KEY_RETURN_FACTOR,KEY_RETURN_RATE_MIN_RAD_S/cap)
  steering_input=advance_steering(steering_input,turn,dt,apply,back_rate)
 else:
  steering_input=move_toward(steering_input,turn,dt*(2.75 if turn!=0 else 4.4))
 steering=steering_input*cap
 roof_down=b.y.dot(Vector3.UP)<.2
 flipped_seconds=flipped_seconds+dt if roof_down else 0.
 var rack=absf(steering);var radius=2.266/maxf(.001,tan(rack))
 var inner=lerpf(rack,atan(2.266/maxf(.2,radius-.707)),.49);var outer=lerpf(rack,atan(2.266/(radius+.707)),.49)
 var space=get_world_3d().direct_space_state;var up=b.y;var origin=global_position;var com=origin+b*center_of_mass
 var points=[];var lengths=[REST,REST,REST,REST];wheel_contact=0
 for i in 4:
  var src=origin+b*MOUNTS[i];var ray=PhysicsRayQueryParameters3D.create(src,src-up*(REST+R),1,[get_rid()]);ray.hit_from_inside=false
  var hit=space.intersect_ray(ray);var row=[]
  if hit.size() and hit.normal.dot(up)>.33:
   var p:Vector3=hit.position;var n:Vector3=hit.normal;var len=clampf((src-p).dot(up)-R,SPORT.MIN_TRAVEL_M,REST);lengths[i]=len
   var arm=p-com;var v=vel+angular_velocity.cross(arm)
   var axle=0 if i<2 else 1
   var load=SPORT.contact_load(axle,len,v.dot(n),mass)
   var sign_side=-1.0 if i%2==0 else 1.0
   var angle=signf(steering)*(inner if (i==0 and steering>0) or (i==1 and steering<0) else outer) if i<2 else 0.
   angle+=sign_side*deg_to_rad(SPORT.TOE_PER_WHEEL_DEG)
   var camber=SPORT.STATIC_CAMBER_DEG[axle]-(SPORT.STATIC_LENGTH_M-len)*SPORT.CAMBER_PER_M[axle]
   wheel_camber[i]=-sign_side*deg_to_rad(camber)
   if i<2:wheel_camber[i]+=sin(angle)*deg_to_rad(SPORT.CASTER_DEG)
   wheel_heading[i]=angle
   var forward=(-b.z).rotated(up,angle).slide(n).normalized();var right=forward.cross(n).normalized()
   row=[p,n,load,forward,right,v.dot(forward),v.dot(right)];wheel_contact+=1
  connected_wheels[i].position=MOUNTS[i]-Vector3.UP*lengths[i];points.append(row)
 # Stranded: roof down for 1.5 s, or no wheel touching for 2 s while nearly still.
 stuck_seconds=stuck_seconds+dt if wheel_contact==0 and vel.length()<1. else 0.
 stranded_seconds=stranded_seconds+dt if flipped_seconds>1.5 or stuck_seconds>2. else 0.
 for i in 4:
  if not points[i].is_empty():points[i][2]=maxf(0.,points[i][2]+(lengths[i^1]-lengths[i])*SPORT.roll_bar_rate(0 if i<2 else 1))
 var forward_ratio=(3.758 if in_reverse else GEARS[gear])*FINAL_DRIVE
 var sign_dir=-1. if in_reverse else 1.
 var axle_speed=maxf(0.,(omega[2]+omega[3])*.5*sign_dir)
 shift_time=maxf(0,shift_time-dt)
 if not manual and shift_time<=0 and not in_reverse:
  var next=gear
  if rpm>6700 and gear<4 and absf(speed)*forward_ratio/R>590:next+=1
  elif rpm<2100 and gear>0:next-=1
  if next!=gear:gear=next;shift_time=.21;forward_ratio=GEARS[gear]*FINAL_DRIVE
 var net=[0.,0.,0.,0.];var side=[0.,0.,0.,0.]
 for i in 4:
  if points[i].is_empty():
   spring_load[i]=0
   rolling_slip[i]=0.
   continue
  var row=points[i];var fz=float(row[2]);spring_load[i]=fz
  var slip_angle=0.
  if fz>0:slip_angle=contact_slip(i,row[5],row[6],dt)
  else:rolling_slip[i]=0.
  # Saturating slip-angle curve; a loaded outside tyre gains less grip per unit of load.
  var mu=clampf(static_mu*(1.-.095*(fz/2557.-1)),.7,1.16)*surface_grip[i]
  var lateral=-fz*mu*LATERAL_GRIP_SCALE*tanh(slip_angle*10.8)*(1.-.13*smoothstep(.18,.65,absf(slip_angle)))
  var road_lean=asin(clampf(b.x.dot(row[1]),-1.,1.))
  lateral-=fz*.48*(wheel_camber[i]+road_lean)*minf(1.,absf(row[5])/3.)
  side[i]=lateral;row.append(mu)
 sound_throttle=throttle
 sound_combustion=0.
 sound_shaft_load=0.
 for step in 6:
  var ds=dt/6.;rpm=engine_omega*60/TAU;var friction=10.+.0020*rpm
  var torque=(crank_torque(rpm)+friction)*throttle if rpm<7040 else 0.
  var drag=friction*(1.0 if rpm>850 else .5)
  var governor=clampf((875-rpm)*.8,0,100.)
  axle_speed=(omega[2]+omega[3])*.5*sign_dir
  var clutch_load=clampf((engine_omega-axle_speed*forward_ratio)*12.0,-200.,210.)
  var target=1000.+throttle*2250.
  if manual:
   var capacity=210.*(1.-smoothstep(.16,.88,clutch_pedal))
   clutch_load=0. if selected_gear==0 else clampf((engine_omega-axle_speed*forward_ratio)*10.,-capacity,capacity)
   if stalled:torque=0.;governor=0.;drag=minf(26.,engine_omega*.85)
  elif axle_speed*forward_ratio*60/TAU<target and clutch_load>0:
   clutch_load=minf(clutch_load,maxf(0.,torque-drag+(rpm-target)*.16))
  if shift_time>0:clutch_load=0;torque*=.12
  if not manual and throttle<.01 and rpm<1050 and absf(speed)<3:clutch_load=0
  sound_combustion+=(torque+governor)/(6.*180.)
  sound_shaft_load+=clutch_load/(6.*180.)
  engine_omega=clampf(engine_omega+(torque+governor-drag-clutch_load)*ds/.18,0. if manual else 78.,750.)
  if manual and not stalled and engine_omega<35.:stalled=true
  var axle_torque=clutch_load*forward_ratio*.895*sign_dir
  var drive_torque=SPORT.differential_torque(axle_torque,omega[2],omega[3],WHEEL_I,ds,clutch_load>0.)
  for i in 4:
   var wheel_t=drive_torque[i-2] if i>1 else 0.
   var br=brake_amount*(1150. if i<2 else 650.)
   if InputMap.has_action("parking_brake") and Input.is_action_pressed("parking_brake") and i>1:br=maxf(br,900.)
   var spin=move_toward(omega[i]+wheel_t*ds/WHEEL_I,0.,br*ds/WHEEL_I);var row=points[i]
   if row.is_empty():omega[i]=spin;continue
   var fz=float(row[2]);var fxmax=fz*float(row[7])
   # Solve wheel inertia and contact force together for stable rolling at low speed.
   var long_stiff=(fz*12.)/maxf(1.5,absf(row[5]))
   var fx=long_stiff*(spin*R-float(row[5]))/(1.+long_stiff*R*R*ds/WHEEL_I)
   var fy=float(side[i]);var budget=Vector2(fx,fy/LATERAL_GRIP_SCALE).length()
   if budget>fxmax and budget>.01:fx*=fxmax/budget;fy*=fxmax/budget
   # As a braked tire stops rotating, the tread slides across the road with
   # the contact patch velocity. Preserve the existing friction envelope,
   # blending out the rolling-slip law only over the last 20% before lock.
   var forward_mps:float=row[5];var sideways_mps:float=row[6]
   var lock_weight=0.
   if br>0.:
    var rolling_fraction=absf(spin*R)/maxf(.10,absf(forward_mps))
    lock_weight=(1.-smoothstep(0.,.20,rolling_fraction))*smoothstep(1.5,3.0,Vector2(forward_mps,sideways_mps).length())
   if lock_weight>0.:
    var slide_reaction=Vector2(spin*R-forward_mps,-sideways_mps)
    var ellipse_length=Vector2(slide_reaction.x,slide_reaction.y/LATERAL_GRIP_SCALE).length()
    var sliding=slide_reaction*(fxmax/maxf(ellipse_length,.001))
    fx=lerpf(fx,sliding.x,lock_weight)
    fy=lerpf(fy,sliding.y,lock_weight)
   var end_spin=spin-fx*R*ds/WHEEL_I
   if br>0 and is_zero_approx(spin) and absf(fx*R)<=br:end_spin=0
   omega[i]=end_spin;net[i]+=fx/6.
   if row.size()<9:row.append(0.)
   row[8]+=fy/6.
 for i in 4:
  var row=points[i];if row.is_empty():continue
  apply_force(row[1]*row[2]+row[3]*net[i]+row[4]*row[8],row[0]-origin)
 if vel.length()>.02:
  apply_central_force(-vel.normalized()*.5*1.225*.65*vel.length_squared())
  var rolling=vel.slide(Vector3.UP);if rolling.length()>.01 and wheel_contact>0:apply_central_force(-rolling.normalized()*minf(rolling.length()*mass/dt,mass*G*.012))
 rpm=engine_omega*60/TAU
 return speed
