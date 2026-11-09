extends Node
var game
var elapsed=0.
var timeout=0.
var stage=0
var samples=[]
var start=Vector2.ZERO
var drive_ticks=0
var trace=[]
var signed_speed=0.
var road_index=0
var finished=false
const STOPS=["Main Street, Stowe, Vermont","61.2162, -149.8922","21.3071, -157.8581"]
const EXPECTED=[Vector2(44.46542,-72.6872),Vector2(61.2162,-149.8922),Vector2(21.3071,-157.8581)]
const IDS=["Stowe","Anchorage","Honolulu"]
func _ready():game=get_parent()
func key(code,pressed=true):
 var e=InputEventKey.new();e.physical_keycode=code;e.keycode=code;e.pressed=pressed
 if code==KEY_SHIFT:Input.parse_input_event(e);return
 get_viewport().push_input(e,true)
 if pressed and code!=KEY_SHIFT:
  e=e.duplicate();e.pressed=false;get_viewport().push_input(e,true)
func step(n):
 stage=n;elapsed=0.;timeout=0.
func shot(label):
 if DisplayServer.get_name()!="headless":get_viewport().get_texture().get_image().save_jpg("/private/tmp/sd-real/qa/"+label+".jpg",.92)
func _physics_process(_dt):
 if stage==10 and not game.closing:
  drive_ticks+=1
  if drive_ticks%30==0:
   trace.append([drive_ticks,snappedf(-game.vehicle.linear_velocity.dot(game.vehicle.basis.z),.01),game.vehicle.wheel_contact,game.vehicle.freeze])
# One line per outcome, then no more processing while the game closes.
func fail(reason):
 if finished:return
 finished=true
 set_process(false)
 print("ROAD TEST FAILED ",stage," ",reason)
 game.request_game_close(1)
func pass_test():
 if finished:return
 finished=true
 set_process(false)
 print("ROAD TEST PASSED")
 game.request_game_close()
func _process(dt):
 if finished or game.closing:return
 elapsed+=dt;timeout+=dt
 if timeout>90.:fail("timeout "+str(game.task));return
 if not game.ready_drive:return
 var car=game.vehicle
 match stage:
  0:
   if not game.jump.is_empty() or elapsed<2.:return
   key(KEY_T);key(KEY_E)
   if not car.manual or car.selected_gear!=0:fail("manual clutch interlock");return
   key(KEY_SHIFT);step(1)
  1:
   if elapsed<.3:return
   key(KEY_E)
   if car.selected_gear!=1:fail("select first clutch %.2f action %.2f"%[car.clutch_pedal,Input.get_action_strength("clutch")]);return
   Input.action_press("gas",.75);step(2)
  2:
   if elapsed<.5:return
   key(KEY_SHIFT,false);step(3)
  3:
   if elapsed<2.4:return
   print("MANUAL 1 speed=",car.linear_velocity.length()," rpm=",car.rpm," stalled=",car.stalled)
   if car.linear_velocity.length()<3. or car.stalled:fail("first gear launch");return
   key(KEY_SHIFT);step(4)
  4:
   if elapsed<.25:return
   key(KEY_E);key(KEY_SHIFT,false);Input.action_release("gas");Input.action_press("parking_brake");step(5)
  5:
   if elapsed<.3:return
   print("MANUAL 2 gear=",car.selected_gear," front=",car.omega[0]," rear=",car.omega[2]," brake_lamps=",car.service_brake)
   if car.selected_gear!=2 or car.service_brake or absf(car.omega[2])>=absf(car.omega[0]):fail("rear e-brake");return
   Input.action_release("parking_brake");key(KEY_T);step(6)
  6:
   game.hud.open_search();game.hud.field.text=STOPS[road_index];game.hud.submit();step(7)
  7:
   if elapsed<.45 or not game.search_queue.is_empty() or (game.job and game.task.kind=="search"):return
   if not game.hud.choices.get_child_count():return
   var selection=game.hud.choices.get_child(0)
   print("LOAD ",IDS[road_index]," choice=",selection.text)
   selection.pressed.emit();Input.action_press("handbrake");step(8)
  8:
   if not game.jump.is_empty() or elapsed<2.5:return
   if game.hud.modal.visible:fail("travel failed "+game.hud.feedback.text);return
   if game.player_coordinates().distance_to(EXPECTED[road_index])>.004:fail("arrived at wrong coordinates "+str(game.player_coordinates()));return
   var tile=game.tiles.get(game.tile_id(game.pos))
   if not tile or not tile.info.get("live_canopy",false):fail("wrong destination "+game.current_title);return
   if not tile.info.get("canopy_ready") or not game.recent_cache.has(game.recent_key(game.tile_id(game.pos))):return
   if road_index==0:
    if not game.live_weather:game.toggle_weather()
    if not tile.learned_edges or not game.observation.has("station"):return
   if car.wheel_contact!=4:return
   print("US ",IDS[road_index]," street=",game.road_name," coord=",game.player_coordinates()," trees=",tile.grove.size()," image=",game.recent_label()," wheels=",car.wheel_contact," learned_surface=",tile.learned_edges!=null," weather=",game.weather_label())
   if road_index==0 and game.road_name!="Main Street":fail("wrong street");return
   game.view=1;game.camera_snap=true;step(9)
  9:
   if elapsed<.6:return
   shot(IDS[road_index]+" driver view")
   Input.action_release("handbrake");start=game.pos;drive_ticks=0;trace=[[0,snappedf(-game.vehicle.linear_velocity.dot(game.vehicle.basis.z),.01),game.vehicle.wheel_contact,game.vehicle.freeze]];Input.action_press("gas",.64);step(10)
  10:
   if drive_ticks<168:return
   Input.action_release("gas");print("US travel ",IDS[road_index]," metres=",game.pos.distance_to(start)," trace=",trace)
   if game.pos.distance_to(start)<3:fail("could not drive");return
   road_index+=1
   if road_index>=STOPS.size():pass_test();return
   step(6)
