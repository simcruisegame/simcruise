extends Node3D
const TILE=preload("res://WorldTile.gd")
const HUD=preload("res://Hud.gd")
const CAR=preload("res://art/miata.glb")
const WORLD=40075016.686
const START_POOL=preload("res://StartPool.gd")
# Check modes and A/B captures start in the bundled place, offline and the same
# every run (StartPool.FALLBACK); --random-start (or start_mode) overrides.
const BUNDLED_START_FLAGS=["--bundled-start","--car-audit","--bethel-facade-ab","--archive-normal-check","--reference","--pad-steering","--capitol-drive-proof","--building-check","--crossfall-drive","--street-check","--us-check","--survey-stops","--seam-check","--landscape-drive","--field-check","--profile-trees","--preference-check","--road-test","--car-turntable"]
var tiles={}
var materials={}
var rangeland=preload("res://Rangeland.gd").new()
var road_banks=preload("res://RoadBankStream.gd").new()
var regional_terrain=preload("res://RegionalTerrain.gd").new()
var roadside=preload("res://Roadside.gd").new()
var forest_stream=preload("res://ForestFormStream.gd").new()
var spectral_ground=preload("res://SpectralGround.gd").new()
var building_stream=preload("res://BuildingStream.gd").new()
var street_detail=preload("res://StreetDetail.gd").new()
var grove=preload("res://Grove.gd").new()
var origin=Vector2i.ZERO
var side=480.0
var pos=Vector2.ZERO
var yaw=0.0
var velocity=0.0
var steering=0.0
var driving_preferences=preload("res://DrivingPreferences.gd").new()
var generic_car:=false
var distance=0.0
var view=1
var vehicle=preload("res://Powertrain.gd").new()
var camera=Camera3D.new()
var cockpit=preload("res://Cockpit.gd").new()
var sun=DirectionalLight3D.new()
var hud
var world_environment:Environment
var daylight=preload("res://SurveyDaylight.gd").new()
var day_sky:Sky
var cloud_sky:Sky
var scene_lighting=""
var archive_weather=preload("res://ArchiveWeather.gd").new()
var live_weather=false
var observation={}
var recent_mode=false
var recent_user_choice=false
var surface_job:Thread
var surface_id=Vector2i.ZERO
var recent_job:Thread
var recent_cache={}
var recent_retry={}
var weather_job:Thread
var next_weather_check=0
var weather_location=Vector2.INF
var canopy_job:Thread
var canopy_retry={}
var photo_job:Thread
var photo_retry={}
var job:Thread
var task={}
var jump={}
var search_queue=""
var query_serial=0
var helper=""
var cache=""
var timer=0.0
var msg_time=0.0
var ready_drive=false
var closing=false
var close_code=0
var camera_snap=true
var road_name="Ocean Drive"
var failures={}
# Godot clears body velocity while a rigid body is frozen. Keep the entry
# velocities once, alongside the exact collision tile that blocks this step.
var waiting_tile = null
var waiting_linear := Vector3.ZERO
var waiting_angular := Vector3.ZERO
var calls=0
var tile_count=0
var previous_frame=0
var frame_times=[]
var fps_label=Label.new()
var engine=AudioStreamPlayer.new()
var engine_stream
var engine_voice
var sound_enabled=false
var pitch_angle=0.0
var current_title="Finding a road…"
var start_mode=""       # "random" or "bundled"; empty: plan_start() reads the command line
var start_rng=RandomNumberGenerator.new()
var start_pool=[]
var start_chain=[]      # pool places still to try while a random start or Surprise me fails
var start_launch=false  # this chain ends in the bundled place (launch), not in the search box
var start_note=""       # said on arrival after a fallback
var traffic=[]
var wheels=[]
var probe=ReflectionProbe.new()
var last_probe=Vector2(INF,INF)
var park_clock=1.0
var wheel_angle=[0.,0.,0.,0.]
var lamp_materials=[]
var lamp_nodes=[]
var steering_wheel=[]   # [node, rest basis] on generated cars (CarBody.gd)
const STEERING_RATIO:=14.5
var lamp_on=false
# Head and tail lamps (L). CarFinish.gd fills head_lamp_parts for the 1999
# roadster: the headlamp lens glow and two low-beam spot lights. With the lamps
# on the stop lamps keep a dimmer tail glow between brake applications.
const BRAKE_GLOW:=1.72
const TAIL_GLOW:=.42
var head_lamps_on=false
var head_lamp_parts=[]  # [material, lit emission energy] pairs and Light3D nodes
var shadow_plane=PlaneMesh.new()
var ambient_life=preload("res://AmbientLife.gd").new()
var roughness=preload("res://RoadRoughness.gd").new()
var graphics=preload("res://GraphicsQuality.gd").new()
var car_wear=preload("res://CarWear.gd").new()
var probe_step_m=32.
func material(tex=null,color=Color.WHITE):
 var m=StandardMaterial3D.new();m.cull_mode=BaseMaterial3D.CULL_DISABLED;m.albedo_color=color;m.roughness=.86;m.texture_filter=BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
 if tex:m.albedo_texture=tex
 return m
