extends SceneTree
## godot --headless --path game --script res://checks/OutcropCheck.gd -- /path/to/z16-tile.json
func _initialize()->void:
 var args=OS.get_cmdline_user_args()
 if args.size()!=1:quit(2);return
 var info=JSON.parse_string(FileAccess.get_file_as_string(args[0]))
 var tile=preload("res://WorldTile.gd").new()
 tile.info=info;tile.grid_steps=int(info.grid);tile.side=441.05718
 tile.surveyed_outcrop=preload("res://SurveyOutcropGround.gd").for_tile(tile)
 if not tile.surveyed_outcrop:push_error("Expected source roads and patch");tile.free();quit(3);return
 var max_road_error=0.
 for r in info.roads:
  for i in range(1,r[3].size()):
   var a=tile.point(r[3][i-1]);var b=tile.point(r[3][i]);var side=(b-a).normalized().orthogonal()*float(r[4])*.42
   for j in range(ceil(a.distance_to(b))+1):
    var pos=a.lerp(b,j/maxf(a.distance_to(b),1.))
    for p in [pos,pos-side,pos+side]:
     max_road_error=maxf(max_road_error,absf(tile.height_at(p)-tile.base_height(p)))
 var material=ShaderMaterial.new();material.shader=preload("res://terrain.gdshader")
 tile.ground_mesh(material)
 var grounds=tile.get_children().filter(func(c):return c is MeshInstance3D and c.mesh!=null and c.mesh.get_surface_count()>0)
 var collisions=tile.get_children().filter(func(c):return c is StaticBody3D and c.get_child_count()>0)
 if grounds.is_empty() or collisions.is_empty():push_error("Missing drivable ground");tile.free();quit(8);return
 var faces=int(info.get("measured_granite_faces",0))
 var target=18000 if int(info.id[0])==13919 else 0
 info.roads[0][4]=float(info.roads[0][4])+1.
 var fallbacks=preload("res://SurveyOutcropGround.gd").for_tile(tile)==null
 print("NEEDLES tile=",info.id," road height maximum error=",max_road_error," granite faces=",faces," changed-road fallback=",fallbacks)
 tile.free()
 quit(0 if max_road_error<.0001 and faces==target and fallbacks else 4)
