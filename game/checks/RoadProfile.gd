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
  var ribbon_mesh=tile.info.get("road_ribbon_mesh",{})
  tile_lines.append("%s build %.0f ms · smoothing %s · ribbon %s · %.2f km, max %.2f°/m, %d creases >%.1f°, height_at vs collider %.3f m"%[id,build_ms,JSON.stringify(smoothing) if not smoothing.is_empty() else "off",JSON.stringify(ribbon_mesh) if not ribbon_mesh.is_empty() else "none",tile_km,tile_max,tile_creases,CREASE_DEG,gap])
  edges[Vector2i(int(data.id[0]),int(data.id[1]))]=tile_edges(tile.info.h,int(data.get("grid",32)))
  raw_edges[Vector2i(int(data.id[0]),int(data.id[1]))]=tile_edges(raw_heights(data),int(data.get("grid",32)))
  tile.free()
 print("ROAD PROFILE  step %.2f m · %d tiles · smoothing %s"%[step_m,tiles.size(),"OFF (--without-road-smoothing)" if "--without-road-smoothing" in args else "on"])
 for line in tile_lines:print("  "+line)
 report(collider_rows,"GROUND COLLIDER (tyre contact surface)")
 report(surface_rows,"height_at() (camera, props and paint)")
 var seam=seams()
 print("SEAMS %d shared edges between built tiles · largest difference in the source data %.3f m · largest difference introduced by the build %.6f m"%[seam.count,seam.raw,seam.introduced])
 var ribbon_errors=ribbon_lane_frame()
 quit(2 if seam.introduced>1e-3 else (1 if failed>0 else (3 if ribbon_errors>0 else 0)))

