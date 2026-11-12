extends SceneTree
## Parked cars stand in parking lanes, never in a travel lane, a junction or a
## driveway mouth (StreetParking.gd).
##   godot --headless --path game --script res://checks/ParkingCheck.gd [-- --places] [--dir=/abs/tiles] [--cache=/abs/cache]
## Default (offline, in the default suite): hand-made streets with known
## answers, then every bundled Miami Beach tile. --places also streams two real
## places through the map helper (Capitol Avenue in Cheyenne, WY and downtown
## Lincoln, NE, 3 x 3 tiles each, into --cache or the game's map cache);
## --dir= reads saved `atlas tile` JSON files instead.
## For every place it tests
##  * every kerbside spot WorldTile makes (StreetParking.spots, with no building
##    footprints, so more spots than the game keeps) as the widest car the spot
##    admits and the longest parked model, against every road of every loaded
##    tile, neighbours included;
##  * every delivery pull-over AmbientLife allows (can_pull_over, every fleet
##    model, both directions, every 10 m), with the model's own footprint.
## A footprint fails when it comes within a road's travel lanes, or within a
## junction or driveway clearance of a road other than its own. One summary
## line per place; nothing prints per spot unless it fails.
const TILE=preload("res://WorldTile.gd")
const LIFE=preload("res://AmbientLife.gd")
const P=preload("res://StreetParking.gd")
const EARTH=40075016.68557849
const BUNDLED=["18178_27905","18178_27906","18178_27907","18178_27908","18178_27909","18178_27910","18179_27905","18179_27906","18179_27907","18179_27908","18179_27909","18179_27910","18180_27905","18180_27906","18180_27907","18180_27908","18180_27909","18180_27910","18181_27905","18181_27906","18181_27907","18181_27908","18181_27909","18181_27910"]
const PLACES=[["Capitol Avenue, Cheyenne WY",-104.81944137,41.13906643],["Downtown Lincoln NE",-96.7026,40.8136]]
const DELIVERY=["tertiary","secondary"]
var failures=[]
var fleet_bounds=[]

class StubGame:
 var tiles={}
 var side=480.
 var origin=Vector2i.ZERO
 func tile_offset(id:Vector2i)->Vector2:return Vector2(id-origin)*side

func fail(what:String):
 failures.append(what)
 if failures.size()<=25:print("PARKING FAIL ",what)

func _initialize():
 for row in LIFE.fleet():fleet_bounds.append(row[1]*row[0].get_aabb())
 var widths=fleet_bounds.map(func(b):return snappedf(b.size.x,.01))
 print("PARKING fleet widths ",widths)
 if fleet_bounds.is_empty():fail("no traffic fleet: parked cars cannot be measured")
 synthetic()
 var args=OS.get_cmdline_user_args()
 var folder=""
 var cache=ProjectSettings.globalize_path("user://map-cache")
 for a in args:
  if a.begins_with("--dir="):folder=a.trim_prefix("--dir=")
  if a.begins_with("--cache="):cache=a.trim_prefix("--cache=")
 place("Miami Beach (bundled)",load_tiles("res://data",BUNDLED))
 if folder!="":
  var ids=[]
  for f in DirAccess.get_files_at(folder):
   var parts=f.get_basename().split("_")
   if f.get_extension()=="json" and parts.size()==2 and parts[0].is_valid_int() and parts[1].is_valid_int():ids.append(f.get_basename())
  place(folder.get_file(),load_tiles(folder,ids))
 if "--places" in args:
  for p in PLACES:place(p[0],stream(p[1],p[2],cache))
 if failures.is_empty():
  print("PARKING PASS · synthetic streets, bundled tiles%s: no parked footprint in a travel lane, junction or driveway"%(" and %d streamed places"%PLACES.size() if "--places" in args else ""))
  quit(0)
 else:
  print("PARKING FAIL %d footprints or cases"%failures.size())
  quit(1)

# ----------------------------------------------------------------- tiles ----
func side_for(y:int)->float:return EARTH/65536./cosh(PI*(1.-2.*(float(y)+.5)/65536.))

func build_segments(data:Dictionary,side:float)->TILE:
 var tile=TILE.new();tile.info=data;tile.side=side
 tile.street_image()   # the same centreline segments the game builds
 return tile

func load_tiles(folder:String,ids:Array)->Dictionary:
 var out={}
 for id in ids:
  var path=folder.path_join("%s.json"%id)
  if not FileAccess.file_exists(path):fail("%s missing"%path);continue
  var data=JSON.parse_string(FileAccess.get_file_as_string(path))
  if not data is Dictionary or not data.has("roads"):fail("%s unreadable"%path);continue
  out[Vector2i(int(data.id[0]),int(data.id[1]))]=data
 return out

