extends SceneTree
## Where a start or a jump sets the car down, at real places (Drive.move_to_road,
## RoadPlacement.gd). Loads the real Drive scene, visits each place through the
## search box's path (choose_place), and measures with no input:
##   on arrival  lateral offset from the matched road's centreline (in the first
##               lane right of it), heading against the road, the body's up
##               against the road's normal under the four tyres, the body's
##               height above the road against its static ride height, and
##               whether the tyres stand on the road ribbon (or bridge deck);
##   after 45 s  how far the car moved and turned and whether it is still on the
##               road (the README capture waits 45 s; a car that rolls away
##               shows a grass bank instead of the road);
##   recover     the back-on-the-road key from where it stands gives the same pose,
##               still facing the way the car faced;
##   gas         two seconds of throttle moves the car (the start hold releases).
## Needs the map helper (tools/atlas) and the network or a warm map cache, so it
## is not in the default suite. Exit 0 when every place passes.
##   SIMCRUISE_QA_PREFS_DIR=/tmp/x godot --headless --path game --fixed-fps 60 \
##     --script res://checks/StartPlacement.gd [-- --only=dragon,beartooth]
const PLACES=[
 # name, lon, lat, street, bearing
 ["dragon",-83.9202491,35.4667227,"Calderwood Highway",315.],          # README shot, Deals Gap NC (7.5° grade)
 ["dragon-pool",-83.9327265,35.4792763,"Calderwood Highway",300.],     # start pool, Tail of the Dragon TN
 ["beartooth",-109.4980825,44.9448086,"Beartooth Highway",54.],        # start pool, Beartooth Pass WY
 ["oatman",-114.3678383,35.0441967,"Oatman Road",250.],                # start pool, Sitgreaves Pass AZ
 ["bigsur",-121.900041,36.363197,"Cabrillo Highway",199.],              # start pool, CA-1 Big Sur
 ["stormking",-73.9844189,41.4270831,"Storm King Highway",345.],       # start pool, NY-218 cliff road
 ["monument",-109.990814,37.101444,"",230.],                           # README shot, US 163 UT
 ["viaduct",-104.81243019,41.13262168,"I-180",165.],                   # README shot, bridge deck, Cheyenne WY
 ["capitol",-104.81944137,41.13906643,"Capitol Avenue",334.],          # README shot, city street, Cheyenne WY
]
const LANE_TOLERANCE_M := .5
const HEADING_TOLERANCE_DEG := 3.
const LEVEL_TOLERANCE_DEG := 2.
const HEIGHT_TOLERANCE_M := .08
const SETTLE_TICKS := 2700   # 45 s at 60 Hz, as Screenshots.gd waits
const MAX_DRIFT_M := .25
const MAX_TURN_DEG := 2.
var game
var places=[]
var idx=-1
var stage=""
var ticks=0
var started=0
var finished=false
var failures=[]
var arrival={}

func _initialize():
 if OS.get_environment("SIMCRUISE_QA_PREFS_DIR").is_empty():
  push_error("STARTPLACE needs SIMCRUISE_QA_PREFS_DIR so it cannot touch the player's preferences")
  quit(2);return
 var only=[]
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--only="):only=Array(a.trim_prefix("--only=").split(",",false))
 for p in PLACES:
  if only.is_empty() or p[0] in only:places.append(p)
 game=load("res://Drive.tscn").instantiate()
 game.start_mode="bundled"
 root.add_child(game)
 started=Time.get_ticks_msec()

func expect(ok:bool,what:String):
 if not ok:failures.append("%s: %s"%[places[idx][0],what])

# Nearest segment of the place's street (any road when the street is blank).
func road_at(c:Vector2,street:String)->Array:
 var best=[];var bd=INF;var bo=Vector2.ZERO
 for id in game.tiles:
  var o=game.tile_offset(id)
  var n=game.tiles[id].nearest(c-o,false,street)
  if n.size() and n[2]<bd:bd=n[2];best=n;bo=o
 return [best,bo]

func ride()->float:
 return game.vehicle.R+game.vehicle.spec.static_length_m-game.vehicle.spec.mount_height_m

