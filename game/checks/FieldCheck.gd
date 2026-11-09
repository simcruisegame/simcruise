extends Node
var stage=0
var elapsed=0.
var total=0.
var game
var frames=[]
var prev_usec=0
var start_id=Vector2i.ZERO
var helper_calls=0
var revs=0.
var output="/private/tmp/sd-real/qa"
var observed_requests=0
var finished=false
func _ready():
 game=get_parent()
 DirAccess.make_dir_recursive_absolute(output)
 game.helper="/usr/bin/false"
# End the check once: one failure line, no further _process, then the graceful
# close. The close can take many frames while workers return, and before this
# every failure path re-ran its stage and printed again on each of them.
func finish(code:int,reason:String=""):
 if finished:return
 finished=true
 set_process(false)
 if reason!="":print("FIELD FAIL ",code," ",reason)
 game.request_game_close(code)
func screenshot(name):
 if DisplayServer.get_name()!="headless":
  get_viewport().get_texture().get_image().save_jpg(output+"/"+name+".jpg",.92)
# The southbound start bends before the comparison point. Exercise the same
# steering actions as a driver, looking ahead along the mapped right lane.
func follow_chula_lane():
 var basis=game.vehicle.global_basis
 var heading=Vector2(-basis.z.x,-basis.z.z).normalized()
 var ahead=clampf(absf(game.velocity)*.85,7.,18.)
 var probe_point=game.pos+heading*ahead
 var best_distance=INF
 var target=probe_point
 for id in game.tiles:
  var offset=game.tile_offset(id)
  if probe_point.distance_to(probe_point.clamp(offset,offset+Vector2.ONE*game.side))>25.:continue
  var hit=game.tiles[id].nearest(probe_point-offset,true,"Chula Road")
  if not hit.size() or hit[2]>=best_distance:continue
  best_distance=hit[2]
  var tangent=(hit[1][1]-hit[1][0]).normalized()
  if tangent.dot(heading)<0.:tangent=-tangent
  target=offset+hit[0]+Vector2(-tangent.y,tangent.x)*minf(1.8,hit[1][3]*.18)
 var delta=target-game.pos
 var x=delta.dot(Vector2(basis.x.x,basis.x.z).normalized())
 var demand=-atan2(2.*2.266*x,maxf(delta.length_squared(),1.))/game.vehicle.available_steering(game.velocity)
 for action in ["left","right"]:
  var strength=clampf(demand if action=="left" else -demand,0.,1.)
  if strength>.001:Input.action_press(action,strength)
  else:Input.action_release(action)

func _process(dt):
 if finished or game.closing:return
 var now=Time.get_ticks_usec()
 if prev_usec and stage==1:frames.append((now-prev_usec)/1000.)
 prev_usec=now
 total+=dt
 if total>75:
  finish(1,"timeout stage=%d task=%s road=%s"%[stage,str(game.task),game.road_name]);return
 if not game.ready_drive or not game.jump.is_empty():return
 elapsed+=dt
 if stage==0 and elapsed>3 and game.tiles.size()>=4:
  game.hud.message("");screenshot("Chula Road")
  print("CHULA ",game.road_name," ",game.origin," ground=",game.ground(game.pos)," contacts=",game.vehicle.wheel_contact)
  if game.vehicle.wheel_contact!=4 or game.road_name!="Chula Road":finish(2,"Chula start contacts=%d road=%s"%[game.vehicle.wheel_contact,game.road_name]);return
  start_id=game.tile_id(game.pos);Input.action_press("gas");stage=1;elapsed=0
 elif stage==1:
  follow_chula_lane()
  revs+=absf(game.vehicle.omega[2])*dt/TAU
  if elapsed>6.5:
   Input.action_release("gas");Input.action_release("left");Input.action_release("right");print("DRIVE metres=",game.distance," mph=",game.velocity*2.23694," revolutions=",revs," crossed_tile=",game.tile_id(game.pos)!=start_id)
   if game.distance<38 or game.tile_id(game.pos)==start_id or game.velocity<8. or game.vehicle.wheel_contact<3:finish(3,"Chula drive metres=%.1f same_tile=%s mps=%.1f contacts=%d"%[game.distance,str(game.tile_id(game.pos)==start_id),game.velocity,game.vehicle.wheel_contact]);return
   game.hud.open_search();game.hud.field.text="raven wing dr in chesterifeld";game.hud.submit()
   if game.hud.choices.get_child_count()!=1:finish(4,"search choices=%d"%game.hud.choices.get_child_count());return
   game.hud.choices.get_child(0).pressed.emit();stage=2;elapsed=0
 elif stage==2 and elapsed>3.5 and game.tiles.size()>=4:
  game.hud.message("");screenshot("Raven Wing Drive");frames.sort()
  print("RAVEN ",game.road_name," ",game.origin," contacts=",game.vehicle.wheel_contact," provider_requests=",game.calls," frame_median_ms=",frames[frames.size()/2]," frame_p95_ms=",frames[int(frames.size()*.95)])
  if game.road_name!="Raven Wing Drive" or game.calls!=0 or game.vehicle.wheel_contact!=4:finish(5,"Raven arrival road=%s requests=%d contacts=%d"%[game.road_name,game.calls,game.vehicle.wheel_contact]);return
  var event=InputEventMouseButton.new()
  event.button_index=MOUSE_BUTTON_LEFT;event.pressed=true
  event.position=game.hud.map.get_global_rect().get_center()
  get_viewport().push_input(event,true)
  event=event.duplicate();event.pressed=false;get_viewport().push_input(event,true)
  if not game.hud.atlas_map.visible:finish(6,"atlas map did not open from the minimap");return
  elapsed=0;stage=3
 elif stage==3 and elapsed>2 and game.hud.atlas_map.images.size()>=2:
  var map=game.hud.atlas_map;var orig=map.level
  var pt=map.size*.55
  var event=InputEventMouseButton.new();event.button_index=MOUSE_BUTTON_WHEEL_DOWN;event.pressed=true;event.position=pt;get_viewport().push_input(event,true)
  if map.level!=orig+1:finish(7,"atlas zoom level=%d expected=%d"%[map.level,orig+1]);return
  event=event.duplicate();event.button_index=MOUSE_BUTTON_LEFT;get_viewport().push_input(event,true)
  event=event.duplicate();event.pressed=false;get_viewport().push_input(event,true)
  if not map.pinned:finish(8,"atlas click did not pin");return
  elapsed=0;stage=4
 elif stage==4 and elapsed>1.4:
  screenshot("Road map")
  var map=game.hud.atlas_map
  print("MAP open=",map.visible," zoom=",map.level," pin=",map.coordinates(map.pin)," tiles=",map.images.size())
  map.recenter()
  if map.pinned or map.center.distance_to(game.pos)>.01:finish(9,"atlas recenter pinned=%s offset=%.3f"%[str(map.pinned),map.center.distance_to(game.pos)]);return
  var key=InputEventKey.new();key.keycode=KEY_TAB;key.pressed=true;get_viewport().push_input(key,true)
  if map.visible:finish(10,"atlas map still open after Tab");return
  print("FIELD CHECK PASSED")
  finish(0)
