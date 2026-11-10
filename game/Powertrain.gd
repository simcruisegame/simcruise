extends RigidBody3D
# SI units, +X right, -Z forwards. Springs, dampers, tires and brakes act at each contact.
# Every car-specific number comes from a CarSpec (res://cars/*.json); the
# defaults are the 1999 NB Sport, so the Miata's calibration is unchanged.
const SPORT=preload("res://NB1999Sport.gd")
const CAR_SPEC=preload("res://CarSpec.gd")
const SUSPENSION=preload("res://CarSuspension.gd")
var spec
var suspension
var GEARS:Array
var FINAL_DRIVE:float
# Nominal free radius from the tyre size. Effective loaded rolling circumference still needs measurement.
var R:float
var WHEELBASE_M:float
var PARKING_RACK_RAD:float
var ROAD_ASSIST_REFERENCE_RAD:float
# Steering map. Assisted lets a full key or stick ask for the rack angle at the
# front axle's grip limit: the Ackermann angle for ASSIST_LATERAL_G plus the
# front-minus-rear slip this tyre model carries at the limit, never more than
# the rack lock. Direct keeps the previous 35.5° map and constant ramp rates:
# the map for a device that positions the rack itself, a real wheel or a bench
# servo such as checks/Track.gd. A car mounted as a scene's `vehicle` is driven
# by a person, so it enters the tree Assisted; --direct-steering keeps Direct.
var steering_assist := false
var ASSIST_LATERAL_G:float
# Rack beyond Ackermann this tyre model needs at the limit: Track.gd's 20.0 m/s
# lap holds 0.885 g with about 0.40 × 18.9° = 7.6° of rack, 2.9° of it Ackermann.
# A held key at 45 mph then reaches 0.86-0.97 g with 10-11° of front slip (the
# curve peaks near 14°). A tyre curve with a sharper peak would lower this.
var LIMIT_SLIP_SURPLUS_RAD:float
const LOCK_BLEND_RAD = 3.0 * PI / 180.0
# A held key moves the rack at a hand's pace: the previous 2.75/s of lock while
# parking, easing to 12°/s (about 160°/s at the wheel) by highway speed, so a
# half-second tap at 60 mph reaches 6° instead of the lock. Release is quicker.
var KEY_RACK_RATE_PARKING_RAD_S:float
const KEY_RACK_RATE_HIGHWAY_RAD_S = 12.0 * PI / 180.0
const KEY_RETURN_RATE_MIN_RAD_S = 24.0 * PI / 180.0
const KEY_RETURN_FACTOR = 1.6
const STEER_DEAD_BAND = 0.01
# Roof-down or stranded timers for Drive's recovery prompt.
var roof_down := false
var flipped_seconds := 0.
var stuck_seconds := 0.
var stranded_seconds := 0.
var REST:float
var wheel_camber=[0.,0.,0.,0.]
var wheel_heading=[0.,0.,0.,0.]
var MOUNTS:Array
var WHEEL_I:float
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
var tc_cut:=0.
var tc_integral:=0.
var tc_slip:=0.   # driven-wheel slip relative to the target as the controller reads it (1 = on target)
var tc_slip_filtered:=0.
var clutch_saturated:=false   # last substep: the clutch slipped at its capacity
# Traction control (cars that had it) cuts engine or motor torque to hold the
# most-slipping driven wheel at its tyre's peak slip (peak_slip: the end of
# the full-grip plateau, where the slip-ratio curve starts to fall), the same
# slip ABS holds under braking. Not past it: beyond the peak the force no
# longer rises with slip, nothing damps the wheel and a target there (1.15 or
# 1.3 × the peak) oscillated for 1-2.5 s in checks/TractionControlCheck.gd.
# The controller reads the slip as wheel-speed sensors and an ECU do: through
# a 20 ms low-pass filter and a slew limit, so one substep's spike cannot
# swing the torque. It acts proportional plus integral on the error, cuts
# harder on a large overshoot, holds its state through a gear change, does not
# wind up while the clutch slips at its capacity (the wheels then take the
# flywheel, not the throttle), and changes the cut no faster than an engine
# can (off within 50 ms by spark retard and fuel cut, back on over 0.15 s).
const TC_FILTER_S = .02
const TC_SLIP_SLEW_PER_S = 100.     # the read slip moves at most one target per 10 ms
const TC_PROPORTIONAL = .15
const TC_OVERSHOOT = .05           # extra cut per unit of error beyond 1 (slip twice the target)
const TC_CUT_RATE_UP = 20.
const TC_CUT_RATE_DOWN = 6.
# Benches set this to read traction control and driveline torque every force
# substep (checks/TractionControlCheck.gd): [tc_cut, clutch or motor torque, shifting].
var trace_substeps := false
var substep_trace := []
func _init():
 use_spec(CAR_SPEC.new())