func _ready():
 get_tree().auto_accept_quit=false
 get_tree().root.close_requested.connect(request_game_close)
 for key in TILE.WALL+TILE.SHOP:
  var finish=material(load("res://art/f%d.jpg"%key))
  if TILE.SHOP_CROP.has(key):
   var top=float(TILE.SHOP_CROP[key])
   finish.uv1_offset=Vector3(0.,top,0.)
   finish.uv1_scale=Vector3(1.,1.-top,1.)
  materials[key]=finish
 for key in {"trim":"#413d34","awning0":"#405947","awning1":"#9a7654","awning2":"#753d34","iron":"#36443e","globe":"#e9e5ce","curb":"#aaa79d","walk":"#ada99e"}:
  var colors={"trim":"#413d34","awning0":"#405947","awning1":"#9a7654","awning2":"#753d34","iron":"#36443e","globe":"#e9e5ce","curb":"#aaa79d","walk":"#ada99e"}
  materials[key]=material(null,Color(colors[key]))
 var roadside_colors={"signal_steel":"#535b55","signal_yellow":"#dba326","signal_dark":"#252b27","signal_go":"#76b38a","fuel_red":"#b2252c","fuel_green":"#52994e","fuel_white":"#d3d4cd","fuel_steel":"#676f70"}
 for key in roadside_colors:materials[key]=material(null,Color(roadside_colors[key]))
 materials.whitepaint=material(null,Color("#d8d5c2"));materials.yellowpaint=material(null,Color("#c6a354"))
 materials.doorframe=material(null,Color("#a79d88"))
 materials.cap=material(null,Color("#b9ad99"))
 # Roofs without an aerial photo still get shingle courses (SurveyRoof.gdshader, no image).
 var plain_roof=ShaderMaterial.new();plain_roof.shader=preload("res://SurveyRoof.gdshader")
 plain_roof.set_shader_parameter("fallback",Color("#5f605f"));materials.roof=plain_roof
 for key in {"house_trim":"#e3ded1"}:
  materials[key]=material(null,Color("#e3ded1"))
 for key in TILE.HOUSE.COLORS:materials[key]=material(null,Color(TILE.HOUSE.COLORS[key]))
 # Window glass reflects the sky and street like real glazing instead of a flat panel.
 var window_glass=material(null,Color("#1d2a30"));window_glass.roughness=.06;window_glass.metallic_specular=.9;window_glass.metallic=.25
 materials["house_glass"]=window_glass
 # House walls (HouseWall.gdshader): 12 common US exterior paints on lap siding
 # (siding0-11) and 4 brick colours (siding12-15), chosen per house by id.
 var house_paints=["#ece9e1","#e2d9c4","#c9cbc7","#b8b1a2","#c6b28d","#a8b29a","#a8bbc7","#6f8293","#e4d397","#8d8372","#53575a","#b9a48a"]
 var bricks=["#8b4838","#76503f","#9f5a42","#a98c6a"]
 for i in 16:
  var wall_material=ShaderMaterial.new();wall_material.shader=preload("res://HouseWall.gdshader")
  wall_material.set_shader_parameter("paint",Color(house_paints[i] if i<12 else bricks[i-12]))
  wall_material.set_shader_parameter("style",0 if i<12 else 1)
  materials["siding"+str(i)]=wall_material
 var grass=Image.new()
 if grass.load_jpg_from_buffer(FileAccess.get_file_as_bytes("res://data/grass-detail.jpg"))==OK:
  grass.generate_mipmaps();materials["grass_detail"]=ImageTexture.create_from_image(grass)
 var mineral:=Image.new()
 if mineral.load_png_from_buffer(FileAccess.get_file_as_bytes("res://data/mineral-ground.png"))==OK:
  mineral.generate_mipmaps()
  materials["mineral_detail"]=ImageTexture.create_from_image(mineral)
 var leaves=Image.new()
 if leaves.load_jpg_from_buffer(FileAccess.get_file_as_bytes("res://data/leaf-litter.jpg"))==OK:
  leaves.generate_mipmaps()
  materials.litter=ImageTexture.create_from_image(leaves)
 add_child(rangeland);rangeland.enabled=not "--without-shrubs" in OS.get_cmdline_user_args()
 add_child(grove);add_child(roadside);add_child(regional_terrain)
 road_banks.enabled=not "--without-road-banks" in OS.get_cmdline_user_args()
 regional_terrain.enabled=not "--without-landscape" in OS.get_cmdline_user_args()
 var env=Environment.new();env.background_mode=Environment.BG_SKY
 var sk=Sky.new();var sky_mat=daylight.sky_material(preload("res://art/day.hdr"));sk.sky_material=sky_mat;env.sky=sk
 daylight.configure_environment(env) # ambient source, tonemapper, one haze model (SurveyDaylight.gd)
 if "--legacy-filters" in OS.get_cmdline_user_args():get_viewport().use_debanding=false;get_viewport().anisotropic_filtering_level=Viewport.ANISOTROPY_4X # A/B for the project.godot filter settings
 world_environment=env
 day_sky=sk
 var clouds=ProceduralSkyMaterial.new()
 clouds.sky_top_color=Color("#8294a6")
 clouds.sky_horizon_color=Color("#c9cdd0")
 clouds.ground_horizon_color=Color("#aab0ad")
 clouds.ground_bottom_color=Color("#687168")
 clouds.sun_angle_max=0.01
 cloud_sky=Sky.new();cloud_sky.sky_material=clouds
 var world=WorldEnvironment.new();world.environment=env;add_child(world)
 var first=plan_start()
 var start_location=Vector2(float(first.lat),float(first.lon))
 daylight.update(start_location,"",sun,sky_mat,0,0.,daylight.capture_weight(start_location,true))
 daylight.configure_sun(sun);daylight.apply_conditions(env,sun,daylight.CLEAR_CLOUD,16093.);add_child(sun)
 probe.size=Vector3(78,30,78);probe.max_distance=100;probe.intensity=.53;probe.cull_mask=1;add_child(probe)
 # The chosen car (preference "car", or --car=<id> for checks). Cars with a
 # generated model use CarBody.gd; the 1999 roadster keeps its CarFinish pass.
 driving_preferences.open()
 var car_id:String=driving_preferences.values.car
 for arg in OS.get_cmdline_user_args():
  if arg.begins_with("--car="):car_id=arg.trim_prefix("--car=")
 var car_spec=preload("res://CarSpec.gd").load_id(car_id)
 # A private model for this car (user://cars/<id>.glb, e.g. one you bought or
 # downloaded for yourself) replaces its exterior; it stays on this machine.
 # --stock-model ignores it.
 var private_model:="user://cars/%s.glb"%car_id
 if FileAccess.file_exists(private_model) and not "--stock-model" in OS.get_cmdline_user_args():
  car_spec.model_scene=private_model;car_spec.model_rig=car_spec.model_rig.duplicate();car_spec.model_rig.fit=true
 if car_spec.model_scene!="" and (ResourceLoader.exists(car_spec.model_scene) or car_spec.model_scene.begins_with("user://")):
  vehicle.use_spec(car_spec);generic_car=true
  preload("res://CarBody.gd").new().build(self)
 else:
  preload("res://CarFinish.gd").new().build(self) # player car, its likeness pass, wheels, lamps, shade and parked cars
 vehicle.add_child(cockpit)
 add_child(camera);camera.current=true;camera.near=.055;camera.far=56000 if regional_terrain.enabled else 1100;camera.fov=62
 driving_preferences.open()
 vehicle.set_manual(driving_preferences.values.manual)
 street_detail.enabled=driving_preferences.values.street_photos
 spectral_ground.enabled=driving_preferences.values.spectral_ground or "--spectral-ground" in OS.get_cmdline_user_args()
 hud=HUD.new();add_child(hud);hud.build(self);hud.camera.connect(switch_view);hud.reset.connect(recover);hud.sound.connect(toggle_sound);hud.search_for.connect(search);hud.choose.connect(choose_place);hud.surprise.connect(surprise_me)
 hud.root.add_child(fps_label);fps_label.position=Vector2(18,58);fps_label.add_theme_font_size_override("font_size",15);fps_label.add_theme_constant_override("outline_size",5);fps_label.hide()
 # Moving traffic and birds (AmbientLife.gd), then the graphics preset that sizes
 # them (GraphicsQuality.gd; headless and QA runs keep project.godot as it is).
 ambient_life.name="AmbientLife";ambient_life.game=self;add_child(ambient_life)
 graphics.name="GraphicsQuality";add_child(graphics);graphics.setup(self)
 apply_lens()
 # Road grime that builds with distance and washes off in real rain (CarWear.gd).
 car_wear.name="CarWear";add_child(car_wear);car_wear.setup(self)
 # The map helper ships beside the game executable (atlas.exe on Windows).
 var helper_name="atlas.exe" if OS.get_name()=="Windows" else "atlas"
 helper=OS.get_executable_path().get_base_dir().path_join(helper_name)
 if OS.has_feature("editor"):helper=ProjectSettings.globalize_path("res://../tools/"+helper_name)
 # Checks that must stay offline set this before the scene enters the tree: a SceneTree
 # script's _initialize() runs before _ready(), so an assignment there would be overwritten.
 if OS.has_environment("SIMCRUISE_MAP_HELPER"):helper=OS.get_environment("SIMCRUISE_MAP_HELPER")
 cache=ProjectSettings.globalize_path("user://map-cache");DirAccess.make_dir_recursive_absolute(cache)
 for pair in [["gas",KEY_W,KEY_UP],["brake",KEY_S,KEY_DOWN],["left",KEY_A,KEY_LEFT],["right",KEY_D,KEY_RIGHT],["handbrake",KEY_SPACE,KEY_SPACE],["parking_brake",KEY_H,KEY_H],["clutch",KEY_SHIFT,KEY_SHIFT]]:
  InputMap.add_action(pair[0])
  for i in [1,2]:
   var k=InputEventKey.new();k.physical_keycode=pair[i];InputMap.action_add_event(pair[0],k)
 for binding in [["gas",JOY_AXIS_TRIGGER_RIGHT,1.0,.04],["brake",JOY_AXIS_TRIGGER_LEFT,1.0,.04],["pad_left",JOY_AXIS_LEFT_X,-1.0,.12],["pad_right",JOY_AXIS_LEFT_X,1.0,.12]]:
  if not InputMap.has_action(binding[0]):InputMap.add_action(binding[0])
  var axis=InputEventJoypadMotion.new()
  axis.device=-1;axis.axis=binding[1];axis.axis_value=binding[2]
  InputMap.action_set_deadzone(binding[0],binding[3])
  InputMap.action_add_event(binding[0],axis)
 var parking_button=InputEventJoypadButton.new();parking_button.device=-1;parking_button.button_index=JOY_BUTTON_A
 InputMap.action_add_event("parking_brake",parking_button)
 var clutch_button=InputEventJoypadButton.new();clutch_button.device=-1;clutch_button.button_index=JOY_BUTTON_LEFT_SHOULDER
 InputMap.action_add_event("clutch",clutch_button)
 Input.joy_connection_changed.connect(func(_id,_connected):hud.update_drive_hint.call_deferred())
 hud.update_drive_hint()
 var stream=AudioStreamGenerator.new();stream.mix_rate=22050;stream.buffer_length=.15;engine.stream=stream;add_child(engine)
 # Cockpit mix: RoadNoise creates the Engine / Road / Wind → Cabin → Master buses in
 # code (levels documented in RoadNoise.gd) and renders the tyre, wind and rumble
 # layers itself. Sound follows the remembered preference and defaults to on.
 var road_noise=preload("res://RoadNoise.gd").new();road_noise.name="RoadNoise";road_noise.game=self;add_child(road_noise)
 engine.bus="Engine";engine.volume_db=road_noise.ENGINE_TRIM_DB
 if driving_preferences.values.sound:toggle_sound()
 # After switching cars the scene reloads where the player was.
 var resume:String=driving_preferences.values.resume_place
 var resumed=resume.split(",",false,2)
 if resumed.size()==3 and resumed[0].is_valid_float() and resumed[1].is_valid_float():
  driving_preferences.remember("resume_place","")
  current_title=resumed[2];choose_place(float(resumed[0]),float(resumed[1]),resumed[2])
 else:
  begin_start()
 if "--car-audit" in OS.get_cmdline_user_args():add_child(preload("res://checks/CarAppearance.gd").new())
 if "--bethel-facade-ab" in OS.get_cmdline_user_args():add_child(preload("res://checks/BethelFacadeAB.gd").new())
 if "--archive-normal-check" in OS.get_cmdline_user_args():add_child(preload("res://checks/ArchiveNormal.gd").new())
 if "--reference" in OS.get_cmdline_user_args():add_child(preload("res://checks/ReferencePose.gd").new())
 if "--pad-steering" in OS.get_cmdline_user_args():add_child(preload("res://checks/PadSteer.gd").new())
 if "--capitol-drive-proof" in OS.get_cmdline_user_args():add_child(preload("res://checks/CapitolSurveyDrive.gd").new())
 if "--building-check" in OS.get_cmdline_user_args():add_child(preload("res://checks/BuildingCheck.gd").new())
 if "--crossfall-drive" in OS.get_cmdline_user_args():add_child(preload("res://checks/CrossfallDrive.gd").new())
 if "--street-check" in OS.get_cmdline_user_args():add_child(preload("res://checks/StreetCheck.gd").new())
 if "--us-check" in OS.get_cmdline_user_args():add_child(preload("res://checks/RoadTest.gd").new())
 if "--survey-stops" in OS.get_cmdline_user_args():add_child(preload("res://checks/SurveyStops.gd").new())
 if "--seam-check" in OS.get_cmdline_user_args():add_child(preload("res://checks/SeamCheck.gd").new())
 if "--landscape-drive" in OS.get_cmdline_user_args():add_child(preload("res://checks/LandscapeDrive.gd").new())
 if "--field-check" in OS.get_cmdline_user_args():add_child(preload("res://checks/FieldCheck.gd").new())
 if "--profile-trees" in OS.get_cmdline_user_args():add_child(preload("res://checks/RenderCost.gd").new())
 if "--preference-check" in OS.get_cmdline_user_args():add_child(preload("res://checks/PreferenceCheck.gd").new())
 if "--road-test" in OS.get_cmdline_user_args():add_child(preload("res://checks/DriveCheck.gd").new())
 if "--screenshots" in OS.get_cmdline_user_args():add_child(preload("res://checks/Screenshots.gd").new())
 if "--car-showcase" in OS.get_cmdline_user_args():add_child(preload("res://checks/CarShowcase.gd").new())
 if "--quality-bench" in OS.get_cmdline_user_args():add_child(preload("res://checks/QualityBench.gd").new())
 if "--rail-view" in OS.get_cmdline_user_args():add_child(preload("res://checks/RailView.gd").new())
 if "--car-turntable" in OS.get_cmdline_user_args():add_child(preload("res://checks/CarTurntable.gd").new())
