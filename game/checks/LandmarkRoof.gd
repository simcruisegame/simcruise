extends SceneTree

const PROFILE = preload("res://LidarRoof.gd")

class TestTile extends RefCounted:
 var info: Dictionary
 var side := 480.0
 var materials := {"roof":StandardMaterial3D.new(), "wall":StandardMaterial3D.new()}
 var triangles := {}
 var invalid_geometry := false
 func tool(key):
  return key
 func vert(p: Vector2, h: float) -> Vector3:
  return Vector3(p.x,h,p.y)
 func tri(key,a,b,c,_u,_v,_w,n):
  if not a.is_finite() or not b.is_finite() or not c.is_finite() or not n.is_finite(): invalid_geometry=true
  triangles[key]=int(triangles.get(key,0))+1
 func quad(key,a,b,c,d,n,_scale):
  tri(key,a,b,c,Vector2.ZERO,Vector2.ZERO,Vector2.ZERO,n)
  tri(key,a,c,d,Vector2.ZERO,Vector2.ZERO,Vector2.ZERO,n)

func check_tile(tx: int,ty: int, expected: int, measured_colors: bool) -> bool:
 var doc=JSON.parse_string(FileAccess.get_file_as_string("res://data/lidar-roofs-%d-%d.json"%[tx,ty]))
 if doc==null or doc.roofs.size()!=expected:return false
 for entry in doc.roofs:
  var t=TestTile.new()
  t.info={"id":[tx,ty]}
  var rings=[]
  for ring in entry.footprint:
   var uv=[]
   for p in ring: uv.append([p[0]/100000.,p[1]/100000.])
   rings.append(uv)
  var center := Vector2.ZERO
  for pair in rings[0]:center+=Vector2(pair[0],pair[1])
  center/=rings[0].size()
  var found=PROFILE.find(t,[rings,[],5.,0.,center.x,center.y])
  if found.is_empty():return false
  var mismatch=rings.duplicate(true)
  mismatch[0][0][0]+=.002
  if not PROFILE.find(t,[mismatch,[],5.,0.,center.x,center.y]).is_empty():return false
  PROFILE.draw(t,entry,150.0,"roof","wall")
  if t.invalid_geometry or int(t.triangles.get("roof",0)) < 1:return false
  if measured_colors:
   if t.materials.size()!=4 or entry.face_finish.length()!=entry.f.size()/3:return false
   var painted=0
   for key in t.triangles:
    if str(key).begins_with("measured_face_"):painted+=int(t.triangles[key])
   if painted<100:return false
  elif t.materials.size()!=2:return false
 return true

func _initialize():
 var ok=check_tile(17907,25198,1,true) and check_tile(18166,27914,15,false)
 var empty=TestTile.new()
 empty.info={"id":[19822,24236]}
 if not PROFILE.find(empty,[[[[.2,.2],[.3,.2],[.3,.3]]],[],5.,0.,.267,.233]).is_empty():ok=false
 if not ok:push_error("Landmark profile, untinted Miami profile or absent coverage regression")
 else:print("LANDMARK ROOF PASSED: Charleston two finishes and dome fallback, 15 untinted Miami roofs, Cambridge abstention and mismatched polygons")
 quit(0 if ok else 1)
