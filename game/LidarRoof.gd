extends RefCounted
# Packaged profiles contain tile-normalized horizontal positions × 100000 and
# centimetres above surveyed local ground. The caller supplies its terrain base.
const UNITS = 100000.0

static func _ring_matches(a, b) -> bool:
 if a.size()!=b.size() or a.size()<3:return false
 var first=Vector2(a[0][0],a[0][1])*UNITS
 for start in b.size():
  if first.distance_to(Vector2(b[start][0],b[start][1]))>1.5:continue
  for direction in [-1,1]:
   var good=true
   for i in a.size():
    var other=b[posmod(start+i*direction,b.size())]
    if (Vector2(a[i][0],a[i][1])*UNITS).distance_to(Vector2(other[0],other[1]))>1.5:
     good=false;break
   if good:return true
 return false

static func _load_profiles(tile) -> Dictionary:
 var out={}
 var path="res://data/lidar-roofs-%d-%d.json" % [tile.info.id[0],tile.info.id[1]]
 var profiles=[]
 if FileAccess.file_exists(path):
  var doc=JSON.parse_string(FileAccess.get_file_as_string(path))
  if doc is Dictionary and doc.get("schema",0)==1:
   var at=doc.get("tile",[])
   if at.size()==2 and at[0]==tile.info.id[0] and at[1]==tile.info.id[1]:
    profiles.append_array(doc.get("roofs",[]))
 # A packaged detailed survey takes priority when both match exactly.
 profiles.append_array(tile.info.get("live_roof_profiles",{}).values())
 for profile in profiles:
  var rings=profile.get("footprint",[])
  if rings.is_empty() or rings[0].size()<3 or profile.get("v",[]).is_empty():continue
  var center=Vector2.ZERO
  for p in rings[0]:center+=Vector2(p[0],p[1])
  var key=Vector2i((center/rings[0].size()).round())
  if not out.has(key):out[key]=[]
  out[key].append(profile)
 return out

static func find(tile, building) -> Dictionary:
 if building.size()<6 or float(building[3])>.1:return {}
 if not tile.has_meta("lidar_roof_profiles"):tile.set_meta("lidar_roof_profiles",_load_profiles(tile))
 var profiles:Dictionary=tile.get_meta("lidar_roof_profiles")
 if profiles.is_empty():return {}
 var key=Vector2i((Vector2(building[4],building[5])*UNITS).round())
 for dx in range(-1,2):
  for dy in range(-1,2):
   for candidate in profiles.get(key+Vector2i(dx,dy),[]):
    var rings=candidate.footprint
    if rings.size()!=building[0].size():continue
    var all_match=true
    for i in rings.size():
     if not _ring_matches(building[0][i],rings[i]):
      all_match=false;break
    if all_match:return candidate
 return {}

static func draw(tile, profile:Dictionary, base:float, roof_key, wall_key) -> void:
 var projection=preload("res://SurveyProjection.gd")
 var roof=tile.tool(projection.material(tile,profile,base,"0",roof_key))
 var palette:=preload("res://SurveyFaceFinish.gd").palette(tile,profile)
 var face_finish: String=profile.get("face_finish","")
 if face_finish.length()!=profile.f.size()/3:face_finish=""
 for code in palette:
  palette[code]=projection.material(tile,profile,base,code,palette[code])
 if palette.has("2"):wall_key=palette["2"]
 var verts=PackedVector3Array()
 var uv=PackedVector2Array()
 for v in profile.v:
  var p=Vector2(v[0],v[1])/UNITS
  uv.append(p);verts.append(tile.vert(p*tile.side,base+float(v[2])*.01))
 for i in range(0,profile.f.size(),3):
  var a=int(profile.f[i]);var b=int(profile.f[i+1]);var c=int(profile.f[i+2])
  var normal=(verts[b]-verts[a]).cross(verts[c]-verts[a]).normalized()
  if normal.y<0.:normal=-normal
  var surface=roof
  if not face_finish.is_empty():
   var kind:=face_finish.substr(i/3,1)
   if palette.has(kind):surface=tile.tool(palette[kind])
  if normal.length_squared()>.5:tile.tri(surface,verts[a],verts[b],verts[c],uv[a],uv[b],uv[c],normal)
 var low=base+float(profile.eave_m)
 for i in range(0,profile.edge.size(),2):
  var a=verts[int(profile.edge[i])];var b=verts[int(profile.edge[i+1])]
  if maxf(a.y,b.y)-low<.005:continue
  var diff=Vector2(b.x-a.x,b.z-a.z)
  if diff.length_squared()<.000001:continue
  var normal=Vector3(diff.y,0.,-diff.x).normalized()
  tile.quad(wall_key,Vector3(a.x,low,a.z),Vector3(b.x,low,b.z),b,a,normal,Vector2(diff.length(),(a.y+b.y)*.5-low))
