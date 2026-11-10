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
##
## The one-surface gate. Each road is followed on its own level, as a wheel does: at every
## step the collider height is the highest driving face (ground, ribbon or deck, never a
## parapet or a building) within 1 m above the last one, so a road under an overpass stays
## under it and a bridge road stays on its deck. height_at(p, near_y) must give the same
## height within 0.1 m at every step of every road, and every metre of a road the map flags
## as a bridge must be on a deck. Every real deck end is also driven from 6 m out to 6 m on
## (largest step per 0.5 m and grade change per metre are reported), and every deck cut at a
## tile edge must meet the neighbour's part of the deck within 0.1 m (DECK SEAMS). Exit 3
## when the gate fails (SURFACE FAIL), after the seam check's exit 2.
const TILE=preload("res://WorldTile.gd")
const EARTH=40075016.68557849
# The bundled Miami Beach block (4 x 6 zoom-16 tiles), the offline start; --all gives the same set.
const DEFAULT_TILES=["18178_27905","18178_27906","18178_27907","18178_27908","18178_27909","18178_27910","18179_27905","18179_27906","18179_27907","18179_27908","18179_27909","18179_27910","18180_27905","18180_27906","18180_27907","18180_27908","18180_27909","18180_27910","18181_27905","18181_27906","18181_27907","18181_27908","18181_27909","18181_27910"]
const BINS=[0.,.25,.5,1.,2.,4.,8.]
const CREASE_DEG=.5
const REFERENCE_MPS=20.1  # 45 mph: the vertical speed a crease imposes on a tyre is v·sin(Δθ)
var step_m=1.
var side_m=0.

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
  if side==0.:side=EARTH/65536./cosh(PI*(1.-2.*(float(data.id[1])+.5)/65536.));side_m=side
  var tile=TILE.new()
  var started=Time.get_ticks_usec()
  tile.build(data,side,mats)
  var build_ms=(Time.get_ticks_usec()-started)/1000.
  var faces=ground_faces(tile,side)
  if faces.is_empty():tile_lines.append("%s has no ground collider"%id);failed+=1;tile.free();continue
  var bin=side/float(data.get("grid",32))
  var index=index_faces(faces,bin)
  var tile_max=0.;var tile_creases=0;var tile_km=0.;var gap=0.;var gap_where=""
  for road in data.roads:
   if not classes.is_empty() and not str(road[0]) in classes:continue
   var name=str(road[2]) if str(road[2])!="" else "(unnamed %s)"%road[0]
   var stations=walk(road[3],side)
   if stations.size()<3:continue
   var on_collider=PackedFloat64Array();var on_surface=PackedFloat64Array()
   # Follow the road on its own level, as a wheel does: each station's collider
   # height is the highest face within reach above the previous one, so a road
   # under an overpass stays on the road and a bridge road stays on its deck.
   var bridge=road.size()>6 and str(road[6])=="bridge"
   var level=NAN
   for p in stations:
    var near=level
    if is_nan(near):near=own_deck(tile,p,str(road[0])) if bridge else tile.height_at(p)
    var z=height_on_faces(p,faces,index,bin,near+TILE.LAYER_REACH_M);on_collider.append(z)
    var s=tile.height_at(p,near);on_surface.append(s)
    if is_nan(z):continue
    level=z
    if absf(s-z)>gap:gap=absf(s-z);gap_where="%s at (%.1f, %.1f): collider %.2f m, height_at %.2f m"%[name,p.x,p.y,z,s]
    if bridge:
     # A road the map puts on a bridge must be driving on a deck.
     bridge_m+=step_m
     var on_deck=false
     if tile.deck_surface!=null:
      for h in tile.deck_surface.heights(p):on_deck=on_deck or absf(h-z)<=GAP_LIMIT_M
     if not on_deck:
      off_deck_m+=step_m
      if off_deck.size()<12:off_deck.append("%s %s at (%.1f, %.1f), surface %.2f m, deck %s"%[id,name,p.x,p.y,z,"none" if tile.deck_surface==null or tile.deck_surface.heights(p).is_empty() else str(tile.deck_surface.heights(p))])
   if gap>GAP_LIMIT_M and gap_tiles.find(id)<0:gap_tiles.append(id)
   var local=blank();accumulate(local,on_collider)
   merge(collider_rows,name,local)
   var surface=blank();accumulate(surface,on_surface)
   merge(surface_rows,name,surface)
   tile_creases+=local.creases;tile_max=maxf(tile_max,local.max);tile_km+=local.m/1000.
  var smoothing=tile.info.get("road_smoothing",{})
  tile_lines.append("%s build %.0f ms · smoothing %s · %.2f km, max %.2f°/m, %d creases >%.1f°, height_at vs collider %.3f m%s"%[id,build_ms,JSON.stringify(smoothing) if not smoothing.is_empty() else "off",tile_km,tile_max,tile_creases,CREASE_DEG,gap," (%s)"%gap_where if gap>GAP_LIMIT_M else ""])
  worst_gap=maxf(worst_gap,gap)
  bridge_ends(tile,data,faces,index,bin,id)
  # Deck ends cut at the tile edge, as built, for the deck seam check.
  for d in tile.bridge_decks:
   for e in [0,d.path.size()-1]:
    var p:Vector3=d.path[e]
    if preload("res://BridgeDeck.gd").on_tile_edge(tile,p):deck_cuts.append([Vector2(float(data.id[0]),float(data.id[1]))*side+Vector2(p.x,p.z),p.y,id])
  edges[Vector2i(int(data.id[0]),int(data.id[1]))]=tile_edges(tile.info.h,int(data.get("grid",32)))
  raw_edges[Vector2i(int(data.id[0]),int(data.id[1]))]=tile_edges(raw_heights(data),int(data.get("grid",32)))
  tile.free()
 print("ROAD PROFILE  step %.2f m · %d tiles · smoothing %s"%[step_m,tiles.size(),"OFF (--without-road-smoothing)" if "--without-road-smoothing" in args else "on"])
 for line in tile_lines:print("  "+line)
 report(collider_rows,"GROUND COLLIDER (tyre contact surface)")
 report(surface_rows,"height_at() (camera, props and paint)")
 var seam=seams()
 print("SEAMS %d shared edges between built tiles · largest difference in the source data %.3f m · largest difference introduced by the build %.6f m"%[seam.count,seam.raw,seam.introduced])
 # The one-surface gate: along every road the tyres' collider and height_at()
 # must agree within 0.1 m, and a road mapped as a bridge must be on a deck.
 print("SURFACE AGREEMENT largest collider vs height_at difference %.3f m (limit %.2f) · %d tiles over · bridge roads %.0f m, %.0f m not on a deck"%[worst_gap,GAP_LIMIT_M,gap_tiles.size(),bridge_m,off_deck_m])
 for line in off_deck:print("  off deck "+line)
 print("BRIDGE ENDS %d ends driven 6 m either side · largest step %.3f m per 0.5 m · largest grade change %.2f°/m · %d ends where the deck is not reached%s"%[ends.count,ends.step,ends.kink,ends.missed,(" (worst %s)"%ends.where) if ends.where!="" else ""])
 for line in ends.missed_at.slice(0,12):print("  deck not reached "+line)
 # A deck cut at a tile edge must meet the neighbour's part of it at the same
 # height. The seam check above reads only the ground grid, so a step between
 # two tiles' decks (2.4-5 m on the Mackinac Bridge when mid-span decks took
 # the elevation model's lake level) passed unseen.
 var cut=deck_seams()
 print("DECK SEAMS %d deck cuts matched across tile edges · largest step %.3f m%s · %d with no deck on the other side"%[cut.matched,cut.step,(" (%s)"%cut.where) if cut.where!="" else "",cut.lone])
 var surface_failed=worst_gap>GAP_LIMIT_M or off_deck_m>0. or ends.missed>0 or cut.step>GAP_LIMIT_M
 if surface_failed:print("SURFACE FAIL")
 quit(2 if seam.introduced>1e-3 else (3 if surface_failed else (1 if failed>0 else 0)))

