extends SceneTree
## Road edges: the road ribbon owns the carriageway.
##   godot --headless --path game --script res://checks/RoadEdgeCheck.gd -- [--dir=/abs/tiles] [--tiles=X_Y,...]
##   godot --path game --script res://checks/RoadEdgeCheck.gd -- --capture=/abs/out --dir=/abs/tiles [--windows=4] [--tiles=X_Y,...]
##
## Headless (default suite): builds tiles through WorldTile.build exactly as
## the streamer does (the bundled Miami Beach tiles with a synthetic road
## ribbon, a mapped sidewalk beside one road and, on the second tile, no
## buildings; or every `atlas tile` X_Y.json in --dir) and measures, with the
## learned aerial mask set to "ground" everywhere (the worst a 1.9 m aerial
## classifier can say about shaded asphalt):
##   * carriageway grip: WorldTile.wheel_grip() at points across every paved
##     half of every ribbon; any value under 1 fails;
##   * carriageway lateral: RoadSurface.lateral() (the numbers the ribbon's UV2
##     carries into terrain.gdshader) must place every such point on the paved
##     half; any miss fails;
##   * edge lines on the asphalt: LaneMarkings.cross_section() for every class,
##     width, lane count, one-way and town case: an edge line reaching past the
##     paved half fails;
##   * the edge treatment: on the fixture a mapped sidewalk makes a kerb on its
##     own side only (and curb geometry), a built-up tile is EDGE_TOWN, an empty
##     one EDGE_RURAL with loose (0.76) grip on the gravel shoulder; town or
##     rural uses LaneMarkings' constants and its majority along the road
##     (changes between gravel shoulder and verge are reported per km for
##     --dir tiles);
##   * plan-view kinks (a station whose heading change exceeds both of its
##     neighbours' by more than KINK_DEG: a corner, not a curve) and edge wobble
##     (each paved edge's deviation from a quadratic fitted over +-10 m), per km
##     of ribbon, reported for --dir tiles and gated there (KINKS_PER_KM,
##     WOBBLE_RMS_M), since the fixture's ribbons are raw map chords.
## --capture (windowed, needs a GPU): builds the --dir tiles with their aerial
## photographs (from the game's map cache) and the learned edge mask, sets
## terrain.gdshader's edge_capture view (red = the pavement weight it drew,
## green = the ribbon, blue = gravel shoulder; unlit) and photographs WINDOW_M
## squares from straight above at 1024 px (4.7 cm a pixel) around road
## stations. Against the ribbon's own geometry it reports
## carriageway_ground_frac (carriageway pixels drawn as ground), poke_frac
## (carriageway pixels where the terrain covers the ribbon) and, along every
## paved edge, where the drawn edge is (mean offset from the paved half width,
## RMS wobble about a 20 m trend, largest step between stations). PNGs of each
## window and capture.json go to the --capture folder. This measures what the
## shader really drew, so it needs no copy of the shader's rules.
## Exit 0 when every measured value is within its gate and no script error was raised.
const TILE=preload("res://WorldTile.gd")
const SURFACE=preload("res://RoadSurface.gd")
const LANES=preload("res://LaneMarkings.gd")
const EDGES=preload("res://RoadEdges.gd")
const EARTH=40075016.68557849
const FIXTURE_TILES=["18180_27907","18180_27908"]
const KINK_DEG:=2.
const KINKS_PER_KM:=.5
const WOBBLE_RMS_M:=.05
const WINDOW_M:=48.
# --capture gates. Before the ribbon owned the carriageway (2026-11-10, six
# study tiles, 24 windows) the aerial classifier drew 5.0 % of it as ground and
# the drawn edge sat 0.27 m from the paved half on average, wobbling 0.056 m.
const CAPTURE_GROUND_FRAC:=.002
const CAPTURE_POKE_FRAC:=.01
const CAPTURE_EDGE_M:=.05
const PIXELS:=1024
var failures:=0
var guard

func expect(ok:bool,what:String)->void:
 if not ok:failures+=1;print("  FAIL "+what)

