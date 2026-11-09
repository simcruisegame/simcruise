extends Node
# --landscape-drive OUTPUT_DIRECTORY selects US 163 once, then uses normal player inputs and the stock cockpit.
const STOP = "Monument Valley · US 163, UT"
const START_LON = -109.990814
const START_LAT = 37.101444
const HEADING = 230.
const TARGET_MPS = 14.3
const MIN_DISTANCE = 640.
var game
var directory = ""
var started_at = 0
var driving = false
var finished = false
var ticks = 0
var elapsed = 0.
var distance_m = 0.
var last_position = Vector2.ZERO
var last_y = 0.
var actual_start=[]
var start_error_m=0.
var max_step_y = 0.
var max_speed = 0.
var max_lane_error = 0.
var wheel_histogram = [0,0,0,0,0]
var road_tiles = []
var regions = []
var boundary_events = []
var first_region = Vector2i(-1,-1)
var old_region = Vector2i(-1,-1)
var previous_display = Vector2i(-1,-1)
var pending_ticks = 0
var pending_drawn = 0
var total_waiting = 0
var hidden_while_driving = 0
var render_us = []
var previous_us = 0
var images = []
var screen_busy = false

func _ready():
 game = get_parent()
 call_deferred("begin")

func begin():
 var args = OS.get_cmdline_user_args()
 var arg = args.find("--landscape-drive")
 if arg < 0 or arg+1 >= args.size():
  game.request_game_close(2)
  return
 directory = args[arg+1]
 if DirAccess.make_dir_recursive_absolute(directory) != OK:
  game.request_game_close(2)
  return
 started_at = Time.get_ticks_msec()
 game.street_detail.enabled = false
 if not "--with-soil" in args:
  game.recent_user_choice = true
  game.recent_mode = false
 game.live_weather = false
 game.vehicle.set_manual(false)
 game.view = 1
 Input.action_press("handbrake")
 game.hud.open_search()
 for item in game.hud.choices.get_children():
  if item is Button and item.text==STOP:
   item.pressed.emit()
   return
 finish(false,"Monument Valley menu stop unavailable")

func region_for(tile:Vector2i)->Vector2i:
 return Vector2i(floori(tile.x/8.),floori(tile.y/8.))

func _process(_dt):
 if not driving or finished:return
 var now = Time.get_ticks_usec()
 if previous_us:render_us.append((now-previous_us)*.001)
 previous_us = now

