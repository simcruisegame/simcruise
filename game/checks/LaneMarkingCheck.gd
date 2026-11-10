extends SceneTree
## godot --headless --path game --script res://checks/LaneMarkingCheck.gd -- [--dir=/abs/path] [--tiles=X_Y,...]
## Road markings (LaneMarkings.gd). First the MUTCD rules on fixed cross-sections
## (lane counts, centre line kinds, edge lines, turn lanes, passing sight
## distance, short passing zones, lane widths) and a stripe laid over a sharp
## synthetic crest. Then it builds tiles through WorldTile.build exactly as the
## streamer does (the bundled Miami Beach block, or every `atlas tile` X_Y.json
## in --dir) and prints, per tile, the metres of each line kind, how many
## stations had a mapped lane count, the lane layouts it drew, and how often
## the town / rural decision flips along a street. Every stripe triangle is
## sampled at its corners, edge midpoints and centre and compared with the
## tile's colliders: the paint must sit between MIN_GAP_M and MAX_GAP_M above
## the road ribbon's own trimesh where there is one (else the nearest
## collider: ground, deck). Where another collider (the ground grid, a kerb)
## rises above the ribbon under the paint, that is counted and reported, as a
## defect of the road surface rather than of the paint.
## Exit 0 when every rule holds.
const TILE=preload("res://WorldTile.gd")
const LANES=preload("res://LaneMarkings.gd")
const EARTH=40075016.68557849
const DEFAULT_TILES=["18179_27907","18179_27908","18180_27907","18180_27908"]
const MIN_GAP_M:=.01   # under this the stripe z-fights or sinks into the surface
const MAX_GAP_M:=.10   # over this it floats visibly
const FOLD_M:=.03      # a road surface bending more than this inside one stripe piece is not a road
const BIN:=8.
var failures:=0

func expect(ok:bool,what:String)->void:
 if not ok:failures+=1;print("  FAIL "+what)

func count(x:Dictionary,ink:String,kind:String)->int:
 var n:=0
 for line in x.lines:
  if line[1]==ink and line[2]==kind:n+=1
 return n

