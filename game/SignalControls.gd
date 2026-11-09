extends RefCounted

const MOTOR=["primary","secondary","tertiary","minor","trunk"]
const COLORS=["signal_steel","signal_dark"]
const JUNCTION=preload("res://Junction.gd")
const MIN_LINK_M=1.0

# Request only in possibly signalized urban districts. Exact topology is
# checked again after the optional observation arrives.
static func eligible(tile) -> bool:
 if not tile.info.get("live_canopy",false) or not tile.capture.record.is_empty():return false
 var named={}
 var major=false
 for road in tile.info.roads:
  if road[0] in MOTOR and str(road[2])!="":
   named[road[2]]=true
   if road[0] in ["primary","secondary","tertiary","trunk"]:major=true
   if major and named.size()>=2:return true
 return false

static func _position(record,side:float) -> Vector2:
 if not record is Dictionary or not record.get("uv") is Array or record.uv.size()!=2:return Vector2.INF
 var a=record.uv[0];var b=record.uv[1]
 if not (a is float or a is int) or not (b is float or b is int):return Vector2.INF
 var uv=Vector2(a,b)
 if not uv.is_finite() or minf(uv.x,uv.y)<0. or maxf(uv.x,uv.y)>=1.:return Vector2.INF
 return uv*side

static func crossings(tile,records:Array) -> Array:
 if records.size()>64:return []
 var observers=[]
 for record in records:
  var at=_position(record,tile.side)
  if at.is_finite():observers.append(at)
 if observers.is_empty():return []
 var points={}
 for seg in tile.segments:
  if not seg[5] in MOTOR or str(seg[2]).is_empty() or seg[0].distance_to(seg[1])<MIN_LINK_M:continue
  for endpoint in 2:
   var p:Vector2=seg[endpoint]
   var key=Vector2i((p*5.).round())
   if not points.has(key):points[key]={"at":p,"arms":[]}
   var arm:Vector2=p.direction_to(seg[1-endpoint])
   var way=int(seg[4])
   var incoming=way==0 or (endpoint==1)==(way==1)
   var same=false
   for existing in points[key].arms:
    if arm.dot(existing.arm)>.9:
     existing.incoming=existing.incoming or incoming
     same=true;break
   if not same:points[key].arms.append({"arm":arm,"width":float(seg[3]),"name":str(seg[2]),"incoming":incoming})
 var candidates=[]
 for point in points.values():
  var observed=false
  for at in observers:
   if point.at.distance_squared_to(at)<=25.:
    observed=true;break
  if not observed:continue
  # Vector simplification can omit a mid-line vertex on a straight arterial
  # while both terminating side roads retain the precise junction vertex.
  for seg in tile.segments:
   if not seg[5] in MOTOR or str(seg[2]).is_empty():continue
   if point.at.distance_to(seg[0])<4. or point.at.distance_to(seg[1])<4.:continue
   var q=Geometry2D.get_closest_point_to_segment(point.at,seg[0],seg[1])
   if point.at.distance_to(q)>1.6:continue
   if not point.arms.any(func(arm):return arm.name!=str(seg[2]) and absf(arm.arm.dot(seg[0].direction_to(seg[1])))<.8):continue
   for end in 2:
    var ray=q.direction_to(seg[end])
    var incoming=int(seg[4])==0 or (end==0)==(int(seg[4])==1)
    if not point.arms.any(func(arm):return arm.arm.dot(ray)>.9):
     point.arms.append({"arm":ray,"width":float(seg[3]),"name":str(seg[2]),"incoming":incoming})
  if point.arms.size()<3 or point.arms.size()>5:continue
  var independent=false
  for i in point.arms.size():
   for j in range(i+1,point.arms.size()):
    var a=point.arms[i];var b=point.arms[j]
    if a.name!=b.name and absf(a.arm.dot(b.arm))<.80:independent=true
  if independent:candidates.append(point)
 return candidates

static func match_records(side:float,records:Array,candidates:Array,existing:Array) -> Array:
 var accepted=[]
 var assigned={}
 for ref in records:
  var at=_position(ref,side)
  if not at.is_finite():continue
  var nearby=[]
  for i in candidates.size():
   if at.distance_to(candidates[i].at)<=5.:nearby.append(i)
  if nearby.size()!=1 or assigned.has(nearby[0]):continue
  var crossing=candidates[nearby[0]]
  var duplicate=false
  for raw in existing:
   if raw is Array and raw.size()==2 and crossing.at.distance_to(Vector2(raw[0],raw[1])*side)<15.:duplicate=true;break
  if duplicate:continue
  assigned[nearby[0]]=true
  var matched=crossing.duplicate()
  matched["osm"]=str(ref.get("osm",""))
  accepted.append(matched)
 return accepted

static func _approach(draw,tile,center:Vector2,row:Dictionary,other_width:float) -> bool:
 var travel:Vector2=-row.arm
 var right=Vector2(-travel.y,travel.x)
 var pole=center+travel*(other_width*.5+1.5)+right*(row.width*.5+1.4)
 if minf(pole.x,pole.y)<.2 or maxf(pole.x,pole.y)>tile.side-.2 or tile.occupied(pole):return false
 # An oblique or divided road can put the nominal far corner in active pavement.
 for seg in tile.segments:
  if not seg[5] in MOTOR:continue
  var nearest=Geometry2D.get_closest_point_to_segment(pole,seg[0],seg[1])
  if nearest.distance_to(pole)<float(seg[3])*.5+.25:return false
 var y=tile.height_at(pole)
 var top=tile.vert(pole,y+3.85)
 var head=top-Vector3(right.x,0,right.y)*.24
 draw.post(tile.vert(pole,y),top,.105,.076,COLORS[0],8)
 draw.post(top,head,.068,.052,COLORS[0],8)
 JUNCTION.box(draw,head-Vector3.UP*.54,travel,Vector3(.34,1.04,.38),COLORS[1])
 var facing=Vector3(-travel.x,0,-travel.y)
 for i in 3:
  var lens=head-Vector3.UP*(.22+.30*i)+facing*.195
  draw.post(lens,lens+facing*.085,.117,.109,COLORS[1],10)
 return true

static func install(tile,records:Array) -> Dictionary:
 var result={"records":records.size(),"crossings":0,"approaches":0,"observed_nodes":{}}
 if tile.has_node("MappedIntersectionSignals") or not tile.capture.record.is_empty() or records.is_empty():return result
 var matches=match_records(tile.side,records,crossings(tile,records),tile.info.get("street_objects",{}).get("signals",[]))
 if matches.is_empty():return result
 var draw=tile.get_script().new()
 draw.materials=tile.materials
 draw.name="MappedIntersectionSignals"
 for point in matches:
  var arms=0
  for row in point.arms:
   if not row.incoming:continue
   var width=0.
   for other in point.arms:
    if absf(row.arm.dot(other.arm))<.8:width=maxf(width,other.width)
   if width>0. and _approach(draw,tile,point.at,row,width):arms+=1
  if arms>0:
   result.crossings+=1
   result.approaches+=arms
   result.observed_nodes[point.osm]=arms
 if result.approaches:
  draw.finish_geometry()
  tile.add_child(draw)
 else:draw.free()
 return result