func _physics_process(dt):
 if finished or started_at==0:return
 if Time.get_ticks_msec()-started_at>150000:
  finish(false,"wall clock timeout")
  return
 if not game.ready_drive or not game.jump.is_empty() or game.current_title!=STOP:return
 var car = game.vehicle
 var landscape = game.regional_terrain
 var here = Vector2(car.position.x,car.position.z)
 var id:Vector2i = game.tile_id(here)
 var region = region_for(id)
 if not driving:
  if car.wheel_contact!=4 or landscape.displayed!=region or not landscape.instance.visible:return
  if not landscape.find_children("*","CollisionObject3D",true,false).is_empty():
   finish(false,"regional relief has collision geometry")
   return
  var ll=game.player_coordinates()
  actual_start=[ll.x,ll.y]
  start_error_m=Vector2((ll.y-START_LON)*111320.*cos(deg_to_rad(START_LAT)),(ll.x-START_LAT)*111320.).length()
  if start_error_m>15.:
   finish(false,"unexpected snapped start")
   return
  Input.action_release("handbrake")
  first_region=region
  last_position=here
  last_y=car.position.y
  driving=true
  game.hud.visible=false
 var step_m=here.distance_to(last_position)
 distance_m+=step_m
 max_step_y=maxf(max_step_y,absf(car.position.y-last_y))
 last_position=here
 last_y=car.position.y
 elapsed+=dt
 ticks+=1
 var speed=Vector2(car.linear_velocity.x,car.linear_velocity.z).length()
 max_speed=maxf(max_speed,speed)
 wheel_histogram[clampi(car.wheel_contact,0,4)]+=1
 if car.freeze:total_waiting+=1
 if not landscape.instance.visible:hidden_while_driving+=1
 var tile_coords=[id.x,id.y]
 if road_tiles.is_empty() or road_tiles[-1]!=tile_coords:road_tiles.append(tile_coords)
 if region!=old_region:
  if old_region!=Vector2i(-1,-1):
   boundary_events.append({"from":[old_region.x,old_region.y],"to":[region.x,region.y],"at_m":distance_m,"contacts":car.wheel_contact,"speed_mps":speed,"camera_visible":landscape.instance.visible})
  old_region=region
 if landscape.displayed!=region:
  pending_ticks+=1
  if landscape.instance.visible:pending_drawn+=1
 if previous_display!=landscape.displayed and landscape.displayed!=landscape.EMPTY:
  previous_display=landscape.displayed
  var evidence=landscape.last_result.duplicate()
  evidence.region=[previous_display.x,previous_display.y]
  evidence["at_m"]=distance_m
  evidence["visible"]=landscape.instance.visible
  regions.append(evidence)
 if not screen_busy and DisplayServer.get_name()!="headless":
  if images.is_empty() and distance_m>=32.:capture("approach")
  elif images.size()==1 and previous_display!=first_region and distance_m>600.:capture("after-boundary")
 if "--bank-drive-proof" in OS.get_cmdline_user_args() and distance_m>=120. and not screen_busy:
  var tile=game.tiles.get(id)
  print("ROAD BANKS ",tile.info.get("road_banks",{})," refined=",tile.info.get("bank_detail_active",false))
  finish(wheel_histogram[0]+wheel_histogram[1]+wheel_histogram[2]==0 and max_lane_error<1.8 and hidden_while_driving==0,"near-road bank and tire contacts")
  return
 if distance_m>=MIN_DISTANCE and previous_display==region and region!=first_region and not screen_busy:
  finish(wheel_histogram[0]==0 and wheel_histogram[1]==0 and wheel_histogram[2]==0 and max_lane_error<1.8 and hidden_while_driving==0,"landscape boundary and handling")
  return
 if distance_m>1250. or ticks>22000:
  finish(false,"regional replacement did not arrive")
  return
 var desired=Vector2(sin(deg_to_rad(HEADING)),-cos(deg_to_rad(HEADING)))
 var probe=here+desired*clampf(speed*.85,8,15)
 var best=INF
 var localbest=INF
 var target=probe
 # The source geometry has no street-name tag in this section. Its sole paved primary carriageway follows US 163 to the southwest.
 for tile_id in game.tiles:
  if maxi(absi(tile_id.x-id.x),absi(tile_id.y-id.y))>1:continue
  var offset=game.tile_offset(tile_id)
  for seg in game.tiles[tile_id].segments:
   if seg[5]!="primary" or (seg.size()>6 and seg[6]=="unpaved"):continue
   var a=seg[0]+offset
   var b=seg[1]+offset
   var t=(b-a).normalized()
   if t.dot(desired)<0.:t=-t
   if t.dot(desired)<.75:continue
   var lane=Vector2(-t.y,t.x)*minf(1.8,seg[3]*.18)
   var q=Geometry2D.get_closest_point_to_segment(probe,a,b)
   if q.distance_to(probe)<best:
    best=q.distance_to(probe)
    target=q+lane
   localbest=minf(localbest,Geometry2D.get_closest_point_to_segment(here,a+lane,b+lane).distance_to(here))
 if best==INF:
  finish(false,"mapped primary road lost")
  return
 max_lane_error=maxf(max_lane_error,localbest)
 var aim=target-here
 var turn=angle_difference(game.yaw,atan2(-aim.x,-aim.y))
 Input.action_release("left")
 Input.action_release("right")
 if absf(turn)>.002:Input.action_press("left" if turn>0 else "right",minf(.75,absf(turn)*1.8))
 Input.action_release("gas")
 Input.action_release("brake")
 if speed<TARGET_MPS+.4:Input.action_press("gas",clampf(.12+(TARGET_MPS-speed)*.115,.01,.74))
 elif speed>TARGET_MPS+1.:Input.action_press("brake",minf(.5,(speed-TARGET_MPS)*.12))

func capture(label:String):
 screen_busy=true
 var aim:Vector3=-game.camera.global_basis.z
 var record={"file":label+".jpg","at_m":distance_m,"eye_above_road_m":game.camera.position.y-game.ground(Vector2(game.camera.position.x,game.camera.position.z)),"bearing_deg":fposmod(rad_to_deg(atan2(aim.x,-aim.z)),360.),"cockpit":game.view==1,"camera_far_m":game.camera.far}
 await RenderingServer.frame_post_draw
 var image=get_viewport().get_texture().get_image()
 if image and image.save_jpg(directory.path_join(record.file),.94)==OK:images.append(record)
 screen_busy=false

func finish(ok:bool,reason:String):
 if finished:return
 finished=true
 for action in ["gas","brake","left","right","handbrake"]:Input.action_release(action)
 render_us.sort()
 var q=[]
 if not render_us.is_empty():
  for p in [.5,.95,.99]:q.append(render_us[mini(render_us.size()-1,int(render_us.size()*p))])
 var ll=game.player_coordinates()
 var receipt={"pass":ok,"reason":reason,"start_lat_lon":[START_LAT,START_LON],"end_lat_lon":[ll.x,ll.y],"start_actual_lat_lon":actual_start,"spawn_error_m":start_error_m,"distance_m":distance_m,"sim_s":elapsed,"ticks":ticks,"wheel_contacts_0_to_4":wheel_histogram,"max_speed_mps":max_speed,"max_lane_error_m":max_lane_error,"max_vertical_step_m":max_step_y,"frozen_ticks":total_waiting,"hidden_relief_ticks":hidden_while_driving,"pending_ticks":pending_ticks,"pending_visible_ticks":pending_drawn,"near_tiles":road_tiles,"regions":regions,"boundary_events":boundary_events,"cockpit_images":images,"frame_ms_p50_p95_p99":q,"renderer":DisplayServer.get_name(),"wall_s":(Time.get_ticks_msec()-started_at)*.001}
 var output=FileAccess.open(directory.path_join("drive.json"),FileAccess.WRITE)
 if output:output.store_string(JSON.stringify(receipt,"  "));output.close()
 print("LANDSCAPE DRIVE ",JSON.stringify(receipt))
 game.request_game_close(0 if ok and output else 4)