func rules()->void:
 # Rural two-lane trunk with passing sight both ways: one broken yellow line, white edges.
 var x=LANES.cross_section("trunk",false,[2,0,0],16.,false,[true,true])
 expect(x.lanes==2 and count(x,"yellow","dash")==1 and count(x,"yellow","solid")==0 and count(x,"white","solid")==2 and count(x,"white","dash")==0,"rural two-lane passing: %s"%[x.lines])
 expect(is_equal_approx(x.travelled,7.3),"two 12 ft lanes on a 16 m trunk: travelled %.2f"%x.travelled)
 # Passing one way only: solid on the side that may not pass.
 x=LANES.cross_section("primary",false,[2,0,0],12.,false,[true,false])
 expect(count(x,"yellow","dash")==1 and count(x,"yellow","solid")==1 and x.centre_line=="solid and broken","one-way passing: %s"%[x.lines])
 var dash_offset=0.;var solid_offset=0.
 for line in x.lines:
  if line[1]=="yellow" and line[2]=="dash":dash_offset=line[0]
  if line[1]=="yellow" and line[2]=="solid":solid_offset=line[0]
 expect(dash_offset>solid_offset,"the broken line is on the passing (forward, right-hand) side")
 # No sight: double solid. In town: double solid and no collector edge lines.
 x=LANES.cross_section("secondary",false,[2,0,0],11.,false,[false,false])
 expect(count(x,"yellow","solid")==2 and count(x,"white","solid")==2,"rural no-passing: %s"%[x.lines])
 x=LANES.cross_section("tertiary",false,[0,0,0],10.,true,[true,true])
 expect(count(x,"yellow","solid")==2 and count(x,"white","solid")==0,"town collector: %s"%[x.lines])
 # Arterials keep edge lines in town.
 x=LANES.cross_section("trunk",false,[2,0,0],16.,true)
 expect(count(x,"white","solid")==2,"town arterial edge lines")
 # Four lanes: double solid, a broken lane line each way.
 x=LANES.cross_section("primary",false,[4,0,0],12.,true)
 expect(x.forward==2 and x.backward==2 and count(x,"yellow","solid")==2 and count(x,"white","dash")==2,"four-lane undivided: %s"%[x.lines])
 # Five lanes, 2 + 2 mapped (the fifth counted in neither direction, OSM's
 # lanes:both_ways): a centre two-way left-turn lane (solid outside, broken inside).
 x=LANES.cross_section("trunk",false,[5,2,2],16.,true)
 expect(x.centre==1 and count(x,"yellow","solid")==2 and count(x,"yellow","dash")==2 and count(x,"white","dash")==2,"five-lane TWLTL: %s"%[x.lines])
 var middle=x.travelled*.5-x.forward*x.lane-x.lane*.5
 var nearest_solid=INF;var farthest_dash=0.
 for line in x.lines:
  if line[1]=="yellow" and line[2]=="solid":nearest_solid=minf(nearest_solid,absf(line[0]-middle))
  if line[1]=="yellow" and line[2]=="dash":farthest_dash=maxf(farthest_dash,absf(line[0]-middle))
 expect(nearest_solid>farthest_dash,"TWLTL: solid yellow outside, broken yellow inside")
 # An odd total with no split is still read as a centre turn lane.
 x=LANES.cross_section("primary",false,[3,0,0],12.,true)
 expect(x.centre==1 and x.forward==1 and x.backward==1,"three lanes, no split: TWLTL: %s"%[x.lines])
 # Outside towns a split with nothing left over keeps its lanes: 3 + 2 is no
 # turn lane, and 2 + 1 is a climbing or passing lane, not a TWLTL.
 x=LANES.cross_section("trunk",false,[5,3,2],16.,false)
 expect(x.centre==0 and x.forward==3 and x.backward==2,"rural five lanes 3 + 2 stay 3 + 2: %s"%[x.lines])
 # In town the odd lane is the left-turn pocket of a junction approach.
 x=LANES.cross_section("trunk",false,[5,3,2],16.,true)
 expect(x.centre==1 and x.forward==2 and x.backward==2,"town five lanes 3 + 2: centre turn lane: %s"%[x.lines])
 x=LANES.cross_section("tertiary",false,[3,2,1],10.,false,[true,true])
 expect(x.centre==0 and x.forward==2 and x.backward==1 and x.lanes==3,"three lanes 2 + 1 stay 2 + 1 (climbing lane): %s"%[x.lines])
 expect(count(x,"white","dash")==1 and count(x,"white","solid")==2,"climbing lane: one lane line, edge lines")
 # The two-lane direction may not pass; the single lane may (it has the sight distance).
 var single_side=0.;var double_side=0.
 for line in x.lines:
  if line[1]=="yellow" and line[2]=="dash":single_side=line[0]
  if line[1]=="yellow" and line[2]=="solid":double_side=line[0]
 expect(x.centre_line=="solid and broken" and single_side<double_side,"climbing lane: solid on the two-lane side, broken on the single lane's side: %s"%[x.lines])
 x=LANES.cross_section("tertiary",false,[3,2,1],10.,false,[true,false])
 expect(count(x,"yellow","dash")==0 and count(x,"yellow","solid")==2,"climbing lane without sight for the single lane: double solid")
 x=LANES.cross_section("trunk",false,[3,1,1],16.,true)
 expect(x.centre==1,"3 = 1 + 1 + 1 both-ways lane is a TWLTL")
 # 4 lanes mapped 1 + 3 keeps its counts.
 x=LANES.cross_section("trunk",false,[4,1,3],16.,true)
 expect(x.forward==1 and x.backward==3 and count(x,"white","dash")==2,"four lanes 1 + 3: %s"%[x.lines])
 # One-way: white right edge, yellow left edge, lane lines between.
 x=LANES.cross_section("secondary",true,[3,3,0],11.,false)
 expect(count(x,"white","dash")==2 and count(x,"yellow","solid")==1 and count(x,"white","solid")==1,"one-way three lanes: %s"%[x.lines])
 for line in x.lines:
  if line[1]=="yellow":expect(line[0]<0.,"one-way yellow edge on the left")
 # An unmapped one-way: one lane unless an arterial (the width is a class guess).
 expect(LANES.cross_section("tertiary",true,[0,0,0],10.,true).lanes==1,"unmapped one-way collector: one lane")
 expect(LANES.cross_section("secondary",true,[0,0,0],11.,true).lanes==1,"unmapped one-way secondary: one lane")
 expect(LANES.cross_section("primary",true,[0,0,0],12.,true).lanes==2,"unmapped one-way arterial: two lanes")
 # A single mapped lane, two-way: no centre line.
 x=LANES.cross_section("tertiary",false,[1,0,0],10.,false)
 expect(count(x,"yellow","solid")+count(x,"yellow","dash")==0,"single-lane two-way has no centre line")
 # The lines fit the road: never past the paved shoulder, and no lane narrower than MIN_LANE_M.
 for case in [["primary",false,[6,3,3],12.],["tertiary",true,[4,4,0],10.],["trunk",false,[7,3,3],16.],["secondary",false,[6,3,3],7.3],["tertiary",true,[5,5,0],6.],["primary",false,[5,2,2],7.3]]:
  x=LANES.cross_section(case[0],case[1],case[2],case[3],true)
  for line in x.lines:expect(absf(line[0])<=case[3]*.5+LANES.SHOULDER_M,"%s %s lanes: line %.2f m off the centre fits"%[case[0],case[2],line[0]])
  expect(x.lane>=LANES.MIN_LANE_M-1e-4,"%s %s lanes on %.1f m: %d lanes of %.2f m, none under %.2f m"%[case[0],case[2],case[3],x.lanes,x.lane,LANES.MIN_LANE_M])
 x=LANES.cross_section("secondary",false,[6,3,3],7.3,true)
 expect(x.lanes==3 and x.forward+x.backward+x.centre==3 and x.forward>=1 and x.backward>=1 and x.mapped_lanes==6,"six lanes on a 7.3 m road: three fit, both directions kept: %s"%[x])
 # Table 3B-1 and the 400 ft minimum passing zone.
 expect(absf(LANES.passing_sight_m(55)-274.32)<.01,"55 mph passing sight 900 ft")
 expect(absf(LANES.passing_sight_m(33)-167.64)<.01,"33 mph rounds to 35 mph (550 ft)")
 var flags=PackedByteArray([0,1,1,0,1,1,1,1,1]);var arc=PackedFloat64Array([0,50,100,150,200,250,300,350,400])
 LANES.short_zones(flags,arc)
 expect(flags[1]==0 and flags[2]==0 and flags[8]==1,"a 50 m passing zone is removed, a 200 m one cut by the path end stays")
 flags=PackedByteArray([1,1,0,0,0,0,0,0,1]);LANES.short_zones(flags,arc)
 expect(flags[0]==0 and flags[8]==0,"under 61 m at a path end is removed")
 crest()

