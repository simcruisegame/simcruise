extends SceneTree
## Windowed: every traffic model in game/art/traffic/manifest.json, fitted
## exactly as AmbientLife.gd fits it (merged mesh, normalised to its stated
## length), parked in a row facing the camera (vehicle front is -Z, so the
## camera looks from -Z). A model that shows its tail needs "flip": true.
## Saves lineup.jpg (front three-quarter) and lineup-side.jpg.
##   godot --path game --script res://checks/TrafficLineup.gd -- OUTPUT_DIRECTORY
func _initialize():
 var out: String = OS.get_cmdline_user_args()[0] if OS.get_cmdline_user_args().size() else "user://lineup"
 DirAccess.make_dir_recursive_absolute(out)
 var life = load("res://AmbientLife.gd")
 var manifest = JSON.parse_string(FileAccess.get_file_as_string("res://art/traffic/manifest.json"))
 var x := 0.
 var world := Node3D.new();root.add_child(world)
 var env := WorldEnvironment.new();var e := Environment.new();e.background_mode = Environment.BG_COLOR;e.background_color = Color("#b8c4cc")
 e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR;e.ambient_light_color = Color("#c8ccd0");e.ambient_light_energy = .6;e.tonemap_mode = Environment.TONE_MAPPER_FILMIC
 env.environment = e;world.add_child(env)
 var sun := DirectionalLight3D.new();sun.rotation_degrees = Vector3(-50, 30, 0);sun.shadow_enabled = true;world.add_child(sun)
 var floor_mesh := MeshInstance3D.new();var pm := PlaneMesh.new();pm.size = Vector2(200, 40);floor_mesh.mesh = pm;world.add_child(floor_mesh)
 var names := []
 for m in manifest.models:
  var mesh: ArrayMesh = life.merged_mesh(str(m.scene))
  if mesh == null:print("LINEUP missing ", m.id);continue
  var fit: Transform3D = life.normalise(mesh.get_aabb(), float(m.length_m), bool(m.get("flip", false)))
  var mi := MeshInstance3D.new();mi.mesh = mesh;mi.transform = Transform3D(Basis.IDENTITY, Vector3(x, 0, 0)) * fit;world.add_child(mi)
  names.append("%s@%.0f" % [m.id, x]);x += 3.2
 var cam := Camera3D.new();cam.fov = 40.;world.add_child(cam);cam.current = true
 var mid := Vector3(x * .5 - 1.6, .8, 0)
 cam.global_position = mid + Vector3(-4, 4.5, -22);cam.look_at(mid, Vector3.UP)
 # Models stream their textures in over the first frames; give them time.
 for i in 120:await process_frame
 await RenderingServer.frame_post_draw
 root.get_viewport().get_texture().get_image().save_jpg(out.path_join("lineup.jpg"), .9)
 cam.global_position = mid + Vector3(0, 3.5, 24);cam.look_at(mid, Vector3.UP)
 for i in 10:await process_frame
 await RenderingServer.frame_post_draw
 root.get_viewport().get_texture().get_image().save_jpg(out.path_join("lineup-rear.jpg"), .9)
 print("LINEUP ", ", ".join(names))
 quit()
