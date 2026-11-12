extends SceneTree
# Keyboard tap bench. Hold a cruise speed on the flat slab, press full right for
# a moment, release, and report what the chassis did. A key asks for the
# whole available rack, so the half-second press is the manoeuvre that puts a
# keyboard driver in the ditch; the 0.1 s taps (an ordinary keypress) are the
# smallest correction a keyboard driver can make, which must be a correction,
# not a lane change. A last case drops the car on its roof and times the
# stranded detector that Drive turns into the "press R" prompt. Same fixed step
# as Track.gd:
#   godot --headless --path game --fixed-fps 60 --script res://checks/KeyboardTap.gd
#   add "-- --direct" to bench the Direct (wheel) steering map instead of Assisted.
# Gate per half-second case: |heading change| < 30 deg two seconds after
# release, roll < 10 deg and no rollover. Per 0.1 s tap (assisted map): lateral
# offset from the original line under 1 m five seconds after the press began
# (the rack-rate map before November 2026 gave 3.2 m at 40 mph and 2.0 m at
# 60 mph). Roof case: the stranded timer starts 1.4-2.5 s after the drop and
# restart() clears it. Only the final lines are printed; a hard tick cap ends
# a stall.
# [mph, press seconds, seconds reported after release]
const CASES=[[45.,.5,2.],[60.,.5,2.],[40.,.1,4.9],[60.,.1,4.9]]
const SETTLE_SECONDS=1.5
const ROOF_SECONDS=4.0
const MAX_TICKS=60*120
const TAP_OFFSET_LIMIT_M=1.
var car
var tick=0
var case_index=0
var state=0
var t=0.
var speed_i=0.
var start_yaw=0.
var start_pos=Vector3.ZERO
var start_right=Vector3.RIGHT
var peak_roll=0.
var peak_rack=0.
var peak_yaw_rate=0.
var peak_slip=0.
var peak_heading=0.
var heading_at_release=0.
var min_contacts=4
var airborne=0.
var rolled=false
var roof_prompt_at=-1.
var done=false
var passed=true
func _initialize():
 var slab=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(2500,1,5000);col.shape=box;slab.position.y=-.5;slab.add_child(col);root.add_child(slab)
 car=load("res://Powertrain.gd").new();car.setup();root.add_child(car)
 # A bench car enters the tree Direct; this one stands in for a person's key.
 if "steering_assist" in car:car.steering_assist=not "--direct" in OS.get_cmdline_user_args()
 begin_case()
func begin_case():
 car.restart(Vector3(0,.18,0),0.)
 state=0;t=0.;speed_i=0.;peak_roll=0.;peak_rack=0.;peak_yaw_rate=0.;peak_slip=0.;peak_heading=0.;heading_at_release=0.;min_contacts=4;airborne=0.;rolled=false
func begin_roof_case():
 # Roof down, 0.8 m up: the box lands on its top within a quarter second.
 car.restart(Vector3(0,.18,0),0.)
 car.global_transform=Transform3D(Basis(Vector3.FORWARD,PI),Vector3(0,.8,0))
 car.linear_velocity=Vector3.ZERO;car.angular_velocity=Vector3.ZERO
 state=4;t=0.;roof_prompt_at=-1.
func yaw_of(b:Basis)->float:
 var forward=-b.z
 return atan2(-forward.x,-forward.z)
func mode_label()->String:
 if "steering_assist" in car:return "assisted" if car.steering_assist else "direct"
 return "legacy"
func conclude():
 done=true
 print("KEYBOARD TAP PASSED" if passed else "KEYBOARD TAP FAILED")
 quit(0 if passed else 1)