# Tile workers create mesh/material resources. Joining one while the engine
# is shutting down can deadlock against the rendering/main thread. Keep the
# frame loop alive until pending work has returned; no new jobs are launched.
func request_game_close(code:int=0):
 if closing:return
 closing=true
 close_code=code
 vehicle.freeze=true
 if hud:
  hud.atlas_map.close_map()
  hud.message("Closing SimCruise…")
func close_when_idle():
 var pending=[job,surface_job,recent_job,weather_job,archive_weather.worker,canopy_job,photo_job,street_detail.worker,building_stream.worker,spectral_ground.worker,road_banks.worker,regional_terrain.worker,regional_terrain.summit.worker,regional_terrain.snow.worker,regional_terrain.ridge.worker,forest_stream.worker,rangeland.worker]
 if hud:pending.append(hud.atlas_map.worker)
 for thread in pending:
  if thread and thread.is_alive():return
 get_tree().quit(close_code)
func _exit_tree():
 archive_weather.shutdown()
 forest_stream.shutdown()
 road_banks.shutdown()
 regional_terrain.shutdown()
 rangeland.shutdown()
 spectral_ground.shutdown()
 building_stream.shutdown()
 street_detail.shutdown()
 if surface_job:surface_job.wait_to_finish()
 if recent_job:recent_job.wait_to_finish()
 if weather_job:weather_job.wait_to_finish()
 if canopy_job:canopy_job.wait_to_finish()
 if photo_job and photo_job.is_started():photo_job.wait_to_finish()
 if job and job.is_started():
  var pending=job.wait_to_finish()
  if pending is Dictionary and pending.has("tile"):pending.tile.free()
func project(lon:float,lat:float)->Vector2:
 return Vector2((lon+180)/360,(1-asinh(tan(deg_to_rad(lat)))/PI)/2)*65536
func scale_at(lat):return WORLD/65536*cos(deg_to_rad(lat))
func tile_id(p:Vector2)->Vector2i:return origin+Vector2i((p/side).floor())
func tile_offset(id:Vector2i)->Vector2:return Vector2(id-origin)*side
func choose_place(lon,lat,title,street="",bearing=null):
 if absf(lat)>82 or absf(lon)>180:return
 start_chain=[];start_launch=false;start_note="" # a chosen place ends any start sequence
 jump_to({"lon":lon,"lat":lat,"title":title,"street":street,"bearing":bearing})
 search_queue="";query_serial+=1;hud.close_search()
# Point the loader at a place without touching a start sequence or the search box.
# loose: a pool point may take the main road under it when the street name differs.
func jump_to(place:Dictionary,loose:=false):
 var lon=float(place.lon);var lat=float(place.lat)
 if absf(lat)>82 or absf(lon)>180:return
 var p=project(lon,lat);jump={"id":Vector2i(p.floor()),"frac":p-p.floor(),"side":scale_at(lat),"title":str(place.title),"ring":0,"street":place.get("street",""),"bearing":place.get("bearing"),"loose":loose}
 velocity=0;say("Loading "+str(place.title)+"…",18)
# Where this drive begins: a seeded random pool road in normal play (StartPool.gd),
# the bundled place in check modes. Returns the first place for the start-up sun.
# RandomNumberGenerator.randomize() seeds from the clock (Godot 4.7 class reference);
# --start-seed N repeats a launch.
func plan_start()->Dictionary:
 var args=OS.get_cmdline_user_args()
 if start_mode=="":
  start_mode="random"
  for flag in BUNDLED_START_FLAGS:
   if flag in args:start_mode="bundled"
  if "--random-start" in args:start_mode="random"
 var at=args.find("--start-seed")
 if at>=0 and at+1<args.size() and args[at+1].is_valid_int():start_rng.seed=args[at+1].to_int()
 else:start_rng.randomize()
 start_pool=START_POOL.load_pool()
 start_chain=START_POOL.sequence(start_pool,start_rng,1+START_POOL.RETRIES) if start_mode=="random" else []
 return start_chain[0] if not start_chain.is_empty() else START_POOL.FALLBACK