func _initialize():
 guard=preload("res://checks/ScriptErrorGuard.gd").install()
 var args:=OS.get_cmdline_user_args()
 var folder:="";var tiles:=[];var capture:="";var windows:=4
 for a in args:
  if a.begins_with("--dir="):folder=a.trim_prefix("--dir=")
  elif a.begins_with("--tiles="):tiles=Array(a.trim_prefix("--tiles=").split(",",false))
  elif a.begins_with("--capture="):capture=a.trim_prefix("--capture=")
  elif a.begins_with("--windows="):windows=maxi(1,int(a.get_slice("=",1)))
 if folder!="" and tiles.is_empty():
  for f in DirAccess.get_files_at(folder):
   if f.ends_with(".json") and f.get_basename().split("_").size()==2 and f.get_basename().split("_")[0].is_valid_int():tiles.append(f.get_basename())
 if capture!="":
  run_capture.call_deferred(folder,tiles,capture,windows)
  return
 rules()
 if folder=="":fixture()
 else:
  for id in tiles:measure_live(folder,id)
 finish()

func finish()->void:
 if guard.count()>0:failures+=1;print("  FAIL script errors: %d, first: %s"%[guard.count(),guard.first])
 print("ROAD EDGE CHECK %s (%d failures)"%["PASSED" if failures==0 else "FAILED",failures])
 quit(0 if failures==0 else 1)

func side_of(data:Dictionary)->float:
 return EARTH/65536./cosh(PI*(1.-2.*(float(data.id[1])+.5)/65536.))

func materials()->Dictionary:
 var out={}
 for key in TILE.WALL+TILE.SHOP+["trim","awning0","awning1","awning2","iron","globe","curb","walk","signal_steel","signal_yellow","signal_dark","signal_go","fuel_red","fuel_green","fuel_white","fuel_steel","whitepaint","yellowpaint","doorframe","cap","roof","siding0","siding1","siding2","siding3","house_trim","house_glass"]:
  var m=StandardMaterial3D.new();m.cull_mode=BaseMaterial3D.CULL_DISABLED;out[key]=m
 return out

## Every edge line inside the paved half, whatever the lanes ask for.
func rules()->void:
 var worst:=-INF;var cases:=0;var outside:=0
 for cls in ["motorway","trunk","primary","secondary","tertiary"]:
  for width in [5.5,6.,7.,8.,9.,9.3,10.,10.5,11.,11.4,12.,14.,16.,19.]:
   for total in range(0,7):
    for oneway in [false,true]:
     for town in [false,true]:
      var x:Dictionary=LANES.cross_section(cls,oneway,[total,0,0],width,town)
      if not x.edges:continue
      for line in x.lines.slice(0,2):
       cases+=1
       var reach:float=absf(float(line[0]))+float(line[3])*.5-width*.5
       worst=maxf(worst,reach)
       if reach>1e-4:outside+=1
 print("EDGE LINES %d edge lines over %d cross-sections · %d reach past the paved edge · closest %.3f m inside"%[cases,cases/2,outside,-worst])
 expect(outside==0,"edge lines past the paved edge: %d (by up to %.2f m)"%[outside,worst])
 var run:=PackedByteArray([1,0,0,1,0,0,0,0,0,0,0,1,1,1,1,1,0,1])
 SURFACE.fill_gaps(run,2)
 expect(Array(run)==[1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,1,1,1],"kerb gaps fill: %s"%[Array(run)])
 SURFACE.drop_short(run,5)
 expect(Array(run)==[0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1],"short kerb runs drop: %s"%[Array(run)])
 # Town or rural is LaneMarkings' rule, so the shoulder and the edge lines agree.
 expect(SURFACE.TOWN_CELL_M==LANES.HOME_CELL_M and SURFACE.TOWN_BUILDINGS==LANES.TOWN_BUILDINGS and SURFACE.TOWN_STREETS_M==LANES.TOWN_STREETS_M and SURFACE.TOWN_SHARE==LANES.TOWN_SHARE and SURFACE.TOWN_RUN_M==LANES.TOWN_RUN_M,"RoadSurface's town rule matches LaneMarkings'")
 # A built-up half with one open 50 m cell and an open half with one built-up
 # cell: one change, at the middle, not three.
 var raw:=PackedByteArray();var arc:=PackedFloat64Array()
 for k in 200:
  raw.append(1 if (k<100 and not (k>=30 and k<55)) or (k>=150 and k<175) else 0);arc.append(k*2.)
 var town:=SURFACE.majority(raw,arc,SURFACE.TOWN_RUN_M)
 var changes:=0
 for k in range(1,200):changes+=1 if town[k]!=town[k-1] else 0
 expect(changes==1 and town[40]==1 and town[160]==0,"town majority along a ribbon: %d changes, station 40 %d, station 160 %d"%[changes,town[40],town[160]])

