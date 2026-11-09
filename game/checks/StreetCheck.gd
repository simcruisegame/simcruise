extends Node
# Run the normal destination, drive, and street sampling path at a supplied US road.
var game
var stage = 0
var latitude = 0.
var longitude = 0.
var expectation = ""
var started: int
var settled = 0.
var initial: Vector2

func _ready():
 game = get_parent()
 var args = OS.get_cmdline_user_args()
 var index = args.find("--street-check")
 latitude = float(args[index+1])
 longitude = float(args[index+2])
 expectation = args[index+3]
 started = Time.get_ticks_msec()

func fail(reason):
 print("STREET TEST FAILED ",reason)
 set_process(false)
 game.request_game_close(1)

func _process(dt):
 if Time.get_ticks_msec()-started > 100000:
  fail("timed out "+str(game.jump))
  return
 if not game.ready_drive or not game.jump.is_empty():return
 if stage==0:
  game.choose_place(longitude,latitude,"Streaming check")
  stage=1
  return
 if game.current_title!="Streaming check":return
 var id=game.tile_id(game.pos)
 var tile=game.tiles.get(id)
 if tile==null or not tile.streaming_ground:return
 if stage==1:
  var rec=game.street_detail.records.get(id)
  if not rec is Dictionary:return
  var appearance=rec.get("appearance",{})
  var observed=appearance is Dictionary and appearance.get("status")=="accepted"
  var value=tile.streaming_ground.get_shader_parameter("photo_strength")
  print("STREET ",id," ",rec.get("status")," frames=",rec.get("frames",[]).size()," accepted=",observed," influence=",value," requests=",rec.get("requests")," road=",game.road_name)
  if expectation in ["accepted","both"] and (not observed or not value is Vector2 or value.x<=0.):
   fail("expected usable observed material");return
  if expectation=="both" and value.y<=0.:
   fail("expected supported verge color");return
  if expectation=="fallback" and (rec.get("status")!="available" or observed or value!=Vector2.ZERO):
   fail("expected default material");return
  game.toggle_street_detail()
  if game.street_detail.enabled or tile.streaming_ground.get_shader_parameter("photo_strength")!=Vector2.ZERO:
   fail("disable did not clear material");return
  game.toggle_street_detail()
  if tile.streaming_ground.get_shader_parameter("photo_strength")!=value:
   fail("reenable did not restore material");return
  var capture=OS.get_environment("SIMCRUISE_QA_SCREENSHOT")
  if capture!="" and DisplayServer.get_name()!="headless":
   get_viewport().get_texture().get_image().save_jpg(capture,.92)
  initial=game.pos
  stage=2
  Input.action_press("gas",.55)
 elif stage==2:
  settled+=dt
  if settled<3.:return
  Input.action_release("gas")
  var travelled=game.pos.distance_to(initial)
  print("STREET DRIVE ",travelled," m, wheel contacts ",game.vehicle.wheel_contact)
  if travelled<2 or game.vehicle.wheel_contact!=4:
   fail("drive contact/distance");return
  set_process(false)
  print("STREET TEST PASSED")
  game.request_game_close()