func begin_start():
 if start_chain.is_empty():
  start_launch=false;print("START %s %s"%[start_mode,START_POOL.FALLBACK.title]);jump_to(START_POOL.FALLBACK);return
 start_launch=true
 var first=start_chain.pop_front()
 print("START %s seed=%d %s"%[start_mode,start_rng.seed,first.title])
 jump_to(first,true)
# The current pool place did not load: try the next one. Past the last, a launch
# drives the bundled place; Surprise me returns false and the caller opens search.
func start_next(reason:String)->bool:
 jump={}
 if not start_chain.is_empty():
  jump_to(start_chain.pop_front(),true);return true
 if not start_launch:return false
 start_launch=false
 print("START fallback after: ",reason)
 start_note="No random road loaded · starting in bundled Miami Beach (works offline)"
 jump_to(START_POOL.FALLBACK)
 return true
# Hud's "Surprise me": a random pool road other than the current place, with up
# to RETRIES more behind it if it does not load.
func surprise_me():
 if start_pool.is_empty():start_pool=START_POOL.load_pool()
 var picks=START_POOL.sequence(start_pool,start_rng,1+START_POOL.RETRIES,current_title)
 if picks.is_empty():hud.results(HUD.preset_places(),"No start pool is bundled. Search for a place instead.");return
 var first=picks.pop_front()
 choose_place(first.lon,first.lat,first.title,first.get("street",""),first.get("bearing"))
 jump.loose=true;start_chain=picks
func search(q):
 query_serial+=1;search_queue=""
 var nums=q.split(",")
 if nums.size()==2 and nums[0].strip_edges().is_valid_float() and nums[1].strip_edges().is_valid_float():
  var lat=nums[0].to_float();var lon=nums[1].to_float()
  if absf(lat)<=82 and absf(lon)<=180:hud.results([{"lon":lon,"lat":lat,"title":"%.5f, %.5f"%[lat,lon]}],"Choose this location.");return
 if q.strip_edges().to_lower() in ["surprise me","surprise","random"]:
  surprise_me();return
 if q.strip_edges().to_lower() in ["davidson hall","gould academy","gould academy, bethel","bethel, me"]:
  hud.results([HUD.davidson_hall()],"Northbound on Church Street beside Davidson Hall.");return
 if q.strip_edges().to_lower() in ["princeton gravel","princeton road, ne","princeton road, nebraska","princeton road, lancaster county, ne"]:
  hud.results([HUD.princeton_gravel()],"Eastbound on the gravel section of Princeton Road.");return
 if q.to_lower() in ["miami beach","south beach","ocean drive","ocean drive, miami beach","art deco district"]:
  hud.results([START_POOL.FALLBACK],"Bundled neighborhood · works offline");return
 search_queue=q;hud.results([],"Searching…")
func say(t,seconds=3.0):hud.message(t);msg_time=seconds
func work(spec):
 if spec.kind=="search":
  var output=[];var code=OS.execute(helper,["search",cache,spec.q],output,true)
  var res=JSON.parse_string(str(output[0])) if code==0 and output.size() else null
  return {"results":res} if res is Array else {"error":"Place search is unavailable. Try coordinates, Surprise me, or Miami Beach (works offline)."}
 var id:Vector2i=spec.id;var name="res://data/%d_%d.json"%[id.x,id.y];var raw=""
 if FileAccess.file_exists(name):raw=FileAccess.get_file_as_string(name)
 else:
  var args=["tile",cache,str(id.x),str(id.y)]
  # Mapped OSM guard rails come from the public Overpass service; opt-in only,
  # so a default install sends no requests to volunteer-run servers.
  if "--mapped-guard-rails" in OS.get_cmdline_user_args() and not "--without-guard-rails" in OS.get_cmdline_user_args():args.append("--mapped-guard-rails")
  if not road_banks.enabled:args.append("--without-road-banks")
  var out=[];var code=OS.execute(helper,args,out,true)
  if code or out.is_empty():return {"error":"Map download failed. Check the connection and retry."}
  raw=str(out[0])
 var d=JSON.parse_string(raw)
 if not d is Dictionary or not d.has("roads"):return {"error":"The map provider returned unreadable data."}
 d["_source_cache"]=cache
 preload("res://TerrainEdges.gd").apply(d,spec.get("terrain_edges",{}),spec.side)
 var t=TILE.new();t.build(d,spec.side,materials);return {"tile":t}
func launch(spec):
 if spec.kind=="tile":
  spec["terrain_edges"]=preload("res://TerrainEdges.gd").collect(tiles,spec.id)
 task=spec;job=Thread.new();job.start(work.bind(spec))
func finish_job(result):
 if task.kind=="search":
  if task.serial==query_serial and hud.modal.visible:
   var rows=result.get("results",[])
   hud.results(rows,result.get("error","Choose a starting place." if rows.size() else "No matches. Try a city and state."))
  return
 var id:Vector2i=task.id
 if result.has("error"):
  failures[id]=Time.get_ticks_msec()+15000
  if task.kind=="jump" and not jump.is_empty() and task.target==jump:
   if start_next(result.error):return
   jump={};hud.open_search();hud.results(HUD.preset_places(),result.error+" Search again, press Surprise me, or pick a place below; Miami Beach works offline.")
  elif jump.is_empty():say("Map connection interrupted · retrying",4)
  return
 var t=result.tile;calls+=int(t.info.get("requests",0))
 if task.kind=="jump":
  if jump.is_empty() or task.target!=jump:t.free();return
  var requested=jump.frac*jump.side+Vector2(jump.id-id)*jump.side;var nearest=t.nearest(requested,true,jump.street)
  if not nearest.size():nearest=t.nearest(requested,false,jump.street)
  if nearest.size() and jump.street!="" and nearest[2]>125:nearest=[]
  # A pool point is an OSM node on the named way; if this tile spells the name
  # differently, take the main road under the node itself.
  if not nearest.size() and jump.get("loose",false):
   nearest=t.nearest(requested,true)
   if nearest.size() and nearest[2]>30.:nearest=[]
  if not nearest.size():
   t.free();jump.ring+=1
   if jump.ring>8:
    if start_next("no matching road near "+str(jump.title)):return
    jump={};hud.open_search();hud.results(HUD.preset_places(),"Could not verify a nearby matching road. Try another street, coordinates, or Surprise me.")
   return
  for old in tiles.values():remove_child(old);old.queue_free()
  tiles.clear();failures.clear();side=jump.side;origin=id;current_title=jump.title;distance=0;last_probe=Vector2(INF,INF);attach(id,t);move_to_road(nearest,Vector2.ZERO)
  if not recent_user_choice:recent_mode=t.info.get("live_canopy",false)
  jump={};start_chain=[];start_launch=false;ready_drive=true;hud.place.text=current_title;camera_snap=true
  if start_note!="":say(start_note,10.);start_note=""
  else:say("RT to accelerate · Y camera · L3 hide HUD" if not Input.get_connected_joypads().is_empty() else "W or ↑ to accelerate · C camera · F2 hide HUD",6)
 elif jump.is_empty() and not tiles.has(id):
  attach(id,t)
 else:t.free()
func attach(id,t):
 var ofs=tile_offset(id);t.position=Vector3(ofs.x,0,ofs.y);add_child(t);tiles[id]=t;tile_count+=1
 var entry=recent_cache.get(recent_key(id),{})
 if recent_mode and entry.has("texture"):t.apply_recent(entry.texture,entry.get("soil"))
 street_detail.apply_tile(id,t,side)