# The bundled Miami tiles carry no road profile, so no ribbon is built from
# them. This builds two synthetic ribbons (RoadSurface.gd) and checks the lane
# frame mesh() hands the ground shader: lane_fit() never overhangs the
# carriageway, every vertex is flagged and carries its own lateral offset and
# the lane layout of its station's width (one ribbon narrows from 14 to 10 m),
# the street condition and distance along follow the exported piece under
# every 2 m station (a 6 m street with another name inside a chain included),
# a chain without that export takes its street from the tile's road index and,
# where the index has none, from the nearest station that found one (never the
# default), two chains do not start their distance along at the same value,
# and two seam copies of one road draw alike (ribbon_seam). Prints one RIBBON
# line.
func ribbon_lane_frame()->int:
 const SURFACE=preload("res://RoadSurface.gd")
 var errors=[]
 for k in 400:
  var half=.05*k
  var fit=SURFACE.lane_fit(half)
  if fit.x==0.:
   if half>=2.7-1e-4 or absf(fit.y-2.*half)>1e-4:errors.append("lane_fit(%.2f)=%s"%[half,fit])
  elif fit.x*fit.y>half+1e-4 or fit.y>3.6+1e-4 or fit.y<2.7-1e-4 or (fit.x>=2. and fit.y<3.15-1e-4) or half-fit.x*fit.y>=2.7:
   errors.append("lane_fit(%.2f)=%s"%[half,fit])
 var side=600.
 var rows=[]
 # Alpha is exported as the helper does now: each station names its part
 # (Alpha, a 6 m "Short Bridge" piece inside it, Alpha again) and carries its
 # distance along that piece (piece offsets 100, 700 and 1300 m). Bravo is a
 # tile cached before that export: no streets, no part, no width.
 var alpha_parts=[[50.,203.,100.],[203.,209.,700.],[209.,550.,1300.]]
 for spec in [["primary",14.,Vector2(50,300),Vector2(1,0)],["residential",5.6,Vector2(300,50),Vector2(0,1)]]:
  var pts=[]
  for i in 251:
   var c:Vector2=spec[2]+spec[3]*2.*i
   var row=[c.x/side,c.y/side,10.,0.,spec[3].x,spec[3].y]
   # Alpha carries a per-station width (14 m, then 10 m from x = 400 m), as
   # the helper exports where a chain joins roads of different widths.
   if spec[0]=="primary":
    row.append(14. if c.x<399. else 10.)
    var part=0 if c.x<203. else (1 if c.x<209. else 2)
    row.append_array([part,alpha_parts[part][2]+c.x-alpha_parts[part][0]])
   pts.append(row)
  var chain={"cls":spec[0],"w":spec[1],"pts":pts}
  if spec[0]=="primary":chain["streets"]=[["Alpha","primary",0],["Short Bridge","primary",0],["Alpha","primary",0]]
  rows.append(chain)
 # Streets under the ribbons as WorldTile.road_index() files them: Alpha with a
 # 6 m "Short Bridge" inside it, and Bravo across, whose road in this tile's
 # index ends at z = 400 m (the rest lies in the neighbouring tile).
 var segs=[[Vector2(50,300),Vector2(203,300),"Alpha",14.,0,"primary"],[Vector2(203,300),Vector2(209,300),"Short Bridge",14.,0,"primary"],
  [Vector2(209,300),Vector2(550,300),"Alpha",14.,0,"primary"],[Vector2(300,50),Vector2(300,400),"Bravo",5.6,0,"residential"]]
 var grid={}
 for seg in segs:
  var reach=Vector2.ONE*(float(seg[3])*.5+2.2);var lo=Vector2i(((seg[0].min(seg[1])-reach)/24.).floor());var hi=Vector2i(((seg[0].max(seg[1])+reach)/24.).floor())
  for x in range(lo.x,hi.x+1):
   for y in range(lo.y,hi.y+1):
    if not grid.has(Vector2i(x,y)):grid[Vector2i(x,y)]=[]
    grid[Vector2i(x,y)].append(seg)
 var tile={"road_grid":grid}
 const ROUGH=preload("res://RoadRoughness.gd")
 var short_condition=SURFACE.condition_of(ROUGH.street_iri("Short Bridge","primary"))
 var alpha_condition=SURFACE.condition_of(ROUGH.street_iri("Alpha","primary"))
 var bravo_condition=SURFACE.condition_of(ROUGH.street_iri("Bravo","residential"))
 if absf(short_condition-alpha_condition)<.01:errors.append("test streets share a condition (%.3f); rename one"%alpha_condition)
 if absf(bravo_condition-SURFACE.condition_of(2.4))<.01:errors.append("Bravo's condition equals the 2.4 default; rename it")
 var surface=SURFACE.new()
 surface.build(rows,side)
 var m=surface.mesh(StandardMaterial3D.new(),side,tile)
 var arrays=m.surface_get_arrays(0)
 var verts:PackedVector3Array=arrays[Mesh.ARRAY_VERTEX];var uv2:PackedVector2Array=arrays[Mesh.ARRAY_TEX_UV2];var colours:PackedColorArray=arrays[Mesh.ARRAY_COLOR]
 if verts.size()!=surface.quads.size()*6 or uv2.size()!=verts.size() or colours.size()!=verts.size():
  errors.append("%d vertices, %d UV2, %d colours for %d quads"%[verts.size(),uv2.size(),colours.size(),surface.quads.size()])
 var start_along={}
 var short_hits=0
 for v in verts.size():
  var p=verts[v]
  if uv2[v].x<SURFACE.LANE_FLAG*.5:errors.append("vertex %d not flagged"%v);break
  # Which ribbon a vertex belongs to, from where it is (Alpha runs along
  # z = 300 m, Bravo along x = 300 m); the crossing itself is ambiguous.
  var near_alpha=absf(p.z-300.)<=8.25;var near_bravo=absf(p.x-300.)<=4.05
  if near_alpha==near_bravo:continue
  var on_alpha=near_alpha
  var half=(7. if p.x<399. else 5.) if on_alpha else 2.8
  # Vertex colour is stored as 8-bit unorm (truncated): compare within one step.
  var fit=SURFACE.lane_fit(half)
  if absf(colours[v].r*8.-fit.y)>.035 or absf(colours[v].a*4.-fit.x)>.02 or absf(colours[v].b*16.-half)>.07:
   errors.append("vertex %d at %s lane layout %s, expected lanes %s half %.1f"%[v,p,colours[v],fit,half]);break
  # UV2.x is the vertex's own lateral offset from the centreline (left positive).
  var lateral=(p.z-300.) if on_alpha else (300.-p.x)
  if absf(uv2[v].x-SURFACE.LANE_FLAG-lateral)>2e-3:errors.append("vertex %d at %s lateral %.3f, placed at %.3f"%[v,p,uv2[v].x-SURFACE.LANE_FLAG,lateral]);break
  var station=Vector2(p.x,p.z) if absf(uv2[v].x-SURFACE.LANE_FLAG)<1e-3 else Vector2.ZERO
  # Where the two streets cross both are 0 m away and float noise picks one.
  if station!=Vector2.ZERO and station.distance_to(Vector2(300,300))>.5:
   # Alpha's stations take their exported street; Bravo's are looked up, and
   # beyond z = 400 m (no street in this tile) take the nearest that found one.
   var want=bravo_condition
   if on_alpha:want=short_condition if station.x>203.5 and station.x<208.5 else alpha_condition
   if absf(colours[v].g-want)>1.05/255.:errors.append("condition %.3f at %s, street gives %.3f"%[colours[v].g,station,want]);break
   if on_alpha and station.x>203.5 and station.x<208.5:
    short_hits+=1
   # Along Alpha the distance is that of the piece under the station.
   if on_alpha:
    var part=0 if station.x<203. else (1 if station.x<209. else 2)
    var expect=alpha_parts[part][2]+station.x-alpha_parts[part][0]
    var ok=absf(uv2[v].y-expect)<1e-3
    # The quad that ends on a piece's first station continues the previous piece.
    if not ok and part>0:ok=absf(uv2[v].y-(alpha_parts[part-1][2]+station.x-alpha_parts[part-1][0]))<1e-3
    if not ok:errors.append("along %.3f at %s, piece gives %.3f"%[uv2[v].y,station,expect]);break
  start_along[on_alpha]=minf(start_along.get(on_alpha,INF),uv2[v].y)
 if short_hits==0:errors.append("no station on the 6 m street carries its condition")
 if start_along.size()==2 and absf(start_along[true]-start_along[false])<1.:errors.append("both chains start at along %.1f m"%start_along[true])
 if int(surface.mesh_stats.get("legacy_stations",-1))!=251:errors.append("%s legacy stations, expected Bravo's 251"%surface.mesh_stats.get("legacy_stations"))
 var seam_errors=ribbon_seam(SURFACE)
 errors.append_array(seam_errors)
 print("RIBBON lane frame (synthetic, 2 chains, %d quads) · %s · seam copies %s · %s"%[surface.quads.size(),JSON.stringify(surface.mesh_stats),"agree" if seam_errors.is_empty() else "differ","ok" if errors.is_empty() else "FAILED: "+"; ".join(errors.slice(0,6))])
 return errors.size()

