extends Node

# --bethel-facade-ab <existing output directory>
const TILE_ID := Vector2i(19880,23726)
const LAT := 44.40799352948302
const LON := -70.79325251782468
const HEADING := 23.87203375
var game
var folder := ""
var menu_used := false
var begin := 0
var settled := 0
var photo_camera := Camera3D.new()

func _ready():
 game=get_parent()
 var args=OS.get_cmdline_user_args()
 var i=args.find("--bethel-facade-ab")
 if i<0 or i+1>=args.size() or not DirAccess.dir_exists_absolute(args[i+1]):
  set_process(false);game.request_game_close(2);return
 folder=args[i+1]
 add_child(photo_camera)
 begin=Time.get_ticks_msec()

func _process(_dt):
 if Time.get_ticks_msec()-begin>90000:
  set_process(false);print("BETHEL FACADE timed out");game.request_game_close(3);return
 if not game.ready_drive or not game.jump.is_empty():return
 if not menu_used:
  game.hud.open_search()
  var title=preload("res://Hud.gd").davidson_hall().title
  for item in game.hud.choices.get_children():
   if item is Button and item.text==title:
    menu_used=true
    item.pressed.emit()
    game.view=1
    game.spectral_ground.set_enabled(game,true)
    return
  set_process(false);print("BETHEL FACADE missing menu entry");game.request_game_close(4);return
 var tile=game.tiles.get(TILE_ID)
 if not tile or game.road_name!="Church Street" or game.vehicle.wheel_contact!=4:
  settled=0;return
 if not tile.structure_layer or not tile.info.has("building_status") or not tile.info.get("canopy_ready",false) or not tile.info.has("low_cover_date") or not tile.streaming_ground.get_shader_parameter("has_low_cover"):return
 settled+=1
 if settled<45:return
 set_process(false)
 var x=(LON+180.)/360.*65536.;var y=(1.-asinh(tan(deg_to_rad(LAT)))/PI)*32768.
 var off=game.tile_offset(TILE_ID)
 var p=off+Vector2(x-TILE_ID.x,y-TILE_ID.y)*game.side
 photo_camera.global_position=Vector3(p.x,game.ground(p)+1.65,p.y)
 var a=deg_to_rad(HEADING)
 photo_camera.look_at(photo_camera.global_position+Vector3(sin(a),0,-cos(a)),Vector3.UP)
 photo_camera.rotate_object_local(Vector3.RIGHT,deg_to_rad(3.82314846))
 photo_camera.rotate_object_local(Vector3.BACK,-deg_to_rad(2.7914034))
 photo_camera.keep_aspect=Camera3D.KEEP_WIDTH
 photo_camera.fov=61.041895
 photo_camera.near=.15
 photo_camera.far=1500.
 photo_camera.cull_mask=1
 game.hud.visible=false
 game.get_node("GroundShade").layers=0
 await facade_ab(tile)

func facade_ab(tile):
 game.set_process(false)
 game.set_physics_process(false)
 game.vehicle.freeze=true
 var original=tile.structure_layer
 if original==null:
  game.request_game_close(6)
  return
 var info:Dictionary=tile.info.duplicate()
 var baseline=tile.get_script().new()
 baseline.photographs.initialized=true
 baseline.build_structures(info,tile.side,tile.materials,tile.segments)
 tile.add_child(baseline)
 var path=folder.path_join("bethel")
 var failed=false
 var receipt={"renderer":RenderingServer.get_video_adapter_name(),"tile":tile.info.id,"baseline_children":baseline.get_child_count(),"survey_children":original.get_child_count(),"base_has_photos":not baseline.photographs.record.is_empty(),"survey_has_photos":not original.photographs.record.is_empty(),"road":game.road_name,"contacts":game.vehicle.wheel_contact,"views":{}}
 for mode in ["registered","church-street"]:
  var cam:Camera3D
  if mode=="church-street":
   game.vehicle.show()
   game.cockpit.set_view(true,game.camera,game.vehicle,0,0.016)
   var body=game.vehicle.global_transform
   game.camera.global_position=body*game.cockpit.EYE
   game.camera.look_at(body*game.cockpit.SIGHT,body.basis.y.normalized())
   game.camera.current=true
   cam=game.camera
   game.daylight.update(Vector2(LAT,LON),"",game.sun,game.day_sky.sky_material,0,0.)
  else:
   game.vehicle.hide()
   photo_camera.current=true
   cam=photo_camera
   var timestamp=Time.get_unix_time_from_datetime_string("2012-06-29T18:44:36")
   game.daylight.update(Vector2(LAT,LON),"",game.sun,game.day_sky.sky_material,timestamp,1.)
  var off=game.tile_offset(TILE_ID)
  await settle_facade_view()
  var current=Vector2(cam.global_position.x,cam.global_position.z)
  var uv=(current-off)/game.side
  var xx=TILE_ID.x+float(uv.x);var yy=TILE_ID.y+float(uv.y)
  var f=-cam.global_basis.z
  var v={"lon":xx/65536.*360.-180.,"lat":rad_to_deg(atan(sinh(PI*(1.-yy/32768.)))),"heading":fposmod(rad_to_deg(atan2(f.x,-f.z)),360.),"eye_m":cam.global_position.y-game.ground(current),"fov":cam.fov,"axis":"horizontal" if cam.keep_aspect==Camera3D.KEEP_WIDTH else "vertical"}
  receipt.views[mode]=v
  for state in ["stock","survey","stock-repeat"]:
   original.visible=state=="survey"
   baseline.visible=state!="survey"
   for i in 7:
    await get_tree().process_frame
    await RenderingServer.frame_post_draw
   var err=get_viewport().get_texture().get_image().save_png(path+"-"+mode+"-"+state+".png")
   if err!=OK:failed=true
 var receipt_file=FileAccess.open(path+"-receipt.json",FileAccess.WRITE)
 receipt_file.store_string(JSON.stringify(receipt,"  "))
 print("FACADE AB ",JSON.stringify(receipt))
 game.set_process(true)
 game.request_game_close(0 if not failed and game.vehicle.wheel_contact==4 and receipt.survey_has_photos and not receipt.base_has_photos else 7)

func settle_facade_view():
 for i in 75:
  await get_tree().process_frame
  await RenderingServer.frame_post_draw
