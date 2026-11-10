extends SceneTree
## godot --headless --path game --script res://checks/RoadProfile.gd -- [--tiles=18179_27907,…] [--step=1] [--all] [--dir=/abs/path] [--without-road-smoothing]
## --dir= reads streamed tiles (`atlas tile CACHE X Y` output saved as X_Y.json) from any folder,
## so a rough road reported anywhere can be measured; with no --tiles it takes every tile there.
## --classes=primary,secondary limits the table to those OSM road classes.
## Builds bundled tiles through WorldTile.build exactly as the streamer does, then walks
## every road centreline at fixed steps over the generated ground collider (what the tyres
## touch) and over height_at() (what the camera and props use). It reports slope changes per
## step ("creases"), their histogram, the largest vertical step and the smoothing cost. One
## table prints at the end; nothing prints per sample or per frame. --without-road-smoothing
## gives the before measurement on the same build.
const TILE=preload("res://WorldTile.gd")
const EARTH=40075016.68557849
# The bundled Miami Beach block (4 x 6 zoom-16 tiles), the offline start; --all gives the same set.
const DEFAULT_TILES=["18178_27905","18178_27906","18178_27907","18178_27908","18178_27909","18178_27910","18179_27905","18179_27906","18179_27907","18179_27908","18179_27909","18179_27910","18180_27905","18180_27906","18180_27907","18180_27908","18180_27909","18180_27910","18181_27905","18181_27906","18181_27907","18181_27908","18181_27909","18181_27910"]
const BINS=[0.,.25,.5,1.,2.,4.,8.]
const CREASE_DEG=.5
const REFERENCE_MPS=20.1  # 45 mph: the vertical speed a crease imposes on a tyre is v·sin(Δθ)
var step_m=1.

func _initialize():
 var args=OS.get_cmdline_user_args()
 var tiles=DEFAULT_TILES.duplicate()
 for arg in args:
  if arg.begins_with("--tiles="):tiles=Array(arg.trim_prefix("--tiles=").split(",",false))
  elif arg.begins_with("--step="):step_m=maxf(.25,float(arg.trim_prefix("--step=")))
 if "--all" in args:tiles=bundled_tiles()
 var folder="res://data"
 var classes=[]
 for arg in args:
  if arg.begins_with("--classes="):classes=Array(arg.trim_prefix("--classes=").split(",",false))
 for arg in args:
  if arg.begins_with("--dir="):
   folder=arg.trim_prefix("--dir=")
   if not "--tiles=" in " ".join(args):tiles=bundled_tiles(folder)
 var mats=materials()
 var collider_rows={};var surface_rows={};var tile_lines=[];var failed=0
 # Drive.gd gives every tile of a session the metre scale of the chosen place; use the first tile's row.
 var side=0.
 for id in tiles:
  var path=folder.path_join("%s.json"%id)
  if not FileAccess.file_exists(path):tile_lines.append("%s missing"%id);failed+=1;continue
  var data=JSON.parse_string(FileAccess.get_file_as_string(path))
  if not data is Dictionary or not data.has("roads") or not data.has("h"):tile_lines.append("%s unreadable"%id);failed+=1;continue
  if side==0.:side=EARTH/65536./cosh(PI*(1.-2.*(float(data.id[1])+.5)/65536.))
  var tile=TILE.new()
  var started=Time.get_ticks_usec()
  tile.build(data,side,mats)
  var build_ms=(Time.get_ticks_usec()-started)/1000.
  var faces=ground_faces(tile,side)
  if faces.is_empty():tile_lines.append("%s has no ground collider"%id);failed+=1;tile.free();continue
  var bin=side/float(data.get("grid",32))
  var index=index_faces(faces,bin)
  var tile_max=0.;var tile_creases=0;var tile_km=0.;var gap=0.
  for road in data.roads:
   if not classes.is_empty() and not str(road[0]) in classes:continue
   var name=str(road[2]) if str(road[2])!="" else "(unnamed %s)"%road[0]
   var stations=walk(road[3],side)
   if stations.size()<3:continue
   var on_collider=PackedFloat64Array();var on_surface=PackedFloat64Array()
   for p in stations:
    var z=height_on_faces(p,faces,index,bin);on_collider.append(z)
    var s=tile.height_at(p);on_surface.append(s)
    if not is_nan(z):gap=maxf(gap,absf(s-z))
   var local=blank();accumulate(local,on_collider)
   merge(collider_rows,name,local)
   var surface=blank();accumulate(surface,on_surface)
   merge(surface_rows,name,surface)
   tile_creases+=local.creases;tile_max=maxf(tile_max,local.max);tile_km+=local.m/1000.
  var smoothing=tile.info.get("road_smoothing",{})
  tile_lines.append("%s build %.0f ms · smoothing %s · %.2f km, max %.2f°/m, %d creases >%.1f°, height_at vs collider %.3f m"%[id,build_ms,JSON.stringify(smoothing) if not smoothing.is_empty() else "off",tile_km,tile_max,tile_creases,CREASE_DEG,gap])
  edges[Vector2i(int(data.id[0]),int(data.id[1]))]=tile_edges(tile.info.h,int(data.get("grid",32)))
  raw_edges[Vector2i(int(data.id[0]),int(data.id[1]))]=tile_edges(raw_heights(data),int(data.get("grid",32)))
  tile.free()
 print("ROAD PROFILE  step %.2f m · %d tiles · smoothing %s"%[step_m,tiles.size(),"OFF (--without-road-smoothing)" if "--without-road-smoothing" in args else "on"])
 for line in tile_lines:print("  "+line)
 report(collider_rows,"GROUND COLLIDER (tyre contact surface)")
 report(surface_rows,"height_at() (camera, props and paint)")
 var seam=seams()
 print("SEAMS %d shared edges between built tiles · largest difference in the source data %.3f m · largest difference introduced by the build %.6f m"%[seam.count,seam.raw,seam.introduced])
 quit(2 if seam.introduced>1e-3 else (1 if failed>0 else 0))

