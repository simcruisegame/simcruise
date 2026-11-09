extends RefCounted
# Only locally packaged, independently checked geometry. The authored pieces are visual terrain-following paint.
var masks: Array[PackedVector2Array] = []
var footprints: Array[PackedVector2Array] = []
var road_name: String

static func for_tile(tile):
 if tile.info.get("field",false):return null
 var id=tile.info.id
 var path="res://data/observed-paint-%d-%d.json"%[int(id[0]),int(id[1])]
 if not FileAccess.file_exists(path):return null
 var data=JSON.parse_string(FileAccess.get_file_as_string(path))
 if not data is Dictionary or int(data.get("version",0))!=1:return null
 var grid=data.get("tile",[])
 if grid.size()!=2 or int(grid[0])!=int(id[0]) or int(grid[1])!=int(id[1]):return null
 var result=new()
 result.road_name=str(data.get("road",""))
 if result.road_name=="" or not result.matches_carriageway(tile,data.get("anchors",[])):return null
 for item in data.get("features",[]):
  if item.size()==2 and item[0] in ["dash","right_edge","symbol"]:
   var ring:PackedVector2Array=result.metric_ring(tile,item[1])
   if ring.size()>=3:result.footprints.append(ring)
 for item in data.get("replacement",[]):
  if item.size()==2 and item[0]=="mask":
   var ring:PackedVector2Array=result.metric_ring(tile,item[1])
   if ring.size()>=3:result.masks.append(ring)
 if result.footprints.is_empty() or result.masks.is_empty():return null
 return result

func metric_ring(tile,points:Array)->PackedVector2Array:
 var ring=PackedVector2Array()
 for p in points:
  if p.size()!=2 or p[0]<0. or p[0]>1. or p[1]<0. or p[1]>1.:return PackedVector2Array()
  ring.append(tile.point(p))
 return ring

# Require the independently observed dash stations to still fall on the named one-way road.
# This avoids applying old paint across a newly realigned carriageway or a changed map tile.
func matches_carriageway(tile,stations:Array)->bool:
 if stations.size()<2:return false
 for anchor in stations:
  var p=tile.point(anchor)
  var near=false
  for road in tile.info.roads:
   if road[2]!=road_name or int(road[1])!=1:continue
   if road.size()>5 and road[5]=="unpaved":continue
   for i in range(1,road[3].size()):
    var a=tile.point(road[3][i-1]);var b=tile.point(road[3][i])
    if a.distance_squared_to(b)<.01:continue
    if Geometry2D.get_closest_point_to_segment(p,a,b).distance_to(p)<=2.2:
     near=true
     break
   if near:break
  if not near:return false
 return true

func remaining(a:Vector2,b:Vector2)->Array[PackedVector2Array]:
 var current:Array[PackedVector2Array]=[PackedVector2Array([a,b])]
 for polygon in masks:
  var parts:Array[PackedVector2Array]=[]
  for line in current:parts.append_array(Geometry2D.clip_polyline_with_polygon(line,polygon))
  current=parts
 return current

func dashed_stroke(tile,a:Vector2,b:Vector2)->String:
 var svg=""
 for line in remaining(a,b):
  if line.size()<2:continue
  var start=line[0];var finish=line[-1]
  var path='M%.3f,%.3f L%.3f,%.3f'%[start.x,start.y,finish.x,finish.y]
  var phase=(start-a).dot((b-a).normalized())
  svg+=tile.stroke(path,"#e2dfcd",.18,'stroke-dasharray="3 7" stroke-dashoffset="%.3f"'%phase)
 return svg

func draw(tile):
 var surface=tile.tool("whitepaint")
 for polygon in footprints:
  var indices=Geometry2D.triangulate_polygon(polygon)
  for i in range(0,indices.size(),3):
   var a=polygon[indices[i]];var b=polygon[indices[i+1]];var c=polygon[indices[i+2]]
   tile.tri(surface,tile.vert(a,tile.height_at(a)+.05),tile.vert(b,tile.height_at(b)+.05),tile.vert(c,tile.height_at(c)+.05),Vector2.ZERO,Vector2.ZERO,Vector2.ZERO,Vector3.UP)