func stream():
 if job:
  if job.is_alive():return
  var r=job.wait_to_finish();job=null;finish_job(r)
 if not jump.is_empty():
  var offsets=[Vector2i.ZERO,Vector2i.RIGHT,Vector2i.LEFT,Vector2i.DOWN,Vector2i.UP,Vector2i(1,1),Vector2i(-1,-1),Vector2i(1,-1),Vector2i(-1,1)]
  launch({"kind":"jump","id":jump.id+offsets[jump.ring],"side":jump.side,"target":jump.duplicate()});return
 if search_queue!="":
  var q=search_queue;search_queue="";launch({"kind":"search","q":q,"serial":query_serial});return
 if not ready_drive:return
 if waiting_tile!=null and not tiles.has(waiting_tile):
  if failures.get(waiting_tile,0)<=Time.get_ticks_msec():
   launch({"kind":"tile","id":waiting_tile,"side":side})
  return
 var center=tile_id(pos);var best=INF;var next=null
 for dx in range(-1,2):
  for dy in range(-1,2):
   var id=center+Vector2i(dx,dy);var o=tile_offset(id)
   if tiles.has(id) or failures.get(id,0)>Time.get_ticks_msec():continue
   var gap=pos.distance_to(pos.clamp(o,o+Vector2.ONE*side))
   var predicted=pos+Vector2(vehicle.linear_velocity.x,vehicle.linear_velocity.z)*5.
   var score=gap*.4+predicted.distance_to(predicted.clamp(o,o+Vector2.ONE*side))*.6
   if gap<minf(400,side*.94) and score<best:next=id;best=score
 for id in tiles.keys():
  if absi(id.x-center.x)>1 or absi(id.y-center.y)>1:
   var old=tiles[id];tiles.erase(id);remove_child(old);old.queue_free()
 if next!=null:launch({"kind":"tile","id":next,"side":side})
func ground(p:Vector2):
 var id=tile_id(p)
 if tiles.has(id):return tiles[id].height_at(p-tile_offset(id))
 return vehicle.position.y-.025
# The surface a car stands on: a bridge deck where there is one (unless the car
# is clearly below it, driving underneath), else the ground.
func surface(p:Vector2,near_y:=INF)->float:
 var under=ground(p);var id=tile_id(p)
 if not tiles.has(id):return under
 var deck=tiles[id].deck_at(p-tile_offset(id))
 if is_nan(deck) or deck<under or (near_y!=INF and near_y<deck-2.5):return under
 return deck
# Nearest road to the car. main_only skips service roads and tracks; reach is
# how far past the car's own tile the search looks (INF: every loaded tile).
func nearest_world(main_only:=true,reach:=20.):
 var best=[];var distance_to_road=INF;var o=Vector2.ZERO;var id=tile_id(pos)
 for near_id in tiles:
  var offset=tile_offset(near_id)
  if near_id!=id and pos.distance_to(pos.clamp(offset,offset+Vector2.ONE*side))>reach:continue
  var n=tiles[near_id].nearest(pos-offset,main_only)
  if n.size() and n[2]<distance_to_road:best=n;distance_to_road=n[2];o=offset
 return [best,o]
func move_to_road(n,o):
 waiting_tile=null
 var d:Vector2=(n[1][1]-n[1][0]).normalized();if n[1][4]==-1:d=-d
 if not jump.is_empty():
  if jump.get("bearing")!=null and n[1][4]==0:
   var a=deg_to_rad(float(jump.bearing))
   if d.dot(Vector2(sin(a),-cos(a)))<0:d=-d
 pos=n[0]+o+Vector2(-d.y,d.x)*minf(1.8,n[1][3]*.18);yaw=atan2(-d.x,-d.y);velocity=0;steering=0;road_name=n[1][2] if n[1][2]!="" else "Local road"
 vehicle.restart(Vector3(pos.x,surface(pos)+.13,pos.y),yaw)
func recover():
 if not ready_drive:return
 # Main roads near the car first; then any road in any loaded tile, so one press
 # works from a parking lot, a field, or a tile edge the stream has not filled yet.
 var r=nearest_world()
 if r[0].is_empty():r=nearest_world(false,INF)
 if r[0].is_empty():say("No road loaded nearby yet",3.);return
 move_to_road(r[0],r[1]);camera_snap=true;say("Back on "+road_name)
func switch_view():
 view=(view+1)%3;camera_snap=true
func rear_lamp_glow()->float:
 return BRAKE_GLOW if lamp_on else (TAIL_GLOW if head_lamps_on else 0.)
func set_head_lamps(on:bool):
 head_lamps_on=on
 for part in head_lamp_parts:
  if part is Light3D:part.visible=on
  else:part[0].emission_energy_multiplier=part[1] if on else 0.
 for mat in lamp_materials:mat.emission_energy_multiplier=rear_lamp_glow()
func toggle_sound():
 sound_enabled=not sound_enabled;hud.audio_on=sound_enabled
 driving_preferences.remember("sound",sound_enabled)
 if sound_enabled:
  engine_voice=preload("res://InlineFourSound.gd").new()
  # Headless checks keep the mixer idle: the dummy audio driver never releases a
  # playing generator before quit, which the exit leak checker then reports.
  # The electric car has no combustion sound (a motor voice is future work).
  if DisplayServer.get_name()!="headless" and not vehicle.electric():engine.play();engine_stream=engine.get_stream_playback()
 else:
  engine.stop();engine_stream=null;engine_voice=null
func _input(e):
 if hud==null or hud.atlas_map==null or hud.modal.visible:return
 if e is InputEventKey and e.keycode==KEY_TAB and e.pressed and not e.echo:
  if hud.atlas_map.visible:hud.atlas_map.close_map()
  else:hud.atlas_map.open_map()
  get_viewport().set_input_as_handled()
# Driver lens (ViewLens.gd): natural by default; scripted captures keep 67°.
func apply_lens():
 var mode:String=driving_preferences.values.lens if graphics.managed else "wide"
 cockpit.driver_fov=preload("res://ViewLens.gd").fov(mode,driving_preferences.values.view_cm)
func cycle_lens():
 var order=preload("res://ViewLens.gd").ORDER
 var next:String=order[(order.find(driving_preferences.values.lens)+1)%order.size()]
 driving_preferences.remember("lens",next);apply_lens()
 say(preload("res://ViewLens.gd").label(next,driving_preferences.values.view_cm),3.)
func toggle_street_detail():
 street_detail.enabled=not street_detail.enabled
 street_detail.reapply(self)
 save_driving_preferences()
 if hud.atlas_map.visible:hud.atlas_map.refresh()

func toggle_spectral_ground():
 spectral_ground.set_enabled(self,not spectral_ground.enabled)
 driving_preferences.remember("spectral_ground",spectral_ground.enabled)
 hud.atlas_map.header()

func save_driving_preferences():
 driving_preferences.remember("manual",vehicle.manual)
 driving_preferences.remember("street_photos",street_detail.enabled)
func toggle_manual():
 vehicle.set_manual(not vehicle.manual)
 save_driving_preferences()
 hud.atlas_map.refresh()
 hud.update_drive_hint()
 say("Manual · clutch + shift to select first gear" if vehicle.manual else "Automatic shifting",4.)
func change_ratio(step):
 var message=vehicle.change_gear(step)
 if message!="":say(message,2.)
func _unhandled_input(e):
 if e is InputEventJoypadButton and e.pressed and not hud.modal.visible:
  if hud.atlas_map.visible:
   match e.button_index:
    JOY_BUTTON_DPAD_DOWN:hud.atlas_map.zoom(1)
    JOY_BUTTON_DPAD_UP:hud.atlas_map.zoom(-1)
    JOY_BUTTON_A:hud.atlas_map.recenter()
    JOY_BUTTON_B,JOY_BUTTON_START:hud.atlas_map.close_map()
   get_viewport().set_input_as_handled();return
  if e.button_index==JOY_BUTTON_START:hud.atlas_map.open_map()
  elif e.button_index==JOY_BUTTON_B:toggle_manual()
  elif e.button_index==JOY_BUTTON_RIGHT_SHOULDER:change_ratio(1)
  elif e.button_index==JOY_BUTTON_X:change_ratio(-1)
  elif e.button_index==JOY_BUTTON_RIGHT_STICK:vehicle.start_engine()
  elif e.button_index==JOY_BUTTON_LEFT_STICK:hud.toggle_driving_panels()
  elif e.button_index==JOY_BUTTON_Y:switch_view()
  elif e.button_index==JOY_BUTTON_BACK:recover()
  elif e.button_index==JOY_BUTTON_DPAD_DOWN:hud.map.zoom(1)
  elif e.button_index==JOY_BUTTON_DPAD_UP:hud.map.zoom(-1)

