extends SceneTree
# Virtual sideslip rig. Prescribe the velocity at the hub, suppress rotation,
# and measure lateral impulse from the chassis velocity on the next Jolt tick.
# Use a small slip step on a level surface, at loads near static ride height.
const TRIALS = [8.,16.,-8.,-16.]
const INITIAL_FRAMES = 90
const WARMUP_FRAMES = 35
const MEASURE_FRAMES = 38
const SLIP_DEG = 1.0
var car
var tick := 0
var trial := 0
var previous_command := 0.0
var baseline := 0.0
var impulses: Array[float] = []
var failures := 0

func _initialize():
 if not InputMap.has_action("clutch"):InputMap.add_action("clutch")
 var road := StaticBody3D.new()
 var collider := CollisionShape3D.new()
 var shape := BoxShape3D.new()
 shape.size = Vector3(100,1,1000)
 collider.shape = shape
 road.position.y = -.5
 road.add_child(collider)
 root.add_child(road)
 car = preload("res://Powertrain.gd").new()
 car.setup()
 root.add_child(car)
 car.restart(Vector3(0,.12,0),0.)
 car.lock_rotation = true
 car.set_manual(true)

func _physics_process(dt):
 if tick < INITIAL_FRAMES:
  car.drive(dt,0.,0.,0.,false)
  tick += 1
  return false
 var speed: float = TRIALS[trial]
 var elapsed := tick-INITIAL_FRAMES
 var measured: float = (previous_command-car.linear_velocity.x)*car.mass/dt
 if elapsed == WARMUP_FRAMES:baseline = measured
 if elapsed > WARMUP_FRAMES:impulses.append(measured-baseline)
 if elapsed == WARMUP_FRAMES+MEASURE_FRAMES:
  check_step(speed,dt)
  trial += 1
  if trial == TRIALS.size():
   quit(0 if failures==0 else 1)
   return false
  impulses.clear()
  previous_command=0.
  car.restart(Vector3(0,.12,0),0.)
  tick=0
  return false
 previous_command = absf(speed)*tan(deg_to_rad(SLIP_DEG)) if elapsed >= WARMUP_FRAMES else 0.
 car.linear_velocity = Vector3(previous_command,0,-speed)
 for i in 4:car.omega[i]=speed/car.R
 car.drive(dt,0.,0.,0.,false)
 if car.wheel_contact!=4:failures+=1
 tick += 1
 return false

func check_step(speed: float, dt: float):
 var steady := 0.0
 for n in range(impulses.size()-5,impulses.size()):steady+=impulses[n]/5.
 var first := impulses[0]/steady
 var distance := 0.
 var prev := 0.
 for n in impulses.size():
  var fraction := impulses[n]/steady
  if fraction>=1.-exp(-1.):
   var time_fraction := inverse_lerp(prev,fraction,1.-exp(-1.))
   var center := maxf(0,(n-.5)*dt)
   distance = absf(speed)*(center+time_fraction*((n+.5)*dt-center))
   break
  prev=fraction
 print("SIDE STEP ",speed," m/s; first tick fraction ",snappedf(first,.001),"; measured 63% distance ",snappedf(distance,.001)," m; steady ",snappedf(steady,1.)," N")
 if steady<1000 or distance<.24 or distance>.45:failures+=1
