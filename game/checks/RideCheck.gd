extends SceneTree
var car
var tick=0
var initial_left=0.
var initial_right=0.
var max_difference=0.
var peak_roll=0.
var roll_after=0.
var surface=StaticBody3D.new()
func _initialize():
 var col=CollisionShape3D.new();var floor_shape=BoxShape3D.new();floor_shape.size=Vector3(160,1,400)
 col.shape=floor_shape;col.position.y=-.5;surface.add_child(col);root.add_child(surface)
 var verts=PackedVector3Array()
 for j in 32:
  var z0=-6.-1.65*j/32.;var z1=-6.-1.65*(j+1)/32.
  var h0=.045*pow(sin(PI*j/32.),2);var h1=.045*pow(sin(PI*(j+1)/32.),2)
  var a=Vector3(-1.1,h0,z0);var b=Vector3(-.31,h0,z0);var c=Vector3(-.31,h1,z1);var d=Vector3(-1.1,h1,z1)
  verts.append_array(PackedVector3Array([a,c,b,a,d,c]))
 var ramp=ConcavePolygonShape3D.new();ramp.backface_collision=true;ramp.set_faces(verts)
 var bump=CollisionShape3D.new();bump.shape=ramp;surface.add_child(bump)
 car=load("res://Powertrain.gd").new();car.setup();root.add_child(car);car.restart(Vector3(0,.15,0),0.)
func _physics_process(dt):
 tick+=1
 car.drive(dt,0.,0.,0.,false)
 if tick==120:
  initial_left=car.spring_load[0];initial_right=car.spring_load[1]
  car.linear_velocity=Vector3(0,0,-8.)
  for i in 4:car.omega[i]=8./car.R
 if tick>122 and tick<280:
  max_difference=maxf(max_difference,car.spring_load[0]-car.spring_load[1])
  peak_roll=maxf(peak_roll,absf(car.rotation_degrees.z))
 if tick==330:
  roll_after=absf(car.rotation_degrees.z)
  print("RIDE baseline_left_N=",initial_left," baseline_right_N=",initial_right," peak_left_minus_right_N=",max_difference," peak_roll_deg=",peak_roll," settled_roll_deg=",roll_after," contacts=",car.wheel_contact)
  quit(0 if absf(initial_left-initial_right)<60 and max_difference>300 and peak_roll>.05 and roll_after<.4 and car.wheel_contact==4 else 1)
 return false