func _unhandled_key_input(e):
 if not e is InputEventKey or not e.pressed or e.echo:return
 if hud.atlas_map.visible:
  if e.keycode in [KEY_ESCAPE,KEY_TAB]:hud.atlas_map.close_map()
  elif e.keycode in [KEY_EQUAL,KEY_PLUS]:hud.atlas_map.zoom(-1)
  elif e.keycode==KEY_MINUS:hud.atlas_map.zoom(1)
  get_viewport().set_input_as_handled();return
 if e.keycode==KEY_TAB and not hud.modal.visible:hud.atlas_map.open_map();get_viewport().set_input_as_handled();return
 if e.keycode==KEY_ESCAPE:
  hud.close_search();return
 if hud.modal.visible:return
 match e.physical_keycode:
  KEY_SLASH:hud.open_search();get_viewport().set_input_as_handled()
  KEY_T:toggle_manual()
  KEY_E:change_ratio(1)
  KEY_Q:change_ratio(-1)
  KEY_I:vehicle.start_engine()
  KEY_F2:hud.toggle_driving_panels()
  KEY_C:switch_view()
  KEY_R:recover()
  KEY_M:toggle_sound()
  KEY_L:set_head_lamps(not head_lamps_on);say("Lights on" if head_lamps_on else "Lights off",2.)
  KEY_F3:fps_label.visible=not fps_label.visible
  KEY_F4:graphics.cycle()
  KEY_F6:cycle_lens()
func _physics_process(dt):
 if closing:return
 if not ready_drive:return
 if not jump.is_empty() or hud.modal.visible or hud.atlas_map.visible:
  vehicle.freeze=true;return
 var delta=Vector2i((pos/side).floor())
 if maxi(absi(delta.x),absi(delta.y))>=6:rebase(delta)
 var actual=Vector2(vehicle.position.x,vehicle.position.z);var id=tile_id(actual)
 var motion:Vector3=vehicle.linear_velocity if waiting_tile==null else waiting_linear
 var ahead_id=tile_id(actual+Vector2(motion.x,motion.z)*.30)
 var required=id if not tiles.has(id) else ahead_id
 if not tiles.has(required):
  if waiting_tile==null:
   waiting_linear=vehicle.linear_velocity
   waiting_angular=vehicle.angular_velocity
  waiting_tile=required
  vehicle.freeze=true
  if msg_time<=0:say("Loading the road ahead…",1)
  return
 vehicle.freeze=false
 if waiting_tile!=null:
  vehicle.linear_velocity=waiting_linear
  vehicle.angular_velocity=waiting_angular
  vehicle.sleeping=false
  waiting_tile=null
 var gas=Input.get_action_strength("gas");var brake=Input.get_action_strength("brake");var steer=Input.get_action_strength("left")-Input.get_action_strength("right")
 var pad=Input.get_action_strength("pad_left")-Input.get_action_strength("pad_right")
 var using_thumbstick=absf(pad)>absf(steer)
 if using_thumbstick:steer=vehicle.thumbstick_command(pad,-vehicle.linear_velocity.dot(vehicle.global_basis.z))
 for i in 4:
  var contact=vehicle.global_transform*vehicle.MOUNTS[i];var at=Vector2(contact.x,contact.z);var key=tile_id(at)
  vehicle.surface_grip[i]=tiles[key].wheel_grip(at-tile_offset(key)) if tiles.has(key) else .72
  # Road roughness (RoadRoughness.gd): IRI from the road under this tyre, profile
  # in absolute map metres so a rebase never moves a bump.
  var bump=0.
  if tiles.has(key) and roughness.enabled:
   var iri=roughness.iri(tiles[key],at-tile_offset(key),vehicle.surface_grip[i])
   bump=roughness.height(at+Vector2(origin)*side,iri)
  vehicle.road_profile_rate[i]=clampf((bump-vehicle.road_profile[i])/dt,-.6,.6)
  vehicle.road_profile[i]=bump
 velocity=vehicle.drive(dt,gas,brake,steer,Input.is_action_pressed("handbrake"),using_thumbstick);steering=vehicle.steering_input
 distance+=minf(3,pos.distance_to(actual));pos=actual;yaw=vehicle.rotation.y
 var forward=-vehicle.global_transform.basis.z;yaw=atan2(-forward.x,-forward.z)
 if vehicle.position.y<ground(pos)-3:recover();return
 var shade=$GroundShade;shade.visible=vehicle.wheel_contact>0
 shade.position=Vector3(pos.x,surface(pos,vehicle.position.y)+.046,pos.y);shade.rotation=Vector3(vehicle.rotation.x,yaw,vehicle.rotation.z)
 if lamp_on!=vehicle.service_brake:
  lamp_on=vehicle.service_brake
  for mat in lamp_materials:mat.emission_energy_multiplier=rear_lamp_glow()
 for i in wheels.size():
  var w=wheels[i];var phys=vehicle.connected_wheels[i];w[0].position=phys.position
  w[0].rotation.y=vehicle.wheel_heading[i];w[0].rotation.z=vehicle.wheel_camber[i];wheel_angle[i]=fmod(wheel_angle[i]-vehicle.omega[i]*dt,TAU);w[1].rotation.x=wheel_angle[i]
 if steering_wheel.size():
  steering_wheel[0].transform.basis=steering_wheel[1]*Basis(Vector3(0,0,1),(vehicle.wheel_heading[0]+vehicle.wheel_heading[1])*.5*STEERING_RATIO)
 # Roof down or stranded (Powertrain keeps the timers): prompt once when the
 # timer starts, then put the car back on a road at each 8 s mark until it is.
 # Deferred so the teleport lands after this tick's forces are consumed.
 if vehicle.stranded_seconds>0.:
  if vehicle.stranded_seconds<=dt:say("Upside down · press R to recover" if vehicle.roof_down else "Stuck · press R to recover",8.)
  elif floori(vehicle.stranded_seconds/8.)!=floori((vehicle.stranded_seconds-dt)/8.):recover.call_deferred()
