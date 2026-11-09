extends SceneTree
var car
var tick=0
var trial=0
var failures=0
var straight_stop=0.
var first_peak=0.
var peak_yaw=0.
var yaw_when_locked=0.
var min_contact=4
var locked_frames=0
var onset=0
var start=Vector3.ZERO
func _initialize():
 InputMap.add_action("clutch")
 var ground=StaticBody3D.new();var shape=CollisionShape3D.new();var box=BoxShape3D.new()
 box.size=Vector3(1000,1,1000);ground.position.y=-.5;shape.shape=box;ground.add_child(shape);root.add_child(ground)
 car=preload("res://Powertrain.gd").new();car.setup();root.add_child(car);car.set_manual(true)
 restart()
func restart():
 tick=0;locked_frames=0;onset=0;min_contact=4;peak_yaw=0.;yaw_when_locked=0.
 car.restart(Vector3(0,.10,0),0.)
func _physics_process(dt):
 tick+=1
 if tick==100:
  car.linear_velocity=-car.basis.z*22.352;car.omega.fill(22.352/car.R);start=car.position
 var demand=[0.,1.,-1.][trial] if tick>=118 else 0.
 car.drive(dt,0.,1. if tick>=104 else 0.,demand,false)
 if tick>=105:
  min_contact=mini(min_contact,car.wheel_contact)
  peak_yaw=maxf(peak_yaw,absf(rad_to_deg(car.rotation.y)))
  if maxf(absf(car.omega[0]),absf(car.omega[1]))<.01 and car.linear_velocity.length()>3.:
   if onset==0:onset=tick
   locked_frames+=1;yaw_when_locked=maxf(yaw_when_locked,absf(rad_to_deg(car.angular_velocity.y)))
  if (tick>115 and car.linear_velocity.length()<1.) or tick>300:
   print("FREE PANIC ",JSON.stringify({"command":demand,"lateral_m":car.position.x-start.x,"stopping_m":-car.position.z+start.z,"peak_heading_deviation_deg":peak_yaw,"yaw_rate_locked_deg_s":yaw_when_locked,"lock_frames":locked_frames,"onset":onset,"contacts":min_contact}))
   var stopped=-car.position.z+start.z
   if min_contact!=4 or locked_frames<50 or stopped<22. or stopped>32.:failures+=1
   if trial==0:
    straight_stop=stopped
    if peak_yaw>.25:failures+=1
   else:
    if peak_yaw>3. or yaw_when_locked>4. or absf(stopped-straight_stop)>1.:failures+=1
    if trial==1:first_peak=peak_yaw
    elif absf(peak_yaw-first_peak)>.12:failures+=1
   trial+=1
   if trial==3:
    print("LOCKED BRAKING "+("PASSED" if failures==0 else "FAILED"))
    quit(0 if failures==0 else 1)
   else:restart()
 return false
