extends SceneTree
## Drives the Miata along a real road on streamed tiles and reports how the ride felt.
##
##   godot --headless --path game --fixed-fps 60 --script res://checks/RoadDrive.gd -- \
##     --dir=/abs/folder/of/X_Y.json [--road="Calderwood Highway"] [--classes=primary] \
##     [--speed=20] [--lateral-g=0.55] [--reverse]
##
## Tiles are the helper's `atlas tile CACHE X Y` output saved as X_Y.json. They are
## built with WorldTile exactly as the streamer builds them and placed side by side.
## The road's pieces are chained into the longest continuous route. A simple driver
## follows it: pure-pursuit steering, and a speed plan that holds the chosen lateral
## acceleration in curves and brakes at 4 m/s² in time for them, like a brisk but
## sane driver. Nothing prints per frame; one report prints at the end.
##
## What it measures: distance driven, average speed, wheel airtime (any wheel off
## the ground for 3+ ticks while moving), all-wheels-off launches, body vertical
## jolts above 0.5 g and 1 g, bump-stop hits, the largest vertical acceleration,
## upward launches above 1.5 m/s, departures from the road and roof-down events.
## Exit 0 when the route was finished without a launch or a rollover.
## Diagnostics (environment): ROADDRIVE_SECONDS=N caps simulated time (default 1200);
## ROADDRIVE_TRACE=FROM-TO prints driver state between those stations every 0.1 s;
## ROADDRIVE_PLAN=1 prints the speed plan; ROADDRIVE_DEBUG=1 prints the first 10 s.
## Each event (airtime, jolt above 1 g, leaving the road, roof down) prints once
## with its tile and position, at most 40 lines.
const TILE=preload("res://WorldTile.gd")
const EARTH=40075016.68557849
var car
var route=PackedVector3Array() # x, z in metres, y = station
var road_width=8.
var target_speed=20.
var lateral_g=.55
var plan=PackedFloat64Array()
var tiles={}
var side=0.
var origin=Vector2i.ZERO
var tick=0
var station=0.
var stats={"air_ticks":0,"air_events":0,"launch":0,"jolt_05":0,"jolt_10":0,"bump_stop":0,"max_vert_g":0.,"max_up_mps":0.,"off_road":0,"roof":0,"impacts":0,"max_cross_m":0.,"seconds":0.}
var air_run=0
var last_vy=0.
var last_speed=0.
var off_run=0
var done=false
var settle=0
var speed_i=0.

func _initialize():
 var args=OS.get_cmdline_user_args()
 var folder="";var road_name="";var classes=[];var reverse="--reverse" in args
 for a in args:
  if a.begins_with("--dir="):folder=a.trim_prefix("--dir=")
  elif a.begins_with("--road="):road_name=a.trim_prefix("--road=")
  elif a.begins_with("--classes="):classes=Array(a.trim_prefix("--classes=").split(",",false))
  elif a.begins_with("--speed="):target_speed=float(a.trim_prefix("--speed="))
  elif a.begins_with("--lateral-g="):lateral_g=float(a.trim_prefix("--lateral-g="))
 if folder=="":print("ROADDRIVE needs --dir=");quit(2);return
 if road_name=="" and classes.is_empty():classes=["primary","trunk","secondary"]
 var pieces=[]
 var mats=materials()
 for file in DirAccess.get_files_at(folder):
  if file.get_extension()!="json":continue
  var parts=file.get_basename().split("_")
  if parts.size()!=2 or not parts[0].is_valid_int() or not parts[1].is_valid_int():continue
  var data=JSON.parse_string(FileAccess.get_file_as_string(folder.path_join(file)))
  if not data is Dictionary or not data.has("roads"):continue
  var id=Vector2i(int(parts[0]),int(parts[1]))
  if tiles.is_empty():
   origin=id
   side=EARTH/65536./cosh(PI*(1.-2.*(float(id.y)+.5)/65536.))
  var tile=TILE.new()
  tile.build(data,side,mats)
  root.add_child(tile)
  var offset=Vector2((id-origin)*1)*side
  tile.position=Vector3(offset.x,0,offset.y)
  tiles[id]=tile
  for road in data.roads:
   if road_name!="" and str(road[2])!=road_name:continue
   if road_name=="" and not str(road[0]) in classes:continue
   var pts=PackedVector2Array()
   for p in road[3]:pts.append(Vector2(p[0],p[1])*side+offset)
   if pts.size()>=2:pieces.append(pts);road_width=float(road[4])
 build_route(pieces,reverse)
 if route.size()<50:print("ROADDRIVE no continuous route found (%d pieces)"%pieces.size());quit(2);return
 plan_speed()
 if OS.has_environment("ROADDRIVE_PLAN"):
  for i in range(0,mini(route.size(),400),6):print("PLAN %d %.1f %.1f want %.1f"%[i,route[i].x,route[i].z,plan[i]])
 car=load("res://Powertrain.gd").new()
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--car="):car.use_spec(preload("res://CarSpec.gd").load_id(a.trim_prefix("--car=")))
 car.setup();root.add_child(car)
 # Start 15 m in: a route often begins exactly at the edge of the loaded tiles.
 station=15.
 var p=Vector2(route[15].x,route[15].z);var q=Vector2(route[23].x,route[23].z);var dir=(q-p).normalized()
 car.restart(Vector3(p.x,ground(p)+.35,p.y),atan2(-dir.x,-dir.y))
 print("ROADDRIVE start tile %s" % [origin+Vector2i(floori(p.x/side),floori(p.y/side))])
 print("ROADDRIVE route %.2f km over %d tiles · target %.1f m/s · lateral %.2f g"%[route[-1].y/1000.,tiles.size(),target_speed,lateral_g])