func _process(dt):
 if closing:
  close_when_idle();return
 if hud==null:return
 stream()
 stream_photo()
 stream_canopy()
 update_weather()
 stream_recent()
 stream_surfaces()
 street_detail.tick(self)
 building_stream.tick(self)
 spectral_ground.tick(self)
 forest_stream.tick(self)
 road_banks.tick(self)
 regional_terrain.tick(self)
 rangeland.tick(self)
 if msg_time>0:
  msg_time-=dt
  if msg_time<=0:hud.message("")
 if not ready_drive:return
 var dir=Vector2(-sin(yaw),-cos(yaw));var right=Vector2(-dir.y,dir.x)
 var body=vehicle.global_transform;var goal:Vector3;var target:Vector3
 cockpit.set_view(view==1 and not generic_car,camera,vehicle,velocity,dt,distance)
 # Read the body the renderer shows this frame: with physics/common/physics_interpolation
 # on, Node3D.get_global_transform_interpolated() is the interpolated transform, and
 # with it off it returns global_transform unchanged (Godot 4.7).
 body=vehicle.get_global_transform_interpolated()
 var head_up=Vector3.UP
 if view==1 and generic_car:
  # Generated cars: the eye point from the car file, looking down the road.
  var eye:Vector3=vehicle.spec.driver_eye
  goal=body*eye;target=body*(eye+Vector3(0,-.32,-12.));head_up=body.basis.y
  camera.fov=cockpit.driver_fov
 elif view==1:
  # Driver view: the lagged head model in Cockpit.gd, reset on teleports and view changes.
  if camera_snap:cockpit.reset_head()
  var head=cockpit.head_view(body,vehicle,velocity,dt)
  goal=head[0];target=head[1];head_up=head[2]
 elif view==2:
  goal=body*Vector3(4.9,3.45,7.2);target=body*Vector3(0,.56,-.65)
 else:
  goal=body*Vector3(1.92*maxf(.18,1-absf(velocity)/14),1.94,5.8);target=body*Vector3(0,.70,-3.9)
 if view!=1:
  goal.y=maxf(goal.y,surface(Vector2(goal.x,goal.z),goal.y)+.55)
  # Retract immediately in tight streets rather than passing the camera through a shop wall.
  var pivot=body*Vector3(0,1.12,0)
  var ray=PhysicsRayQueryParameters3D.create(pivot,goal,1,[vehicle.get_rid()]);ray.hit_back_faces=true
  var hit=get_world_3d().direct_space_state.intersect_ray(ray)
  if not hit.is_empty():
   goal=hit.position+(pivot-goal).normalized()*.22
   camera_snap=true
 camera.position=goal if camera_snap or view==1 else camera.position.lerp(goal,1-exp(-10.0*dt))
 camera.look_at(target,head_up if view==1 else Vector3.UP);camera_snap=false
 grove.refresh(self);roadside.refresh(self)
 park_clock+=dt
 if park_clock>.75:
  update_parked();park_clock=0
 if not job and (not is_finite(last_probe.x) or last_probe.distance_to(pos)>probe_step_m):
  probe.position=vehicle.position+Vector3.UP*1.1;last_probe=pos
 timer+=dt
 if timer>.16:
  var near=nearest_world()
  if near[0].size():
   var rr=near[0][1];road_name=rr[2] if rr[2]!="" else "Local road"
  hud.refresh(roundi(absf(velocity)*2.23694),vehicle.gear_text()+" · "+str(roundi(vehicle.rpm))+" rpm",distance/1609.344,road_name,view)
  hud.update_drive_hint()
  fps_label.text="%d FPS    %d active tiles    %d map requests    %s    %s"%[Engine.get_frames_per_second(),tiles.size(),calls,graphics.label(),car_wear.label()];timer=0
 if sound_enabled and engine_stream:
  var available=engine_stream.get_frames_available()
  if available>0:
   var buffer=engine_voice.render(available,vehicle.rpm,vehicle.sound_combustion,vehicle.sound_shaft_load,vehicle.stalled,view==1,vehicle.sound_throttle)
   engine_stream.push_buffer(buffer)

func update_parked():
 var nearby=[]
 for id in tiles:
  var tile=tiles[id];var o=tile_offset(id)
  for car in tile.parked:
   var p=car[0]+o;var dd=p.distance_squared_to(pos)
   if dd<115*115:nearby.append([dd,p,car[1],car[2]])
 nearby.sort_custom(func(a,b):return a[0]<b[0])
 # Assign fixed variants by coordinate so passing cars do not change model with distance.
 var used={}
 for candidate in nearby:
  var want_pickup=candidate[3]==0
  for i in traffic.size():
   if used.has(i) or (i%5==0)!=want_pickup:continue
   var node=traffic[i];var p=candidate[1];node.position=Vector3(p.x,ground(p)+.015,p.y);node.rotation.y=candidate[2];node.show();node.collision_layer=1;used[i]=true;break
  if used.size()==traffic.size():break
 for i in traffic.size():
  if not used.has(i):traffic[i].hide();traffic[i].collision_layer=0

# Keep native physics coordinates small even on a long road trip.
func rebase(delta:Vector2i):
 var off=Vector2(delta)*side;var v=Vector3(off.x,0,off.y)
 origin+=delta;pos-=off;vehicle.position-=v;camera.position-=v;probe.position-=v;last_probe-=off;ambient_life.rebase(v)
 vehicle.reset_physics_interpolation()
 for tile in tiles.values():tile.position-=v
 for car in traffic:car.position-=v
 grove.last_revision=-1;grove.refresh(self)

func fetch_photo(key:String,id:Vector2i):
 var lines=[]
 var code=OS.execute(helper,["aerial",cache,str(floori(id.x/4.)),str(floori(id.y/4.))],lines,true)
 var data=JSON.parse_string(str(lines[0])) if not code and lines.size() else null
 var result={"key":key,"requests":0}
 if data is Dictionary:
  result.requests=int(data.get("requests",0))
  if data.get("aerial","")==key:
   var image=TILE.read_aerial(cache,key)
   if image and not image.is_empty():image.generate_mipmaps();result["image"]=image
 return result

func stream_photo():
 if photo_job:
  if photo_job.is_alive():return
  var loaded=photo_job.wait_to_finish();photo_job=null;calls+=loaded.requests
  if loaded.has("image"):
   var texture=ImageTexture.create_from_image(loaded.image)
   for t in tiles.values():
    if t.info.get("aerial_key","")==loaded.key:t.apply_photo(texture,loaded.image)
 if not jump.is_empty() or hud.modal.visible or hud.atlas_map.visible:return
 var now=Time.get_ticks_msec()
 for key in photo_retry.keys():
  if photo_retry[key]<=now:photo_retry.erase(key)
 for id in tiles:
  var data=tiles[id].info;var key=data.get("aerial_key","")
  if key=="" or data.get("aerial","")!="" or photo_retry.has(key):continue
  photo_retry[key]=now+60000
  photo_job=Thread.new();photo_job.start(fetch_photo.bind(key,id));return

func fetch_canopy(id,with_signals:bool=false):
 var lines=[]
 var args=["canopy",cache,str(id.x),str(id.y)]
 if with_signals:args.append("--signal-controls")
 var code=OS.execute(helper,args,lines,true)
 var result=JSON.parse_string(str(lines[0])) if code==0 and lines.size() else null
 return {"id":id,"rows":result.trees,"requests":result.requests,"controls":result.get("signalControls",{})} if result is Dictionary and result.get("trees") is Array else {"id":id}
func stream_canopy():
 if canopy_job:
  if canopy_job.is_alive():return
  var result=canopy_job.wait_to_finish()
  canopy_job=null
  if result.has("rows") and tiles.has(result.id):
   var tile=tiles[result.id]
   tile.replace_crowns(result.rows)
   var controls=result.get("controls",{})
   if controls is Dictionary and controls.get("records") is Array:
    tile.info["signal_controls"]=preload("res://SignalControls.gd").install(tile,controls.records)
    tile.info.signal_controls["status"]=controls.get("status","unavailable")
    tile.info.signal_controls["release"]=controls.get("release")
   tile.info["canopy_ready"]=true
   calls+=int(result.requests)
   tile_count+=1
 if not ready_drive or not jump.is_empty():return
 var now=Time.get_ticks_msec()
 var best=null
 var best_distance=INF
 for id in tiles:
  var data=tiles[id].info
  if not data.get("live_canopy",false) or data.get("canopy_ready",false) or canopy_retry.get(id,0)>now:continue
  var distance_sq=(tile_offset(id)+Vector2.ONE*side*.5).distance_squared_to(pos)
  if distance_sq<best_distance:best=id;best_distance=distance_sq
 for id in canopy_retry.keys():
  if canopy_retry[id]<=now:canopy_retry.erase(id)
 if best!=null:
  canopy_retry[best]=now+120000
  canopy_job=Thread.new()
  canopy_job.start(fetch_canopy.bind(best,preload("res://SignalControls.gd").eligible(tiles[best])))

