extends SceneTree
## Drivable-corridor clearance: nothing that collides may stand on a road.
##
##   godot --headless --path game --script res://checks/RoadClearance.gd -- [--dir=/abs/folder/of/X_Y.json] \
##     [--classes=motorway,trunk,primary,secondary,tertiary,minor] [--step=1] [--strict] [--without-road-clearance]
##
## With no --dir it checks the bundled Miami Beach block (game/data, the offline
## start; default suite). --without-road-clearance builds rails, parapets and
## building walls unclipped, with decks and parapets in the tile's shared
## Structures collider: the before measurement. (It does not undo the ribbon
## join rule in RoadSurface.gd, which applies either way.)
## Tiles are the helper's `atlas tile CACHE X Y` output saved as X_Y.json (any
## folder; the same files checks/RoadDrive.gd drives). They are built with
## WorldTile exactly as the streamer builds them and placed side by side. Then a
## car-sized box (1.8 m wide, 4.4 m long, 1.2 m tall, its floor 0.3 m above the
## road) is swept along every road centreline of every tile, and along the lane
## centres (a quarter of the width either side) of roads 6 m wide or wider, every
## --step metres. It is posed on the surface the car drives there: the bridge deck
## on a mapped bridge, otherwise height_at() (the road ribbon or the ground), and
## pitched and rolled with it.
##
## Every static body the box touches is a hit. On the ground or road-ribbon
## colliders (WorldTile names them Ground and RoadRibbon) it is SURFACE: the
## driven surface rises 0.3 m into the car's footprint (a step, a bank, terrain
## the surface disagrees with: problem C of the 2026-11-10 field test). Any
## contact with a bridge deck (BridgeDeck) is a STEP: the deck end stands proud
## of its approach (problem C again). On any
## other body (RailCollider, BridgeParapets, Structures: buildings) a
## contact whose normal is mostly horizontal (|n.y| < 0.6) is a WALL: something
## the car runs into; other contacts there are STEP. A building wall met only
## by a lane sweep, not the centreline, is BUILDING: the mapped width is a
## class default, so that is a road-width question (problem D).
## SURFACE, STEP and BUILDING are listed and fail only with --strict. Hits within 3 m of
## the loaded area's outer edge are not tested (the neighbour tile is missing).
##
## By default the failing classes are the public through roads (motorway, trunk,
## primary, secondary, tertiary, minor/residential/unclassified); service roads
## and tracks are swept and listed but do not fail. One line per hit stretch
## (at most 60, or --lines=N), one summary, exit 1 when any WALL hit is on a failing class.
## A "data" line sums what the clearance changed: building wall edges opened and
## their length against all colliding wall length (WorldTile.wall_spans), rail
## and parapet metres removed from the corridor, and the rail length that became
## turned-down terminals (counted apart: a terminal is rail kept, not removed).
## Exit 1 too when the openings pass MAX_WALL_OPEN (1 %) of the wall length (the
## bundled Miami Beach start: 0.43 %). MAX_WALL_OPEN is a guard chosen for this
## check, not a measured value.
##
## What a pass means: this is a static box sweep along the mapped centrelines.
## It proves that no rail, parapet or wall stands in a car-sized box on a road;
## it does not drive anything. A tile can pass here and still stop or roll a car
## on a step, a bank or a deck end (STEP and SURFACE lines, field-test problems
## C and D); only checks/RoadDrive.gd measures that. In the default suite it
## costs about 25-50 s (the bundled 24 tiles are built and swept once; more on
## a loaded machine).
const TILE=preload("res://WorldTile.gd")
const EARTH=40075016.68557849
const BOX:=Vector3(1.8,1.2,4.4)   # width, height, length
const FLOOR_M:=.3
const EDGE_M:=3.
const MAX_WALL_OPEN:=.01
const DEFAULT_FAIL:=["motorway","trunk","primary","secondary","tertiary","minor","residential","unclassified"]
var tiles={}
var side=0.
var origin=Vector2i.ZERO
var lo=Vector2.ZERO
var hi=Vector2.ZERO
var step_m=1.
var strict=false
var max_lines=60
var fail_classes=DEFAULT_FAIL.duplicate()
var frames=0

func _initialize():
 var args=OS.get_cmdline_user_args()
 var folder=""
 for a in args:
  if a.begins_with("--dir="):folder=a.trim_prefix("--dir=")
  elif a.begins_with("--classes="):fail_classes=Array(a.trim_prefix("--classes=").split(",",false))
  elif a.begins_with("--step="):step_m=maxf(.25,float(a.trim_prefix("--step=")))
  elif a.begins_with("--lines="):max_lines=int(a.trim_prefix("--lines="))
 strict="--strict" in args
 if folder=="":folder="res://data"
 var mats=materials()
 var first=true
 for file in DirAccess.get_files_at(folder):
  if file.get_extension()!="json":continue
  var parts=file.get_basename().split("_")
  if parts.size()!=2 or not parts[0].is_valid_int() or not parts[1].is_valid_int():continue
  var data=JSON.parse_string(FileAccess.get_file_as_string(folder.path_join(file)))
  if not data is Dictionary or not data.has("roads"):continue
  var id=Vector2i(int(parts[0]),int(parts[1]))
  if first:
   origin=id;first=false
   side=EARTH/65536./cosh(PI*(1.-2.*(float(id.y)+.5)/65536.))
  var tile=TILE.new()
  tile.build(data,side,mats)
  root.add_child(tile)
  var offset=Vector2(id-origin)*side
  tile.position=Vector3(offset.x,0,offset.y)
  tiles[id]=tile
 if tiles.is_empty():print("ROADCLEARANCE no tiles in %s"%folder);quit(2);return