func roof_step(dt):
 car.drive(dt,0.,0.,0.,false)
 if roof_prompt_at<0. and car.stranded_seconds>0.:roof_prompt_at=t
 if t<ROOF_SECONDS:return
 var stranded=car.stranded_seconds
 var was_roof_down=car.roof_down
 car.restart(Vector3(0,.18,0),0.)
 var ok=was_roof_down and roof_prompt_at>1.4 and roof_prompt_at<2.5 and stranded>1. and car.stranded_seconds==0. and not car.roof_down
 passed=passed and ok
 print("ROOF detector: roof_down=%s stranded_timer_started_s=%.2f stranded_s_at_%.0fs=%.2f cleared_by_restart=%s -> %s"%[str(was_roof_down),roof_prompt_at,ROOF_SECONDS,stranded,str(car.stranded_seconds==0.),"PASS" if ok else "FAIL"])
 conclude()
func _physics_process(dt):
 if done:return true
 tick+=1;t+=dt
 if tick>MAX_TICKS:
  done=true
  print("KEYBOARD TAP TIMEOUT case=",case_index," state=",state," speed_mps=",car.linear_velocity.length())
  quit(1);return true
 if state==4:
  roof_step(dt);return done
 var b:Basis=car.global_transform.basis;var vel:Vector3=car.linear_velocity;var speed=-vel.dot(b.z)
 var row:Array=CASES[case_index]
 var target=float(row[0])*.44704
 var press:float=row[1]
 var gas=0.
 var turn=0.
 if state==0:
  gas=1.
  if speed>=target-.3:state=1;t=0.;speed_i=0.
 else:
  # Hold the cruise speed the way the skidpad lap does, so the tap is the only change.
  speed_i=clampf(speed_i+(target-speed)*dt*.12,0.,.75)
  gas=clampf((target-speed)*.65+speed_i,0.,1.)
  if state==1 and t>=SETTLE_SECONDS:state=2;t=0.;start_yaw=yaw_of(b);start_pos=car.global_position;start_right=b.x
  if state==2:
   turn=-1.
   if t>=press+dt*.5:state=3;t=0.;turn=0.;heading_at_release=rad_to_deg(angle_difference(start_yaw,yaw_of(b)))
 car.drive(dt,gas,0.,turn,false)
 if state<2:return false
 var heading=rad_to_deg(angle_difference(start_yaw,yaw_of(b)))
 peak_heading=maxf(peak_heading,absf(heading))
 peak_roll=maxf(peak_roll,absf(rad_to_deg(asin(clampf(b.x.y,-1.,1.)))))
 peak_rack=maxf(peak_rack,absf(rad_to_deg(car.steering)))
 peak_yaw_rate=maxf(peak_yaw_rate,absf(rad_to_deg(car.angular_velocity.y)))
 peak_slip=maxf(peak_slip,absf(rad_to_deg(atan2(vel.dot(b.x),maxf(.5,-vel.dot(b.z))))))
 min_contacts=mini(min_contacts,car.wheel_contact)
 if car.wheel_contact==0:airborne+=dt
 if b.y.dot(Vector3.UP)<.2:rolled=true
 if state==3 and t>=float(row[2]):
  var offset:float=(car.global_position-start_pos).dot(start_right)
  var ok=absf(heading)<30. and peak_roll<10. and not rolled
  var gate_offset=press<.2 and mode_label()=="assisted"
  if gate_offset:ok=ok and absf(offset)<TAP_OFFSET_LIMIT_M
  passed=passed and ok
  print("TAP %d mph %.1f s %s: heading_change_deg=%.1f at_release_deg=%.1f peak_heading_deg=%.1f lateral_offset_m=%.2f%s peak_roll_deg=%.2f peak_rack_deg=%.1f peak_yaw_rate_deg_s=%.1f peak_sideslip_deg=%.1f min_contacts=%d airborne_s=%.2f rollover=%s speed_mps=%.1f -> %s"%[int(row[0]),press,mode_label(),heading,heading_at_release,peak_heading,offset," (limit 1 m)" if gate_offset else "",peak_roll,peak_rack,peak_yaw_rate,peak_slip,min_contacts,airborne,str(rolled),speed,"PASS" if ok else "FAIL"])
  case_index+=1
  if case_index>=CASES.size():
   if "stranded_seconds" in car:begin_roof_case()
   else:conclude()
   return done
  begin_case()
 return false