## A bundled tile with a ribbon along every paved road (the helper's row
## format), a sidewalk mapped 3 m beyond the paved edge on the left of the
## longest major road; the second tile without buildings (rural).
func fixture()->void:
 var mats:=materials()
 for n in FIXTURE_TILES.size():
  var id:String=FIXTURE_TILES[n]
  var data=JSON.parse_string(FileAccess.get_file_as_string("res://data/%s.json"%id))
  if not data is Dictionary:expect(false,"%s unreadable"%id);continue
  var side:=side_of(data)
  var built:=ribbon_fixture(data,side,n==1)
  var tile=TILE.new()
  tile.build(built.data,side,mats)
  expect(tile.road_surface!=null,"%s builds a ribbon"%id)
  if tile.road_surface==null:tile.free();continue
  hostile_mask(tile)
  var m:=measure(tile,side,"%s fixture"%id)
  expect(m.grip_low==0,"%s: %d carriageway points with grip under 1 (lowest %.2f)"%[id,m.grip_low,m.grip_min])
  expect(m.off_ribbon==0,"%s: %d carriageway points the ribbon does not place on its paved half"%[id,m.off_ribbon])
  var kinds:=edge_kinds(tile.road_surface)
  if n==0:
   var line:Dictionary=tile.road_surface.lines[built.walk_line]
   var own:=0;var other:=0;var stations:=0
   var edge:PackedByteArray=line.edge
   for i in line.sections.size():
    var c:Vector3=line.sections[i][2]
    if c.x<8. or c.z<8. or c.x>side-8. or c.z>side-8.:continue
    if i<built.walk_from or i>built.walk_to:continue
    stations+=1
    if edge[i*2+1]==SURFACE.EDGE_KERB:own+=1
    if edge[i*2]==SURFACE.EDGE_KERB:other+=1
   print("  %s kerb: sidewalk side %d/%d stations, other side %d · edges %s · curb %s"%[id,own,stations,other,kinds,tile.info.get("kerbs",{})])
   expect(stations>0 and own>=stations*.8,"the mapped sidewalk's side is kerbed: %d of %d stations"%[own,stations])
   expect(other==0,"the side without a sidewalk is not kerbed: %d stations"%other)
   expect(float(tile.info.get("kerbs",{}).get("kerb_m",0.))>20.,"curb geometry along the sidewalk: %s"%[tile.info.get("kerbs",{})])
   expect(kinds.get("town",0)>0,"built-up stations are EDGE_TOWN: %s"%[kinds])
  else:
   print("  %s edges %s · shoulder grip %s"%[id,kinds,m.shoulder])
   expect(kinds.get("town",0)==0 and kinds.get("kerb",0)==0 and kinds.get("rural",0)>0,"a tile with no buildings is rural: %s"%[kinds])
   expect(m.shoulder.get("0.76",0)>0,"the rural gravel shoulder is loose (0.76): %s"%[m.shoulder])
  tile.free()

## The learned aerial mask saying "ground, not road" everywhere: grip on the
## ribbon must not depend on it.
func hostile_mask(tile)->void:
 var mask:=Image.create(256,256,false,Image.FORMAT_RG8)
 mask.fill(Color(0.,1.,0.))
 tile.learned_edges=mask

func edge_kinds(surface)->Dictionary:
 var out:={"rural":0,"town":0,"kerb":0,"town_rural_changes":0}
 for line in surface.lines:
  for v in line.edge:out[["rural","town","kerb"][v]]+=1
  # Changes between gravel shoulder and verge along each side (a kerb run
  # keeps the treatment before it).
  for s in 2:
   var last:=-1
   for i in line.sections.size():
    var v:int=line.edge[i*2+s]
    if v==SURFACE.EDGE_KERB:continue
    if last>=0 and v!=last:out.town_rural_changes+=1
    last=v
 return out

