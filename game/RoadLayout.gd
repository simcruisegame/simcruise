extends RefCounted
## Only a certified paint line or a uniquely registered, explicit single motor lane
## can replace the inferred divider. Neither association implies occupancy.
var by_name:Dictionary={}
var paint:Array=[]
var side:float
const MAP_SIGNATURE=preload("res://SurveyOutcropGround.gd")

static func uv(value)->bool:
 return value is Array and value.size()==2 and value.all(func(v):return typeof(v) in [TYPE_FLOAT,TYPE_INT] and is_finite(v) and v>=0. and v<=1.)

static func for_tile(tile):
 if "--without-road-layout" in OS.get_cmdline_user_args():return null
 var info:Dictionary=tile.info;var id=info.get("id",[])
 if id.size()!=2 or info.get("field",false):return null
 var survey_path="res://data/road-layout-%d-%d.json"%[int(id[0]),int(id[1])]
 var observed=FileAccess.file_exists(survey_path)
 var cached=info.get("road_layout")
 if not observed and not cached is Dictionary:return null
 var signature=MAP_SIGNATURE.road_signature(info.roads,info.get("road_class_width",[])).hex_encode()
 var model=new();model.side=tile.side
 if cached is Dictionary and cached.get("id",[])==id and cached.get("version") == 1 and cached.get("status")=="checked" and cached.get("roadSignature")==signature:
  var entries=cached.get("spans",[])
  if entries is Array and entries.size()<=192:
   for scope in entries:
    if not scope is Dictionary:continue
    var index=int(scope.get("road",-1))
    if index<0 or index>=info.roads.size():continue
    var road=info.roads[index]
    if not road[1] or road[0] not in ["motorway","trunk","primary","secondary","tertiary"] or (road.size()>5 and road[5]=="unpaved"):continue
    model.add_scope(road[2],scope)
 if observed:
  var doc=JSON.parse_string(FileAccess.get_file_as_string(survey_path))
  if doc is Dictionary and doc.get("id",[])==id and doc.get("version")==1 and doc.get("roadSignature")==signature and model.add_scope(str(doc.get("name","")),doc):
   for stripe in doc.get("observedPaint",[]):
    if stripe is Dictionary and uv(stripe.get("a")) and uv(stripe.get("b")):
     model.paint.append([model.metres(stripe.a),model.metres(stripe.b)])
   # Placement consumers must also check their own object footprint, survey
   # date, crossing/driveway restrictions and whether a vehicle was observed.
   info["measured_bay_envelopes"]=doc.get("availableBayEnvelopes",[]).duplicate(true)
   info["road_paint_observation"]={"way":doc.way,"observed":doc.observed,"source":doc.geometrySource,"count":model.paint.size()}
 return null if model.by_name.is_empty() else model

func metres(v)->Vector2:return Vector2(v[0],v[1])*side

func add_scope(name:String,row:Dictionary)->bool:
 if name.is_empty() or not str(row.get("way","")).is_valid_int() or not uv(row.get("a")) or not uv(row.get("b")):return false
 var a=metres(row.a);var b=metres(row.b)
 if a.distance_to(b)<2.8 or a.distance_to(b)>side:return false
 if not by_name.has(name):by_name[name]=[]
 by_name[name].append([a,b])
 return true

func applies(street:String)->bool:return by_name.has(street)

func remaining(a:Vector2,b:Vector2,name:String)->Array:
 var parts=[[a,b]]
 var direction=(b-a).normalized()
 for row in by_name.get(name,[]):
  var x:Vector2=row[0];var y:Vector2=row[1];var tangent=(y-x).normalized()
  if absf(direction.dot(tangent))<.995:continue
  var center=(a+b)*.5
  # Compare lateral distance at the overlapping interval along the source,
  # not at the midpoint of a potentially long curved-away map segment.
  var start=(a-x).dot(tangent);var finish=(b-x).dot(tangent)
  var mid=(maxf(0.,minf(start,finish))+minf(x.distance_to(y),maxf(start,finish)))*.5
  if mid<0. or mid>x.distance_to(y) or is_zero_approx(finish-start):continue
  center=a.lerp(b,clampf((mid-start)/(finish-start),0.,1.))
  if absf((center-x).cross(tangent))>1.55:continue
  var kept=[]
  for endpoints in parts:
   var s=(endpoints[0]-x).dot(tangent);var e=(endpoints[1]-x).dot(tangent)
   if is_zero_approx(s-e):kept.append(endpoints);continue
   var f=-s/(e-s);var h=(y.distance_to(x)-s)/(e-s)
   var lo=clampf(minf(f,h),0.,1.);var hi=clampf(maxf(f,h),0.,1.)
   if hi<=lo:kept.append(endpoints);continue
   if lo>.00001:kept.append([endpoints[0],endpoints[0].lerp(endpoints[1],lo)])
   if hi<.99999:kept.append([endpoints[0].lerp(endpoints[1],hi),endpoints[1]])
  parts=kept
  if parts.is_empty():break
 return parts

func dashed_stroke(tile,a:Vector2,b:Vector2,name:String)->String:
 var svg=""
 var direction=(b-a).normalized()
 for part in remaining(a,b,name):
  var start:Vector2=part[0];var finish:Vector2=part[1]
  var path='M%.2f,%.2f L%.2f,%.2f'%[start.x,start.y,finish.x,finish.y]
  var phase=(start-a).dot(direction)
  svg+=tile.stroke(path,"#e2dfcd",.18,'stroke-dasharray="3 7" stroke-dashoffset="%.3f"'%phase)
 return svg

func draw(tile)->void:
 for stroke in paint:
  var a:Vector2=stroke[0];var b:Vector2=stroke[1]
  if a.distance_squared_to(b)<.01:continue
  var t=(b-a).normalized()
  tile.raised_stripe("whitepaint",a,b,Vector2(-t.y,t.x)*.064)
