extends Node
# --reference LAT LON HEADING FOV_H OUTPUT.jpg [PITCH_DOWN ROLL]
# --complete-reference  --reference-id UUID  --shadow-control BEFORE.jpg
var game
var location=Vector2.ZERO
var target_tile=Vector2i.ZERO
var uv=Vector2.ZERO
var heading:float
var horizontal_fov:float
var destination=""
var pitch_down=0.
var roll=0.
var requested_id=""
var reference_epoch:=0
var shadow_control=""
var wall_survey_ab=false
var ground_cover_ab=false
var frame=0
var started_at=0
var started=false
var photo_camera=Camera3D.new()
var wait_started=0
func _ready():
 game=get_parent()
 started_at=Time.get_ticks_msec()
 var args=OS.get_cmdline_user_args()
 wall_survey_ab="--wall-survey-ab" in args
 ground_cover_ab="--ground-cover-ab" in args
 var index=args.find("--reference")
 if index<0 or index+5>=args.size():game.request_game_close(2);return
 var lat=args[index+1].to_float();var lon=args[index+2].to_float()
 location=Vector2(lat,lon)
 var x=(lon+180.)/360.*65536.;var y=(1.-asinh(tan(deg_to_rad(lat)))/PI)*32768.
 target_tile=Vector2i(floori(x),floori(y));uv=Vector2(x-target_tile.x,y-target_tile.y)
 heading=args[index+3].to_float();horizontal_fov=args[index+4].to_float();destination=args[index+5]
 if args.size()>index+6 and args[index+6].is_valid_float():pitch_down=args[index+6].to_float()
 if args.size()>index+7 and args[index+7].is_valid_float():roll=args[index+7].to_float()
 index=args.find("--reference-epoch")
 if index>=0 and index+1<args.size() and args[index+1].is_valid_int():
  reference_epoch=maxi(0,args[index+1].to_int())
 index=args.find("--reference-id")
 if index>=0 and index+1<args.size():requested_id=args[index+1]
 index=args.find("--shadow-control")
 if index>=0 and index+1<args.size():shadow_control=args[index+1]
 add_child(photo_camera)

