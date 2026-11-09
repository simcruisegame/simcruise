extends Node
var g
var route:Dictionary
var tile
var data_id:Vector2i
var phase=0
var started_ms=0
var ticks=0
var launch:Vector2
var tangent:Vector2
var normal:Vector2
var run_m:float
var hist=[0,0,0,0,0]
var max_ray=0.
var worst={}
var ray_errors=[]
var max_y_step=0.
var max_speed=0.
var max_off=0.
var old_y=0.
var samples=[]
var out:String

func _ready():
 started_ms=Time.get_ticks_msec()
 g=get_parent()
 var args=OS.get_cmdline_user_args();var k=args.find("--crossfall-drive")
 if k<0 or k+2>=args.size():push_error("Supply a route JSON and a writable receipt path.");g.request_game_close(2);return
 var cache_index=args.find("--crossfall-cache")
 if cache_index>=0 and cache_index+1<args.size():
  var dir=args[cache_index+1]
  if not dir.is_absolute_path() or not DirAccess.dir_exists_absolute(dir):push_error("Invalid test cache path");g.request_game_close(2);return
  g.cache=dir
 var parsed=JSON.parse_string(FileAccess.get_file_as_string(args[k+1]))
 if not parsed is Dictionary or not parsed.has_all(["id","dir","start","length","lat","lon","way","name"]):push_error("Invalid route fixture");g.request_game_close(2);return
 route=parsed;out=args[k+2]
 g.vehicle.set_manual(false)
 data_id=Vector2i(route.id[0],route.id[1]);tangent=Vector2(route.dir[0],route.dir[1]).normalized();normal=Vector2(-tangent.y,tangent.x)
 launch=Vector2(route.start[0],route.start[1]);run_m=route.length
 g.recent_user_choice=true;g.recent_mode=false;g.street_detail.enabled=false
 Input.action_press("handbrake")
 call_deferred("begin")

func begin():
 g.choose_place(route.lon,route.lat,route.name,route.name,fposmod(rad_to_deg(atan2(tangent.x,-tangent.y)),360.))

func _physics_process(dt):
 ticks+=1
 if (phase==0 and Time.get_ticks_msec()-started_ms>35000) or (phase>0 and ticks>15000):print("STATE ",phase," ",g.ready_drive," ",g.jump," ",g.tiles.keys()," ",g.vehicle.position," ",g.vehicle.wheel_contact," ",g.helper);finish("timeout");return
 if phase==0:
  if not g.ready_drive or not g.jump.is_empty() or not g.tiles.has(data_id):return
  tile=g.tiles[data_id]
  var p=g.tile_offset(data_id)+launch+normal*1.55-tangent*15.
  g.vehicle.restart(Vector3(p.x,g.ground(p)+.13,p.y),atan2(-tangent.x,-tangent.y));g.pos=p;phase=1;ticks=0
  return
 if phase==1:
  if ticks<110 or g.vehicle.wheel_contact<4:return
  phase=2;ticks=0;Input.action_release("handbrake")
  old_y=g.vehicle.position.y
 if phase!=2:return
 var car=g.vehicle
 var world=Vector2(car.position.x,car.position.z);var p=world-g.tile_offset(data_id);var s=(p-launch).dot(tangent);var u=(p-launch).dot(normal);var velocity=Vector2(car.linear_velocity.x,car.linear_velocity.z).length();max_speed=maxf(max_speed,velocity)
 hist[car.wheel_contact]+=1;max_y_step=maxf(max_y_step,absf(car.position.y-old_y));old_y=car.position.y;max_off=maxf(max_off,absf(u-1.55))
 var aim=tangent*6.+normal*(1.55-u);var target=atan2(-aim.x,-aim.y);var err=angle_difference(g.yaw,target)
 Input.action_release("left");Input.action_release("right")
 if absf(err)>.002:Input.action_press("left" if err>0 else "right",clampf(absf(err)*1.8,0.,.68))
 Input.action_release("gas");Input.action_release("brake")
 if velocity<5:Input.action_press("gas",clampf((5.-velocity)*.3,0.,.6))
 elif velocity>5.7:Input.action_press("brake",.13)
 var world3=g.get_world_3d().direct_space_state
 if ticks%4==0:
  var ys=[]
  for i in 4:
   var v:Vector3=car.global_transform*car.MOUNTS[i];var q=PhysicsRayQueryParameters3D.create(v+Vector3.UP,v-Vector3.UP*2.,1,[car.get_rid()]);q.hit_back_faces=true
   var hit=world3.intersect_ray(q)
   if not hit.has("position"):finish("ray missed");return
   var at=Vector2(hit.position.x,hit.position.z)-g.tile_offset(data_id);var expected=tile.height_at(at);ys.append(hit.position.y)
   var difference=absf(hit.position.y-expected)
   ray_errors.append(difference)
   if difference>max_ray:
    max_ray=difference
    var kk=Vector2i((at/tile.side*tile.grid_steps).floor())
    worst={"p":[at.x,at.y],"reported":expected,"ray":hit.position.y,"base":tile.base_height(at),"face":hit.get("face_index"),"collider":str(hit.collider.get_path()),"bins":tile.crossfall_surface.bins.get(kk,[]).size() if tile.crossfall_surface else 0}

  var roll=rad_to_deg(asin(clampf(car.global_basis.x.y,-1.,1.)))
  var wheelplane=rad_to_deg(atan((ys[1]+ys[3]-ys[0]-ys[2])/(2.*1.427)))
  samples.append([s,u,velocity,roll,wheelplane])
 if s>=run_m+14.:finish("complete")

func finish(status):
 set_physics_process(false)
 ray_errors.sort()
 var value={"terrain":tile.info.get("road_grade",{}).get("source","") if tile else null,"survey":tile.info.get("surveyed_crossfall",{}) if tile else {},"banks":tile.info.get("bank_detail_active",false) if tile else false,"worst":worst,"rayErrorP95":ray_errors[int(.95*(ray_errors.size()-1))] if not ray_errors.is_empty() else null,"status":status,"road":route.name,"way":route.way,"corrected":tile.crossfall_surface!=null if tile else false,"simTicks":ticks,"histogram":hist,"maxTireRayVsHeightMapM":max_ray,"maxBodyStepM":max_y_step,"maxLaneOffsetErrorM":max_off,"maxSpeedMps":max_speed,"series":samples}
 var required="--require-crossfall" in OS.get_cmdline_user_args()
 var passed=status=="complete"
 if required:
  passed=passed and value.survey.get("ways",[]).has(str(route.way)) and hist[4]>0 and hist[4]==hist.reduce(func(a,b):return a+b,0) and max_ray<=.0006 and max_off<=.15
 value["passed"]=passed
 var file=FileAccess.open(out,FileAccess.WRITE)
 if not file:push_error("Could not write the route receipt");g.request_game_close(3);return
 file.store_string(JSON.stringify(value));file.close()
 print("CROSSFALL DRIVER ",status," ",hist," ",max_ray," ",max_off)
 g.request_game_close(0 if passed else 6)