# Install a car. Call before setup(); the body, contacts and wheels follow it.
func use_spec(car_spec):
 spec=car_spec;suspension=SUSPENSION.new(spec)
 GEARS=spec.gears;FINAL_DRIVE=spec.final_drive;R=spec.tire_radius_m();WHEELBASE_M=spec.wheelbase_m
 PARKING_RACK_RAD=spec.parking_rack_deg * PI / 180.0
 ROAD_ASSIST_REFERENCE_RAD=spec.road_reference_deg * PI / 180.0
 ASSIST_LATERAL_G=spec.assist_lateral_g
 LIMIT_SLIP_SURPLUS_RAD=spec.limit_slip_surplus_deg * PI / 180.0
 KEY_RACK_RATE_PARKING_RAD_S=2.75 * PARKING_RACK_RAD
 REST=spec.max_travel_m;MOUNTS=spec.mounts();WHEEL_I=spec.wheel_inertia
 LATERAL_GRIP_SCALE=spec.lateral_grip_scale;static_mu=spec.static_mu;SIDE_RELAXATION_M=spec.relaxation_m
 rpm=spec.idle_rpm;engine_omega=spec.idle_omega
func electric()->bool:return spec.engine=="electric"
func set_manual(enabled):
 if electric():enabled=false
 manual=enabled;clutch_pedal=0.;stalled=false
 selected_gear=-1 if in_reverse else gear+1
 engine_omega=maxf(engine_omega,spec.idle_omega)
 if manual and linear_velocity.length()<1.:select_ratio(0)
func select_ratio(ratio):
 selected_gear=ratio
 in_reverse=ratio==-1
 gear=clampi(ratio-1,0,GEARS.size()-1)
func change_gear(step):
 if not manual:return ""
 var proposed=clampi(selected_gear+step,-1,GEARS.size())
 if proposed==selected_gear:return ""
 if clutch_pedal<.75:return "Hold the clutch to change gear"
 var forward_speed=-linear_velocity.dot(global_transform.basis.z)
 if (proposed==-1 and forward_speed>1.) or (proposed>0 and forward_speed< -1.):return "Stop before changing direction"
 if proposed>0 and absf(forward_speed)/R*GEARS[proposed-1]*FINAL_DRIVE*60./TAU>spec.max_gear_rpm:return "Speed is too high for that gear"
 select_ratio(proposed)
 return ""
func start_engine():
 if manual and stalled and (selected_gear==0 or clutch_pedal>.75):
  engine_omega=spec.idle_omega;rpm=spec.idle_rpm;stalled=false
func gear_text():
 if electric():return "R" if in_reverse else "D"
 if manual:
  if selected_gear==0:return "N"
  return "R" if selected_gear==-1 else str(selected_gear)
 return "R" if in_reverse else str(gear+1)
