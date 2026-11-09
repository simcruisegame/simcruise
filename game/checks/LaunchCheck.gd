extends SceneTree
## Headless integration check for the random start (Drive.gd, StartPool.gd).
## Loads the real Drive scene in normal (random) start mode with the map helper
## disabled, as an offline player would, and checks that:
##  1. the launch tries four distinct pool roads, then drives the bundled
##     Miami Beach fallback with its message, without opening the search box and
##     without a provider request;
##  2. the search box's "Surprise me" button tries four more pool roads (never
##     the current place) and, when none loads, reopens the search box with the
##     presets and the failure message while the car stays where it was;
##  3. choosing a place while a Surprise me sequence runs ends that sequence.
## Prints once per stage, then closes the game.
##   SIMCRUISE_QA_PREFS_DIR=/tmp/x godot --headless --path game --script res://checks/LaunchCheck.gd [-- --start-seed N]
const POOL=preload("res://StartPool.gd")
const HUD=preload("res://Hud.gd")
var game
var failures := []
var stage := 0
var clock := 0.
var attempts := []
var finished := false
var started := 0 # wall clock: under --fixed-fps the frame clock outruns the tile worker thread

func _initialize():
 if OS.get_environment("SIMCRUISE_QA_PREFS_DIR").is_empty():
  push_error("LAUNCH needs SIMCRUISE_QA_PREFS_DIR so it cannot touch the player's preferences")
  quit(2);return
 OS.set_environment("SIMCRUISE_MAP_HELPER","/usr/bin/false") # read in Drive._ready: every pool road fails like offline
 game = load("res://Drive.tscn").instantiate()
 game.start_mode = "random"
 root.add_child(game)
 game.helper = "/usr/bin/false"
 started = Time.get_ticks_msec()

func expect(ok: bool, message: String):
 if not ok:failures.append(message)

# The titles the loader has been pointed at, in order, without repeats in a row.
func watch_jump():
 var title := str(game.jump.get("title",""))
 if title != "" and (attempts.is_empty() or attempts[-1] != title):attempts.append(title)

func pool_titles() -> Dictionary:
 var titles := {}
 for entry in game.start_pool:titles[entry.title] = true
 return titles

func surprise_button() -> Button:
 for button in game.hud.modal.find_children("*","Button",true,false):
  if button.text == "Surprise me":return button
 return null

func finish():
 finished = true
 if failures.is_empty():print("LAUNCH PASSED")
 else:
  for f in failures:print("LAUNCH FAILED ",f)
 game.request_game_close(0 if failures.is_empty() else 1)

func _process(dt):
 if finished or game == null:return false
 clock += dt
 if Time.get_ticks_msec() - started > 90000:
  failures.append("timeout in stage %d, attempts %s"%[stage,str(attempts)]);finish();return false
 watch_jump()
 match stage:
  0:
   if not game.ready_drive:return false
   var pool := pool_titles()
   var tried := attempts.slice(0,attempts.size()-1)
   var distinct := {}
   for title in tried:distinct[title] = true
   print("LAUNCH seed=%d attempts=%s arrived=%s note=%s requests=%d"%[game.start_rng.seed,str(attempts),game.current_title,game.hud.note.text,game.calls])
   expect(tried.size() == 1+POOL.RETRIES and distinct.size() == tried.size(),"the launch did not try four distinct pool roads: "+str(tried))
   for title in tried:expect(pool.has(title),"launch tried a place outside the pool: "+title)
   expect(game.current_title == POOL.FALLBACK.title and attempts[-1] == POOL.FALLBACK.title,"the launch did not fall back to bundled Miami Beach")
   expect("Miami Beach" in game.hud.note.text,"no fallback message on arrival")
   expect(not game.hud.modal.visible,"the search box opened during the launch")
   expect(game.calls == 0 and game.start_chain.is_empty() and not game.start_launch,"start sequence not finished cleanly")
   expect(HUD.preset_places().size() > 0 and surprise_button() != null,"no Surprise me button in the search box")
   attempts.clear();clock = 0.;stage = 1
   game.hud.open_search();surprise_button().pressed.emit()
  1:
   # Wait until Surprise me has run out: the search box is back with the failure text.
   if not game.jump.is_empty() or not game.hud.modal.visible or clock < .5:return false
   var distinct := {}
   for title in attempts:distinct[title] = true
   print("SURPRISE attempts=%s search_open=%s choices=%d message=%s current=%s"%[str(attempts),str(game.hud.modal.visible),game.hud.choices.get_child_count(),game.hud.feedback.text,game.current_title])
   expect(attempts.size() == 1+POOL.RETRIES and distinct.size() == attempts.size(),"Surprise me did not try four distinct roads: "+str(attempts))
   expect(not attempts.has(POOL.FALLBACK.title),"Surprise me offered the current place")
   expect(game.hud.choices.get_child_count() == HUD.preset_places().size(),"the presets are not offered after the failure")
   expect("Surprise me" in game.hud.feedback.text,"the failure message does not offer Surprise me again")
   expect(game.ready_drive and game.current_title == POOL.FALLBACK.title and not game.tiles.is_empty(),"the car did not stay in Miami Beach")
   attempts.clear();clock = 0.;stage = 2
   game.hud.close_search();game.surprise_me()
  2:
   # A place chosen while Surprise me is still trying ends the sequence.
   if attempts.size() < 1:return false
   game.choose_place(POOL.FALLBACK.lon,POOL.FALLBACK.lat,"Launch QA return")
   expect(game.start_chain.is_empty() and not game.start_launch,"choose_place did not end the Surprise me sequence")
   clock = 0.;stage = 3
  3:
   if not game.jump.is_empty() or game.current_title != "Launch QA return":
    if game.hud.modal.visible:failures.append("the search box opened after a direct choice");finish()
    return false
   print("RETURN current=%s requests=%d search_open=%s"%[game.current_title,game.calls,str(game.hud.modal.visible)])
   expect(game.calls == 0 and not game.hud.modal.visible,"the direct choice did not arrive cleanly")
   finish()
 return false