# Bodies enter the physics space on the first frame; sweep on the second.
func _physics_process(_dt):
 frames+=1
 if frames==2:sweep()
 return false

func tile_at(p:Vector2):
 return tiles.get(origin+Vector2i(floori(p.x/side),floori(p.y/side)))

func local(p:Vector2,tile)->Vector2:
 return p-Vector2(tile.position.x,tile.position.z)

# The surface a car drives at p: a mapped deck on a bridge road, else height_at().
func surface(p:Vector2,bridge:bool)->float:
 var tile=tile_at(p)
 if tile==null:return NAN
 var q=local(p,tile)
 if bridge:
  var d=tile.deck_at(q)
  if not is_nan(d):return d
 return tile.height_at(q)

func inside(p:Vector2)->bool:
 return tile_at(p)!=null and tile_at(p+Vector2(EDGE_M,0))!=null and tile_at(p-Vector2(EDGE_M,0))!=null and tile_at(p+Vector2(0,EDGE_M))!=null and tile_at(p-Vector2(0,EDGE_M))!=null

func sweep():
 var space=root.get_world_3d().direct_space_state
 var shape=BoxShape3D.new();shape.size=BOX
 var q=PhysicsShapeQueryParameters3D.new();q.shape=shape;q.collide_with_areas=false
 var swept={}        # class -> metres
 var hits=[]         # [kind, class, name, tile, local, body, normal, metres, bridge or road]
 var stretch={}      # key -> index into hits while a stretch continues
 var printed=0
 for id in tiles:
  var tile=tiles[id]
  var offset=Vector2(tile.position.x,tile.position.z)
  for road in tile.info.roads:
   var cls=str(road[0]);var w=float(road[4])
   var bridge=road.size()>6 and str(road[6])=="bridge"
   var lanes=[0.]
   if w>=6.:lanes=[0.,-w*.25,w*.25]
   var pts=PackedVector2Array()
   for p in road[3]:pts.append(Vector2(float(p[0]),float(p[1]))*side+offset)
   for k in range(1,pts.size()):
    var a=pts[k-1];var b=pts[k];var length=a.distance_to(b)
    if length<.05:continue
    var t=(b-a)/length;var n=Vector2(-t.y,t.x)
    var s=0.
    while s<=length:
     var c=a+t*s
     # Each tile sweeps only its own part of a road; the helper clips with a margin.
     if tile_at(c)==tile and inside(c):
      swept[cls]=swept.get(cls,0.)+step_m
      for lane in lanes:
       var p=c+n*lane
       var h=surface(p,bridge)
       if is_nan(h):continue
       var hf=surface(p+t*2.,bridge);var hb=surface(p-t*2.,bridge)
       var hl=surface(p+n*.8,bridge);var hr=surface(p-n*.8,bridge)
       if is_nan(hf):hf=h
       if is_nan(hb):hb=h
       if is_nan(hl):hl=h
       if is_nan(hr):hr=h
       var f=Vector3(t.x,(hf-hb)/4.,t.y).normalized()
       var l=Vector3(n.x,(hl-hr)/1.6,n.y).normalized()
       var up=l.cross(f)
       if up.y<0.:up=-up
       up=up.normalized()
       var basis=Basis.looking_at(f,up)
       q.transform=Transform3D(basis,Vector3(p.x,h,p.y)+up*(FLOOR_M+BOX.y*.5))
       q.exclude=[]
       for attempt in 4:
        var info=space.get_rest_info(q)
        if info.is_empty():break
        var normal:Vector3=info.normal
        var body=instance_from_id(info.collider_id)
        var body_name=str(body.name) if body!=null else "?"
        var kind="WALL" if absf(normal.y)<.6 else "STEP"
        if body_name.begins_with("Ground") or body_name.begins_with("RoadRibbon"):kind="SURFACE"
        elif body_name.begins_with("BridgeDeck"):kind="STEP"
        # A building wall beside a lane is a road-width question (the mapped
        # width is a class default); one across the centreline is not.
        elif kind=="WALL" and body_name.begins_with("Structures") and lane!=0.:kind="BUILDING"
        var key="%s|%s|%d|%s|%s"%[id,cls,k,str(road[2]),kind]
        var here=Vector3(p.x-offset.x,h,p.y-offset.y)
        if stretch.has(key) and hits[stretch[key]][4].distance_to(here)<8.:
         hits[stretch[key]][7]+=step_m;hits[stretch[key]][4]=here
        else:
         stretch[key]=hits.size()
         hits.append([kind,cls,str(road[2]),id,here,body_name,normal,step_m,"bridge" if bridge else "road"])
        q.exclude=q.exclude+[info.rid]
     s+=step_m
 var walls_fail=0;var walls_other=0;var steps=0;var surface=0;var buildings=0;var walls_m=0.
 var by_body={}
 # Failing hits first, then the rest, at most max_lines lines in all.
 hits.sort_custom(func(x,y):return rank(x)<rank(y))
 for hit in hits:
  var failing=hit[1] in fail_classes
  var body_kind=str(hit[5]).rstrip("0123456789")
  if hit[0]=="WALL":
   if failing:walls_fail+=1;walls_m+=hit[7];by_body[body_kind]=by_body.get(body_kind,0)+1
   else:walls_other+=1
  elif hit[0]=="STEP":steps+=1
  elif hit[0]=="BUILDING":buildings+=1
  else:surface+=1
  if printed<max_lines:
   printed+=1
   print("HIT %s %s %s %s \"%s\" tile %d_%d (%.0f, %.0f, %.1f) body %s normal (%.2f, %.2f, %.2f) over %.0f m"%[mark(hit),hit[0],hit[1],hit[8],hit[2],hit[3].x,hit[3].y,hit[4].x,hit[4].z,hit[4].y,hit[5],hit[6].x,hit[6].y,hit[6].z,hit[7]])
 var classes=swept.keys();classes.sort()
 var row=PackedStringArray()
 for c in classes:row.append("%s=%.1f"%[c,swept[c]/1000.])
 print("ROADCLEARANCE swept km: %s over %d tiles"%[", ".join(row),tiles.size()])
 var bodies=PackedStringArray()
 for b in by_body:bodies.append("%s=%d"%[b,by_body[b]])
 print("RESULT clearance: wall_hits=%d wall_m=%.0f wall_bodies={%s} wall_hits_service_track=%d building_lane_hits=%d step_hits=%d surface_hits=%d"%[walls_fail,walls_m,", ".join(bodies),walls_other,buildings,steps,surface])
 # What the clearance changed in the data, summed over the tiles: building
 # wall openings (WorldTile.wall_spans), clipped rails and parapets, rail
 # length given terminals (GuardRails.clip).
 var sums={}
 for key in ["building_walls_on_road","building_wall_open_m","building_wall_m","guard_rail_clipped_m","guard_rail_terminals","guard_rail_terminal_m","bridge_parapet_clipped_m"]:
  var total=0.
  for id in tiles:total+=float(tiles[id].info.get(key,0.))
  sums[key]=total
 var open_share=sums.building_wall_open_m/sums.building_wall_m if sums.building_wall_m>0. else 0.
 print("ROADCLEARANCE data: wall_edges_opened=%d wall_open_m=%.0f of %.0f (%.2f %%) rail_clipped_m=%.0f rail_terminals=%d terminal_m=%.0f parapet_clipped_m=%.0f"%[sums.building_walls_on_road,sums.building_wall_open_m,sums.building_wall_m,open_share*100.,sums.guard_rail_clipped_m,sums.guard_rail_terminals,sums.guard_rail_terminal_m,sums.bridge_parapet_clipped_m])
 # A negative removal would mean the accounting counts something else.
 if sums.guard_rail_clipped_m<-.05 or sums.bridge_parapet_clipped_m<-.05:print("ROADCLEARANCE FAIL negative clipped length");quit(1);return
 # No road swept is no evidence: the folder held no roads (or none inside).
 if swept.is_empty():print("ROADCLEARANCE swept no road; nothing was checked");quit(2);return
 # Openings are for a car's path only; a share above MAX_WALL_OPEN means the
 # opening rule has grown into removing buildings.
 if open_share>MAX_WALL_OPEN:print("ROADCLEARANCE FAIL building wall openings %.2f %% of wall length, above %.2f %%"%[open_share*100.,MAX_WALL_OPEN*100.])
 var bad=walls_fail>0 or open_share>MAX_WALL_OPEN or (strict and (steps>0 or surface>0 or buildings>0))
 quit(1 if bad else 0)

func mark(hit)->String:
 var failing=hit[1] in fail_classes
 if failing and (hit[0]=="WALL" or strict):return "FAIL"
 return "info"

func rank(hit)->int:
 if mark(hit)=="FAIL":return 0
 return {"WALL":1,"BUILDING":2,"STEP":3}.get(hit[0],4)

func materials()->Dictionary:
 var out={}
 for key in TILE.WALL+TILE.SHOP+["sign","trim","awning0","awning1","awning2","iron","globe","curb","walk","signal_steel","signal_yellow","signal_dark","signal_go","fuel_red","fuel_green","fuel_white","fuel_steel","whitepaint","yellowpaint","theatre_glass","doorframe","cap","roof","siding0","siding1","siding2","siding3","house_trim","house_glass"]:
  var m=StandardMaterial3D.new();m.cull_mode=BaseMaterial3D.CULL_DISABLED;out[key]=m
 return out