# Join pieces end to end (2 m tolerance) and keep the longest chain, resampled every metre.
func build_route(pieces:Array,reverse:bool):
 var chains=[]
 var left=pieces.duplicate()
 while not left.is_empty():
  var chain:PackedVector2Array=left.pop_back()
  var grew=true
  while grew:
   grew=false
   for i in range(left.size()-1,-1,-1):
    var c:PackedVector2Array=left[i]
    if c[0].distance_to(chain[-1])<2.:chain.append_array(c.slice(1));left.remove_at(i);grew=true
    elif c[-1].distance_to(chain[-1])<2.:var r=c.duplicate();r.reverse();chain.append_array(r.slice(1));left.remove_at(i);grew=true
    elif c[-1].distance_to(chain[0])<2.:var r2=c.duplicate();r2.append_array(chain.slice(1));chain=r2;left.remove_at(i);grew=true
    elif c[0].distance_to(chain[0])<2.:var r3=c.duplicate();r3.reverse();r3.append_array(chain.slice(1));chain=r3;left.remove_at(i);grew=true
  chains.append(chain)
 var best:PackedVector2Array=PackedVector2Array();var best_len=0.
 for c in chains:
  var l=0.
  for k in range(1,c.size()):l+=c[k].distance_to(c[k-1])
  if l>best_len:best_len=l;best=c
 if reverse:best.reverse()
 # Resample every metre, then smooth with a ±4 m moving average for the driver's line.
 var dense=PackedVector2Array();var acc=0.;var next=0.
 for k in range(1,best.size()):
  var a=best[k-1];var b=best[k];var l=a.distance_to(b)
  while next<=acc+l and l>0:dense.append(a.lerp(b,(next-acc)/l));next+=1.
  acc+=l
 for i in dense.size():
  var sum=Vector2.ZERO;var n=0
  for j in range(maxi(0,i-4),mini(dense.size(),i+5)):sum+=dense[j];n+=1
  route.append(Vector3(sum.x/n,float(i),sum.y/n))

# Speed plan: curvature-limited speed, then braking at 4 m/s² ahead of each limit.
func plan_speed():
 plan.resize(route.size())
 for i in route.size():
  var a=Vector2(route[maxi(0,i-8)].x,route[maxi(0,i-8)].z);var b=Vector2(route[i].x,route[i].z);var c=Vector2(route[mini(route.size()-1,i+8)].x,route[mini(route.size()-1,i+8)].z)
  var turn=absf(angle_difference((b-a).angle(),(c-b).angle()))
  # The two chords' midpoints are 8 m apart, so the heading change spans 8 m of arc.
  var radius=8./maxf(turn,1e-4)
  plan[i]=minf(target_speed,sqrt(lateral_g*9.80665*radius))
 for i in range(route.size()-2,-1,-1):plan[i]=minf(plan[i],sqrt(plan[i+1]*plan[i+1]+2.*4.*1.))