# Switch to the next car in res://cars and reopen the drive where it is now.
func cycle_car():
 var ids=preload("res://CarSpec.gd").available()
 if ids.is_empty():return
 var next=ids[(ids.find(vehicle.spec.id)+1)%ids.size()]
 driving_preferences.remember("car",next)
 if ready_drive:
  var here=player_coordinates()
  driving_preferences.remember("resume_place","%.6f,%.6f,%s"%[here.y,here.x,current_title.replace(",",";")])
 get_tree().reload_current_scene()
func player_coordinates():
 var xx=float(origin.x)+pos.x/side
 var yy=float(origin.y)+pos.y/side
 return Vector2(rad_to_deg(atan(sinh(PI*(1.-2.*yy/65536.)))),xx*360./65536.-180.)
func toggle_weather():
 live_weather=not live_weather
 next_weather_check=0
 observation.clear()
 scene_lighting=""
 update_weather()
 hud.atlas_map.refresh()
func read_observation(at:Vector2):
 var lines=[]
 var rc=OS.execute(helper,["conditions",cache,str(at.x),str(at.y)],lines,true)
 var result=JSON.parse_string(str(lines[0])) if rc==0 and lines.size() else null
 return {"at":at,"data":result if result is Dictionary else {}}
func weather_label():
 if not live_weather:
  var label=daylight.weather.reference_label
  return "Photo daylight · "+label if not label.is_empty() else "Reference daylight"
 if observation.has("station"):
  return "%s · %s UTC · %.0f km · %s"%[observation.station,str(observation.at).substr(11,5),observation.km,observation.description]
 return "Looking for a weather station…" if weather_job else "Weather observation unavailable"
func update_weather():
 if not ready_drive:return
 var t=tiles.get(tile_id(pos))
 if not t:return
 var loc=player_coordinates()
 var now=Time.get_ticks_msec()
 if live_weather and (not weather_location.is_finite() or weather_location.distance_to(loc)>.30):
  observation.clear();next_weather_check=0
 if weather_job and not weather_job.is_alive():
  var result=weather_job.wait_to_finish()
  weather_job=null
  if live_weather and result.at.distance_to(loc)<.30:observation=result.data
  if hud.atlas_map.visible:hud.atlas_map.refresh()
 if live_weather and not weather_job and now>=next_weather_check:
  next_weather_check=now+900000
  weather_location=loc;weather_job=Thread.new();weather_job.start(read_observation.bind(loc))
 var region := str(t.info.get("region",""))
 var nearby=street_detail.nearby_reference(self) if not live_weather and t.info.get("live_canopy",false) else {}
 var epoch=int(nearby.get("frame",{}).get("captureUnix",0))
 var photograph:Dictionary=nearby.get("frame",{})
 var source_position:=Vector2(float(photograph.get("lat",loc.x)),float(photograph.get("lon",loc.y)))
 apply_daylight(loc,region,epoch,float(nearby.get("fade",0.)),source_position)

func apply_daylight(loc:Vector2,region:String,epoch:int,weight:float,source_position:Vector2=Vector2.INF):
 var weather=daylight.weather
 var winter=region==daylight.LEAF_OFF_REGION
 weather.select(loc,winter,live_weather,observation,epoch,weight)
 daylight.update(loc,region,sun,day_sky.sky_material,weather.solar_epoch,weather.solar_weight,daylight.capture_weight(loc,not live_weather))
 var historical:Dictionary={}
 if not live_weather and weather.sky_kind==weather.NONE and daylight.applied_epoch==epoch and epoch>0 and weight>.03:
  var anchor:=source_position if source_position.is_finite() else loc
  historical=archive_weather.lookup(helper,cache,anchor,epoch,ground(pos))
  calls+=archive_weather.completed_requests
  archive_weather.completed_requests=0
  weather.apply_station(historical,weight)
 daylight.update_appearance(day_sky.sky_material)
 daylight.update_capture_blend(loc,not live_weather)
 var cloud=weather.cloud
 var visibility=float(observation.get("visibility_m",16093))
 if historical.get("status")=="available" and historical.get("visibility_m")!=null and not historical.get("visibilityAtLeast10SM",false):
  visibility=lerpf(visibility,float(historical.visibility_m),weather.sky_weight)
 var local_broken=weather.sky_kind==weather.BROKEN
 # The exposure follows the sun's height, so the key tracks it in steps of 0.01 in sin(elevation).
 var key=str(winter)+str(cloud)+str(visibility)+str(local_broken)+str(snappedf(sun.basis.z.y,.01))
 if key==scene_lighting:return
 scene_lighting=key
 var overcast_sky=cloud>=.62 and not local_broken
 world_environment.sky=cloud_sky if overcast_sky else day_sky
 daylight.apply_conditions(world_environment,sun,cloud,visibility,overcast_sky)

func recent_key(id:Vector2i):
 return Vector2i(floori(id.x/4.),floori(id.y/4.))
func recent_label():
 var entry=recent_cache.get(recent_key(tile_id(pos)),{})
 if recent_mode and entry.has("date"):return "Sentinel-2 "+entry.date
 if recent_mode:return "Seeking recent clear imagery"
 return "Survey imagery"
func toggle_recent():
 recent_user_choice=true
 recent_mode=not recent_mode
 for id in tiles:
  var entry=recent_cache.get(recent_key(id),{}) if recent_mode else {}
  tiles[id].apply_recent(entry.get("texture"),entry.get("soil"))
 hud.atlas_map.refresh()
func fetch_recent(key):
 var rows=[]
 var code=OS.execute(helper,["recent",cache,str(key.x),str(key.y)],rows,true)
 var entry=JSON.parse_string(str(rows[0])) if code==0 and not rows.is_empty() else null
 var result={"key":key}
 if entry is Dictionary and entry.has("png"):
  var image=Image.new()
  if image.load_png_from_buffer(Marshalls.base64_to_raw(entry.png))==OK:
   image.generate_mipmaps()
   result["image"]=image
   result["date"]=entry.date
   if entry.get("bare") is String:
    var bare:=Image.new()
    if bare.load_png_from_buffer(Marshalls.base64_to_raw(entry.bare))==OK and bare.get_size()==Vector2i(128,128):
     result["bare"]=bare
 return result
func stream_recent():
 if recent_job and not recent_job.is_alive():
  var result=recent_job.wait_to_finish()
  recent_job=null
  if result.has("image"):
   var texture=ImageTexture.create_from_image(result.image)
   if recent_cache.size()>=8:recent_cache.erase(recent_cache.keys()[0])
   var soil=ImageTexture.create_from_image(result.bare) if result.has("bare") else null
   recent_cache[result.key]={"texture":texture,"soil":soil,"date":result.date,"loaded":Time.get_ticks_msec()}
   if recent_mode:
    for id in tiles:
     if recent_key(id)==result.key:tiles[id].apply_recent(texture,soil)
  if hud.atlas_map.visible:hud.atlas_map.refresh()
 if not recent_mode or recent_job or not ready_drive or not jump.is_empty():return
 var now=Time.get_ticks_msec()
 for key in recent_retry.keys():
  if recent_retry[key]<=now:recent_retry.erase(key)
 var options=[recent_key(tile_id(pos))]
 for id in tiles:
  var k=recent_key(id)
  if not options.has(k):options.append(k)
 for key in options:
  if recent_cache.has(key) and now-recent_cache[key].loaded<86400000:continue
  if recent_retry.has(key):continue
  recent_retry[key]=now+900000
  recent_job=Thread.new();recent_job.start(fetch_recent.bind(key));return

func stream_surfaces():
 if surface_job:
  if surface_job.is_alive():return
  var result=surface_job.wait_to_finish()
  surface_job=null
  if tiles.has(surface_id) and tiles[surface_id].road_prior:
   if tiles[surface_id].accept_edges(result):tile_count+=1
 if not ready_drive or not jump.is_empty():return
 for id in tiles:
  var tile=tiles[id]
  if tile.road_prior and tile.aerial_pixels:
   surface_id=id;surface_job=Thread.new()
   surface_job.start(preload("res://RoadEdges.gd").classify.bind(tile.aerial_pixels,tile.road_prior))
   return
