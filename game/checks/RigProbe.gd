extends SceneTree
## Headless: rig each Sketchfab model in game/art/traffic against a spec and
## print what ModelRig found (wheels, scale, hub positions) and how long it took.
func _initialize():
 var spec = load("res://CarSpec.gd").load_id(OS.get_environment("RIG_CAR") if OS.has_environment("RIG_CAR") else "meridian_z")
 for path in ["res://art/traffic/c8-790c40cc/scene.gltf","res://art/traffic/mustang65-05f2a294/scene.gltf"]:
  var rig = load("res://ModelRig.gd").new()
  var t := Time.get_ticks_msec()
  var out = rig.build(path, spec, {})
  print("RIG ", path, " ms=", Time.get_ticks_msec()-t, " ok=", out != null)
  for l in rig.log_lines:print("RIG   ", l)
  if out:
   var box := AABB()
   var body: MeshInstance3D = out.get_node("body")
   print("RIG   body aabb ", body.mesh.get_aabb())
   for k in ["wheel_fl","wheel_fr","wheel_rl","wheel_rr"]:
    var w = out.get_node(k);print("RIG   ", k, " at ", w.position.snapped(Vector3.ONE*.001), " mesh aabb ", w.get_child(0).mesh.get_aabb())
   out.free()
 quit()
