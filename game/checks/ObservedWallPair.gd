extends RefCounted

# Runs after the normal streaming structure replacement in ReferencePose.
func capture(game,tile,destination:String):
 var panel=tile.structure_layer.get_node_or_null("ObservedWallPatch") if tile.structure_layer else null
 if panel==null:
  print("WALL SURVEY no eligible native wall at ",tile.info.id)
  game.request_game_close(4)
  return
 game.set_process(false)
 game.set_physics_process(false)
 var original:StandardMaterial3D=panel.mesh.surface_get_material(0)
 var coverage:StandardMaterial3D=original.duplicate()
 coverage.shading_mode=BaseMaterial3D.SHADING_MODE_UNSHADED
 var image=original.albedo_texture.get_image()
 image.clear_mipmaps()
 for y in image.get_height():
  for x in image.get_width():
   image.set_pixel(x,y,Color(1,0,1,image.get_pixel(x,y).a))
 image.generate_mipmaps()
 coverage.albedo_texture=ImageTexture.create_from_image(image)
 var result=0
 var stem=destination.get_basename()
 for state in ["control","survey","mask","repeat"]:
  panel.visible=state in ["survey","mask"]
  panel.material_override=coverage if state=="mask" else null
  for i in 6:
   await game.get_tree().process_frame
   await RenderingServer.frame_post_draw
  var save=game.get_viewport().get_texture().get_image().save_png(stem+"-"+state+".png")
  print("WALL SURVEY ",tile.info.id," ",state," ",save)
  if save!=OK:result=5
 panel.material_override=null
 panel.show()
 game.set_process(true)
 game.request_game_close(result)
