extends Node
# Exercise real Jolt driving and the bundled southbound Chula tile seam with
# one tile temporarily unavailable. All data is local; no provider is needed.
var game
var pilot=preload("res://checks/FieldCheck.gd").new()
var original: Vector2i
var required: Vector2i
var phase=0
var elapsed=0.
var ticks=0
var entry=Vector3.ZERO
var held_linear=Vector3.ZERO
var held_angular=Vector3.ZERO
var prior=Vector3.ZERO
var axle=[]
var rpm=0.
var anchor=Vector3.ZERO
var crossed=Vector3.ZERO
var observed_request=false

func _ready():
 game=get_parent()
 game.helper="/usr/bin/false"
 pilot.game=game

func _exit_tree():
 pilot.free()

func finish(ok,reason):
 for action in ["gas","left","right"]:Input.action_release(action)
 print(("SEAM PASSED " if ok else "SEAM FAILED ")+reason)
 set_physics_process(false)
 game.request_game_close(0 if ok else 1)

func _physics_process(dt):
 elapsed+=dt
 if elapsed>55.:
  finish(false,"timeout phase="+str(phase)+" task="+str(game.task))
  return
 if not game.ready_drive or not game.jump.is_empty():return
 var car=game.vehicle
 if phase==0:
  original=game.tile_id(game.pos)
  required=original+Vector2i.DOWN
  if elapsed<1.5 or car.wheel_contact!=4 or not game.tiles.has(required):return
  if original!=Vector2i(18585,25423):finish(false,"unexpected bundled start");return
  # The native streamer’s normal failure backoff holds this known good tile.
  # Removing its collision node ensures the test cannot cross on stale ground.
  var old=game.tiles[required]
  game.tiles.erase(required);game.remove_child(old);old.queue_free()
  game.failures[required]=Time.get_ticks_msec()+60000
  Input.action_press("gas",.80)
  phase=1
 elif phase==1:
  pilot.follow_chula_lane()
  if car.freeze:
   entry=prior
   if entry.length()<6.:finish(false,"entry speed insufficient "+str(entry));return
   # The production branch captures motion immediately before freezing, one
   # integration step later than the preceding sampled tick. The fallback lets
   # this same regression run against a pre-fix build.
   if game.get("waiting_linear") is Vector3:
    held_linear=game.get("waiting_linear")
    held_angular=game.get("waiting_angular")
   anchor=car.position;axle=car.omega.duplicate();rpm=car.engine_omega
   Input.action_release("gas")
   Input.action_release("left");Input.action_release("right")
   print("SEAM hold required=",required," entry_mps=",entry.length()," at=",anchor)
   phase=2
  else:prior=car.linear_velocity
 elif phase==2:
  if not car.freeze or car.position.distance_to(anchor)>.02 or car.omega!=axle or car.engine_omega!=rpm:
   finish(false,"motion or driveline changed before terrain arrived");return
  ticks+=1
  if ticks<24:return
  game.hud.open_search()
  game.failures.erase(required)
  phase=3
 elif phase==3:
  if game.task.get("kind")=="tile" and game.task.get("id")==required:observed_request=true
  if not game.tiles.has(required):return
  if not car.freeze or car.position.distance_to(anchor)>.02 or not observed_request:
   finish(false,"menu pause or requested tile selection");return
  game.hud.close_search()
  phase=4
 elif phase==4:
  if car.freeze:return
  var dv=car.linear_velocity.distance_to(entry)
  print("SEAM restored_mps=",car.linear_velocity.length()," velocity_delta=",dv," frozen_ticks=",ticks)
  if dv>.70 or not car.linear_velocity.is_equal_approx(held_linear) or not car.angular_velocity.is_equal_approx(held_angular):
   finish(false,"resume lost entry motion");return
  crossed=car.position
  phase=5
 elif phase==5:
  if game.tile_id(game.pos)!=required or car.position.distance_to(crossed)<8. or car.wheel_contact!=4:return
  if car.linear_velocity.length()<5. or game.calls!=0:finish(false,"seam crossing failed");return
  print("SEAM coasted ",snappedf(car.position.distance_to(crossed),.01),"m, contacts=4")
  var old=game.tiles[required]
  game.tiles.erase(required);game.remove_child(old);old.queue_free()
  game.failures[required]=Time.get_ticks_msec()+60000
  phase=6
 elif phase==6:
  if not car.freeze or game.get("waiting_tile")!=required:return
  game.choose_place(-77.6244769,37.3850801,"Seam QA teleport","Raven Wing Drive")
  phase=7
 elif phase==7:
  if game.current_title!="Seam QA teleport" or car.wheel_contact!=4:return
  if game.get("waiting_tile")!=null or car.linear_velocity.length()>1.:
   finish(false,"teleport restored old seam velocity");return
  Input.action_press("gas",.6)
  phase=8
 elif phase==8:
  if car.linear_velocity.length()<2.:return
  game.recover()
  if car.linear_velocity.length()>.05 or game.calls!=0:finish(false,"recover retained motion or contacted network");return
  finish(true,"entry motion restored, four contacts, menu/teleport/recover, 0 requests")
