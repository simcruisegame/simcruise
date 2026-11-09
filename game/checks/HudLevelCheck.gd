extends SceneTree
## Headless integration check for the cockpit stream. Loads the real Drive
## scene offline (no map helper), waits for the bundled Chula start, then
## exercises the HUD levels (F2), the sound toggle (M), the audio bus tree and
## cabin filter, the head-motion slider, the legend timeout and the driver
## camera, writing every preference into SIMCRUISE_QA_PREFS_DIR. Prints once,
## then closes the game.
##   SIMCRUISE_QA_PREFS_DIR=/tmp/x godot --headless --path game --script res://checks/HudLevelCheck.gd
var game
var failures := []
var stage := 0
var clock := 0.
var finished := false
var prefs := ""

func _initialize():
 prefs = OS.get_environment("SIMCRUISE_QA_PREFS_DIR")
 if prefs.is_empty():
  push_error("HUDCHECK needs SIMCRUISE_QA_PREFS_DIR so it cannot touch the player's preferences")
  quit(2);return
 game = load("res://Drive.tscn").instantiate()
 root.add_child(game)
 game.helper = "/usr/bin/false"

func expect(ok: bool, message: String):
 if not ok:failures.append(message)

func key(code: Key):
 var event := InputEventKey.new()
 event.keycode = code;event.physical_keycode = code;event.pressed = true
 root.push_input(event,true)

func saved(name: String):
 var record := ConfigFile.new()
 if record.load(prefs.path_join(name+".cfg")) != OK:return null
 for key in ["value","enabled"]:
  if record.has_section_key("driving",key):return record.get_value("driving",key)
 return null

func advance():
 stage += 1;clock = 0.

func _process(dt):
 if finished or game == null or not game.ready_drive:return false
 clock += dt
 var hud = game.hud
 match stage:
  0:
   if clock < .25:return false
   # Defaults: Glance HUD, sound on, bus tree in place, cabin filter engaged in the driver view.
   expect(hud.level == hud.GLANCE and hud.clean_view,"default HUD level is not Glance")
   expect(not hud.header.visible and not hud.bottom.visible and hud.location.visible and hud.tag.visible,"Glance shows the wrong panels")
   expect(hud.legend.visible,"the legend did not show at the start of the drive")
   # Headless keeps the mixer idle (Drive.toggle_sound), so playing is only expected with a real audio driver.
   var live := DisplayServer.get_name() != "headless"
   expect(game.sound_enabled and game.engine.playing == live and game.engine.bus == "Engine","sound is not on by default on the Engine bus")
   for bus in ["Cabin","Engine","Road","Wind"]:
    expect(AudioServer.get_bus_index(bus) != -1,"missing audio bus "+bus)
   var cabin := AudioServer.get_bus_index("Cabin")
   expect(cabin != -1 and AudioServer.get_bus_send(AudioServer.get_bus_index("Road")) == "Cabin","Road does not send to Cabin")
   expect(cabin != -1 and AudioServer.is_bus_effect_enabled(cabin,0),"cabin low-pass is off in the driver view")
   var noise = game.get_node("RoadNoise")
   expect(noise.running == live and noise.road_player.playing == live and noise.wind_player.playing == live,"road and wind layers do not follow the sound state")
   expect(game.cockpit.head_motion == 1.0 and hud.motion_slider.value == 1.0,"head_motion default is not 1")
   var eye: Vector3 = game.vehicle.global_transform*game.cockpit.EYE
   expect(game.camera.global_position.distance_to(eye) < .05,"driver camera is %.3f m from the eye at rest"%game.camera.global_position.distance_to(eye))
   key(KEY_F2);advance()
  1:
   expect(hud.level == hud.FULL and hud.header.visible and hud.bottom.visible and not hud.tag.visible and not hud.clean_view,"F2 did not reach Full")
   expect(saved("hud_level") == 2,"hud_level 2 was not remembered")
   key(KEY_F2);advance()
  2:
   expect(hud.level == hud.OFF and not hud.location.visible and not hud.tag.visible,"second F2 did not reach Off")
   game.say("Prompt",2.)
   expect(hud.note.get_parent().visible,"prompts must stay visible at HUD Off")
   key(KEY_F2);advance()
  3:
   expect(hud.level == hud.GLANCE and hud.legend.visible and hud.legend_time > 0.,"third F2 did not return to Glance with the legend")
   key(KEY_M);advance()
  4:
   expect(not game.sound_enabled and not game.engine.playing,"M did not stop the engine sound")
   expect(saved("sound") == false,"sound off was not remembered")
   var noise = game.get_node("RoadNoise")
   expect(not noise.running,"road layer kept running with sound off")
   key(KEY_M);advance()
  5:
   expect(game.sound_enabled and game.engine.playing == (DisplayServer.get_name() != "headless") and saved("sound") == true,"M did not restore the sound")
   hud.motion_slider.value = .5
   expect(is_equal_approx(game.cockpit.head_motion,.5) and is_equal_approx(float(saved("head_motion")),.5),"head motion slider did not reach the cockpit and the record")
   hud.legend_time = 0.;hud.legend.visible = false
   key(KEY_C);advance()
  6:
   # hud.refresh runs on the 0.16 s telemetry timer; give it time to notice the view.
   if clock < .4:return false
   expect(game.view == 2 and hud.legend.visible,"C did not switch the view and show the legend")
   expect(not AudioServer.is_bus_effect_enabled(AudioServer.get_bus_index("Cabin"),0),"cabin low-pass stayed on outside the car")
   advance()
  7:
   if clock < hud.LEGEND_S+.6:return false
   expect(not hud.legend.visible,"the legend did not hide %.0f s after the view change"%hud.LEGEND_S)
   finished = true
   var prefs_dir := DirAccess.open(prefs)
   var files := prefs_dir.get_files() if prefs_dir else PackedStringArray()
   print("HUDCHECK level=%d sound=%s head_motion=%.2f view=%d records=%s"%[hud.level,str(game.sound_enabled),game.cockpit.head_motion,game.view,", ".join(files)])
   if failures.is_empty():print("HUDCHECK PASSED")
   else:
    for f in failures:print("HUDCHECK FAILED ",f)
   game.request_game_close(0 if failures.is_empty() else 1)
 return false