## Points across every paved half (and the shoulder band) of every ribbon.
func measure(tile,side:float,label:String)->Dictionary:
 var surface=tile.road_surface
 var out:={"points":0,"grip_low":0,"grip_min":1.,"off_ribbon":0,"shoulder":{}}
 for line in surface.lines:
  var sections:Array=line.sections
  for i in range(1,sections.size()):
   var a:Vector3=sections[i-1][2];var b:Vector3=sections[i][2]
   if Vector2(a.x,a.z).distance_to(Vector2(b.x,b.z))>=8.:continue
   var c:=(Vector2(a.x,a.z)+Vector2(b.x,b.z))*.5
   var na:=Vector2(sections[i-1][4].x-a.x,sections[i-1][4].z-a.z).normalized()
   var nb:=Vector2(sections[i][4].x-b.x,sections[i][4].z-b.z).normalized()
   var nrm:=(na+nb).normalized()
   var half:float=minf(line.halves[i-1],line.halves[i])
   for f in [-.98,-.6,-.2,.2,.6,.98]:
    var p:Vector2=c+nrm*half*f
    if p.x<1. or p.y<1. or p.x>side-1. or p.y>side-1.:continue
    out.points+=1
    var g:float=tile.wheel_grip(p)
    if g<1.:out.grip_low+=1;out.grip_min=minf(out.grip_min,g)
    var across:Vector3=surface.lateral(p)
    if is_nan(across.x) or absf(across.x)>across.y+.01:out.off_ribbon+=1
   for f in [-1.,1.]:
    var p:Vector2=c+nrm*f*(half+.5)
    if p.x<1. or p.y<1. or p.x>side-1. or p.y>side-1.:continue
    var across:Vector3=surface.lateral(p)
    if is_nan(across.x) or absf(across.x)<=across.y:continue
    var key:="%.2f"%tile.wheel_grip(p)
    out.shoulder[key]=out.shoulder.get(key,0)+1
 print("  %s: %d carriageway points, %d with grip under 1, %d off the paved half · shoulder grip %s"%[label,out.points,out.grip_low,out.off_ribbon,out.shoulder])
 return out

## Plan-view kinks and paved-edge wobble of every ribbon line.
func geometry(surface,side:float)->Dictionary:
 var out:={"km":0.,"kinks":0,"worst_kink":0.,"wobble_sq":0.,"wobble_n":0,"wobble_max":0.}
 for line in surface.lines:
  var sections:Array=line.sections
  var n:=sections.size()
  var heads:=PackedFloat64Array();var ok:=PackedByteArray()
  for i in range(1,n):
   var a:Vector3=sections[i-1][2];var b:Vector3=sections[i][2]
   var d:=Vector2(b.x-a.x,b.z-a.z)
   heads.append(atan2(d.y,d.x));ok.append(1 if d.length()>.5 and d.length()<4. else 0)
   if ok[i-1]==1 and inside(Vector2(a.x,a.z),side):out.km+=d.length()/1000.
  var turn:=PackedFloat64Array()
  for k in range(1,heads.size()):turn.append(absf(wrapf(heads[k]-heads[k-1],-PI,PI)) if ok[k]==1 and ok[k-1]==1 else -1.)
  for k in range(1,turn.size()-1):
   if turn[k-1]<0. or turn[k]<0. or turn[k+1]<0.:continue
   var c:Vector3=sections[k+1][2]
   if not inside(Vector2(c.x,c.z),side):continue
   var spike:=rad_to_deg(turn[k]-maxf(turn[k-1],turn[k+1]))
   out.worst_kink=maxf(out.worst_kink,spike)
   if spike>KINK_DEG:out.kinks+=1
  for e in [1,3]:
   for i in range(5,n-5):
    var c:Vector3=sections[i][e]
    if not inside(Vector2(c.x,c.z),side):continue
    var r:=edge_residual(sections,e,i)
    if is_nan(r):continue
    out.wobble_sq+=r*r;out.wobble_n+=1;out.wobble_max=maxf(out.wobble_max,absf(r))
 return out

func inside(p:Vector2,side:float)->bool:return p.x>=0. and p.y>=0. and p.x<=side and p.y<=side

## The paved edge's offset at station i from a quadratic fitted to it over
## +-5 stations (10 m each way): what is left is wobble, not curvature.
func edge_residual(sections:Array,e:int,i:int)->float:
 var p0:Vector3=sections[i-5][e];var p1:Vector3=sections[i+5][e]
 var t:=Vector2(p1.x-p0.x,p1.z-p0.z)
 if t.length()<12. or t.length()>30.:return NAN
 t=t.normalized();var nrm:=Vector2(-t.y,t.x)
 var o:=Vector2(sections[i][e].x,sections[i][e].z)
 var s0:=0.;var s1:=0.;var s2:=0.;var s3:=0.;var s4:=0.;var y0:=0.;var y1:=0.;var y2:=0.
 for j in range(i-5,i+6):
  var q:=Vector2(sections[j][e].x,sections[j][e].z)-o
  var s:=q.dot(t);var y:=q.dot(nrm)
  s0+=1.;s1+=s;s2+=s*s;s3+=s*s*s;s4+=s*s*s*s;y0+=y;y1+=s*y;y2+=s*s*y
 # Normal equations for y = a + b s + c s^2; the residual at s = 0 is -a.
 var m:=Basis(Vector3(s0,s1,s2),Vector3(s1,s2,s3),Vector3(s2,s3,s4))
 if absf(m.determinant())<1e-9:return NAN
 var abc:=m.inverse()*Vector3(y0,y1,y2)
 return -abc.x