var shift_time=0.
var steering_input=0.
var steering=0.
var wheel_contact=0
var service_brake=false
var LATERAL_GRIP_SCALE:float
var static_mu:float
var surface_grip=[1.,1.,1.,1.]
# Road profile under each tyre (m, + raises the road) and its rate of change
# (m/s), set by Drive.gd from RoadRoughness.gd. Zero on test pads.
var road_profile=[0.,0.,0.,0.]
var road_profile_rate=[0.,0.,0.,0.]
var spring_load=[0.,0.,0.,0.]
# Rolling distance to 63% of steady small-slip response. This is a representative
# passenger radial estimate; no transient force records for the OE 195/50R15.
var SIDE_RELAXATION_M:float
var rolling_slip = [0.,0.,0.,0.]
var slip_ratio = [0.,0.,0.,0.]      # longitudinal slip per wheel, last substep
# Driven-wheel slip as traction control measures it: over the road speed, but
# never over less than 1.5 m/s, the floor the tyre force law uses, so at a
# walking pace it reacts to the slip that already saturates the tyre.
var tc_wheel_slip = [0.,0.,0.,0.]
# Share of each tyre's grip its longitudinal force used, last substep (benches read it).
var grip_use = [0.,0.,0.,0.]
var abs_release = [1.,1.,1.,1.]     # ABS pressure fraction per wheel
var peak_slip = [.1,.1,.1,.1]       # slip ratio at peak force per wheel, last substep
# ABS modulator: integral gain on the slip error (1/s), a proportional dump
# when the wheel is past its peak, and the slip it aims at relative to the peak.
const ABS_GAIN_PER_S = 20.
const ABS_PROPORTIONAL = .3
const ABS_TARGET_OVER_PEAK = 1.
var contact_rows := []
func row_speed(i:int)->float:
 return float(contact_rows[i][5]) if i<contact_rows.size() and not contact_rows[i].is_empty() else 0.

func contact_slip(i: int, longitudinal: float, transverse: float, dt: float, fz: float = -1.) -> float:
 var speed := absf(longitudinal)
 var target := atan2(transverse,maxf(1.8,speed))
 # Below walking speed the existing regularized contact also handles static
 # friction. Blend continuously into the rolling-tire model as travel builds.
 # A lightly loaded tyre builds side force over a shorter distance.
 var load_scale := pow(clampf(fz/spec.reference_corner_load_n,.25,2.5),spec.relaxation_load_exponent) if fz>0. else 1.
 var length_m := SIDE_RELAXATION_M*load_scale*smoothstep(.8,3.5,speed)
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
 mass=spec.mass_kg;freeze=true;can_sleep=false;continuous_cd=true
 center_of_mass_mode=RigidBody3D.CENTER_OF_MASS_MODE_CUSTOM;center_of_mass=Vector3(0,spec.cg_height_m,(.5-spec.front_weight)*spec.wheelbase_m)
 inertia=spec.inertia
 linear_damp_mode=RigidBody3D.DAMP_MODE_REPLACE;linear_damp=0
 angular_damp_mode=RigidBody3D.DAMP_MODE_REPLACE;angular_damp=spec.angular_damp
 var box=BoxShape3D.new();box.size=spec.collision_box;var c=CollisionShape3D.new();c.shape=box;c.position.y=spec.collision_box_y;add_child(c)
 for pos in MOUNTS:
  var wh=Node3D.new();wh.position=pos-Vector3.UP*spec.hub_drop_m;add_child(wh);connected_wheels.append(wh)
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
 omega=[0.,0.,0.,0.];gear=0;in_reverse=false;shift_time=0.;steering=0;steering_input=0;service_brake=false;rpm=spec.idle_rpm;engine_omega=rpm*TAU/60.;reset_physics_interpolation()
func crank_torque(n):
 var curve=spec.torque_curve
 for i in range(1,curve.size()):
  if n<=curve[i].x:return lerpf(curve[i-1].y,curve[i].y,clampf(inverse_lerp(curve[i-1].x,curve[i].x,n),0,1))
 return spec.torque_past_curve_nm