func _process(_dt):
 if Time.get_ticks_msec()-started_at>90000:
  print("REFERENCE timed out before capture");set_process(false);game.request_game_close(3);return
 if not game.ready_drive:return
 if not started:
  game.choose_place(location.y,location.x,"Reference pose")
  game.street_detail.qa_camera_tile=target_tile
  game.street_detail.qa_camera_uv=uv
  game.street_detail.qa_reference_id=requested_id
  started=true;return
 if not game.jump.is_empty() or game.tiles.size()<4 or not game.tiles.has(target_tile):return
 var pos=game.tile_offset(target_tile)+uv*game.side
 game.rangeland.survey_focus=pos
 var a=deg_to_rad(heading)
 photo_camera.global_position=Vector3(pos.x,game.ground(pos)+(2.3 if wall_survey_ab else 1.65),pos.y)
 photo_camera.look_at(photo_camera.global_position+Vector3(sin(a),0,-cos(a)),Vector3.UP)
 photo_camera.rotate_object_local(Vector3.RIGHT,-deg_to_rad(pitch_down))
 photo_camera.rotate_object_local(Vector3.BACK,-deg_to_rad(roll))
 photo_camera.keep_aspect=Camera3D.KEEP_WIDTH;photo_camera.fov=horizontal_fov
 photo_camera.cull_mask=1;photo_camera.near=.15;photo_camera.far=56000. if game.regional_terrain.enabled else 1500.
 photo_camera.current=true;game.hud.visible=false
 if reference_epoch>0:
  game.apply_daylight(location,str(game.tiles[target_tile].info.get("region","")),reference_epoch,1.)
 # Culling the player camera layer alone still leaves its shadow in the world.
 game.vehicle.hide();game.get_node("GroundShade").layers=0
 frame+=1
 if frame<60:return
 var active=game.tiles[target_tile]
 if wall_survey_ab and not active.info.has("building_status"):return
 if "--roof-pair" in OS.get_cmdline_user_args() and not active.info.has("roof_observation"):return
 var rec=game.street_detail.records.get(target_tile,{})
 var nearest=game.street_detail.nearby_reference(game)
 var selected=nearest.get("frame",{})
 var catalog=rec.get("panoramax",{})
 var ready=not rec.is_empty() and catalog.get("status")!="unavailable" and active.info.get("canopy_ready",false) and active.info.has("building_status") and (str(active.info.get("aerial_key",""))=="" or str(active.info.get("aerial",""))!="")
 if not requested_id.is_empty():ready=ready and selected.get("id")==requested_id and game.daylight.applied_epoch==int(selected.get("captureUnix",0))
 if "--complete-reference" in OS.get_cmdline_user_args() or not requested_id.is_empty():
  if wait_started==0:wait_started=Time.get_ticks_msec()
  if not ready and Time.get_ticks_msec()-wait_started<26000:return
 if ground_cover_ab:
  if not game.spectral_ground.enabled or active.info.get("low_cover_status") in ["unavailable","unsupported"]:
   print("REFERENCE ground-cover observation unavailable");set_process(false);game.request_game_close(5);return
  if not active.info.has("low_cover_date") or not active.streaming_ground.get_shader_parameter("has_low_cover"):return
 if "--landscape-reference" in OS.get_cmdline_user_args():
  if game.regional_terrain.displayed!=Vector2i(floori(target_tile.x/8.),floori(target_tile.y/8.)):return
  if not "--shrub-reference" in OS.get_cmdline_user_args() and not "--ridge-study" in OS.get_cmdline_user_args():print("REFERENCE LANDSCAPE ",game.regional_terrain.last_result)
 if "--snow-study" in OS.get_cmdline_user_args():
  if game.regional_terrain.snow.result.get("status")!="available" or not game.regional_terrain.finish.get_shader_parameter("has_snow"):return
 if "--ridge-study" in OS.get_cmdline_user_args():
  if game.regional_terrain.ridge.last_result.get("region")!=game.regional_terrain.displayed or game.regional_terrain.finish.get_shader_parameter("ridge_count")<1:return
 if "--summit-study" in OS.get_cmdline_user_args():
  if not game.regional_terrain.finish.get_shader_parameter("has_fine_0"):return
  print("SUMMIT SNOW ",game.regional_terrain.summit.last_result)
 if "--shrub-reference" in OS.get_cmdline_user_args():
  var region:=Vector2i(target_tile.x>>4,target_tile.y>>4)
  if not game.rangeland.observations.has(region) or game.rangeland.incomplete:return
  game.rangeland.save_stems(game,destination.get_basename()+"-stems.json")
  print("RANGELAND instances=",game.rangeland.draws," blocks=",game.rangeland.patches.size()," cells=",game.rangeland.tested_cells)
 if "--soil-reference" in OS.get_cmdline_user_args():
  if not active.streaming_ground or not active.streaming_ground.get_shader_parameter("has_soil"):return
  print("SOIL ",game.recent_label())
 if "--snow-study" in OS.get_cmdline_user_args():
  print("SATELLITE SNOW ",game.regional_terrain.snow.result)
 if "--ridge-study" in OS.get_cmdline_user_args():
  print("VIEW RIDGES ",game.regional_terrain.ridge.last_result)
 if "--archive-weather-ab" in OS.get_cmdline_user_args():
  if game.archive_weather.last.is_empty():return
  set_process(false)
  await archive_pair()
  return
 var sun:Vector3=game.sun.global_basis.z
 var status={"tile":[target_tile.x,target_tile.y],"ready":ready,"kartaview":rec.get("status","pending"),"panoramax":catalog,"references":rec.get("references",[]).size(),"id":selected.get("id"),"source":selected.get("source"),"capturedAt":selected.get("capturedAt"),"distance_m":nearest.get("distance_m"),"applied_epoch":game.daylight.applied_epoch,"archived_weather":game.daylight.weather.reference_label,"sky_kind":game.daylight.weather.sky_kind,"applied_weight":game.daylight.applied_weight,"sun_azimuth":fposmod(rad_to_deg(atan2(sun.x,-sun.z)),360.),"sun_elevation":rad_to_deg(asin(sun.y)),"heights":active.info.get("enriched_heights",{}).size(),"crowns":active.grove.size(),"camera_heading":heading,"horizontal_fov":horizontal_fov,"player_visible":game.vehicle.visible,"player_shade_layers":game.get_node("GroundShade").layers}
 print("REFERENCE LAYERS ",JSON.stringify(status))
 set_process(false)
 await RenderingServer.frame_post_draw
 if wall_survey_ab:
  await preload("res://checks/ObservedWallPair.gd").new().capture(game,active,destination)
  return
 if "--landscape-cost" in OS.get_cmdline_user_args():
  await measure_landscape()
  return
 if "--roof-pair" in OS.get_cmdline_user_args():
  await capture_roof_pair(active)
  return
 if ground_cover_ab:
  await capture_ground_pair(active)
  return
 var err=get_viewport().get_texture().get_image().save_jpg(destination,.94)
 if "--summit-study" in OS.get_cmdline_user_args():
  for idx in 2:game.regional_terrain.finish.set_shader_parameter("has_fine_"+str(idx),false)
  for i in 6:
   await get_tree().process_frame
   await RenderingServer.frame_post_draw
  get_viewport().get_texture().get_image().save_jpg(destination.get_basename()+"-broad.jpg",.94)
 if "--ridge-study" in OS.get_cmdline_user_args():
  game.regional_terrain.finish.set_shader_parameter("ridge_count",0)
  for i in 6:
   await get_tree().process_frame
   await RenderingServer.frame_post_draw
  get_viewport().get_texture().get_image().save_jpg(destination.get_basename()+"-coarse.jpg",.94)
 if "--snow-study" in OS.get_cmdline_user_args():
  game.regional_terrain.finish.set_shader_parameter("has_snow",false)
  for i in 6:
   await get_tree().process_frame
   await RenderingServer.frame_post_draw
  get_viewport().get_texture().get_image().save_jpg(destination.get_basename()+"-summer.jpg",.94)
 if "--soil-reference" in OS.get_cmdline_user_args():
  for tile in game.tiles.values():
   if tile.streaming_ground:tile.streaming_ground.set_shader_parameter("has_soil",false)
  for i in 6:
   await get_tree().process_frame
   await RenderingServer.frame_post_draw
  get_viewport().get_texture().get_image().save_jpg(destination.get_basename()+"-survey.jpg",.94)
 if "--shrub-reference" in OS.get_cmdline_user_args():
  game.rangeland.hide()
  for i in 6:
   await get_tree().process_frame
   await RenderingServer.frame_post_draw
  get_viewport().get_texture().get_image().save_jpg(destination.get_basename()+"-control.jpg",.94)
  game.rangeland.show()
 if not shadow_control.is_empty():
  game.vehicle.show();game.get_node("GroundShade").layers=1
  # Wait through the Metal renderer's in-flight frames after the visibility
  # switch. The canonical shot above was taken after sixty settled frames.
  for i in 6:
   await get_tree().process_frame
   await RenderingServer.frame_post_draw
  get_viewport().get_texture().get_image().save_jpg(shadow_control,.94)
  game.vehicle.hide();game.get_node("GroundShade").layers=0
 print("Reference ",location," heading=",heading," FOV=",horizontal_fov," file=",destination," result=",err)
 game.request_game_close(0 if err==OK and (requested_id.is_empty() or (ready and game.daylight.applied_weight>0.)) else 4)

