extends SceneTree
var car
var tick=0
var state=0
var t=0.0
var last=0.
var start_z=0.
var reached=false
var a60=0.
var b70=0.
var skid_samples=[]
var best_lat=0.
var last_radial=0.
var lap_clock=0.
var lap_radius=0.
var report=[]
var test_speed=18.
var speed_i=0.
var radial_i=0.
var valid_lap=false
var total_angle=0.
func _initialize():
 var rd=StaticBody3D.new();var col=CollisionShape3D.new();var box=BoxShape3D.new();box.size=Vector3(2500,1,5000);col.shape=box;rd.position.y=-.5;rd.add_child(col);root.add_child(rd)
 car=load("res://Powertrain.gd").new();car.setup();root.add_child(car);car.restart(Vector3(0,.18,0),0.)
func _physics_process(dt):
 tick+=1;t+=dt
 if tick>18000:print("TIMEOUT ",state," ",car.linear_velocity);quit(1);return false
 var v=car.linear_velocity;var speed=-v.dot(car.transform.basis.z)
 match state:
  0:
   car.drive(dt,0.,0.,0.,false)
   if tick==90:t=0;state=1
  1:
   car.drive(dt,1.,0.,0.,false)
   if tick%120==0:print("ACCEL ",snappedf(t,.01)," ",snappedf(speed,.1)," ",car.gear+1," ",roundi(car.rpm))
   if speed>=26.8224 and not reached:a60=t-dt+(26.8224-last)/maxf(.0001,speed-last)*dt;reached=true
   if speed>=31.2928:start_z=car.position.z+(speed-31.2928)*dt/2;state=2;t=0
  2:
   car.drive(dt,0.,1.,0.,false)
   if speed<.1:
    b70=absf(start_z-car.position.z)*3.28084;print("RESULT flat: 0-60 mph = ",a60," s; 70-0 mph = ",b70," ft")
    state=3;car.restart(Vector3(0,.15,0),0.);t=0
  3:
   car.drive(dt,0.,0.,0.,false)
   if t>=1.0:state=4;t=0
  4:
   # Driver closes the loop around a 300 ft diameter circle. Only count a
   # full lap while the measured centre stays within 1.5 m of the target radius.
   var radius=45.72;var p=Vector2(car.position.x+radius,car.position.z);var current=p.length();var heading=-car.transform.basis.z
   var ahead=p.rotated(-.18).normalized()*radius-p;var desired=atan2(-ahead.x,-ahead.y);var yaw=atan2(-heading.x,-heading.z)
   radial_i=clampf(radial_i+(current-radius)*dt*.045,-.3,.45)
   var turn=clampf(angle_difference(yaw,desired)*1.9+.035+radial_i,-1,1)
   speed_i=clampf(speed_i+(test_speed-speed)*dt*.12,0,.75)
   car.drive(dt,clampf((test_speed-speed)*.65+speed_i,0,1),0.,turn,false)
   if absf(current-radius)<1.5 and absf(speed-test_speed)<.5:
    total_angle+=absf(angle_difference(last_radial,p.angle()));lap_clock+=dt;lap_radius+=current*dt
    if total_angle>TAU:best_lat=(total_angle/lap_clock)**2*(lap_radius/lap_clock)/9.80665;print("SKID stable lap ",test_speed,"m/s  ",best_lat," g");test_speed+=.4;t=0;total_angle=0;lap_clock=0;lap_radius=0
   else:total_angle=0;lap_clock=0;lap_radius=0
   last_radial=p.angle()
   if tick%300==0:print("CIRCLE ",test_speed," rad=",current," mps=",speed," steer=",turn)
   if t>38 or test_speed>22.5:print("RESULT skidpad maximum demonstrated stable g: ",best_lat," at next trial ",test_speed);quit()
 last=speed
 return false