func ground(p:Vector2)->float:
 var id=origin+Vector2i(floori(p.x/side),floori(p.y/side))
 var tile=tiles.get(id)
 if tile==null:return 0.
 return tile.height_at(p-Vector2(id-origin)*side)

func _physics_process(dt):
 if done:return false
 tick+=1
 if tick>60*int(OS.get_environment("ROADDRIVE_SECONDS") if OS.has_environment("ROADDRIVE_SECONDS") else "1200"):finish("timeout");return false
 if OS.has_environment("ROADDRIVE_DEBUG") and tick in [1,30,60,61,90,120,300,600]:print("DBG tick %d pos %s vel %s contacts %d ground %.2f"%[tick,car.global_position,car.linear_velocity,car.wheel_contact,ground(Vector2(car.global_position.x,car.global_position.z))])
 if settle<60:settle+=1;car.drive(dt,0.,0.,0.,false);return false
 var b=car.global_transform.basis;var pos=Vector2(car.global_position.x,car.global_position.z)
 var v=car.linear_velocity;var speed=-v.dot(b.z)
 # Progress: nearest route station within a window ahead of the last one.
 var best=station;var best_d=INF
 for s in range(int(station),mini(route.size(),int(station)+40)):
  var d=pos.distance_squared_to(Vector2(route[s].x,route[s].z))
  if d<best_d:best_d=d;best=s
 station=best
 var cross=sqrt(best_d)
 stats.max_cross_m=maxf(stats.max_cross_m,cross)
 if cross>road_width*.5+1.:off_run+=1
 else:off_run=0
 if off_run==30:stats.off_road+=1;event("off road %.1f m"%cross)
 if station>=route.size()-12:finish("finished");return false
 # Pure pursuit on the smoothed line.
 var look=clampf(speed*.9,6.,30.)
 var target_i=mini(route.size()-1,int(station+look))
 var target=Vector2(route[target_i].x,route[target_i].z)
 var to=target-pos;var forward=Vector2(-b.z.x,-b.z.z).normalized()
 var alpha=forward.angle_to(to)
 var delta=atan(2.*car.WHEELBASE_M*sin(alpha)/maxf(to.length(),1.))
 var turn=clampf(-delta/maxf(car.available_steering(speed,car.steering_assist),.01),-1.,1.)
 var want=plan[int(station)]
 var gas=0.;var brake=0.
 if speed<want:speed_i=clampf(speed_i+(want-speed)*dt*.1,0.,.5);gas=clampf((want-speed)*.5+speed_i,0.,1.)
 else:speed_i=maxf(0.,speed_i-dt*.2);brake=clampf((speed-want)*.35,0.,1.)
 # Like a person: lift off when the car slides sideways, never add power mid-slide.
 var slide=absf(v.dot(b.x))
 if slide>1.:gas*=clampf(1.-(slide-1.)/1.5,0.,1.);speed_i=0.
 car.drive(dt,gas,brake,turn,false)
 if OS.has_environment("ROADDRIVE_TRACE") and tick%6==0 and station>=int(OS.get_environment("ROADDRIVE_TRACE").split("-")[0]) and station<=int(OS.get_environment("ROADDRIVE_TRACE").split("-")[1]):print("TRACE st %d want %.1f v %.1f gas %.2f brk %.2f turn %.2f yawrate %.2f lat %.2f cross %.1f contacts %d"%[station,want,speed,gas,brake,turn,car.angular_velocity.y,v.dot(b.x),cross,car.wheel_contact])
 if OS.has_environment("ROADDRIVE_DEBUG") and tick%60==0 and tick<=600:print("DBG drive station %d want %.1f speed %.2f gas %.2f brake %.2f turn %.2f gear %s rpm %d"%[station,want,speed,gas,brake,turn,car.gear_text(),car.rpm])
 # Observations.
 stats.seconds+=dt
 var moving=absf(speed)>3.
 if moving and car.wheel_contact<4:
  air_run+=1
  if air_run==3:stats.air_events+=1;event("air (%d wheels down)"%car.wheel_contact)
  if air_run>=3:stats.air_ticks+=1
 else:air_run=0
 if moving and car.wheel_contact==0 and air_run==3:stats.launch+=1
 var vert_g=absf(v.y-last_vy)/dt/9.80665;last_vy=v.y
 # A collision: forward speed falls by more than 3 m/s in one tick.
 if last_speed-speed>3. and absf(last_speed)>3.:stats.impacts+=1;event("impact %.1f -> %.1f m/s"%[last_speed,speed])
 last_speed=speed
 if moving and tick>settle+5:
  stats.max_vert_g=maxf(stats.max_vert_g,vert_g)
  if vert_g>.5:stats.jolt_05+=1
  if vert_g>1.:stats.jolt_10+=1;event("jolt %.1f g"%vert_g)
  stats.max_up_mps=maxf(stats.max_up_mps,v.y-0.)
 for i in 4:
  if car.spring_load[i]>0. and car.connected_wheels[i].position.y>car.MOUNTS[i].y-car.spec.min_travel_m-.002:stats.bump_stop+=1
 if car.roof_down:
  if stats.roof==0:event("roof down")
  stats.roof+=1
 return false