func measure_live(folder:String,id:String)->void:
 var data=JSON.parse_string(FileAccess.get_file_as_string(folder.path_join("%s.json"%id)))
 if not data is Dictionary or not data.has("roads"):expect(false,"%s unreadable"%id);return
 var side:=side_of(data)
 var tile=TILE.new()
 tile.build(data,side,materials())
 if tile.road_surface==null:print("  %s: no road ribbon"%id);tile.free();return
 hostile_mask(tile)
 var m:=measure(tile,side,id)
 var g:=geometry(tile.road_surface,side)
 var rms:=sqrt(g.wobble_sq/maxi(g.wobble_n,1))
 print("  %s: %.2f km of ribbon · %d kinks over %.0f deg (%.2f per km, worst %.1f deg) · paved-edge wobble %.3f m RMS, %.3f m max · edges %s · curb %s"%[id,g.km,g.kinks,KINK_DEG,g.kinks/maxf(g.km,.001),g.worst_kink,rms,g.wobble_max,edge_kinds(tile.road_surface),tile.info.get("kerbs",{})])
 print("RESULT %s grip_below_1=%d off_ribbon=%d kinks_per_km=%.2f wobble_rms_m=%.3f town_rural_changes_per_km=%.2f"%[id,m.grip_low,m.off_ribbon,g.kinks/maxf(g.km,.001),rms,edge_kinds(tile.road_surface).town_rural_changes/maxf(g.km,.001)])
 expect(m.grip_low==0,"%s: %d carriageway points with grip under 1"%[id,m.grip_low])
 expect(m.off_ribbon==0,"%s: %d carriageway points off the paved half"%[id,m.off_ribbon])
 expect(g.kinks/maxf(g.km,.001)<=KINKS_PER_KM,"%s: %.2f plan-view kinks per km"%[id,g.kinks/maxf(g.km,.001)])
 expect(rms<=WOBBLE_RMS_M,"%s: paved-edge wobble %.3f m RMS"%[id,rms])
 tile.free()

## Synthetic ribbon rows along every paved road (ground + 0.7 m), a sidewalk
## beside the longest major road and, when rural, no buildings.
func ribbon_fixture(data:Dictionary,side:float,rural:bool)->Dictionary:
 var out:Dictionary=data.duplicate(true)
 var n:int=int(out.get("grid",32));var h:Array=out.h;var cell:=side/n
 var ground:=func(p:Vector2)->float:
  var g:=(p/cell).clamp(Vector2.ZERO,Vector2(n-.001,n-.001));var i:=int(g.x);var j:=int(g.y);var f:=g-Vector2(i,j)
  var a:=lerpf(float(h[j*(n+1)+i]),float(h[j*(n+1)+i+1]),f.x);var b:=lerpf(float(h[(j+1)*(n+1)+i]),float(h[(j+1)*(n+1)+i+1]),f.x)
  return lerpf(a,b,f.y)
 var chains:=[];var best:=-1;var best_len:=0.
 for ri in out.roads.size():
  var road:Array=out.roads[ri]
  var cls:=str(road[0])
  if cls not in ["motorway","trunk","primary","secondary","tertiary","minor"]:continue
  var route:Array=road[3]
  var pts:=PackedVector2Array()
  for j in range(1,route.size()):
   var a:=Vector2(route[j-1][0],route[j-1][1])*side;var b:=Vector2(route[j][0],route[j][1])*side
   var steps:=maxi(1,ceili(a.distance_to(b)/2.))
   for k in range(0 if pts.is_empty() else 1,steps+1):pts.append(a.lerp(b,float(k)/steps))
  if pts.size()<3:continue
  var rows:=[];var length:=0.
  for k in pts.size():
   if k>0:length+=pts[k].distance_to(pts[k-1])
   var t:=(pts[mini(k+1,pts.size()-1)]-pts[maxi(k-1,0)]).normalized()
   rows.append([pts[k].x/side,pts[k].y/side,ground.call(pts[k])+.7,0.,t.x,t.y,float(road[4])])
  if cls!="minor" and length>best_len:best_len=length;best=chains.size()
  chains.append({"cls":cls,"w":float(road[4]),"pts":rows})
 out["road_surface"]=chains
 out["live_canopy"]=false
 var walk_from:=0;var walk_to:=-1
 if best>=0:
  # 3 m beyond the left paved edge over the middle 60 % of the road.
  var rows:Array=chains[best].pts
  walk_from=int(rows.size()*.2);walk_to=int(rows.size()*.8)
  var walk:=[]
  for k in range(walk_from,walk_to+1):
   var r:Array=rows[k]
   var nrm:=Vector2(-float(r[5]),float(r[4]))
   var p:=Vector2(float(r[0]),float(r[1]))*side+nrm*(float(r[6])*.5+3.)
   walk.append([p.x/side,p.y/side])
  out["pedestrian"]=[["sidewalk",1.8,walk]]
  # The sidewalk's stations, a little inside its ends (the gap filling and
  # the shortest run work in stations).
  walk_from+=3;walk_to-=3
 # Rural: no buildings and no local street grid (LaneMarkings' town rule
 # counts either); the ribbons built above stay.
 if rural:out["buildings"]=[];out["pedestrian"]=[];out["roads"]=out.roads.filter(func(r):return str(r[0]) not in ["minor","service"])
 return {"data":out,"walk_line":best,"walk_from":walk_from,"walk_to":walk_to}

