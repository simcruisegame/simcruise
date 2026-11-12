extends SceneTree
## Windowed: orthographic sheets of every downloaded model as ModelRig.gd fits
## it, to check the rig by eye: left side (hub markers in red, the wheels' cut
## cylinders in yellow, the steering wheel in green), top and the driver's
## side of the cabin close up. One PNG per model and view.
##
##   godot --path game --windowed --resolution 1600x900 --script res://checks/RigView.gd -- OUT_DIR [--model=<id prefix>]
const MANIFEST := "res://art/traffic/manifest.json"
const CAR_SPEC := preload("res://CarSpec.gd")
var out := ""
var only := ""

func _initialize():
 var args := OS.get_cmdline_user_args()
 for a in args:
  if a.begins_with("--model="):only = a.trim_prefix("--model=")
  elif not a.begins_with("--"):out = a
 if out == "":print("RIGVIEW usage: -- OUT_DIR");quit(1);return
 DirAccess.make_dir_recursive_absolute(out)
 run.call_deferred()

func marker(at: Vector3, colour: Color, r: float) -> MeshInstance3D:
 var m := MeshInstance3D.new();var s := SphereMesh.new();s.radius = r;s.height = r * 2.
 var mat := StandardMaterial3D.new();mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED;mat.albedo_color = colour;mat.no_depth_test = true
 s.material = mat;m.mesh = s;m.position = at;return m

func run():
 var by_scene := {}
 for id in CAR_SPEC.available():
  var s = CAR_SPEC.load_id(id)
  if s.model_scene != "":by_scene[s.model_scene] = s
 var manifest = JSON.parse_string(FileAccess.get_file_as_string(MANIFEST))
 var sun := DirectionalLight3D.new();sun.rotation_degrees = Vector3(-50, -30, 0);root.add_child(sun)
 var env := WorldEnvironment.new();env.environment = Environment.new()
 env.environment.background_mode = Environment.BG_COLOR;env.environment.background_color = Color(.82, .84, .86)
 env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR;env.environment.ambient_light_color = Color(.7, .7, .7)
 root.add_child(env)
 var cam := Camera3D.new();cam.projection = Camera3D.PROJECTION_ORTHOGONAL;root.add_child(cam);cam.current = true
 for m in manifest.models:
  if only != "" and not str(m.id).begins_with(only):continue
  var spec = by_scene.get(m.scene, null)
  if spec == null:spec = CAR_SPEC.from_manifest(m)
  var rig = load("res://ModelRig.gd").new()
  var car: Node3D = rig.build(m.scene, spec, spec.model_rig)
  if car == null:print("RIGVIEW ", m.id, " could not be fitted");continue
  root.add_child(car)
  for k in ["wheel_fl","wheel_fr","wheel_rl","wheel_rr"]:
   var w: Node3D = car.get_node(k)
   car.add_child(marker(w.position, Color.RED, .04))
   for a in 16:
    var ang := TAU * a / 16.
    car.add_child(marker(w.position + Vector3(0, sin(ang), cos(ang)) * spec.tire_radius_m(), Color.YELLOW, .015))
  var sw: Node3D = car.find_child("steering_wheel", true, false)
  if sw:
   car.add_child(marker(sw.position, Color.GREEN, .03))
   car.add_child(marker(sw.position + sw.basis.z * .25, Color.DARK_GREEN, .02))
  car.add_child(marker(spec.driver_eye, Color.BLUE, .03))
  var views := [["side", Vector3(-10, .8, 0), Vector3(0, .8, 0), 6.4], ["top", Vector3(0, 10, .001), Vector3(0, 0, 0), 6.4], ["front", Vector3(0, .9, -10), Vector3(0, .9, 0), 3.2], ["cabin", spec.driver_eye + Vector3(0, .3, 3.), spec.driver_eye + Vector3(0, -.2, -.6), 1.5]]
  if sw:views.append(["wheel", sw.position + sw.basis.z * 3., sw.position, .7])
  views.append_array([["nose", Vector3(0, .75, -10), Vector3(0, .75, 0), 1.3], ["tail", Vector3(0, .8, 10), Vector3(0, .8, 0), 1.3], ["hood", Vector3(0, 10, -1.6), Vector3(0, 0, -1.6), 1.6], ["flank", Vector3(-10, .8, -1.2), Vector3(0, .8, -1.2), 1.8]])
  for v in views:
   cam.size = v[3];cam.position = v[1];cam.look_at(v[2], Vector3.FORWARD if v[0] in ["top", "hood"] else Vector3.UP)
   if v[0] == "cabin" or v[0] == "wheel":cam.near = 3.05 if v[0] == "cabin" else 2.4
   else:cam.near = .05
   for i in 6:await process_frame
   await RenderingServer.frame_post_draw
   root.get_viewport().get_texture().get_image().save_png(out.path_join("%s_%s.png" % [m.kind, v[0]]))
  print("RIGVIEW ", m.id, " saved")
  car.queue_free()
  await process_frame
 quit()
