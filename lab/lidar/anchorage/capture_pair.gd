extends RefCounted
func capture(game,tile,destination):
 game.set_process(false);game.set_physics_process(false)
 var original=tile.structure_layer
 var control=tile.get_script().new()
 var data=tile.info.duplicate()
 var live_only={}
 for profile in data.get("live_roof_profiles",{}).values():
  var center=Vector2.ZERO
  for p in profile.footprint[0]:center+=Vector2(p[0],p[1])
  var key=Vector2i((center/profile.footprint[0].size()).round())
  if not live_only.has(key):live_only[key]=[]
  live_only[key].append(profile)
 control.set_meta("lidar_roof_profiles",live_only)
 control.build_structures(data,tile.side,tile.materials.duplicate(),tile.segments)
 tile.add_child(control)
 for idx in [14,18]:
  var b=data.buildings[idx]
  var found=preload("res://LidarRoof.gd").find(original,b)
  if found.is_empty():print("ROOF MISMATCH ",idx);game.request_game_close(4);return
  print("ANCHORAGE MEASURED ",idx," surveyed ",found.eave_m," to ",found.top_m," base ",b[2]," contacts ",game.vehicle.wheel_contact)
 for state in ["control","survey","repeat"]:
  original.visible=state=="survey";control.visible=not original.visible
  for i in 7:
   await game.get_tree().process_frame
   await RenderingServer.frame_post_draw
  var err=game.get_viewport().get_texture().get_image().save_png(destination.get_basename()+"-"+state+".png")
  if err!=OK:game.request_game_close(5);return
 tile.remove_child(control);control.free();original.show()
 game.set_process(true);game.request_game_close(0)
