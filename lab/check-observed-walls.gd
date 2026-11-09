extends SceneTree

const TILE=preload("res://WorldTile.gd")
const WALLS=preload("res://ObservedWallPatches.gd")

func _initialize():
 var cases=0
 for id in WALLS.RECORDS:
  var rec=JSON.parse_string(FileAccess.get_file_as_string(WALLS.DIRECTORY+WALLS.RECORDS[id]))
  if not rec is Dictionary:quit(2);return
  var origin=[id.x,id.y]
  var b=[[rec.ring],[],80.,0.,rec.center[0],rec.center[1]]
  for mode in ["normal","revised","reversed","other_tile","below_roof"]:
   var f=b.duplicate(true)
   var tile=TILE.new()
   tile.side=450.;tile.grid_steps=1
   tile.info={"id":origin.duplicate(),"h":[0.,0.,0.,0.]}
   var top=70.
   if mode=="revised":f[0][0][0][0]+=.006
   if mode=="reversed":f[0][0].reverse()
   if mode=="other_tile":tile.info.id[0]+=1
   if mode=="below_roof":top=1.
   WALLS.new().add_if_matched(tile,f,top)
   var node=tile.get_node_or_null("ObservedWallPatch")
   var ok=is_instance_valid(node)==(mode=="normal")
   if node:
    ok=ok and node.mesh.get_surface_count()==1 and node.mesh.surface_get_array_len(0)==6
    ok=ok and node.cast_shadow==GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
    ok=ok and node.mesh.surface_get_material(0).metallic_specular==0.
   tile.free()
   if not ok:
    printerr("WALL SURVEY check failed ",id," ",mode)
    quit(3);return
   cases+=1
 print("WALL SURVEY registration and fallback checks passed ",cases)
 quit(0)
