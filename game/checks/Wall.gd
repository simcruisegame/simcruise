extends SceneTree
var car
var wall
var ticks=0
var crossed=false
var hit=false
var closest=INF
func solid(size:Vector3,pos:Vector3):
 var b=StaticBody3D.new();var c=CollisionShape3D.new();var box=BoxShape3D.new();box.size=size;c.shape=box;b.add_child(c);b.position=pos;root.add_child(b);return b
func _initialize():
 solid(Vector3(85,1,90),Vector3(0,-.5,0));wall=solid(Vector3(18,4,.15),Vector3(0,2,-15))
 car=load("res://Powertrain.gd").new();car.setup();car.contact_monitor=true;car.max_contacts_reported=8;root.add_child(car);car.restart(Vector3(0,.12,0),0)
 car.body_entered.connect(func(body):if body==wall:hit=true)
func _physics_process(dt):
 ticks+=1
 if ticks==75:
  car.linear_velocity=Vector3(0,0,-31.2928)
  for i in 4:car.omega[i]=31.2928/car.R
 car.drive(dt,0.,0.,0.,false);closest=minf(closest,car.position.z)
 if car.position.z < -15.075:crossed=true
 if ticks==180:print("70 mph / 15 cm wall: contact=",hit," crossed=",crossed," nearest chassis centre z=",closest);quit(0 if hit and not crossed else 1)
 return false