# Polynomial smooth minimum (Quilez): equals the smaller value once the two
# differ by more than k, and rounds the corner where they cross.
static func smooth_min(a:float, b:float, k:float) -> float:
 var h := clampf(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
 return lerpf(b, a, h) - k * h * (1.0 - h)

# Rack angle a full press asks for at this speed. Assisted: the Ackermann angle
# for ASSIST_LATERAL_G plus the slip surplus at the limit, blended into the
# parking lock where the two meet (about 6 m/s). Direct: the previous map.
func available_steering(speed_mps:float, assisted:bool=true) -> float:
 var speed := absf(speed_mps)
 if not assisted:
  var road_angle := ROAD_ASSIST_REFERENCE_RAD / (1.0 + speed * 0.044)
  return lerpf(PARKING_RACK_RAD, road_angle, smoothstep(2.0, 6.0, speed))
 var limit_angle := atan(WHEELBASE_M * ASSIST_LATERAL_G * G / maxf(speed * speed, 0.01)) + LIMIT_SLIP_SURPLUS_RAD
 return smooth_min(PARKING_RACK_RAD, limit_angle, LOCK_BLEND_RAD)

# How fast a held key moves the rack at this speed, in radians per second.
func key_rack_rate(speed_mps:float) -> float:
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
  # The selector changes direction only near standstill, like a real
  # automatic's reverse inhibitor: a pedal below the brake threshold while
  # rolling the other way coasts instead of engaging the opposite gear.
  if gas>0 and speed>-.45:in_reverse=false;throttle=gas
  elif back>0 and speed<.45:in_reverse=true;throttle=back
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
 var half_track=spec.track_front_m*.5;var blend=spec.ackermann_blend
 var rack=absf(steering);var radius=WHEELBASE_M/maxf(.001,tan(rack))
 var inner=lerpf(rack,atan(WHEELBASE_M/maxf(.2,radius-half_track)),blend);var outer=lerpf(rack,atan(WHEELBASE_M/(radius+half_track)),blend)
 var space=get_world_3d().direct_space_state;var up=b.y;var origin=global_position;var com=origin+b*center_of_mass
 var points=[];var lengths=[REST,REST,REST,REST];wheel_contact=0
 for i in 4:
  var src=origin+b*MOUNTS[i];var ray=PhysicsRayQueryParameters3D.create(src,src-up*(REST+R),1,[get_rid()]);ray.hit_from_inside=false
  var hit=space.intersect_ray(ray);var row=[]
  if hit.size() and hit.normal.dot(up)>.33:
   var p:Vector3=hit.position;var n:Vector3=hit.normal;var len=clampf((src-p).dot(up)-R-road_profile[i],spec.min_travel_m,REST);lengths[i]=len
   var arm=p-com;var v=vel+angular_velocity.cross(arm)
   var axle=0 if i<2 else 1
   var load=suspension.contact_load(axle,len,v.dot(n)-road_profile_rate[i],mass)
   var sign_side=-1.0 if i%2==0 else 1.0
   var angle=signf(steering)*(inner if (i==0 and steering>0) or (i==1 and steering<0) else outer) if i<2 else 0.
   angle+=sign_side*deg_to_rad(spec.toe_per_wheel_deg)
   var camber=spec.static_camber_deg[axle]-(spec.static_length_m-len)*spec.camber_per_m[axle]
   wheel_camber[i]=-sign_side*deg_to_rad(camber)
   if i<2:wheel_camber[i]+=sin(angle)*deg_to_rad(spec.caster_deg)
   wheel_heading[i]=angle
   var forward=(-b.z).rotated(up,angle).slide(n).normalized();var right=forward.cross(n).normalized()
   row=[p,n,load,forward,right,v.dot(forward),v.dot(right)];wheel_contact+=1
  connected_wheels[i].position=MOUNTS[i]-Vector3.UP*lengths[i];points.append(row)
 # Stranded: roof down for 1.5 s, or no wheel touching for 2 s while nearly still.
 stuck_seconds=stuck_seconds+dt if wheel_contact==0 and vel.length()<1. else 0.
 stranded_seconds=stranded_seconds+dt if flipped_seconds>1.5 or stuck_seconds>2. else 0.
 for i in 4:
  if not points[i].is_empty():points[i][2]=maxf(0.,points[i][2]+(lengths[i^1]-lengths[i])*suspension.roll_bar_rate(0 if i<2 else 1))
 var forward_ratio=(spec.reverse_ratio if in_reverse else GEARS[gear])*FINAL_DRIVE
 var sign_dir=-1. if in_reverse else 1.
 var axle_speed=maxf(0.,driven_omega()*sign_dir)
 shift_time=maxf(0,shift_time-dt)
 if not manual and shift_time<=0 and not in_reverse:
  var next=gear
  if rpm>spec.upshift_rpm and gear<GEARS.size()-1 and absf(speed)*forward_ratio/R>spec.upshift_min_omega:next+=1
  elif rpm<spec.downshift_rpm and gear>0:next-=1
  if next!=gear:gear=next;shift_time=spec.shift_seconds;forward_ratio=GEARS[gear]*FINAL_DRIVE
 var net=[0.,0.,0.,0.];var side=[0.,0.,0.,0.];contact_rows=points
 for i in 4:
  if points[i].is_empty():
   spring_load[i]=0
   rolling_slip[i]=0.
   continue
  var row=points[i];var fz=float(row[2]);spring_load[i]=fz
  var slip_angle=0.
  if fz>0:slip_angle=contact_slip(i,row[5],row[6],dt,fz)
  else:rolling_slip[i]=0.
  # Saturating slip-angle curve; a loaded outside tyre gains less grip per unit of load.
  var mu=clampf(static_mu*(1.-spec.load_sensitivity*(fz/spec.reference_corner_load_n-1)),spec.mu_min,spec.mu_max)*surface_grip[i]*float(spec.axle_grip[0 if i<2 else 1])
  var lateral=-fz*mu*LATERAL_GRIP_SCALE*tanh(slip_angle*spec.slip_stiffness)*(1.-spec.slip_falloff*smoothstep(spec.slip_falloff_from,spec.slip_falloff_to,absf(slip_angle)))
  var road_lean=asin(clampf(b.x.dot(row[1]),-1.,1.))
  lateral-=fz*spec.camber_thrust*(wheel_camber[i]+road_lean)*minf(1.,absf(row[5])/3.)
  side[i]=lateral;row.append(mu)
 var throttle_aid=spec.traction_control
 if not throttle_aid or throttle<=0.:tc_cut=0.;tc_integral=0.;tc_slip=0.;tc_slip_filtered=0.
 var pedal_throttle=throttle
 sound_throttle=throttle
 sound_combustion=0.
 sound_shaft_load=0.
 if trace_substeps:substep_trace.clear()
 for step in 6:
  var ds=dt/6.
  # Traction control (cars that have it): engine or motor torque reduction,
  # updated every substep, from how far the most-slipping driven wheel is
  # past its target slip.
  if throttle_aid and pedal_throttle>0.:
   var ratio=0.
   for i in 4:
    if spec.driven(i) and spring_load[i]>0.:ratio=maxf(ratio,tc_wheel_slip[i]*sign_dir/maxf(maxf(spec.tc_target_slip,peak_slip[i]),.01))
   tc_slip_filtered+=(ratio-tc_slip_filtered)*(1.-exp(-ds/TC_FILTER_S))
   tc_slip=move_toward(tc_slip,tc_slip_filtered,TC_SLIP_SLEW_PER_S*ds)
   var excess=tc_slip-1.
   if shift_time<=0.:
    # While the clutch slips at its capacity the wheels take the flywheel, not
    # the throttle: cutting harder would not help, so the cut does not grow.
    var integral_step=ds*spec.tc_gain*clampf(excess,-1.,.5)
    if not clutch_saturated or integral_step<0.:tc_integral=clampf(tc_integral+integral_step,0.,.9)
    var wanted=clampf(tc_integral+TC_PROPORTIONAL*clampf(excess,-1.,2.)+TC_OVERSHOOT*maxf(excess-1.,0.),0.,.9)
    tc_cut=clampf(wanted,tc_cut-TC_CUT_RATE_DOWN*ds,tc_cut+(0. if clutch_saturated else TC_CUT_RATE_UP*ds))
   throttle=pedal_throttle*(1.-tc_cut)
  var clutch_load:float
  if electric():
   clutch_load=motor_torque(throttle,forward_ratio,sign_dir,speed)
  else:
   rpm=engine_omega*60/TAU;var friction=spec.engine_friction_nm+spec.engine_friction_per_rpm*rpm
   var torque=(crank_torque(rpm)+friction)*throttle if rpm<spec.fuel_cut_rpm else 0.
   var drag=friction*(1.0 if rpm>spec.idle_rpm else .5)
   var governor=clampf((spec.governor_rpm-rpm)*.8,0,100.)
   axle_speed=driven_omega()*sign_dir
   # The clutch (or torque converter, dual clutch) couples the engine and the
   # driven wheels with a torque proportional to their speed difference. In low
   # gears, with the tyres at their grip limit, the wheels reflect so little
   # inertia that this explicit coupling overshot every substep: the clutch
   # torque alternated sign by tens of percent once a dual clutch locked up.
   # Never couple harder than the torque that closes the gap in one substep.
   var reflected=forward_ratio*forward_ratio*spec.driveline_efficiency/(WHEEL_I*(4. if spec.layout=="awd" else 2.))
   var coupling=minf(spec.clutch_stiffness,1./(ds*(1./spec.engine_inertia+reflected)))
   clutch_load=clampf((engine_omega-axle_speed*forward_ratio)*coupling,-spec.clutch_back_nm,spec.clutch_capacity_nm)
   # The launch rpm follows the pedal, not the torque traction control leaves.
   var target=spec.launch_rpm+pedal_throttle*spec.launch_rpm_per_throttle
   if manual:
    var capacity=spec.clutch_capacity_nm*(1.-smoothstep(.16,.88,clutch_pedal))
    clutch_load=0. if selected_gear==0 else clampf((engine_omega-axle_speed*forward_ratio)*spec.manual_clutch_stiffness,-capacity,capacity)
    if stalled:torque=0.;governor=0.;drag=minf(26.,engine_omega*.85)
   elif axle_speed*forward_ratio*60/TAU<target and clutch_load>0:
    clutch_load=minf(clutch_load,maxf(0.,torque-drag+(rpm-target)*.16))
   # A gear change interrupts the drive, except on a dual clutch, which hands
   # the torque from one clutch to the other without a gap.
   if shift_time>0 and spec.transmission!="dct":clutch_load=0;torque*=.12
   if not manual and throttle<.01 and rpm<spec.idle_rpm+200. and absf(speed)<3:clutch_load=0
   sound_combustion+=(torque+governor)/(6.*180.)
   sound_shaft_load+=clutch_load/(6.*180.)
   engine_omega=clampf(engine_omega+(torque+governor-drag-clutch_load)*ds/spec.engine_inertia,0. if manual else spec.min_omega_auto,spec.max_omega)
   if manual and not stalled and engine_omega<spec.stall_omega:stalled=true
  clutch_saturated=not electric() and clutch_load>=spec.clutch_capacity_nm*.98
  if trace_substeps:substep_trace.append([tc_cut,clutch_load,shift_time>0.])
  var axle_torque=clutch_load*forward_ratio*spec.driveline_efficiency*sign_dir
  var drive_torque=wheel_torques(axle_torque,ds,clutch_load>0. if not electric() else absf(clutch_load)>0.)
  for i in 4:
   var wheel_t=drive_torque[i]
   var br=brake_amount*(spec.brake_front_nm if i<2 else spec.brake_rear_nm)
   # ABS (cars that had it): a per-wheel pressure modulator. It integrates the
   # slip error and dumps in proportion past the peak, so each tyre cycles
   # about its peak slip instead of locking, as production systems do.
   if spec.abs_enabled and br>0. and absf(row_speed(i))>1.5:
    var target=peak_slip[i]*ABS_TARGET_OVER_PEAK
    # Braking slip is positive as the wheel turns slower than the road in the
    # direction the car travels, forwards or in reverse.
    var braking_slip=-slip_ratio[i]*signf(row_speed(i))
    var slip_error=(target-braking_slip)/target
    abs_release[i]=clampf(abs_release[i]+ds*ABS_GAIN_PER_S*clampf(slip_error,-3.,1.),.05,1.)
    br*=clampf(abs_release[i]+ABS_PROPORTIONAL*clampf(slip_error,-1.,0.),0.,1.)
   else:abs_release[i]=1.
   if InputMap.has_action("parking_brake") and Input.is_action_pressed("parking_brake") and i>1:br=maxf(br,spec.parking_brake_nm)
   var spin=move_toward(omega[i]+wheel_t*ds/WHEEL_I,0.,br*ds/WHEEL_I);var row=points[i]
   if row.is_empty():omega[i]=spin;continue
   var fz=float(row[2])
   # Slip-ratio curve: the friction circle holds its full size up to the peak
   # slip and shrinks toward the sliding value as the wheel spins or locks, so a
   # spinning rear tyre also loses side grip (combined slip, via the budget).
   # The slip is the wheel's as it stands (end of the last substep, where brake
   # and contact impulses balance), not the brake-only intermediate spin.
   var kappa=(omega[i]*R-float(row[5]))/maxf(absf(float(row[5])),5.)
   slip_ratio[i]=kappa
   tc_wheel_slip[i]=(omega[i]*R-float(row[5]))/maxf(absf(float(row[5])),1.5)
   # The force cannot fall before it has peaked: a grippy tyre on this linear
   # stiffness reaches its full grip at mu/stiffness, which can pass the
   # tyre type's nominal peak slip.
   peak_slip[i]=maxf(spec.slip_ratio_peak,float(row[7])/spec.longitudinal_stiffness)
   var fxmax=fz*float(row[7])*(1.-spec.slip_ratio_falloff*smoothstep(peak_slip[i],peak_slip[i]*3.5,absf(kappa)))
   # Solve wheel inertia and contact force together for stable rolling at low speed.
   var long_stiff=(fz*spec.longitudinal_stiffness)/maxf(1.5,absf(row[5]))
   var fx=long_stiff*(spin*R-float(row[5]))/(1.+long_stiff*R*R*ds/WHEEL_I)
   var fy=float(side[i]);var budget=Vector2(fx,fy/LATERAL_GRIP_SCALE).length()
   if budget>fxmax and budget>.01:fx*=fxmax/budget;fy*=fxmax/budget
   grip_use[i]=fx/maxf(fz*float(row[7]),1.)
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
  apply_central_force(-vel.normalized()*.5*1.225*spec.drag_area_m2*vel.length_squared())
  var rolling=vel.slide(Vector3.UP);if rolling.length()>.01 and wheel_contact>0:apply_central_force(-rolling.normalized()*minf(rolling.length()*mass/dt,mass*G*spec.rolling_resistance))
 rpm=engine_omega*60/TAU
 return speed

# Mean speed of the driven wheels as the propeller shaft sees it. An AWD centre
# differential with front share s turns at s·front + (1−s)·rear.
func driven_omega()->float:
 match spec.layout:
  "fwd":return (omega[0]+omega[1])*.5
  "awd":return spec.awd_front_share*(omega[0]+omega[1])*.5+(1.-spec.awd_front_share)*(omega[2]+omega[3])*.5
 return (omega[2]+omega[3])*.5

# Axle torque to the four wheels through the centre and axle differentials.
func wheel_torques(axle_torque:float,ds:float,drive:bool)->Array:
 match spec.layout:
  "fwd":
   var f=SUSPENSION.differential_torque(axle_torque,omega[0],omega[1],WHEEL_I,ds,drive,spec.front_diff_bias)
   return [f.x,f.y,0.,0.]
  "awd":
   var s:float=spec.awd_front_share
   var front=axle_torque*s;var rear=axle_torque*(1.-s)
   if drive and spec.center_bias>1. and ds>0.:
    # Limited-slip centre: torque moves toward the slower axle within the bias.
    var limit=absf(axle_torque)*.5*(spec.center_bias-1.)/(spec.center_bias+1.)
    var gap=((omega[2]+omega[3])-(omega[0]+omega[1]))*.5*WHEEL_I*2./ds
    var moved=clampf(gap,-limit,limit)
    front+=moved;rear-=moved
   var f=SUSPENSION.differential_torque(front,omega[0],omega[1],WHEEL_I,ds,drive,spec.front_diff_bias)
   var r=SUSPENSION.differential_torque(rear,omega[2],omega[3],WHEEL_I,ds,drive,spec.rear_diff_bias)
   return [f.x,f.y,r.x,r.y]
 var v=SUSPENSION.differential_torque(axle_torque,omega[2],omega[3],WHEEL_I,ds,drive,spec.rear_diff_bias)
 return [0.,0.,v.x,v.y]

# Electric drive: constant torque to the base speed, constant power above it,
# nothing past the motor's top speed; light regeneration when coasting.
# Returns torque at the motor (the clutch_load slot of a combustion car).
func motor_torque(throttle:float,ratio:float,sign_dir:float,speed:float)->float:
 var motor_omega=driven_omega()*sign_dir*ratio
 var available=minf(spec.motor_peak_torque_nm,spec.motor_peak_power_w/maxf(absf(motor_omega),1.))
 if absf(motor_omega)*60./TAU>spec.motor_max_rpm:available=0.
 var torque=available*throttle
 if throttle<.01 and absf(speed)>1.:torque=-spec.regen_torque_nm*smoothstep(1.,5.,absf(speed))*signf(motor_omega)
 engine_omega=absf(motor_omega)
 sound_combustion+=absf(torque)/(6.*maxf(spec.motor_peak_torque_nm,1.))
 sound_shaft_load+=torque/(6.*maxf(spec.motor_peak_torque_nm,1.))
 return torque