# ---------------------------------------------------------------------------
# --capture: what terrain.gdshader draws, photographed from above.

func run_capture(folder:String,ids:Array,out_dir:String,windows:int)->void:
 DirAccess.make_dir_recursive_absolute(out_dir)
 var view:=SubViewport.new()
 view.size=Vector2i(PIXELS,PIXELS);view.render_target_update_mode=SubViewport.UPDATE_ALWAYS
 view.msaa_3d=Viewport.MSAA_DISABLED;view.screen_space_aa=Viewport.SCREEN_SPACE_AA_DISABLED
 root.add_child(view)
 var camera:=Camera3D.new()
 camera.projection=Camera3D.PROJECTION_ORTHOGONAL;camera.size=WINDOW_M;camera.near=1.;camera.far=900.
 var env:=Environment.new()
 env.background_mode=Environment.BG_COLOR;env.background_color=Color.BLACK
 env.ambient_light_source=Environment.AMBIENT_SOURCE_COLOR;env.ambient_light_color=Color.BLACK;env.ambient_light_energy=0.
 env.reflected_light_source=Environment.REFLECTION_SOURCE_DISABLED
 env.tonemap_mode=Environment.TONE_MAPPER_LINEAR
 camera.environment=env
 view.add_child(camera)
 var report:={"windows":[],"tiles":{}}
 var total:={"carriage":0,"ground":0,"poke":0,"edges":[]}
 for id in ids:
  var data=JSON.parse_string(FileAccess.get_file_as_string(folder.path_join("%s.json"%id)))
  if not data is Dictionary or not data.has("roads"):print("  %s unreadable"%id);continue
  var side:=side_of(data)
  var tile=TILE.new()
  tile.build(data,side,materials())
  if tile.road_surface==null or tile.streaming_ground==null:print("  %s: no ribbon or no streamed ground"%id);tile.free();continue
  var classified:=false
  if tile.road_prior and tile.aerial_pixels:
   tile.accept_edges(EDGES.classify(tile.aerial_pixels,tile.road_prior));classified=true
  view.add_child(tile)
  # Only the ground (terrain and ribbon share its material) is photographed.
  for node in tile.find_children("*","GeometryInstance3D",true,false):
   var keep:bool=node is MeshInstance3D and node.mesh!=null and node.mesh.get_surface_count()>0 and node.mesh.surface_get_material(0)==tile.streaming_ground
   node.visible=keep
  tile.streaming_ground.set_shader_parameter("edge_capture",true)
  var spots:=pick_windows(tile.road_surface,side,windows)
  var tile_sum:={"carriage":0,"ground":0,"poke":0,"edges":[]}
  for spot in spots:
   var centre:Vector3=spot.centre
   camera.look_at_from_position(centre+Vector3.UP*400.,centre,Vector3.FORWARD)
   for f in 6:await process_frame
   await RenderingServer.frame_post_draw
   var img:=view.get_texture().get_image()
   img.convert(Image.FORMAT_RGB8)
   var name:="%s-%d"%[id,report.windows.size()]
   img.save_png(out_dir.path_join(name+".png"))
   img.srgb_to_linear()
   var w:=measure_window(tile,img,centre,side)
   w["name"]=name;w["tile"]=id;w["class"]=spot.cls;w["aerial"]=str(tile.info.get("aerial",""));w["classified"]=classified
   report.windows.append(w)
   for k in ["carriage","ground","poke"]:tile_sum[k]+=w[k];total[k]+=w[k]
   tile_sum.edges.append_array(w.edge_list);total.edges.append_array(w.edge_list)
   w.erase("edge_list")
   print("  %s %s: carriageway %d px, %.4f drawn as ground, %.4f terrain over the ribbon · edge offset %s"%[name,spot.cls,w.carriage,w.ground_frac,w.poke_frac,JSON.stringify(w.edge)])
  report.tiles[id]=summary(tile_sum)
  view.remove_child(tile);tile.free()
 report["total"]=summary(total)
 var file:=FileAccess.open(out_dir.path_join("capture.json"),FileAccess.WRITE)
 file.store_string(JSON.stringify(report,"  "))
 file=null
 var t:Dictionary=report.total
 print("RESULT capture carriageway_ground_frac=%.4f poke_frac=%.4f edge_offset_mean_m=%.3f edge_wobble_rms_m=%.3f edge_step_max_m=%.3f edges=%d"%[t.ground_frac,t.poke_frac,t.edge.mean_abs_m,t.edge.wobble_rms_m,t.edge.step_max_m,t.edge.n])
 expect(t.carriage>0,"no carriageway was photographed")
 expect(t.ground_frac<=CAPTURE_GROUND_FRAC,"carriageway drawn as ground: %.4f"%t.ground_frac)
 expect(t.poke_frac<=CAPTURE_POKE_FRAC,"terrain over the ribbon: %.4f"%t.poke_frac)
 expect(t.edge.mean_abs_m<=CAPTURE_EDGE_M,"drawn edge %.3f m from the paved half on average"%t.edge.mean_abs_m)
 expect(t.edge.wobble_rms_m<=WOBBLE_RMS_M,"drawn edge wobble %.3f m RMS"%t.edge.wobble_rms_m)
 finish()

