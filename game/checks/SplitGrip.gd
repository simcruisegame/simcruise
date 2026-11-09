extends SceneTree
# Same flat road, throttle and initial conditions for dry, left-slippery and right-slippery.
var car
var tick=0
var run=0
var wheel_difference=0.
var from=Vector3.ZERO
const GRIP=[[1.,1.],[.20,1.],[1.,.20]]
func _initialize():
 var floor_body=StaticBody3D.new()
 var box=BoxShape3D.new();box.size=Vector3(150,1,450)
 var collider=CollisionShape3D.new();collider.shape=box;floor_body.add_child(collider)
 floor_body.position.y=-.5;root.add_child(floor_body)
 car=preload("res://Powertrain.gd").new();car.setup();root.add_child(car)
 begin_run()
func begin_run():
 tick=0;wheel_difference=0.
 car.restart(Vector3(0,.18,0),0.)
 car.surface_grip=[1.,1.,GRIP[run][0],GRIP[run][1]]
func _physics_process(dt):
 tick+=1
 car.drive(dt,0. if tick<=75 else .63,0.,0.,false)
 if tick==75:from=car.position
 if tick<=75:return false
 if car.wheel_contact!=4:
  print("SPLIT FAILED missing contact");quit(1);return false
 wheel_difference+=absf(car.omega[2]-car.omega[3])*dt
 if tick==165:
  print("SPLIT ",run," forward_m=",from.z-car.position.z," mps=",car.linear_velocity.length()," mean_wheel_gap_radps=",wheel_difference/1.5," yaw_deg=",rad_to_deg(car.rotation.y))
  run+=1
  if run<GRIP.size():begin_run()
  else:quit()
 return false
