extends SceneTree
## Headless: fit every downloaded model in game/art/traffic/manifest.json with
## ModelRig.gd and print what it found: orientation, wheels, scale, fitted size
## against the real car, glass and lamp roles, hidden badge surfaces and the
## steering wheel. A car file that uses the model supplies the spec; otherwise
## the manifest's "spec" block (published wheelbase, track and tyre size).
##
##   godot --headless --path game --script res://checks/RigProbe.gd [-- --model=<id prefix>]
const MANIFEST := "res://art/traffic/manifest.json"
const CAR_SPEC := preload("res://CarSpec.gd")

func _initialize():
 var only := ""
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--model="):only = a.trim_prefix("--model=")
 var by_scene := {}
 for id in CAR_SPEC.available():
  var s = CAR_SPEC.load_id(id)
  if s.model_scene != "":by_scene[s.model_scene] = s
 var manifest = JSON.parse_string(FileAccess.get_file_as_string(MANIFEST))
 var failed := 0
 for m in manifest.models:
  if only != "" and not str(m.id).begins_with(only):continue
  var spec = by_scene.get(m.scene, null)
  if spec == null:spec = CAR_SPEC.from_manifest(m)
  var rig = load("res://ModelRig.gd").new()
  var t := Time.get_ticks_msec()
  var out: Node3D = rig.build(m.scene, spec, spec.model_rig)
  print("RIG ", m.id, " spec ", spec.id, " ms=", Time.get_ticks_msec() - t, " ok=", out != null)
  for l in rig.log_lines:print("RIG   ", l)
  if out == null:failed += 1;continue
  var box := AABB()
  var first := true
  for mi in out.find_children("*", "MeshInstance3D", true, false):
   var b: AABB = rig.relative(out, mi) * mi.mesh.get_aabb()
   box = b if first else box.merge(b);first = false
  print("RIG   fitted size %s (real length %.2f m)" % [box.size.snapped(Vector3.ONE * .01), float(m.get("length_m", 0.))])
  for k in ["wheel_fl","wheel_fr","wheel_rl","wheel_rr"]:
   var w = out.get_node(k);print("RIG   ", k, " at ", w.position.snapped(Vector3.ONE * .001), " mesh ", w.get_child(0).mesh.get_aabb().size.snapped(Vector3.ONE * .01))
  var sw: Node3D = out.find_child("steering_wheel", true, false)
  print("RIG   steering wheel ", "none" if sw == null else "at %s column %s" % [sw.position.snapped(Vector3.ONE * .001), (sw.basis.z).snapped(Vector3.ONE * .001)])
  if absf(box.size.z - float(m.get("length_m", box.size.z))) > .25:print("RIG   WARNING fitted length differs from the real car by %.2f m" % (box.size.z - float(m.length_m)))
  out.free()
 print("RIG PROBE %s" % ("done" if failed == 0 else "%d models could not be fitted" % failed))
 quit()