# One line per event (not per frame), capped, with where it happened.
var events_printed=0
func event(what:String):
 if events_printed>=40:return
 events_printed+=1
 var p=Vector2(car.global_position.x,car.global_position.z)
 var id=origin+Vector2i(floori(p.x/side),floori(p.y/side))
 var local=p-Vector2(id-origin)*side
 print("EVENT %s at station %d m, tile %d_%d (%.0f, %.0f), speed %.1f m/s"%[what,station,id.x,id.y,local.x,local.y,-car.linear_velocity.dot(car.global_transform.basis.z)])
 if what.begins_with("impact") and OS.has_environment("ROADDRIVE_DEBUG"):
  var q=PhysicsShapeQueryParameters3D.new();var box=BoxShape3D.new();box.size=Vector3(2.,1.,4.4);q.shape=box
  q.transform=car.global_transform.translated_local(Vector3(0,.55,0));q.exclude=[car.get_rid()]
  for h in root.get_world_3d().direct_space_state.intersect_shape(q,8):
   var owner=h.collider.get_parent();var tid="?"
   for k in tiles:if tiles[k]==owner or tiles[k]==owner.get_parent():tid=str(k)
   print("  touching ",tid," ",h.collider.name," ",h.collider.get_child(0).shape.get_class()," car y %.2f"%car.global_position.y)

func finish(reason:String):
 done=true
 var km=station/1000.
 print("ROADDRIVE %s · %.2f of %.2f km in %.0f s (avg %.1f m/s)"%[reason,km,route[-1].y/1000.,stats.seconds,station/maxf(stats.seconds,.01)])
 print("RESULT ride: air_events=%d air_seconds=%.2f launches=%d jolts>0.5g=%d jolts>1g=%d max_vertical_g=%.2f max_up_mps=%.2f bump_stop_ticks=%d off_road=%d impacts=%d roof_ticks=%d max_cross_m=%.1f"%[
  stats.air_events,stats.air_ticks/60.,stats.launch,stats.jolt_05,stats.jolt_10,stats.max_vert_g,stats.max_up_mps,stats.bump_stop,stats.off_road,stats.impacts,stats.roof,stats.max_cross_m])
 var ok=reason=="finished" and stats.launch==0 and stats.roof==0 and stats.impacts==0
 quit(0 if ok else 1)

func materials()->Dictionary:
 var out={}
 for key in TILE.WALL+TILE.SHOP+["byrd","sign","trim","awning0","awning1","awning2","iron","globe","curb","walk","signal_steel","signal_yellow","signal_dark","signal_go","fuel_red","fuel_green","fuel_white","fuel_steel","whitepaint","yellowpaint","theatre_glass","doorframe","cap","roof","siding0","siding1","siding2","siding3","house_trim","house_glass"]:
  var m=StandardMaterial3D.new();m.cull_mode=BaseMaterial3D.CULL_DISABLED;out[key]=m
 return out