func stream(lon:float,lat:float,cache:String)->Dictionary:
 var helper_name="atlas.exe" if OS.get_name()=="Windows" else "atlas"
 var helper=ProjectSettings.globalize_path("res://../tools/"+helper_name)
 if OS.has_environment("SIMCRUISE_MAP_HELPER"):helper=OS.get_environment("SIMCRUISE_MAP_HELPER")
 var x=floori((lon+180.)/360.*65536.);var y=floori((1.-asinh(tan(deg_to_rad(lat)))/PI)/2.*65536.)
 var out={}
 for dx in [-1,0,1]:
  for dy in [-1,0,1]:
   var lines=[]
   var code=OS.execute(helper,["tile",cache,str(x+dx),str(y+dy)],lines,true)
   var data=JSON.parse_string(str(lines[0])) if code==0 and lines.size() else null
   if not data is Dictionary or not data.has("roads"):fail("tile %d_%d did not stream (helper exit %d)"%[x+dx,y+dy,code]);continue
   out[Vector2i(x+dx,y+dy)]=data
 return out

# ------------------------------------------------------------ the test ----
func place(title:String,datas:Dictionary):
 if datas.is_empty():fail("%s: no tiles"%title);return
 var game=StubGame.new()
 var ids=datas.keys();ids.sort()
 game.origin=ids[0];game.side=side_for(ids[0].y)
 var world=[];var spots=[]
 for id in ids:
  var tile=build_segments(datas[id],game.side)
  game.tiles[id]=tile
  var o=game.tile_offset(id)
  var local=[]
  for seg in tile.segments:
   var w=seg.duplicate();w[0]=seg[0]+o;w[1]=seg[1]+o;world.append(w);local.append(w)
  for s in tile.parking_spots():spots.append([s[0]+o,s[3],s[4],local[s[5]]])
 var grid=P.index(world)
 var lanes_m=0.;var parking_lanes_m=0.
 for k in world:
  var l=k[0].distance_to(k[1])
  if str(k[5]) in P.PARKING_CLASSES:
   lanes_m+=l
   if P.parking_m(k)>0.:parking_lanes_m+=l
 # Kerbside spots, as the widest car each admits.
 var bad=0
 for s in spots:
  var room:float=s[1];var inward:Vector2=s[2];var own:Array=s[3]
  var centre:Vector2=s[0]+inward*(P.KERB_GAP+(room-P.KERB_GAP)*.5)
  var t:Vector2=(own[1]-own[0]).normalized()
  var corners=P.box(centre,t,P.SPOT_LENGTH,room-P.KERB_GAP)
  var why=P.conflict(corners,P.nearby(grid,centre),own)
  if why=="":why=lane_edge(corners,own)
  if why!="":bad+=1;fail("%s: spot at %s on %s %s: %s"%[title,centre.round(),own[5],own[2],why])
 # Delivery pull-overs.
 var life=LIFE.new();life.game=game;life.model_bounds=fleet_bounds
 life.rebuild_graph()
 var tried=0;var allowed=0;var pull_bad=0
 for k in life.segs.size():
  var s=life.segs[k]
  if not s[4] in DELIVERY or s[3]!=0:continue
  var length=s[0].distance_to(s[1]);if length<40.:continue
  for m in fleet_bounds.size():
   for dir in [1,-1]:
    for at in range(15,int(length)-14,10):
     tried+=1
     var car={"model":m}
     if not life.can_pull_over(car,k,dir,float(at)):continue
     allowed+=1
     var probe={"seg":k,"dir":dir,"s":float(at),"lat":0.,"model":m}
     probe.lat=life.lane_point(probe)[3]
     var lane=life.lane_point(probe)
     var corners=P.box(lane[0],lane[1],fleet_bounds[m].size.z,fleet_bounds[m].size.x)
     var own=life.street_segment(k)
     var why=P.conflict(corners,P.nearby(grid,lane[0]),own)
     if why=="":why=lane_edge(corners,own)
     if why!="":pull_bad+=1;fail("%s: pull-over at %s on %s %s: %s"%[title,lane[0].round(),own[5],own[2],why])
 life.free()
 print("PARKING %s · %d tiles · street km %.1f, with parking lanes %.1f · %d kerb spots, %d in a lane/junction/driveway · pull-overs %d of %d allowed, %d in a lane/junction/driveway"%[title,ids.size(),lanes_m/1000.,parking_lanes_m/1000.,spots.size(),bad,allowed,tried,pull_bad])
 for id in ids:game.tiles[id].free()

## Independent of conflict(): the footprint's nearest point to its own centre
## line must be outside the travel lanes.
func lane_edge(corners:PackedVector2Array,own:Array)->String:
 var a:Vector2=own[0];var b:Vector2=own[1];var t=(b-a).normalized();var n=Vector2(-t.y,t.x)
 var inner=INF;var outer=-INF
 for c in corners:
  var d=absf((c-a).dot(n));inner=minf(inner,d);outer=maxf(outer,d)
 if inner<P.travel_half(own)-.02:return "inner edge %.2f m from the centre line, travel lanes reach %.2f m"%[inner,P.travel_half(own)]
 if outer>float(own[3])*.5+.02:return "over the kerb by %.2f m"%(outer-float(own[3])*.5)
 return ""

