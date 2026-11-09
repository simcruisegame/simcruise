extends SceneTree
var game
var launch=Vector2.ZERO
var crossing=Vector2.ZERO
var started=Time.get_ticks_msec()
var phase=0
var settled=0
var min_wheels=4
var peak_speed=0.
var receipts
func _initialize():
 game=load("res://Drive.tscn").instantiate()
 root.call_deferred("add_child",game)
 physics_frame.connect(step)
func step():
 if Time.get_ticks_msec()-started>65000:finish(false,"timeout");return
 if not game.ready_drive or not game.jump.is_empty():return
 if phase==0:
  game.choose_place(-119.7669947,39.164335,"Carson signal test","North Carson Street",0.)
  phase=1;return
 if game.current_title!="Carson signal test":return
 var id=Vector2i(10965,25007);var tile=game.tiles.get(id)
 if not tile or not tile.info.has("signal_controls"):return
 if phase==1:
  receipts=tile.info.signal_controls
  var mesh=tile.get_node_or_null("MappedIntersectionSignals")
  if not mesh or not mesh.find_children("*","CollisionObject3D",true,false).is_empty():finish(false,"fixture geometry");return
  settled=settled+1 if game.vehicle.wheel_contact==4 else 0
  if settled<25:return
  var w=game.project(-119.76699471473694,39.16458192652182)
  crossing=game.tile_offset(id)+(w-Vector2(id))*game.side
  launch=game.pos;game.vehicle.set_manual(false);Input.action_press("gas",.48);phase=2;return
 if phase!=2:return
 min_wheels=mini(min_wheels,game.vehicle.wheel_contact)
 peak_speed=maxf(peak_speed,absf(game.velocity))
 var delta=game.pos-launch
 if delta.length()<43.:return
 finish(min_wheels==4 and game.road_name=="North Carson Street" and launch.y>crossing.y+12. and game.pos.y<crossing.y-8. and absf(game.pos.x-crossing.x)<7.,"northbound through signal")
func finish(ok:bool,label:String):
 physics_frame.disconnect(step)
 Input.action_release("gas")
 print("CARSON SIGNAL DRIVE ", "PASS" if ok else "FAIL"," ",label," count=",receipts," road=",game.road_name," travel=",game.pos.distance_to(launch)," before_m=",launch.y-crossing.y," beyond_m=",crossing.y-game.pos.y," min_contacts=",min_wheels," peak_mps=",peak_speed)
 game.request_game_close(0 if ok else 1)