const GAP_LIMIT_M=.1
var worst_gap=0.
var deck_cuts=[]   # [world xz, height, tile id] of every deck end on a tile edge

# Match every deck cut with the other tiles' cuts within 0.5 m (the same point
# of the same deck seen from both sides of the edge). A cut with no partner on
# a loaded neighbour is counted: the deck ends in mid-air there.
func deck_seams()->Dictionary:
 var out={"matched":0,"step":0.,"where":"","lone":0}
 for a in deck_cuts:
  var best=-1;var best_d=.5
  for b in range(deck_cuts.size()):
   if deck_cuts[b][2]==a[2]:continue
   var d=a[0].distance_to(deck_cuts[b][0])
   if d<best_d:best_d=d;best=b
  if best<0:
   out.lone+=1;continue
  out.matched+=1
  var step=absf(a[1]-deck_cuts[best][1])
  if step>out.step:out.step=step;out.where="%s / %s: %.2f vs %.2f m"%[a[2],deck_cuts[best][2],a[1],deck_cuts[best][1]]
 return out
var gap_tiles=[]
var bridge_m=0.
var off_deck_m=0.
var off_deck=[]
var ends={"count":0,"step":0.,"kink":0.,"missed":0,"missed_at":[],"where":""}

# Where a bridge road starts: on the fitted deck of its own class under p (two
# decks can overlap there, a ramp above a street), else on the top surface.
func own_deck(tile,p:Vector2,cls:String)->float:
 for d in tile.bridge_decks:
  if d.cls!=cls:continue
  var h=preload("res://BridgeDeck.gd").polyline_height(d.path,p,d.width*.5)
  if not is_nan(h):return h
 return tile.height_at(p,INF)