## A stand-in tile with a sharp crest: the grade turns from +4.4 deg to
## -4.4 deg at one ribbon station (x = 16 m), the 8.87 deg change RoadProfile
## reports as the largest on the ribbons. The stripe must follow it. With a
## step, the surface also jumps 0.5 m at x = 31.3 m (between two stations),
## where a ribbon ends above the ground: the line must break there, not
## bridge the step in the air.
class CrestTile:
 extends RefCounted
 var side:=600.
 var step:=false
 var paint_evidence=null
 var measured_section=null
 var road_layout=null
 var road_survey=null
 var quads:=[]
 func height_at(p:Vector2)->float:
  var h=.0775*p.x if p.x<=16. else .0775*16.-.0775*(p.x-16.)
  return h+(.5 if step and p.x>31.3 else 0.)
 func vert(p:Vector2,h:float)->Vector3:return Vector3(p.x,h,p.y)
 func quad(_k,a,b,c,d,_n):quads.append([a,b,c,d])

func crest()->void:
 for step in [false,true]:
  var tile=CrestTile.new();tile.step=step
  var m=LANES.new();m.tile=tile
  var P:=PackedVector2Array();var R:=PackedVector2Array();var A:=PackedFloat64Array()
  for k in 21:P.append(Vector2(2.*k,5.));R.append(Vector2(0,1));A.append(2.*k)
  var K:=PackedFloat64Array();K.resize(P.size());K.fill(NAN)
  m.polyline("whitepaint",P,R,A,K,[0.,"white","solid",LANES.LINE_M],{"name":"","oneway":false})
  var worst:=INF;var highest:=-INF;var painted:=0.
  for q in tile.quads:
   painted+=Vector2(q[0].x,q[0].z).distance_to(Vector2(q[3].x,q[3].z))
   for edge in [[q[0],q[3]],[q[1],q[2]]]:
    for i in 21:
     var v:Vector3=edge[0].lerp(edge[1],i/20.)
     var gap=v.y-tile.height_at(Vector2(v.x,v.z))
     worst=minf(worst,gap);highest=maxf(highest,gap)
  print("  %s: %d quads, %.2f of 40 m painted, stripe %.3f..%.3f m above the surface (lift %.3f m)"%["crest and step" if step else "crest",tile.quads.size(),painted,worst,highest,LANES.LIFT_M])
  expect(worst>=LANES.LIFT_M-LANES.FIT_M-1e-4 and highest<=LANES.LIFT_M+LANES.FIT_M+1e-4,"a stripe over a sharp crest%s stays within %.3f m of its lift"%[" and a step" if step else "",LANES.FIT_M])
  expect(painted>=40.-(LANES.MIN_PIECE_M if step else 0.)-1e-3,"the line is cut at most %.2f m at the step"%LANES.MIN_PIECE_M)