func summary(s:Dictionary)->Dictionary:
 return {"carriage":s.carriage,"ground_frac":float(s.ground)/maxi(s.carriage,1),"poke_frac":float(s.poke)/maxi(s.carriage,1),"edge":edge_stats(s.edges)}

## Stations spread over the tile's longest major ribbons, at least 80 m apart
## and a window clear of the tile edge.
func pick_windows(surface,side:float,count:int)->Array:
 var rank:={"motorway":6,"trunk":5,"primary":4,"secondary":3,"tertiary":2,"minor":1}
 var order:=range(surface.lines.size())
 order.sort_custom(func(a,b):return rank.get(surface.lines[a].cls,0)*100000+surface.lines[a].sections.size()>rank.get(surface.lines[b].cls,0)*100000+surface.lines[b].sections.size())
 var out:=[]
 for li in order:
  var line:Dictionary=surface.lines[li]
  for i in range(0,line.sections.size(),10):
   var c:Vector3=line.sections[i][2]
   if c.x<WINDOW_M or c.z<WINDOW_M or c.x>side-WINDOW_M or c.z>side-WINDOW_M:continue
   var far:=true
   for o in out:
    if Vector2(o.centre.x,o.centre.z).distance_to(Vector2(c.x,c.z))<80.:far=false;break
   if far:out.append({"centre":c,"cls":line.cls})
   if out.size()>=count:return out
 return out

func pixel_of(centre:Vector3,p:Vector2)->Vector2:
 var mpp:=WINDOW_M/PIXELS
 return Vector2((p.x-centre.x)/mpp+PIXELS*.5-.5,(p.y-centre.z)/mpp+PIXELS*.5-.5)

