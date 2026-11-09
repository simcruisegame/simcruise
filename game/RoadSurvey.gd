extends RefCounted
var bounds=[]
var strokes=[]

static func for_tile(tile):
 if not tile.info.get("live_canopy",false):return null
 var path="res://data/road-survey-%d-%d.json"%[int(tile.info.id[0]),int(tile.info.id[1])]
 if not FileAccess.file_exists(path):return null
 var doc=JSON.parse_string(FileAccess.get_file_as_string(path))
 if not doc is Dictionary or int(doc.get("version",0))!=1:return null
 var id=doc.get("tile",[])
 if not id is Array or id.size()!=2 or int(id[0])!=int(tile.info.id[0]) or int(id[1])!=int(tile.info.id[1]):return null
 var self_ref=load("res://RoadSurvey.gd").new()
 for ab in doc.bounds:self_ref.bounds.append([tile.point(ab[0]),tile.point(ab[1])])
 for ab in doc.strokes:self_ref.strokes.append([tile.point(ab[0]),tile.point(ab[1]),ab[2]])
 return self_ref

func observed(p:Vector2,direction:Vector2):
 for ab in bounds:
  if absf(direction.dot((ab[1]-ab[0]).normalized()))<.976:continue
  if p.distance_squared_to(Geometry2D.get_closest_point_to_segment(p,ab[0],ab[1]))<25.:return true
 return false

func remaining(a:Vector2,b:Vector2):
 var count=maxi(1,ceili(a.distance_to(b)/1.25));var out=[];var start=-1
 var d=(b-a).normalized()
 for i in count:
  var keep=not observed(a.lerp(b,(i+.5)/count),d)
  if keep and start<0:start=i
  if start>=0 and (not keep or i==count-1):
   var end=i+1 if keep else i
   out.append([a if start==0 else a.lerp(b,float(start)/count),b if end==count else a.lerp(b,float(end)/count)])
   start=-1
 return out

# Verify the rendered road still follows the surveyed two-way alignment.
func road_matches(tile,p:Vector2,d:Vector2):
 for axis in bounds:
  var along:Vector2=(axis[1]-axis[0]).normalized()
  if absf(d.dot(along))<.976:continue
  var foot=Geometry2D.get_closest_point_to_segment(p,axis[0],axis[1])
  if p.distance_squared_to(foot)>26.:continue
  for seg in tile.road_grid.get(Vector2i((foot/24.).floor()),[]):
   if seg[4] or seg[5] not in ["trunk","primary","secondary","tertiary"] or (seg.size()>6 and seg[6]=="unpaved"):continue
   if absf(along.dot((seg[1]-seg[0]).normalized()))<.976:continue
   if foot.distance_squared_to(Geometry2D.get_closest_point_to_segment(foot,seg[0],seg[1]))<25.:return true
 return false

func draw(tile):
 for ab in strokes:
  var a:Vector2=ab[0];var b:Vector2=ab[1];var n=(b-a).normalized().orthogonal()*.06
  if a.distance_squared_to(b)>.0001 and road_matches(tile,(a+b)*.5,(b-a).normalized()):
   tile.raised_stripe(ab[2],a,b,n)