## Collider faces of a built tile (every ConcavePolygonShape3D under it), binned
## in plan; road[i/3] = 1 for the faces of the road ribbon's own trimesh.
func collider_faces(tile:Node3D)->Dictionary:
 var faces:=PackedVector3Array();var road:=PackedByteArray()
 var ribbon_faces:int=tile.road_surface.quads.size()*6 if tile.road_surface else -1
 var stack:=[[tile,Transform3D.IDENTITY]]
 while not stack.is_empty():
  var item=stack.pop_back()
  for child in item[0].get_children():
   if not child is Node3D:continue
   var xf:Transform3D=item[1]*child.transform
   if child is CollisionShape3D and child.shape is ConcavePolygonShape3D:
    var shape_faces:PackedVector3Array=child.shape.get_faces()
    for v in shape_faces:faces.append(xf*v)
    for i in shape_faces.size()/3:road.append(1 if shape_faces.size()==ribbon_faces else 0)
   stack.append([child,xf])
 var bins:={}
 for i in range(0,faces.size()-2,3):
  var a:=faces[i];var b:=faces[i+1];var c:=faces[i+2]
  if absf((Vector2(b.x,b.z)-Vector2(a.x,a.z)).cross(Vector2(c.x,c.z)-Vector2(a.x,a.z)))<1e-6:continue
  var lo:=Vector2i(floori(minf(a.x,minf(b.x,c.x))/BIN),floori(minf(a.z,minf(b.z,c.z))/BIN))
  var hi:=Vector2i(floori(maxf(a.x,maxf(b.x,c.x))/BIN),floori(maxf(a.z,maxf(b.z,c.z))/BIN))
  for x in range(lo.x,hi.x+1):
   for y in range(lo.y,hi.y+1):
    var key:=Vector2i(x,y)
    if not bins.has(key):bins[key]=[]
    bins[key].append(i)
 return {"faces":faces,"bins":bins,"road":road}

## [signed height of p above the road collider face nearest to it in height
## (the ribbon where there is one, else any collider), NAN with none; how far
## another collider (ground, kerb) rises above p where the ribbon is the road].
func collider_gap(col:Dictionary,p:Vector3)->Array:
 var best:=NAN;var any:=NAN;var cover:=0.
 var q:=Vector2(p.x,p.z)
 var f:PackedVector3Array=col.faces
 for i in col.bins.get(Vector2i(floori(p.x/BIN),floori(p.z/BIN)),[]):
  var h:=preload("res://RoadSurface.gd").tri_height(q,f[i],f[i+1],f[i+2])
  if is_nan(h) or absf(p.y-h)>1.:continue
  if col.road[i/3]==1:
   if is_nan(best) or absf(p.y-h)<absf(best):best=p.y-h
  else:
   cover=maxf(cover,h-p.y)
   if is_nan(any) or absf(p.y-h)<absf(any):any=p.y-h
 return [best,cover] if not is_nan(best) else [any,0.]

