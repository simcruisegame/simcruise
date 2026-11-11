extends SceneTree
## Headless: every car file in res://cars loads, names itself by its file name
## and points at a model that exists ("" keeps the 1999 roadster's CarFinish);
## an unknown or retired car id, from --car= or a saved "car" preference, falls
## back to the default car instead of failing.
##   godot --headless --path game --script res://checks/CarRoster.gd
const CAR_SPEC=preload("res://CarSpec.gd")
const PREFS=preload("res://DrivingPreferences.gd")
const RETIRED=["kestrel_s","tora_rs","volta","bison_xl"]

func fail(message:String) -> void:
 push_error("CAR ROSTER FAILED: "+message);quit(1)

func _initialize():
 var ids:=CAR_SPEC.available()
 if not "nb1999" in ids:fail("the default car nb1999 is missing");return
 for id in ids:
  var spec=CAR_SPEC.load_id(id)
  if spec.id!=id:fail("%s.json names itself %s"%[id,spec.id]);return
  if spec.model_scene!="" and not ResourceLoader.exists(spec.model_scene):fail("%s: model %s does not exist"%[id,spec.model_scene]);return
 for id in RETIRED:
  if id in ids:fail("retired car %s is still in res://cars"%id);return
  if CAR_SPEC.load_id(id).id!="nb1999":fail("--car=%s did not fall back to nb1999"%id);return
 # A preference saved by an older build with a retired car reads back as the default car.
 var dir:=OS.get_temp_dir().path_join("simcruise-car-roster-%d"%OS.get_process_id())
 DirAccess.make_dir_recursive_absolute(dir)
 var record:=ConfigFile.new()
 record.set_value("driving","value","kestrel_s")
 record.save(dir.path_join("car.cfg"))
 var before:=OS.get_environment("SIMCRUISE_QA_PREFS_DIR")
 OS.set_environment("SIMCRUISE_QA_PREFS_DIR",dir)
 var prefs=PREFS.new();prefs.open()
 var read_back:String=prefs.values.car
 prefs.remember("car","volta")
 var remembered:String=prefs.values.car
 OS.set_environment("SIMCRUISE_QA_PREFS_DIR",before)
 DirAccess.remove_absolute(dir.path_join("car.cfg"));DirAccess.remove_absolute(dir)
 if read_back!="nb1999":fail("saved car kestrel_s read back as %s"%read_back);return
 if remembered!="nb1999":fail("remembering a retired car kept %s"%remembered);return
 print("CAR ROSTER PASSED ",ids.size()," cars: ",", ".join(PackedStringArray(ids)),"; retired ids fall back to nb1999")
 quit()
