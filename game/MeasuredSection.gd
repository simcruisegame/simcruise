extends RefCounted
# A tile can carry independently surveyed material and paint for a short signed carriageway.
# The optional mask encodes pavement in red, affirmative classification in green. Black abstains.
var road_name:String
var zone:Image
var crop:Rect2
var marks=[]
var replacement:PackedVector2Array
var png_path:String

static func for_tile(tile):
 if not tile.info.get("live_canopy",false) or OS.get_environment("SD_DISABLE_MEASURED_SECTION")=="1":return null
 var id=tile.info.id
 var prefix="res://data/measured-section-%d-%d"%[int(id[0]),int(id[1])]
 # Existing hand-verified projections and field road surveys own their tiles.
 if tile.info.get("field",false) or FileAccess.file_exists("res://data/road-survey-%d-%d.json"%[int(id[0]),int(id[1])]) or FileAccess.file_exists("res://data/observed-paint-%d-%d.json"%[int(id[0]),int(id[1])]):return null
 if not FileAccess.file_exists(prefix+".json"):return null
 var data=JSON.parse_string(FileAccess.get_file_as_string(prefix+".json"))
 if not data is Dictionary or int(data.get("version",0))!=1 or data.get("tile",[]).size()!=2:return null
 if int(data.tile[0])!=int(id[0]) or int(data.tile[1])!=int(id[1]):return null
 var it=new()
 it.road_name=str(data.get("road",""))
 if it.road_name.is_empty() or not it.agrees(tile,data.get("anchors",[])):return null
 var uv=data.get("crop",[])
 if uv.size()!=4 or uv[2]<=0. or uv[3]<=0.:return null
 it.crop=Rect2(uv[0],uv[1],uv[2],uv[3])
 it.zone=Image.new()
 it.png_path=prefix+".png"
 if it.zone.load_png_from_buffer(FileAccess.get_file_as_bytes(it.png_path))!=OK:return null
 for m in data.get("marks",[]):
  if m.size()==4 and m[0] in ["whitepaint","yellowpaint"] and float(m[3])>=.06 and float(m[3])<=.30:
   it.marks.append([m[0],tile.point(m[1]),tile.point(m[2]),float(m[3])])
 for p in data.get("replacement",[]):it.replacement.append(tile.point(p))
 if it.marks.is_empty() or it.replacement.size()<4:return null
 return it

func agrees(tile,anchors)->bool:
 if anchors.size()<3:return false
 for a in anchors:
  var p=tile.point(a[0]);var d:Vector2=(tile.point(a[1])-p).normalized();var found=false
  for road in tile.info.roads:
   if road[2]!=road_name or int(road[1])!=1 or road[0] not in ["primary","trunk","secondary","tertiary"]:continue
   if road.size()>5 and road[5]=="unpaved":continue
   for i in range(1,road[3].size()):
    var x=tile.point(road[3][i-1]);var y=tile.point(road[3][i])
    if d.dot((y-x).normalized())>.978 and p.distance_to(Geometry2D.get_closest_point_to_segment(p,x,y))<3.2:found=true;break
   if found:break
  if not found:return false
 return true

func surface(p:Vector2,side:float)->float:
 var uv=p/side
 if not crop.has_point(uv):return -1.
 var relative=(uv-crop.position)/crop.size
 var c=zone.get_pixel(clampi(floori(relative.x*zone.get_width()),0,zone.get_width()-1),clampi(floori(relative.y*zone.get_height()),0,zone.get_height()-1))
 if c.g<.5:return -1.
 return 1. if c.r>.5 else .65

func bind(mat:ShaderMaterial):
 mat.set_shader_parameter("section_map",ImageTexture.create_from_image(zone))
 mat.set_shader_parameter("section_box",Vector4(crop.position.x,crop.position.y,crop.size.x,crop.size.y))
 mat.set_shader_parameter("has_section",true)

func remaining(a:Vector2,b:Vector2):
 return Geometry2D.clip_polyline_with_polygon(PackedVector2Array([a,b]),replacement)

func dashed_stroke(tile,a:Vector2,b:Vector2)->String:
 var result=""
 for part in remaining(a,b):
  if part.size()<2:continue
  var x:Vector2=part[0];var y:Vector2=part[-1];var t=(x-a).dot((b-a).normalized())
  result+=tile.stroke('M%.3f,%.3f L%.3f,%.3f'%[x.x,x.y,y.x,y.y],"#e2dfcd",.18,'stroke-dasharray="3 7" stroke-dashoffset="%.3f"'%t)
 return result

func draw(tile):
 for item in marks:
  var a:Vector2=item[1];var b:Vector2=item[2]
  tile.raised_stripe(item[0],a,b,(b-a).normalized().orthogonal()*item[3]*.5)
