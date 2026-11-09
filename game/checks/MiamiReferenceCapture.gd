extends Node
# Optional continuation of --building-check miami after verifying surveyed mesh vertices.
# --miami-reference <existing directory> saves two driver-eye views of KartaView 11591982/217.
const LAT = 25.748401
const LON = -80.208896
const TRACK = 52.21
const OPTICAL = 66.0
const HORIZONTAL_FOV = 81.7
const PITCH_DOWN = 2.32
var game
var output_dir = ""
var started = 0
var phase = 0
var target: Vector2
var settled_frames = 0
var report = {}

func _ready():
 game = get_parent()
 started = Time.get_ticks_msec()
 var args = OS.get_cmdline_user_args()
 var i = args.find("--miami-reference")
 if i < 0 or i + 1 >= args.size() or not DirAccess.dir_exists_absolute(args[i+1]):
  set_process(false)
  game.request_game_close(3)
  return
 output_dir = args[i+1]
 var x = (LON+180.)/360.*65536.-game.origin.x
 var y = (1.-asinh(tan(deg_to_rad(LAT)))/PI)*32768.-game.origin.y
 target = Vector2(x,y)*game.side
 var yaw = -deg_to_rad(TRACK)
 var offset = Basis(Vector3.UP,yaw)*game.cockpit.EYE
 var center = target-Vector2(offset.x,offset.z)
 game.view = 1
 game.hud.visible = false
 game.fps_label.visible = false
 game.vehicle.restart(Vector3(center.x,game.ground(center)+.12,center.y),yaw)
 game.camera_snap = true

func _process(_dt):
 var elapsed = Time.get_ticks_msec()-started
 if elapsed > 35000:
  set_process(false)
  game.request_game_close(3)
  return
 var tile = game.tiles.get(Vector2i(18166,27914))
 if not tile or game.closing:
  return
 if phase==0:
  settled_frames = settled_frames+1 if game.vehicle.wheel_contact==4 else 0
  var ready = tile.info.get("canopy_ready",false) and tile.terrain_material.get_shader_parameter("imagery") and game.tiles.size()>=8
  if elapsed<1800 or (not ready and elapsed<18000) or settled_frames<4:
   return
  report = {"frame":"11591982/217","lat":LAT,"lon":LON,"travel_bearing_deg":TRACK,"tile":str(game.tile_id(game.pos)),"tiles":game.tiles.size(),"inventory_heights":tile.info.get("enriched_heights",{}).size(),"inventory_uses":tile.info.get("enriched_uses",{}).size(),"crown_stream_ready":tile.info.get("canopy_ready",false),"tile_crowns":tile.grove.size(),"orthophoto_bound":tile.terrain_material.get_shader_parameter("imagery"),"recent_bound":tile.terrain_material.get_shader_parameter("use_recent"),"wheel_contacts":game.vehicle.wheel_contact,"renderer":RenderingServer.get_video_adapter_name(),"driver":RenderingServer.get_current_rendering_driver_name()}
  var sun: Vector3 = game.sun.global_basis.z
  report["solar_azimuth_deg"] = fposmod(rad_to_deg(atan2(sun.x,-sun.z)),360.)
  report["solar_elevation_deg"] = rad_to_deg(asin(sun.y))
  phase=1
  await save_view("forward")
  phase=2
 elif phase==2:
  var az=deg_to_rad(OPTICAL)
  var down=deg_to_rad(PITCH_DOWN)
  var direction=Vector3(sin(az)*cos(down),-sin(down),-cos(az)*cos(down))
  game.camera.look_at(game.camera.global_position+direction,Vector3.UP)
  game.camera.keep_aspect=Camera3D.KEEP_WIDTH
  game.camera.fov=HORIZONTAL_FOV
  phase=3
  await save_view("reference")
  var file=FileAccess.open(output_dir.path_join("capture.json"),FileAccess.WRITE)
  if file: file.store_string(JSON.stringify(report,"  "))
  print("MIAMI METAL REFERENCE ",JSON.stringify(report))
  set_process(false)
  game.request_game_close(0 if file and report.get("reference",{}).get("saved",false) and report.get("forward",{}).get("saved",false) else 4)

func save_view(name: String):
 var cam: Camera3D=game.camera
 var p: Vector3=cam.global_position
 var f: Vector3=-cam.global_basis.z
 var planar=Vector2(p.x,p.z)
 var state={"eye_plan_error_m":planar.distance_to(target),"eye_m_above_terrain":p.y-game.ground(planar),"heading_deg":fposmod(rad_to_deg(atan2(f.x,-f.z)),360.),"pitch_down_deg":rad_to_deg(asin(-f.y)),"fov_deg":cam.fov,"fov_axis":"horizontal" if cam.keep_aspect==Camera3D.KEEP_WIDTH else "vertical"}
 await RenderingServer.frame_post_draw
 var frame=get_viewport().get_texture().get_image()
 state["saved"]=frame!=null and frame.save_jpg(output_dir.path_join(name+".jpg"),.96)==OK
 if frame:state["pixel_size"]=[frame.get_width(),frame.get_height()]
 report[name]=state