func capture_roof_pair(tile):
 var original=tile.structure_layer
 var profiles=tile.info.get("live_roof_profiles",{})
 if profiles.is_empty():
  print("ROOF PAIR no measurements ",tile.info.get("roof_observation"))
  game.request_game_close(5);return
 game.set_process(false)
 game.set_physics_process(false)
 game.vehicle.freeze=true
 var control=tile.get_script().new()
 var data=tile.info.duplicate()
 data.erase("live_roof_profiles")
 control.build_structures(data,tile.side,tile.materials.duplicate(),tile.segments)
 tile.add_child(control)
 var receipt={"tile":tile.info.id,"profiles":profiles.size(),"survey":tile.info.roof_observation,"contacts":game.vehicle.wheel_contact,"frames":{},"projected":[]}
 for key in profiles:
  var row=profiles[key]
  var hit=preload("res://LidarRoof.gd").find(original,tile.info.buildings[key])
  if hit!=row:continue
  var box=Rect2()
  for idx in row.v.size():
   var v=row.v[idx]
   var pt=Vector2(v[0],v[1])*.00001*tile.side
   var center=Vector2(tile.info.buildings[key][4],tile.info.buildings[key][5])*tile.side
   var world=tile.to_global(Vector3(pt.x,tile.height_at(center)+v[2]*.01,pt.y))
   if photo_camera.is_position_behind(world):continue
   var pixel=photo_camera.unproject_position(world)
   box=Rect2(pixel,Vector2(.001,.001)) if not box.has_area() else box.expand(pixel)
  receipt.projected.append([key,row.validation.form,box.position.x,box.position.y,box.end.x,box.end.y])
 var failed=false
 for mode in ["before","survey","repeat"]:
  original.visible=mode=="survey"
  control.visible=not original.visible
  for f in 8:
   await get_tree().process_frame
   await RenderingServer.frame_post_draw
  var file=destination.get_basename()+"-"+mode+".png"
  var rc=get_viewport().get_texture().get_image().save_png(file)
  receipt.frames[mode]=rc
  if rc!=OK:failed=true
 print("ROOF PAIR ",JSON.stringify(receipt))
 tile.remove_child(control);control.free();original.show()
 game.set_process(true)
 game.request_game_close(5 if failed else 0)

