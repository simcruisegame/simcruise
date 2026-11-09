extends SceneTree
# Both modes select the upstream KartaView station using the normal location API and ordinary Miata inputs.
const PLACE="Mystic Valley Parkway, Medford"
var game
var phase=0
var spawned=Vector2.ZERO
var start_time=0
var settled=0
var time_report=0
var grounded=4
var dest=""
var mode=""
var tile
var motion_tick=0

func _initialize():
 start_time=Time.get_ticks_msec()
 var args=OS.get_cmdline_user_args()
 var idx=args.find("--section-proof")
 if idx<0 or idx+2>=args.size():quit(9);return
 mode=args[idx+1];dest=args[idx+2]
 OS.set_environment("SD_DISABLE_MEASURED_SECTION","1" if mode=="before" else "0")
 game=load("res://Drive.tscn").instantiate()
 root.add_child(game)

func _physics_process(_delta):
 if phase>=5:return false
 if Time.get_ticks_msec()-time_report>10000:
  time_report=Time.get_ticks_msec()
  print("MEDFORD phase ",phase," wheels ",game.vehicle.wheel_contact," ticks=",motion_tick," speed=",game.velocity," t=",game.current_title," pos=",game.pos," settled=",settled," aero=",(tile.aerial_pixels!=null or tile.learned_edges!=null) if tile else false)
 if Time.get_ticks_msec()-start_time>110000:
  phase=5;printerr("MEDFORD timeout ",mode," ",game.current_title," ",game.pos," ",game.tiles.keys()," ",game.jump)
  game.request_game_close(4);return false
 if phase==0 and game.hud!=null:
  game.choose_place(-71.084348,42.405039,PLACE,"Mystic Valley Parkway",256.7);phase=1;return false
 if not game.ready_drive or not game.jump.is_empty() or game.current_title!=PLACE:return false
 tile=game.tiles.get(Vector2i(19827,24228))
 if tile==null:return false
 if phase==1:
  if (tile.measured_section!=null)!=(mode!="before"):printerr("Unexpected integration on ",mode);phase=5;game.request_game_close(2);return false
  game.view=1;game.vehicle.set_manual(false);game.hud.root.hide()
  phase=2
 if phase==2:
  settled=settled+1 if game.vehicle.wheel_contact==4 else 0
  if settled<16 or game.tiles.size()<3 or (tile.aerial_pixels==null and tile.learned_edges==null):return false
  spawned=game.pos;phase=3;Input.action_press("gas",.58)
  return false
 if phase==3:
  grounded=mini(grounded,game.vehicle.wheel_contact);motion_tick+=1
  if game.pos.distance_to(spawned)<4.:return false
  Input.action_release("gas");game.set_physics_process(false);game.vehicle.freeze=true;game.vehicle.linear_velocity=Vector3.ZERO;game.vehicle.angular_velocity=Vector3.ZERO
  var receipt={"mode":mode,"road":game.road_name,"start":[spawned.x,spawned.y],"stop":[game.pos.x,game.pos.y],"heading":fposmod(-rad_to_deg(game.yaw),360.),"travelM":game.pos.distance_to(spawned),"minContacts":grounded,"physicsFrames":motion_tick,"speedMPS":absf(game.velocity),"tileCount":game.tiles.size(),"camera":game.view,"grade":tile.info.get("road_grade"),"surveyMarks":tile.measured_section.marks.size() if tile.measured_section else 0}
  var f=FileAccess.open(dest+".json",FileAccess.WRITE);f.store_string(JSON.stringify(receipt));f.close()
  print("MEDFORD DRIVE ",JSON.stringify(receipt))
  phase=5
  if DisplayServer.get_name()=="headless":game.request_game_close(0 if grounded==4 else 3)
  else:capture.call_deferred()
 return false

func capture():
 for i in 4:await RenderingServer.frame_post_draw
 var err=root.get_texture().get_image().save_png(dest+".png")
 print("MEDFORD COCKPIT ",mode," ",dest," ",err)
 game.request_game_close(0 if err==OK and grounded==4 else 3)
