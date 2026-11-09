extends Node
# Run twice with a fresh SIMCRUISE_QA_PREFS_DIR, using write then read.
var game
var completed := false
func _ready():
 game=get_parent()
 game.helper="/usr/bin/false"
func fail(message):
 push_error(message)
 game.request_game_close(1)
func _process(_dt):
 if completed or not game.ready_drive:return
 completed=true
 var args=OS.get_cmdline_user_args()
 var check=args.find("--preference-check")
 if check+1>=args.size() or OS.get_environment("SIMCRUISE_QA_PREFS_DIR").is_empty():
  fail("Preference check needs its isolated directory and write/read phase");return
 var map=game.hud.atlas_map
 map.open_map()
 if args[check+1]=="write":
  if game.vehicle.manual or not game.street_detail.enabled or game.spectral_ground.enabled:fail("Incorrect clean installation preferences");return
  map.transmission_button.pressed.emit()
  map.photos_button.pressed.emit()
  map.ground_cover_button.pressed.emit()
  if not game.vehicle.manual or game.street_detail.enabled or not game.spectral_ground.enabled:fail("Map driving preferences did not switch");return
  if not map.transmission_button.text.ends_with("Manual") or not map.photos_button.text.ends_with("Off") or not map.ground_cover_button.text.ends_with("On"):fail("Map controls did not update");return
 elif args[check+1]=="read":
  if not game.vehicle.manual or game.street_detail.enabled or not game.spectral_ground.enabled:fail("Preferences did not survive process restart");return
  map.close_map()
  var key=InputEventKey.new()
  key.keycode=KEY_T;key.physical_keycode=KEY_T;key.pressed=true
  get_viewport().push_input(key,true)
  if game.vehicle.manual:fail("T did not return to automatic");return
  var button=InputEventJoypadButton.new()
  button.button_index=JOY_BUTTON_B;button.pressed=true
  get_viewport().push_input(button,true)
  if not game.vehicle.manual:fail("B did not restore manual");return
 else:
  fail("Unknown driving preference test phase");return
 print("PREFERENCES PASSED ",args[check+1]," manual=",game.vehicle.manual," street_photos=",game.street_detail.enabled)
 get_tree().root.close_requested.emit()