func measure()->Dictionary:
 var place=places[idx]
 var car:Transform3D=game.vehicle.global_transform
 var c=Vector2(car.origin.x,car.origin.z)
 var fwd3=-car.basis.z;var fwd=Vector2(fwd3.x,fwd3.z).normalized()
 var m={"c":c,"fwd":fwd,"speed":game.vehicle.linear_velocity.length()}
 var r=road_at(c,place[3]);var n=r[0]
 if n.is_empty():m["road"]=false;return m
 m["road"]=true
 var d:Vector2=(n[1][1]-n[1][0]).normalized()
 var along=1. if fwd.dot(d)>=0. else -1.
 m["heading"]=rad_to_deg(acos(clampf(absf(fwd.dot(d)),0.,1.)))
 m["lateral"]=(c-(n[0]+r[1])).dot(Vector2(-d.y,d.x)*along)   # + is right of the centreline
 m["width"]=float(n[1][3]);m["oneway"]=float(n[1][4]);m["class"]=str(n[1][5]);m["dist"]=float(n[2])
 # The road plane under the four tyres, sampled as the tyres see it.
 var right=Vector2(-fwd.y,fwd.x);var pts=[]
 for mount in game.vehicle.MOUNTS:
  var q:Vector2=c+right*float(mount.x)-fwd*float(mount.z)
  pts.append(Vector3(q.x,game.surface(q,car.origin.y),q.y))
 var ahead=(pts[0]+pts[1]-pts[2]-pts[3])*.5;var across=(pts[1]+pts[3]-pts[0]-pts[2])*.5
 var normal=across.cross(ahead).normalized()
 var centre=(pts[0]+pts[1]+pts[2]+pts[3])*.25
 m["grade"]=rad_to_deg(asin(clampf(ahead.normalized().y,-1.,1.)))
 m["level"]=rad_to_deg(car.basis.y.normalized().angle_to(normal))
 m["height"]=(car.origin-centre).dot(normal)-ride()
 var id=game.tile_id(c)
 var ribbon=NAN
 if game.tiles.has(id) and game.tiles[id].get("road_surface"):ribbon=game.tiles[id].road_surface.height(c-game.tile_offset(id))
 var deck=game.tiles[id].deck_at(c-game.tile_offset(id)) if game.tiles.has(id) else NAN
 m["paved"]=not is_nan(ribbon) or not is_nan(deck)
 m["ribbons"]=game.tiles.has(id) and game.tiles[id].get("road_surface")!=null
 return m

func line(tag:String,m:Dictionary)->String:
 if not m.road:return "STARTPLACE %s %s no road under the car"%[places[idx][0],tag]
 return "STARTPLACE %s %s lateral %.2f m (width %.1f, %s%s) heading %.1f° grade %.1f° level %.1f° height %+.3f m paved %s speed %.2f"%[places[idx][0],tag,m.lateral,m.width,m["class"]," one-way" if m.oneway!=0. else "",m.heading,m.grade,m.level,m.height,"yes" if m.paved else ("no" if m.ribbons else "n/a"),m.speed]

func check_pose(tag:String,m:Dictionary):
 expect(m.road,tag+" no road under the car")
 if not m.road:return
 var lane=minf(1.8,m.width*.18)
 expect(m.lateral>0. and absf(m.lateral-lane)<=LANE_TOLERANCE_M,"%s lateral %.2f m, lane centre %.2f m"%[tag,m.lateral,lane])
 expect(m.heading<=HEADING_TOLERANCE_DEG,"%s heading %.1f° off the road"%[tag,m.heading])
 expect(m.level<=LEVEL_TOLERANCE_DEG,"%s body %.1f° off the road plane"%[tag,m.level])
 expect(absf(m.height)<=HEIGHT_TOLERANCE_M,"%s body %+.3f m from its ride height"%[tag,m.height])
 expect(m.paved or not m.ribbons,"%s tyres off the road ribbon"%tag)

func _process(_dt):
 if finished:return false
 if Time.get_ticks_msec()-started>900000:failures.append("timeout at "+(places[idx][0] if idx>=0 and idx<places.size() else "-"));finish();return false
 if not game.ready_drive or not game.jump.is_empty():return false
 match stage:
  "":
   idx+=1
   if idx>=places.size():finish();return false
   var p=places[idx]
   game.choose_place(p[1],p[2],p[0],p[3],p[4]);stage="arrive"
  "arrive":
   arrival=measure();print(line("arrive",arrival));check_pose("arrive",arrival)
   stage="settle";ticks=0
 return false

func _physics_process(_dt):
 if finished or stage not in ["settle","recover","gas"]:return false
 if not game.ready_drive or not game.jump.is_empty():return false
 ticks+=1
 match stage:
  "settle":
   if ticks<SETTLE_TICKS:return false
   var m=measure();print(line("45s",m))
   if m.road:
    var moved=m.c.distance_to(arrival.c);var turned=rad_to_deg(absf(m.fwd.angle_to(arrival.fwd)))
    print("STARTPLACE %s 45s moved %.2f m turned %.1f° off-centre %.2f m"%[places[idx][0],moved,turned,m.dist])
    expect(moved<=MAX_DRIFT_M,"rolled %.2f m in 45 s with no input"%moved)
    expect(turned<=MAX_TURN_DEG,"turned %.1f° in 45 s with no input"%turned)
    expect(m.dist<=m.width*.5,"left the road (%.1f m from the centreline)"%m.dist)
   else:expect(false,"no road under the car after 45 s")
   game.recover();stage="recover";ticks=0
  "recover":
   if ticks==1:
    var m=measure();print(line("recover",m));check_pose("recover",m)
    expect(m.fwd.dot(arrival.fwd)>0.,"recover turned the car around on a two-way road")
   if ticks>=60:Input.action_press("gas");stage="gas";ticks=0
  "gas":
   if ticks>=120:
    Input.action_release("gas")
    var v=game.vehicle.linear_velocity.length()
    print("STARTPLACE %s gas 2 s speed %.2f m/s"%[places[idx][0],v])
    expect(v>=1.,"2 s of throttle left the car at %.2f m/s (still held?)"%v)
    stage=""
 return false

func finish():
 finished=true
 Input.action_release("gas")
 if failures.is_empty():print("STARTPLACE PASSED %d places"%places.size())
 else:
  for f in failures:print("STARTPLACE FAILED ",f)
  print("STARTPLACE RESULT %d failures over %d places"%[failures.size(),places.size()])
 game.request_game_close(0 if failures.is_empty() else 1)