func on_other_row(rows:Array,self_row:Array,p:Vector2)->bool:
 for row in rows:
  if row==self_row:continue
  var pts:Array=row[2]
  for k in range(1,pts.size()):
   var a=Vector2(pts[k-1][0],pts[k-1][1])*side_m;var b=Vector2(pts[k][0],pts[k][1])*side_m
   var ab=b-a;var l2=ab.length_squared()
   if l2<1e-6:continue
   if p.distance_to(a+ab*clampf((p-a).dot(ab)/l2,0.,1.))<=1.:return true
 return false

# Drive across every real deck end (not a cut at the tile edge) from 6 m out on
# the approach to 6 m onto the deck, on the collider as a wheel follows it.
func bridge_ends(tile,data:Dictionary,faces:PackedVector3Array,index:Dictionary,bin:float,id:String)->void:
 if tile.deck_surface==null:return
 for row in data.get("bridges",[]):
  var pts:Array=row[2]
  if pts.size()<2:continue
  for e in [0,pts.size()-1]:
   var at=Vector2(pts[e][0],pts[e][1])*side_m
   if at.x<=.05 or at.y<=.05 or at.x>=side_m-.05 or at.y>=side_m-.05:continue
   var other=Vector2(pts[1 if e==0 else pts.size()-2][0],pts[1 if e==0 else pts.size()-2][1])*side_m
   if other.distance_to(at)<.3:continue
   # A joint with another deck row (one bridge split where its class or
   # direction changes): straight on leads off the side of the other row.
   if on_other_row(data.bridges,row,at):continue
   var inward=(other-at).normalized()
   var reach=minf(6.,other.distance_to(at)) if pts.size()==2 else 6.
   var start=at-inward*6.
   var level=tile.height_at(start)
   var zs=PackedFloat64Array()
   var d=-6.
   while d<=reach+1e-6:
    var z=height_on_faces(at+inward*d,faces,index,bin,level+TILE.LAYER_REACH_M)
    if not is_nan(z):level=z
    zs.append(level)
    d+=.5
   ends.count+=1
   var deck_h=tile.deck_surface.heights(at+inward*reach)
   var reached=false
   for h in deck_h:reached=reached or absf(h-zs[-1])<=GAP_LIMIT_M
   if not reached:
    ends.missed+=1
    ends.missed_at.append("%s %s end at (%.1f, %.1f): surface %.2f m, deck %s"%[id,str(row[0]),at.x,at.y,zs[-1],str(deck_h)])
   for k in range(1,zs.size()):
    var step=absf(zs[k]-zs[k-1])
    if step>ends.step:ends.step=step;ends.where="%s %s end at (%.1f, %.1f)"%[id,str(row[0]),at.x,at.y]
   # Grade change per metre between consecutive 1 m chords.
   for k in range(4,zs.size(),1):
    var g1=rad_to_deg(atan((zs[k]-zs[k-2])/1.));var g0=rad_to_deg(atan((zs[k-2]-zs[k-4])/1.))
    ends.kink=maxf(ends.kink,absf(g1-g0))

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

# The tile's driving surfaces: the ground, the road ribbons (RoadSurface.gd) and
# the bridge decks (BridgeDeck.gd), as WorldTile.collider() tags them. Walls,
# parapets and buildings ("obstacles") are things a car hits, not surfaces it
# rolls on. Untagged colliders keep the old rule: the tile-wide one is ground,
# others count where a road ribbon exists.
func ground_faces(tile,side:float)->PackedVector3Array:
 var out=PackedVector3Array();var has_ground=false
 for body in tile.get_children():
  if not body is StaticBody3D:continue
  for col in body.get_children():
   if not col is CollisionShape3D or not col.shape is ConcavePolygonShape3D:continue
   var faces:PackedVector3Array=col.shape.get_faces()
   if faces.is_empty():continue
   var kind=str(col.get_meta("surface",""))
   if kind=="obstacles":continue
   if kind in ["ground","ribbon","deck"]:
    out.append_array(faces);has_ground=has_ground or kind=="ground"
    continue
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

# The highest face at p no higher than `below`: what a wheel ray cast down from
# there meets (INF: the top face).
func height_on_faces(p:Vector2,faces:PackedVector3Array,index:Dictionary,bin:float,below:=INF)->float:
 var best=NAN
 for f in index.get(Vector2i(floori(p.x/bin),floori(p.y/bin)),[]):
  var a=faces[f];var b=faces[f+1];var c=faces[f+2]
  var ab=Vector2(b.x-a.x,b.z-a.z);var ac=Vector2(c.x-a.x,c.z-a.z);var ap=p-Vector2(a.x,a.z)
  var det=ab.cross(ac)
  if absf(det)<1e-9:continue
  var u=ap.cross(ac)/det;var v=ab.cross(ap)/det
  if u>=-1e-4 and v>=-1e-4 and u+v<=1.+1e-4:
   var h=a.y+u*(b.y-a.y)+v*(c.y-a.y)
   if h<=below and (is_nan(best) or h>best):best=h
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
