extends SceneTree
## godot --headless --path game --script res://checks/LaneMarkingCheck.gd -- [--dir=/abs/path] [--tiles=X_Y,...]
## Road markings (LaneMarkings.gd). First the MUTCD rules on fixed cross-sections
## (lane counts, centre line kinds, edge lines, turn lanes, passing sight
## distance, short passing zones). Then it builds tiles through WorldTile.build
## exactly as the streamer does (the bundled Miami Beach block, or every
## `atlas tile` X_Y.json in --dir) and prints, per tile, the metres of each line
## kind, how many stations had a mapped lane count, and the lane layouts it
## drew. Every built stripe corner must sit on WorldTile.height_at() + LIFT_M
## (the road ribbon where there is one). Exit 0 when every rule holds.
const TILE=preload("res://WorldTile.gd")
const LANES=preload("res://LaneMarkings.gd")
const EARTH=40075016.68557849
const DEFAULT_TILES=["18179_27907","18179_27908","18180_27907","18180_27908"]
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
 # Five lanes, 2 + 2 mapped: a centre two-way left-turn lane (solid outside, broken inside).
 x=LANES.cross_section("trunk",false,[5,2,2],16.,true)
 expect(x.centre==1 and count(x,"yellow","solid")==2 and count(x,"yellow","dash")==2 and count(x,"white","dash")==2,"five-lane TWLTL: %s"%[x.lines])
 var middle=x.travelled*.5-x.forward*x.lane-x.lane*.5
 var nearest_solid=INF;var farthest_dash=0.
 for line in x.lines:
  if line[1]=="yellow" and line[2]=="solid":nearest_solid=minf(nearest_solid,absf(line[0]-middle))
  if line[1]=="yellow" and line[2]=="dash":farthest_dash=maxf(farthest_dash,absf(line[0]-middle))
 expect(nearest_solid>farthest_dash,"TWLTL: solid yellow outside, broken yellow inside")
 # 3 + 2 at an intersection approach reads the same; 3 lanes 2 + 1 is a centre turn lane too.
 expect(LANES.cross_section("trunk",false,[5,3,2],16.,true).centre==1,"five lanes 3 + 2")
 expect(LANES.cross_section("trunk",false,[3,2,1],16.,true).centre==1,"three lanes 2 + 1")
 # 4 lanes mapped 1 + 3 keeps its counts.
 x=LANES.cross_section("trunk",false,[4,1,3],16.,true)
 expect(x.forward==1 and x.backward==3 and count(x,"white","dash")==2,"four lanes 1 + 3: %s"%[x.lines])
 # One-way: white right edge, yellow left edge, lane lines between.
 x=LANES.cross_section("secondary",true,[3,3,0],11.,false)
 expect(count(x,"white","dash")==2 and count(x,"yellow","solid")==1 and count(x,"white","solid")==1,"one-way three lanes: %s"%[x.lines])
 for line in x.lines:
  if line[1]=="yellow":expect(line[0]<0.,"one-way yellow edge on the left")
 # A single mapped lane, two-way: no centre line.
 x=LANES.cross_section("tertiary",false,[1,0,0],10.,false)
 expect(count(x,"yellow","solid")+count(x,"yellow","dash")==0,"single-lane two-way has no centre line")
 # The lines fit the road: never past the paved shoulder.
 for case in [["primary",false,[6,3,3],12.],["tertiary",true,[4,4,0],10.],["trunk",false,[7,3,3],16.]]:
  x=LANES.cross_section(case[0],case[1],case[2],case[3],true)
  for line in x.lines:expect(absf(line[0])<=case[3]*.5+LANES.SHOULDER_M,"%s %s lanes: line %.2f m off the centre fits"%[case[0],case[2],line[0]])
 # Table 3B-1 and the 400 ft minimum passing zone.
 expect(absf(LANES.passing_sight_m(55)-274.32)<.01,"55 mph passing sight 900 ft")
 expect(absf(LANES.passing_sight_m(33)-167.64)<.01,"33 mph rounds to 35 mph (550 ft)")
 var flags=PackedByteArray([0,1,1,0,1,1,1,1,1]);var arc=PackedFloat64Array([0,50,100,150,200,250,300,350,400])
 LANES.short_zones(flags,arc)
 expect(flags[1]==0 and flags[2]==0 and flags[8]==1,"a 50 m passing zone is removed, a 200 m one cut by the path end stays")
 flags=PackedByteArray([1,1,0,0,0,0,0,0,1]);LANES.short_zones(flags,arc)
 expect(flags[0]==0 and flags[8]==0,"under 61 m at a path end is removed")

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
 var layouts={};var built=0;var off_surface=0;var corners=0
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
  var s:Dictionary=preload("res://LaneMarkings.gd").paint(tile)
  var paint_ms=(Time.get_ticks_usec()-started_paint)/1000.
  for key in ["whitepaint","yellowpaint"]:
   if not tile.batches.has(key):continue
   var arrays=tile.batches[key].commit_to_arrays()
   var v:PackedVector3Array=arrays[Mesh.ARRAY_VERTEX]
   for i in range(0,v.size(),7):
    corners+=1
    if absf(v[i].y-tile.height_at(Vector2(v[i].x,v[i].z))-LANES.LIFT_M)>.02:off_surface+=1
  built+=1
  var lanes_doc=data.get("road_lanes",{})
  print("  %s build %.0f ms, markings %.0f ms, %d quads · white %.0f m solid %.0f m broken · yellow %.0f m solid %.0f m broken · lanes %s, %d/%d stations mapped · ribbon %s"%[id,ms,paint_ms,s.quads,s.white_solid_m,s.white_dash_m,s.yellow_solid_m,s.yellow_dash_m,lanes_doc.get("status","absent") if lanes_doc is Dictionary else "absent",s.mapped_stations,s.stations,"yes" if tile.road_surface else "no"])
  for k in totals:totals[k]+=s[k]
  for label in s.layouts:layouts[label]=layouts.get(label,0)+int(s.layouts[label])
  tile.free()
 var labels=layouts.keys();labels.sort()
 for label in labels:print("    %5d stations  %s"%[layouts[label],label])
 print("LANE MARKINGS %d tiles · white %.0f m solid %.0f m broken · yellow %.0f m solid %.0f m broken · %d of %d sampled corners off height_at()+lift"%[built,totals.white_solid_m,totals.white_dash_m,totals.yellow_solid_m,totals.yellow_dash_m,off_surface,corners])
 expect(built>0 and totals.white_solid_m>0.,"edge lines were drawn")
 expect(off_surface==0,"stripes lie on the road surface")
 print("LANE MARKING CHECK %s"%["PASSED" if failures==0 else "FAILED"])
 quit(0 if failures==0 else 1)