func sample(img:Image,px:Vector2)->Color:
 var x0:=floori(px.x);var y0:=floori(px.y)
 if x0<0 or y0<0 or x0+1>=PIXELS or y0+1>=PIXELS:return Color(NAN,NAN,NAN)
 var fx:=px.x-x0;var fy:=px.y-y0
 return img.get_pixel(x0,y0).lerp(img.get_pixel(x0+1,y0),fx).lerp(img.get_pixel(x0,y0+1).lerp(img.get_pixel(x0+1,y0+1),fx),fy)

func measure_window(tile,img:Image,centre:Vector3,side:float)->Dictionary:
 var surface=tile.road_surface
 var mpp:=WINDOW_M/PIXELS
 var carriage:=0;var ground:=0;var poke:=0
 for py in range(0,PIXELS,2):
  for px in range(0,PIXELS,2):
   var p:=Vector2(centre.x+(px+.5-PIXELS*.5)*mpp,centre.z+(py+.5-PIXELS*.5)*mpp)
   var across:Vector3=surface.lateral(p)
   if is_nan(across.x) or absf(across.x)>across.y-.15:continue
   carriage+=1
   var c:=img.get_pixel(px,py)
   if c.r<.5:ground+=1
   if c.g<.5:poke+=1
 # Where the drawn edge is, along each paved edge in the window.
 var edges:=[]
 for li in surface.lines.size():
  var line:Dictionary=surface.lines[li]
  for s in [-1.,1.]:
   var run:=[]
   for i in line.sections.size():
    var c:Vector3=line.sections[i][2]
    var outer:Vector3=line.sections[i][4 if s>0. else 0]
    var nrm:=Vector2(outer.x-c.x,outer.z-c.z).normalized()
    var half:float=line.halves[i]
    var at:=Vector2(c.x,c.z)
    if absf(at.x-centre.x)>WINDOW_M*.42 or absf(at.y-centre.z)>WINDOW_M*.42:
     if not run.is_empty():edges.append(run);run=[]
     continue
    # Skip where another ribbon covers this edge (a junction).
    var probe:Vector3=surface.lateral(at+nrm*(half+.3))
    if not is_nan(probe.x) and absf(probe.x)<=probe.y:
     if not run.is_empty():edges.append(run);run=[]
     continue
    var found:=NAN;var last:=NAN
    for k in range(0,61):
     var o:=-.6+k*.02
     var v:=sample(img,pixel_of(centre,at+nrm*(half+o))).r
     if is_nan(v):break
     if not is_nan(last) and last>=.5 and v<.5:found=o-.02+.02*(last-.5)/maxf(last-v,1e-6);break
     last=v
    run.append([i,found])
   if not run.is_empty():edges.append(run)
 var stats:=edge_stats(edges)
 return {"carriage":carriage,"ground":ground,"poke":poke,"ground_frac":float(ground)/maxi(carriage,1),"poke_frac":float(poke)/maxi(carriage,1),"edge":stats,"edge_list":edges}

## Edge offsets: mean |offset|, RMS about a quadratic over +-10 stations (a
## 20 m trend), largest step between neighbouring stations, misses (no edge
## found within 0.6 m of the paved half).
func edge_stats(runs:Array)->Dictionary:
 var n:=0;var abs_sum:=0.;var sq:=0.;var m:=0;var step:=0.;var missed:=0
 for run in runs:
  for k in run.size():
   var v=run[k][1]
   if is_nan(v):missed+=1;continue
   n+=1;abs_sum+=absf(v)
   if k>0 and not is_nan(run[k-1][1]):step=maxf(step,absf(v-run[k-1][1]))
   var s0:=0.;var s1:=0.;var s2:=0.;var s3:=0.;var s4:=0.;var y0:=0.;var y1:=0.;var y2:=0.
   for j in range(maxi(0,k-5),mini(run.size(),k+6)):
    var y=run[j][1]
    if is_nan(y):continue
    var s:=float(j-k)
    s0+=1.;s1+=s;s2+=s*s;s3+=s*s*s;s4+=s*s*s*s;y0+=y;y1+=s*y;y2+=s*s*y
   if s0<5.:continue
   var b:=Basis(Vector3(s0,s1,s2),Vector3(s1,s2,s3),Vector3(s2,s3,s4))
   if absf(b.determinant())<1e-9:continue
   var abc:=b.inverse()*Vector3(y0,y1,y2)
   sq+=(v-abc.x)*(v-abc.x);m+=1
 return {"n":n,"missed":missed,"mean_abs_m":abs_sum/maxi(n,1),"wobble_rms_m":sqrt(sq/maxi(m,1)),"step_max_m":step}
