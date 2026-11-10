extends SceneTree
## Offline bench for how a start or a jump sets the car down (RoadPlacement.gd,
## Powertrain.park): on a test plane with grades of 0 to 15° and a 3° cross
## slope, facing uphill and downhill, the car is posed with RoadPlacement.pose
## (as Drive.move_to_road does), parked, and left 45 s with no input. It must
## stand level with the plane (within 0.5°), at its ride height (within 1 cm),
## not move (under 1 cm) or turn, and 2 s of full throttle afterwards must move
## it (the hold releases). The same pose left unparked is run as a control and
## only reported: the tyre law is regularised, so it creeps or rolls on a grade.
## The real-place version is checks/StartPlacement.gd (needs map data).
##   godot --headless --path game --fixed-fps 60 --script res://checks/ParkedStart.gd
const RP=preload("res://RoadPlacement.gd")
const GRADES_DEG=[0.,5.4,10.,15.]
const CROSS_DEG=3.
const SETTLE_TICKS=2700
const GAS_TICKS=120
var car
var ground
var normal=Vector3.UP
var cases=[]
var at=-1
var tick=0
var phase=""
var start:Transform3D
var failures=[]

func _initialize():
 InputMap.add_action("clutch")
 for g in GRADES_DEG:
  for facing in [0.,PI]:
   cases.append([g,facing,true]);cases.append([g,facing,false])
 car=preload("res://Powertrain.gd").new();car.setup();root.add_child(car)
 next.call_deferred() # the root enters the tree after _initialize

# The test plane's height at p (x, z): the top face of the tilted box.
func plane_height(p:Vector2)->float:
 return -(normal.x*p.x+normal.z*p.y)/normal.y

func next():
 at+=1
 if at>=cases.size():
  if failures.is_empty():print("PARKED START PASSED %d poses"%(cases.size()/2))
  else:
   for f in failures:print("PARKED START FAILED ",f)
   print("PARKED START RESULT %d failures"%failures.size())
  quit(0 if failures.is_empty() else 1);return
 var c=cases[at]
 if ground:ground.free()
 var tilt=Basis(Vector3.RIGHT,deg_to_rad(c[0]))*Basis(Vector3.BACK,deg_to_rad(CROSS_DEG))
 normal=tilt*Vector3.UP
 ground=StaticBody3D.new();var shape=CollisionShape3D.new();var box=BoxShape3D.new()
 box.size=Vector3(1000,1,1000);shape.shape=box;ground.add_child(shape)
 ground.transform=Transform3D(tilt,-normal*.5);root.add_child(ground)
 var d=Vector2(-sin(c[1]),-cos(c[1]))
 var pose=RP.pose(Vector2.ZERO,d,car.MOUNTS,car.ride_height(),plane_height)
 car.restart(pose[0],pose[1],pose[2])
 if c[2]:car.park()
 start=car.global_transform;tick=0;phase="settle"

func label()->String:
 var c=cases[at]
 return "grade %4.1f° %s %s"%[c[0],"uphill  " if (c[1]==0.)==(c[0]>=0.) else "downhill","parked" if c[2] else "control"]

func _physics_process(dt):
 if phase=="":return false
 tick+=1
 var c=cases[at]
 if phase=="settle":
  car.drive(dt,0.,0.,0.,false)
  if tick<SETTLE_TICKS:return false
  var t=car.global_transform
  var moved=t.origin.distance_to(start.origin)
  var turned=rad_to_deg((-t.basis.z).angle_to(-start.basis.z))
  var level=rad_to_deg(t.basis.y.angle_to(normal))
  var height=(t.origin-Vector3(t.origin.x,plane_height(Vector2(t.origin.x,t.origin.z)),t.origin.z)).dot(normal)-car.ride_height()
  if not c[2]:print("PARKED START %s 45 s: moved %.1f m"%[label(),moved]);next();return false
  print("PARKED START %s 45 s: moved %.3f m turned %.2f° level %.2f° height %+.4f m"%[label(),moved,turned,level,height])
  if c[2]:
   if moved>.01:failures.append("%s moved %.3f m"%[label(),moved])
   if turned>.2:failures.append("%s turned %.2f°"%[label(),turned])
   if level>.5:failures.append("%s %.2f° off the plane"%[label(),level])
   if absf(height)>.01:failures.append("%s %+.4f m from ride height"%[label(),height])
   phase="gas";tick=0
  else:next()
  return false
 if phase=="gas":
  car.drive(dt,1.,0.,0.,false)
  if tick<GAS_TICKS:return false
  var v=car.linear_velocity.length()
  print("PARKED START %s gas 2 s: %.2f m/s"%[label(),v])
  if v<1.:failures.append("%s 2 s of throttle gave %.2f m/s"%[label(),v])
  next()
 return false