# ------------------------------------------------------- hand-made cases ----
func synthetic():
 var main=[Vector2(0,0),Vector2(240,0),"Main Street",12.,0,"primary"]
 check(is_equal_approx(P.parking_m(main),2.4) and is_equal_approx(P.travel_half(main),3.6),"12 m two-way primary: 3.6 m lanes and 2.4 m parking lanes")
 check(is_equal_approx(P.parking_m([main[0],main[1],"",10.,0,"tertiary"]),2.),"10 m tertiary: 2.0 m parking lanes")
 check(P.parking_m([main[0],main[1],"",9.6,1,"primary"])==0.,"one-way carriageway: no parking lane")
 check(P.parking_m([main[0],main[1],"",8.,0,"minor"])==0.,"minor street: no parking lane")
 check(P.parking_m([main[0],main[1],"",11.,0,"secondary","unpaved"])==0.,"unpaved: no parking lane")
 check(is_equal_approx(P.lane_offset(12.,false,"primary"),1.8),"traffic keeps to the middle of the 3.6 m lane")
 check(is_equal_approx(P.lane_offset(8.,false,"minor"),2.),"no parking lane: traffic as before (quarter width)")
 # A footprint in the right-hand lane and one at the old one-way kerb offset are caught.
 check(P.conflict(P.box(Vector2(100,1.8),Vector2.RIGHT,4.6,1.9),[main],main).begins_with("travel lane"),"car in the lane is caught")
 var oneway=[Vector2(0,0),Vector2(240,0),"Couplet",9.6,1,"primary"]
 check(P.conflict(P.box(Vector2(100,-(4.8-1.05)),Vector2.RIGHT,4.6,1.9),[oneway],oneway)!="","old one-way median-side spot is caught")
 # A cross street, a driveway, and a one-way couplet.
 var cross=[Vector2(120,0),Vector2(120,-120),"Oak Street",8.,0,"minor"]
 var drive=[Vector2(60,6.),Vector2(60,30),"",4.6,0,"service"]
 var segs=[[main[0],Vector2(120,0),"Main Street",12.,0,"primary"],[Vector2(120,0),main[1],"Main Street",12.,0,"primary"],cross,drive,oneway.duplicate()]
 segs[4][0]=Vector2(0,60);segs[4][1]=Vector2(240,60)
 var found=P.spots(segs,[0,0],func(_p):return false)
 check(not found.is_empty(),"spots on the two-way street")
 for s in found:
  var own:Array=segs[s[5]]
  var c:Vector2=s[0]+s[4]*(P.KERB_GAP+(s[3]-P.KERB_GAP)*.5)
  check(own[5]=="primary","spot only on the two-way street with parking lanes (got %s)"%own[5])
  check(absf(absf(c.y-own[0].y)-(6.-P.KERB_GAP-(s[3]-P.KERB_GAP)*.5))<.01,"spot centred in the parking lane")
  check(absf(c.x-120.)>4.+P.JUNCTION_CLEAR+P.SPOT_LENGTH*.5-.01,"spot clear of the Oak Street junction (x %.1f)"%c.x)
  check(not (c.y>0. and absf(c.x-60.)<2.3+P.DRIVEWAY_CLEAR+P.SPOT_LENGTH*.5-.01),"spot clear of the driveway (x %.1f)"%c.x)
 var near_side=found.filter(func(s):return s[0].y>0.)
 var far_side=found.filter(func(s):return s[0].y<0.)
 check(not near_side.is_empty() and not far_side.is_empty(),"spots on both kerbs")
 for s in found:check(is_equal_approx(wrapf(s[1]-(atan2(1.,0.) if s[0].y>0. else atan2(1.,0.)+PI),-PI,PI),0.),"parked with the traffic on its side")
 # An older 12 m one-way street (bundled tiles): parking lanes, both kerbs facing its way.
 var wide_oneway=[Vector2(0,0),Vector2(0,200),"Collins",12.,-1,"secondary"]
 check(is_equal_approx(P.parking_m(wide_oneway),2.4),"12 m one-way: 2.4 m parking lanes")
 var both=P.spots([wide_oneway],[0,0],func(_p):return false)
 check(both.filter(func(s):return s[0].x>0.).size()>0 and both.filter(func(s):return s[0].x<0.).size()>0,"12 m one-way: spots on both kerbs")
 for s in both:check(is_equal_approx(wrapf(s[1]-atan2(0.,-1.),-PI,PI),0.),"12 m one-way: parked facing its way (b to a)")
 print("PARKING synthetic · %d spots on a 240 m street with a junction, a driveway and a one-way neighbour; %d on a 12 m one-way"%[found.size(),both.size()])

func check(ok:bool,what:String):
 if not ok:fail("synthetic: "+what)