# One road ("Charlie", secondary) crossing the seam at x = 600 m between two
# tiles, as both export it: tile A walks its chain westward from its 6 m apron
# (so the chain runs against the piece) and has no road index at all, tile B
# walks it eastward from x = 594 m. The piece runs east from x = 500 m with
# its distance along starting at 900 m. Every vertex both tiles draw at the
# same world point must carry the same lateral offset, distance along and
# condition, so the two coplanar copies shade alike.
func ribbon_seam(SURFACE)->Array:
 var errors=[]
 var side=600.
 var drawn=[]
 for copy in [[606.,500.,Vector2(-1,0),Vector2.ZERO,1],[594.,700.,Vector2(1,0),Vector2(600,0),0]]:
  var pts=[]
  var x:float=copy[0]
  while (x-copy[1])*copy[2].x<=1e-6:
   var local:Vector2=Vector2(x,150.)-copy[3]
   pts.append([local.x/side,local.y/side,10.,0.,copy[2].x,copy[2].y,7.,0,900.+x-500.])
   x+=2.*copy[2].x
  var surface=SURFACE.new()
  surface.build([{"cls":"secondary","w":7.,"pts":pts,"streets":[["Charlie","secondary",copy[4]]]}],side)
  var m=surface.mesh(StandardMaterial3D.new(),side,{"road_grid":{}})
  var arrays=m.surface_get_arrays(0)
  var at={}
  var verts:PackedVector3Array=arrays[Mesh.ARRAY_VERTEX];var uv2:PackedVector2Array=arrays[Mesh.ARRAY_TEX_UV2];var colours:PackedColorArray=arrays[Mesh.ARRAY_COLOR]
  for v in verts.size():
   var world:Vector3=verts[v]+Vector3(copy[3].x,0,copy[3].y)
   at[Vector2i((Vector2(world.x,world.z)*100.).round())]=[uv2[v],colours[v].g]
  drawn.append(at)
 var shared=0
 for key in drawn[0]:
  if not drawn[1].has(key):continue
  var a=drawn[0][key];var b=drawn[1][key]
  # Only stations inside both copies (a quad's far end may continue the other way).
  var x=key.x/100.
  if x<596. or x>604.:continue
  shared+=1
  if absf(a[0].x-b[0].x)>1e-3 or absf(a[0].y-b[0].y)>1e-3 or absf(a[1]-b[1])>1e-3:
   errors.append("seam copies differ at %s: %s / %.3f vs %s / %.3f"%[Vector2(key)/100.,a[0],a[1],b[0],b[1]]);break
 if shared<10:errors.append("seam copies share only %d vertices"%shared)
 return errors

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