func capture_ground_pair(tile):
 # Freeze streaming, input and scene lighting before comparing this tile's
 # material. A repeated off frame exposes any remaining temporal differences.
 game.set_process(false)
 game.set_physics_process(false)
 for loaded in game.tiles.values():
  if loaded.streaming_ground:loaded.streaming_ground.set_shader_parameter("has_low_cover",false)
 var failed=false
 var root=destination.get_basename()
 for state in ["off","on","off-repeat"]:
  tile.streaming_ground.set_shader_parameter("has_low_cover",state=="on")
  for i in 8:
   await get_tree().process_frame
   await RenderingServer.frame_post_draw
  var file=root+"-"+state+".png"
  var err=get_viewport().get_texture().get_image().save_png(file)
  if err!=OK:failed=true
  print("REFERENCE ground cover ",state," ",tile.info.low_cover_date," ",file," result=",err)
 game.set_process(true)
 game.request_game_close(5 if failed else 0)

func measure_landscape():
 game.set_process(false)
 game.set_physics_process(false)
 var vp=get_viewport().get_viewport_rid()
 RenderingServer.viewport_set_measure_render_time(vp,true)
 var groups={"visible":[],"hidden":[]}
 for enabled in [true,false,false,true]:
  game.regional_terrain.instance.visible=enabled
  for frame_id in 128:
   var begin=Time.get_ticks_usec()
   await RenderingServer.frame_post_draw
   if frame_id<24:continue
   var stack=groups["visible" if enabled else "hidden"]
   stack.append([(Time.get_ticks_usec()-begin)/1000.,RenderingServer.viewport_get_measured_render_time_gpu(vp),RenderingServer.viewport_get_measured_render_time_cpu(vp)])
 var summary={}
 for state in groups:
  var stats={}
  for i in 3:
   var values=[]
   for row in groups[state]:values.append(row[i])
   values.sort()
   stats[["frame_ms","gpu_ms","cpu_ms"][i]]=[values[values.size()/2],values[floori(values.size()*.95)]]
  summary[state]=stats
 print("LANDSCAPE COST ",JSON.stringify(summary))
 game.set_process(true)
 game.request_game_close(0)


func archive_pair():
 game.set_process(false)
 game.set_physics_process(false)
 game.vehicle.freeze=true
 var receipt={"observation":game.archive_weather.last,"epoch":reference_epoch,"modes":{}}
 for mode in ["before","after","repeat"]:
  game.archive_weather.enabled=mode=="after"
  game.apply_daylight(location,str(game.tiles[target_tile].info.get("region","")),reference_epoch,1.)
  for idx in 8:
   await get_tree().process_frame
   await RenderingServer.frame_post_draw
  var samples=[]
  for idx in 36:
   var at=Time.get_ticks_usec()
   await RenderingServer.frame_post_draw
   samples.append((Time.get_ticks_usec()-at)/1000.)
  samples.sort()
  var path=destination.get_basename()+"-"+mode+".png"
  var rc=get_viewport().get_texture().get_image().save_png(path)
  receipt.modes[mode]={"result":rc,"frame_interval_ms_median":samples[18],"frame_interval_ms_p95":samples[34],"label":game.weather_label(),"kind":game.daylight.weather.sky_kind,"weight":game.daylight.weather.sky_weight,"epoch":game.daylight.applied_epoch}
 var out=FileAccess.open(destination.get_basename()+".json",FileAccess.WRITE)
 out.store_string(JSON.stringify(receipt,"  "));out.close()
 print("ARCHIVE A/B ",JSON.stringify(receipt))
 game.set_process(true)
 game.request_game_close(0)