var edges={}
var raw_edges={}
# Source heights as build() receives them: the file grid with the authored roadbed applied.
func raw_heights(data:Dictionary)->Array:
 var h:Array=data.h.duplicate()
 var roadbed:Array=data.get("roadbed",[])
 for i in range(0,roadbed.size()-1,2):h[int(roadbed[i])]=roadbed[i+1]
 return h
# Boundary rows and columns of a built tile: west, east, north, south.
func tile_edges(h,n:int)->Dictionary:
 var west=PackedFloat64Array();var east=PackedFloat64Array();var north=PackedFloat64Array();var south=PackedFloat64Array()
 for k in n+1:
  west.append(h[k*(n+1)]);east.append(h[k*(n+1)+n]);north.append(h[k]);south.append(h[n*(n+1)+k])
 return {"west":west,"east":east,"north":north,"south":south}

# Two built neighbours must agree on every shared node as well as their source
# data did, or the car meets a step at the seam. Source disagreements are reported
# separately; the build must not add to them.
func seams()->Dictionary:
 var count=0;var raw=0.;var introduced=0.;var offenders=[]
 for id in edges:
  for pair in [[Vector2i(1,0),"east","west"],[Vector2i(0,1),"south","north"]]:
   var other=edges.get(id+pair[0])
   if other==null:continue
   var a:PackedFloat64Array=edges[id][pair[1]];var b:PackedFloat64Array=other[pair[2]]
   var ra:PackedFloat64Array=raw_edges[id][pair[1]];var rb:PackedFloat64Array=raw_edges[id+pair[0]][pair[2]]
   if a.size()!=b.size():continue
   count+=1
   var local=0.;var at=-1
   for k in a.size():
    raw=maxf(raw,absf(ra[k]-rb[k]))
    var added=absf(a[k]-b[k])-absf(ra[k]-rb[k])
    if added>local:local=added;at=k
   introduced=maxf(introduced,local)
   if local>1e-6:offenders.append("%s/%s %s node %d: %.6f vs %.6f (source %.3f vs %.3f)"%[id,id+pair[0],pair[1],at,a[at],b[at],ra[at],rb[at]])
 for line in offenders.slice(0,12):print("  seam mismatch "+line)
 return {"count":count,"raw":raw,"introduced":introduced}

func bundled_tiles(folder:="res://data")->Array:
 var out=[]
 for file in DirAccess.get_files_at(folder):
  if file.get_extension()=="json" and file.get_basename().is_valid_int()==false and file.count("_")==1:
   var parts=file.get_basename().split("_")
   if parts.size()==2 and parts[0].is_valid_int() and parts[1].is_valid_int():out.append(file.get_basename())
 out.sort()
 return out

# The streamer's material table without textures; geometry does not depend on them.
func materials()->Dictionary:
 var out={}
 for key in TILE.WALL+TILE.SHOP+["trim","awning0","awning1","awning2","iron","globe","curb","walk","signal_steel","signal_yellow","signal_dark","signal_go","fuel_red","fuel_green","fuel_white","fuel_steel","whitepaint","yellowpaint","doorframe","cap","roof","siding0","siding1","siding2","siding3","house_trim","house_glass"]:
  var m=StandardMaterial3D.new();m.cull_mode=BaseMaterial3D.CULL_DISABLED;out[key]=m
 return out

# Stations every step_m along the road, in tile metres.
func walk(route:Array,side:float)->PackedVector2Array:
 var out=PackedVector2Array()
 if route.size()<2:return out
 var acc=0.;var next=0.
 out.append(Vector2(route[0][0],route[0][1])*side);next=step_m
 for k in range(1,route.size()):
  var a=Vector2(route[k-1][0],route[k-1][1])*side;var b=Vector2(route[k][0],route[k][1])*side;var l=a.distance_to(b)
  if l<1e-9:continue
  while next<=acc+l+1e-9:
   out.append(a.lerp(b,(next-acc)/l));next+=step_m
  acc+=l
 return out