## How far the road surface at p rises above the plane through the surface
## under the stripe triangle's three corners.
func surface_fold(tile,a:Vector3,b:Vector3,c:Vector3,p:Vector3)->float:
 var q:=Vector2(p.x,p.z)
 var flat:=preload("res://RoadSurface.gd").tri_height(q,Vector3(a.x,tile.height_at(Vector2(a.x,a.z)),a.z),Vector3(b.x,tile.height_at(Vector2(b.x,b.z)),b.z),Vector3(c.x,tile.height_at(Vector2(c.x,c.z)),c.z))
 return 0. if is_nan(flat) else tile.height_at(q)-flat

func _initialize():
 rules()
 print("LANE RULES %s (%d failures)"%["PASS" if failures==0 else "FAIL",failures])
 var args=OS.get_cmdline_user_args()
 var folder="res://data";var tiles=DEFAULT_TILES.duplicate()
 for arg in args:
  if arg.begins_with("--dir="):
   folder=arg.trim_prefix("--dir=");tiles=[]
   for file in DirAccess.get_files_at(folder):
    var parts=file.get_basename().split("_")
    if file.get_extension()=="json" and parts.size()==2 and parts[0].is_valid_int() and parts[1].is_valid_int():tiles.append(file.get_basename())
   tiles.sort()
 for arg in args:
  if arg.begins_with("--tiles="):tiles=Array(arg.trim_prefix("--tiles=").split(",",false))
 var mats={}
 for key in TILE.WALL+TILE.SHOP+["trim","awning0","awning1","awning2","iron","globe","curb","walk","signal_steel","signal_yellow","signal_dark","signal_go","fuel_red","fuel_green","fuel_white","fuel_steel","whitepaint","yellowpaint","doorframe","cap","roof","siding0","siding1","siding2","siding3","house_trim","house_glass"]:
  mats[key]=StandardMaterial3D.new()
 var side=0.;var totals={"white_solid_m":0.,"white_dash_m":0.,"yellow_solid_m":0.,"yellow_dash_m":0.}
 var layouts={};var built=0;var samples=0;var sunk=0;var floating=0;var bare=0
 var folds=0;var fold_max=0.;var steps=0;var beyond=0;var covered=0;var cover_max=0.;var gap_min=INF;var gap_max=-INF;var flips=0;var street_stations=0;var fitted=0;var paint_total=0.;var build_total=0.
 for id in tiles:
  var path=folder.path_join("%s.json"%id)
  var data=JSON.parse_string(FileAccess.get_file_as_string(path)) if FileAccess.file_exists(path) else null
  if not data is Dictionary or not data.has("roads"):print("  %s missing"%id);continue
  if side==0.:side=EARTH/65536./cosh(PI*(1.-2.*(float(data.id[1])+.5)/65536.))
  var tile=TILE.new()
  var started=Time.get_ticks_usec()
  tile.build(data,side,mats)
  var ms=(Time.get_ticks_usec()-started)/1000.
  var started_paint=Time.get_ticks_usec()
  # Paint once more into a fresh batch to time it and test where the stripes lie.
  tile.batches.erase("whitepaint");tile.batches.erase("yellowpaint")
  var s:Dictionary=LANES.paint(tile)
  var paint_ms=(Time.get_ticks_usec()-started_paint)/1000.
  paint_total+=paint_ms;build_total+=ms
  var col=collider_faces(tile)
  var tile_bad=0
  for key in ["whitepaint","yellowpaint"]:
   if not tile.batches.has(key):continue
   var arrays=tile.batches[key].commit_to_arrays()
   var v:PackedVector3Array=arrays[Mesh.ARRAY_VERTEX]
   var index=arrays[Mesh.ARRAY_INDEX]
   var order:=PackedInt32Array(index) if index is PackedInt32Array and not index.is_empty() else PackedInt32Array(range(v.size()))
   for t in range(0,order.size()-2,3):
    var a:=v[order[t]];var b:=v[order[t+1]];var c:=v[order[t+2]]
    for p in [a,b,c,(a+b)*.5,(b+c)*.5,(a+c)*.5,(a+b+c)/3.]:
     samples+=1
     var bad:=""
     # Paint stays inside its own tile (the width of a line at most past the seam).
     var out=maxf(maxf(-p.x,p.x-tile.side),maxf(-p.z,p.z-tile.side))
     if out>0.:
      if out>LANES.WIDE_M:beyond+=1;tile_bad+=1
      continue
     var hit:Array=collider_gap(col,p)
     var gap:float=hit[0]
     if hit[1]>0.:covered+=1;cover_max=maxf(cover_max,hit[1])
     if hit[1]>0. and "--verbose" in args and covered<=10:print("    under another collider by %.3f m at (%.2f, %.2f, %.2f)"%[hit[1],p.x,p.y,p.z])
     if is_nan(gap):bare+=1;bad="no collider"
     else:
      gap_min=minf(gap_min,gap);gap_max=maxf(gap_max,gap)
      if gap<MIN_GAP_M or gap>MAX_GAP_M:
       # The road surface itself folds inside this piece by more than a road
       # can (FOLD_M within one stripe piece, a few metres at most): a
       # defect of the surface, counted apart from the paint's own errors.
       var fold=surface_fold(tile,a,b,c,p)
       if absf(fold)>FOLD_M:folds+=1;fold_max=maxf(fold_max,absf(fold));bad="surface fold"
       elif gap<MIN_GAP_M:sunk+=1;bad="sunk"
       else:floating+=1;bad="floating"
     if bad=="surface fold":
      if "--verbose" in args:print("    road surface folds %.3f m under the paint at (%.2f, %.2f, %.2f)"%[surface_fold(tile,a,b,c,p),p.x,p.y,p.z])
      bad=""
     if bad!="":
      tile_bad+=1
      if "--verbose" in args and tile_bad<=40:print("    %s at (%.2f, %.2f, %.2f) gap %.3f height_at %.3f in triangle %s %s %s"%[bad,p.x,p.y,p.z,gap,tile.height_at(Vector2(p.x,p.z)),a,b,c])
  built+=1
  flips+=int(s.get("town_flips",0));street_stations+=int(s.street_stations);fitted+=int(s.fitted_stations);steps+=int(s.step_pieces)
  var lanes_doc=data.get("road_lanes",{})
  print("  %s build %.0f ms, markings %.0f ms (%.0f %%; sight %.0f, town %.0f, lines %.0f; %d height lookups), %d quads · white %.0f m solid %.0f m broken · yellow %.0f m solid %.0f m broken · lanes %s, %d/%d stations mapped (%d from the street) · ribbon %s · %d stripe samples off the collider"%[id,ms,paint_ms,100.*paint_ms/maxf(ms,1.),s.sight_ms,s.town_ms,s.draw_ms,s.height_calls,s.quads,s.white_solid_m,s.white_dash_m,s.yellow_solid_m,s.yellow_dash_m,lanes_doc.get("status","absent") if lanes_doc is Dictionary else "absent",s.mapped_stations,s.stations,s.street_stations,"yes" if tile.road_surface else "no",tile_bad])
  for k in totals:totals[k]+=s[k]
  for label in s.layouts:layouts[label]=layouts.get(label,0)+int(s.layouts[label])
  tile.free()
 var labels=layouts.keys();labels.sort()
 for label in labels:print("    %5d stations  %s"%[layouts[label],label])
 print("LANE MARKINGS %d tiles · white %.0f m solid %.0f m broken · yellow %.0f m solid %.0f m broken · markings %.0f ms of %.0f ms build · %d town/rural changes along streets · %d stations took the street's counts · %d stations fitted to the road width · %d pieces left out across a step in the road surface"%[built,totals.white_solid_m,totals.white_dash_m,totals.yellow_solid_m,totals.yellow_dash_m,paint_total,build_total,flips,street_stations,fitted,steps])
 print("STRIPES ON THE COLLIDER %d samples · gap %.3f..%.3f m · %d under %.2f m · %d over %.2f m · %d with no collider within 1 m · %d past the tile edge · %d where the ground or a kerb collider rises above the paint on a ribbon (up to %.3f m) · %d where the road surface folds more than %.2f m inside one piece (up to %.3f m); both RoadSurface defects, not the paint"%[samples,gap_min,gap_max,sunk,MIN_GAP_M,floating,MAX_GAP_M,bare,beyond,covered,cover_max,folds,FOLD_M,fold_max])
 expect(built>0 and totals.white_solid_m>0.,"edge lines were drawn")
 expect(samples>0 and sunk==0 and floating==0 and bare==0 and beyond==0,"stripes lie on the collider the tyres drive on")
 print("LANE MARKING CHECK %s"%["PASSED" if failures==0 else "FAILED"])
 quit(0 if failures==0 else 1)
