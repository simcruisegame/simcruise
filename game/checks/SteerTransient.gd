extends SceneTree
# Reproduce a partial-stick pulse in both directions on the same chassis. Report
# the measured response after releasing the stick, including any return swing.
const SPEED=17.8816
const AMPLITUDE=.20
const STEER_FRAMES=30
var car
var tick=0
var trial=0
var peak_yaw=0.
var peak_angle=0.
var peak_slip=0.
var release_yaw=0.
var after_half=0.
var min_contacts=4
var settled=-1.
var measurements=[]
func _initialize():
 InputMap.add_action("clutch")
 var ground=StaticBody3D.new()
 var col=CollisionShape3D.new()
 var box=BoxShape3D.new()
 box.size=Vector3(100,1,450)
 col.shape=box;ground.position.y=-.5;ground.add_child(col);root.add_child(ground)
 car=preload("res://Powertrain.gd").new()
 car.setup();root.add_child(car);car.set_manual(true)
 reset_trial()
func reset_trial():
 car.restart(Vector3(0,.1,0),0.)
 tick=0;peak_yaw=0.;peak_angle=0.;peak_slip=0.;release_yaw=0.;after_half=0.;min_contacts=4;settled=-1.
func _physics_process(dt):
 tick+=1
 if tick==90:
  car.linear_velocity=-car.basis.z*SPEED
  car.omega.fill(SPEED/car.R)
 var frame=tick-100
 var stick=AMPLITUDE if trial%2==0 else -AMPLITUDE
 if frame<0 or frame>=STEER_FRAMES:stick=0.
 var demand=stick
 if trial>=2:demand=car.thumbstick_command(stick,-car.linear_velocity.dot(car.basis.z))
 car.drive(dt,0.,0.,demand,false)
 if frame>=0:
  var yaw=absf(rad_to_deg(car.angular_velocity.y))
  var slip=absf(rad_to_deg(atan2(car.linear_velocity.dot(car.basis.x),-car.linear_velocity.dot(car.basis.z))))
  min_contacts=mini(min_contacts,car.wheel_contact)
  peak_yaw=maxf(peak_yaw,yaw);peak_slip=maxf(peak_slip,slip)
  peak_angle=maxf(peak_angle,absf(rad_to_deg(car.steering)))
  if frame==STEER_FRAMES:release_yaw=yaw
  if frame==STEER_FRAMES+30:after_half=yaw
  if frame>STEER_FRAMES and settled<0. and yaw<.05*peak_yaw:settled=(frame-STEER_FRAMES)*dt
 if frame==STEER_FRAMES+100:
  measurements.append([peak_yaw,peak_angle,peak_slip,after_half,settled,min_contacts])
  print("PULSE ",trial," rack_deg=",peak_angle," yaw_deg_s=",peak_yaw," sideslip_deg=",peak_slip," release_yaw_deg_s=",release_yaw," yaw_after_0.5s=",after_half," settles_s=",settled," contacts=",min_contacts)
  trial+=1
  if trial==4:
   var ok=true
   for i in 4:
    var m=measurements[i]
    ok=ok and m[5]==4 and m[0]>1. and m[4]>0. and m[4]<1.0
   for i in 2:
    ok=ok and measurements[i+2][0]<measurements[i][0]*.75
   ok=ok and absf(measurements[2][0]-measurements[3][0])<.15
   ok=ok and is_equal_approx(car.thumbstick_command(1.,SPEED),1.) and is_equal_approx(car.thumbstick_command(.2,1.),.2)
   print("PULSE PASSED" if ok else "PULSE FAILED")
   quit(0 if ok else 1)
  else:reset_trial()
 return false