# Every concave collider of the tile: the ground and, where RoadSurface.gd built
# them, the road ribbons above it. A wheel ray meets the highest face.
func ground_faces(tile,side:float)->PackedVector3Array:
 var out=PackedVector3Array();var has_ground=false
 for body in tile.get_children():
  if not body is StaticBody3D:continue
  for col in body.get_children():
   if not col is CollisionShape3D or not col.shape is ConcavePolygonShape3D:continue
   var faces:PackedVector3Array=col.shape.get_faces()
   if faces.is_empty():continue
   var box=AABB(faces[0],Vector3.ZERO)
   for v in faces:box=box.expand(v)
   if box.size.x>side*.9 and box.size.z>side*.9:has_ground=true;out.append_array(faces)
   elif tile.road_surface!=null and box.size.y<400.:out.append_array(faces)
 return out if has_ground else PackedVector3Array()

func index_faces(faces:PackedVector3Array,bin:float)->Dictionary:
 var grid={}
 for f in range(0,faces.size(),3):
  var lo=Vector2(INF,INF);var hi=Vector2(-INF,-INF)
  for k in 3:
   var v=faces[f+k];lo=lo.min(Vector2(v.x,v.z));hi=hi.max(Vector2(v.x,v.z))
  for y in range(floori(lo.y/bin),floori(hi.y/bin)+1):
   for x in range(floori(lo.x/bin),floori(hi.x/bin)+1):
    var key=Vector2i(x,y)
    if not grid.has(key):grid[key]=[]
    grid[key].append(f)
 return grid

func height_on_faces(p:Vector2,faces:PackedVector3Array,index:Dictionary,bin:float)->float:
 var best=NAN
 for f in index.get(Vector2i(floori(p.x/bin),floori(p.y/bin)),[]):
  var a=faces[f];var b=faces[f+1];var c=faces[f+2]
  var ab=Vector2(b.x-a.x,b.z-a.z);var ac=Vector2(c.x-a.x,c.z-a.z);var ap=p-Vector2(a.x,a.z)
  var det=ab.cross(ac)
  if absf(det)<1e-9:continue
  var u=ap.cross(ac)/det;var v=ab.cross(ap)/det
  if u>=-1e-5 and v>=-1e-5 and u+v<=1.+1e-5:
   var h=a.y+u*(b.y-a.y)+v*(c.y-a.y)
   if is_nan(best) or h>best:best=h
 return best

func blank()->Dictionary:return {"m":0.,"n":0,"max":0.,"creases":0,"step":0.,"ss":0.,"hist":[0,0,0,0,0,0,0]}

func merge(rows:Dictionary,name:String,part:Dictionary)->void:
 var r=rows.get(name,blank())
 r.m+=part.m;r.n+=part.n;r.creases+=part.creases;r.ss+=part.ss;r.max=maxf(r.max,part.max);r.step=maxf(r.step,part.step)
 for b in BINS.size():r.hist[b]+=part.hist[b]
 rows[name]=r

# Slope change between consecutive steps, in degrees; a step with a missing sample resets.
func accumulate(row:Dictionary,z:PackedFloat64Array)->void:
 var previous=NAN
 row.m+=float(z.size()-1)*step_m
 for i in z.size()-1:
  if is_nan(z[i]) or is_nan(z[i+1]):previous=NAN;continue
  var angle=rad_to_deg(atan((z[i+1]-z[i])/step_m))
  row.step=maxf(row.step,absf(z[i+1]-z[i]))
  if not is_nan(previous):
   var change=absf(angle-previous)
   row.n+=1;row.ss+=change*change;row.max=maxf(row.max,change)
   if change>CREASE_DEG:row.creases+=1
   var b=BINS.size()-1
   while b>0 and change<BINS[b]:b-=1
   row.hist[b]+=1
  previous=angle

func report(rows:Dictionary,title:String)->void:
 print(title)
 print("%-24s %7s %8s %9s %8s %7s  %s"%["road","km","max°/m","creases/km","step m","rms°","0-.25 .25-.5 .5-1 1-2 2-4 4-8 8+"])
 var total={}
 for name in rows:
  line(name,rows[name]);merge(total,"ALL",rows[name])
 var all=total.get("ALL",blank())
 line("ALL",all)
 print("RESULT %s: creases_per_km=%.1f max_deg_per_m=%.2f rms_deg=%.3f max_step_m=%.3f tyre_vertical_mps_at_45mph=%.2f"%[title.split(" ")[0],all.creases/maxf(.001,all.m/1000.),all.max,sqrt(all.ss/maxf(1,all.n)),all.step,REFERENCE_MPS*sin(deg_to_rad(all.max))])

func line(name:String,r:Dictionary)->void:
 var hist=PackedStringArray()
 for v in r.hist:hist.append(str(v))
 print("%-24s %7.2f %8.2f %9.1f %8.3f %7.3f  %s"%[name.left(24),r.m/1000.,r.max,r.creases/maxf(.001,r.m/1000.),r.step,sqrt(r.ss/maxf(1,r.n))," ".join(hist)])
