extends RefCounted
## Builds the player's car from a generated model (authoring/cars/build_car.py)
## for any CarSpec with a model_scene; the 1999 roadster keeps CarFinish.gd.
## Fills the same state Drive.gd reads from CarFinish: wheels ([hub, roller,
## front]), lamp_materials, lamp_nodes, the ground shade and the parked cars.
##
## The model's nodes wheel_fl/fr/rl/rr have their origin at the hub; each is
## moved under a hub pivot (steer, camber) and a roller (spin). The matching
## wheel_*_upright nodes (disc, caliper) stay on the hub and do not spin.
## Surfaces whose material is named lamp_rear become the brake lamps; when the
## lamp has a painted emission mask only the light guides glow. The node
## steering_wheel (origin at the hub, local Z along the column) turns with the
## front wheels at the car's steering ratio.
const CAR_FINISH=preload("res://CarFinish.gd")
const WHEELS=["wheel_fl","wheel_fr","wheel_rl","wheel_rr"]

func build(drive) -> void:
 var game=drive
 var vehicle=game.vehicle
 var spec=vehicle.spec
 vehicle.setup();game.add_child(vehicle)
 var model:Node3D
 if spec.model_rig.get("fit",false):
  # A downloaded model (ModelRig.gd): wheels found and cut out, scaled to the
  # spec's wheelbase, turned to face -Z and set at the static ride height.
  var rig=preload("res://ModelRig.gd").new()
  model=rig.build(spec.model_scene,spec,spec.model_rig)
  if model==null:push_warning("Could not fit %s: %s"%[spec.model_scene,"; ".join(rig.log_lines)])
 if model==null:model=load(spec.model_scene).instantiate()
 vehicle.add_child(model)
 for part in model.find_children("*","MeshInstance3D",true,false):part.layers=2
 for i in WHEELS.size():
  var hub=Node3D.new();vehicle.add_child(hub)
  hub.position=vehicle.MOUNTS[i]-Vector3.UP*spec.hub_drop_m
  var roller=Node3D.new();hub.add_child(roller)
  var wheel:Node3D=model.find_child(WHEELS[i],true,false)
  var upright:Node3D=model.find_child(WHEELS[i]+"_upright",true,false)
  if wheel:
   wheel.reparent(roller,false);wheel.position=Vector3.ZERO
  if upright:
   upright.reparent(hub,false);upright.position=Vector3.ZERO
  game.wheels.append([hub,roller,i<2])
 for node in model.find_children("*","MeshInstance3D",true,false):
  var mesh:Mesh=node.mesh
  for s in mesh.get_surface_count():
   var m=mesh.surface_get_material(s)
   if m==null or m.resource_name!="lamp_rear":continue
   var lamp:StandardMaterial3D=m.duplicate()
   lamp.emission_enabled=true;lamp.emission_energy_multiplier=0.
   if lamp.emission_texture:
    lamp.emission=Color("#ffd2c8");lamp.emission_operator=BaseMaterial3D.EMISSION_OP_MULTIPLY
   else:
    lamp.emission=Color("#ff2a18")
   node.set_surface_override_material(s,lamp)
   game.lamp_materials.append(lamp);game.lamp_nodes.append(node)
 # Glass: the generated material is blended; keep it off the depth pre-pass.
 for node in model.find_children("*","MeshInstance3D",true,false):
  for s in node.mesh.get_surface_count():
   var m=node.mesh.surface_get_material(s)
   if m is BaseMaterial3D and m.resource_name=="glass":
    var g:StandardMaterial3D=m.duplicate();g.transparency=BaseMaterial3D.TRANSPARENCY_ALPHA;g.metallic_specular=.9;g.roughness=.03
    g.albedo_color=Color(.05,.07,.08,.38)   # light tint: the driver looks out through it
    node.set_surface_override_material(s,g)
 var wheel:Node3D=model.find_child("steering_wheel",true,false)
 if wheel:game.steering_wheel=[wheel,wheel.transform.basis]
 CAR_FINISH.shade_and_traffic(game,Vector2(spec.collision_box.x+.5,spec.collision_box.z+.6))
