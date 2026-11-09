extends SceneTree
# Run with --path game --script /absolute/path/to/check_road.gd.
func _initialize():
 var directory=get_script().resource_path.get_base_dir()
 var fixtures=JSON.parse_string(FileAccess.get_file_as_string(directory+"/map-fixture.json"))
 var result=[]
 var drawn=0
 var authored=0
 for f in fixtures:
  var tile=preload("res://WorldTile.gd").new()
  tile.info={"id":f.id,"live_canopy":true}
  var lat=atan(sinh(PI*(1.-2.*(float(f.id[1])+.5)/65536.)))
  tile.side=40075016.68557849/65536.*cos(lat)
  for road in f.roads:
   for i in range(1,road[3].size()):
    var seg=[tile.point(road[3][i-1]),tile.point(road[3][i]),road[2],road[4],road[1],road[0]]
    if road.size()>5:seg.append(road[5])
    tile.segments.append(seg)
  tile.road_index()
  var survey=preload("res://RoadSurvey.gd").for_tile(tile)
  if survey==null:push_error("No valid authored survey");quit(1);return
  var passed=0
  for ab in survey.strokes:
   if survey.road_matches(tile,(ab[0]+ab[1])*.5,(ab[1]-ab[0]).normalized()):passed+=1
  drawn+=passed
  authored+=survey.strokes.size()
  result.append([f.id,passed,survey.strokes.size()])
  var a:Vector2=survey.bounds[0][0];var b:Vector2=survey.bounds[0][1]
  if a.distance_to(b)>2.5:
   var d=(b-a).normalized()
   if not survey.observed((a+b)*.5,d) or survey.observed((a+b)*.5,d.orthogonal()):
    push_error("Invalid heading gate");quit(1);return
  var off=Vector2(10000,0)
  if survey.remaining(a+off,b+off).size()!=1:push_error("Changed unobserved span");quit(1);return
  tile.free()
 print(JSON.stringify({"drawn":drawn,"authored":authored,"tiles":result}))
 quit(0 if drawn>0 and authored>0 else 1)
