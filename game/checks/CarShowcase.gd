extends Node
## --car-showcase OUTPUT_DIRECTORY [--car=<id>] [--place=lon,lat,bearing]: loads a
## place, drives the current car along the road with a simple autopilot (road
## heading plus a lane-centre correction, about 14 m/s), and saves frames while
## it moves: chase view at 3, 6 and 9 s, then the driver view and the scenic
## view. Used to look for visual faults on generated cars (wheels, glass, lamps,
## body and shadow) the way a player sees them. Run windowed (GPU or software
## Vulkan); nothing prints per frame.
var game
var out=""
var stage="load"
var clock=0.
var last_wall=0.
var drive_time=0.
var shots=[]
var pending=[]
var place=Vector3(-104.81944137,41.13906643,334.)
const TARGET_MPS=14.

func _ready():
 game=get_parent()
 var args=OS.get_cmdline_user_args()
 out=args[args.find("--car-showcase")+1]
 DirAccess.make_dir_recursive_absolute(out)
 for a in args:
  if a.begins_with("--place="):
   var p=a.trim_prefix("--place=").split(",")
   if p.size()==3:place=Vector3(float(p[0]),float(p[1]),float(p[2]))
 game.live_weather=false
 pending=[["chase_3",0,3.],["chase_6",0,6.],["chase_9",0,9.],["driver",1,10.5],["scenic",2,12.]]
 call_deferred("begin")

func begin():
 game.choose_place(place.x,place.y,"Showcase","",place.z)

func _process(_dt):
 var now=Time.get_ticks_msec()/1000.;var dt=now-last_wall;last_wall=now
 clock+=dt
 match stage:
  "load":
   if clock>240.:print("SHOWCASE timed out loading");game.request_game_close(1);return
   if game.ready_drive and game.jump.is_empty():
    game.view=0;game.camera_snap=true;game.hud.set_level(game.hud.OFF,false)
    stage="settle";clock=0.
  "settle":
   if clock>float(OS.get_environment("SCREENSHOT_SETTLE") if OS.has_environment("SCREENSHOT_SETTLE") else "30"):stage="drive";clock=0.

func _physics_process(dt):
 if stage!="drive":return
 drive_time+=dt
 var car=game.vehicle
 var speed=-car.linear_velocity.dot(car.global_transform.basis.z)
 # Autopilot: steer toward the nearest road's direction, corrected toward its centre.
 var near=game.nearest_world(false,40.)
 var steer=0.
 if near[0].size():
  var hit=near[0];var seg=hit[1];var off=near[1]
  var a:Vector2=seg[0]+off;var b:Vector2=seg[1]+off
  var dir=(b-a).normalized()
  var fwd=Vector2(-car.global_transform.basis.z.x,-car.global_transform.basis.z.z).normalized()
  if dir.dot(fwd)<0:dir=-dir
  var pos=Vector2(car.global_position.x,car.global_position.z)
  var closest:Vector2=hit[0]+off
  var right=Vector2(-dir.y,dir.x)
  var lateral=(pos-closest).dot(right)-1.8   # keep right of the centreline
  var goal=dir-right*clampf(lateral*.12,-.5,.5)
  steer=clampf(fwd.angle_to(goal)*-2.2,-1.,1.)
 Input.action_press("gas",clampf((TARGET_MPS-speed)*.3,0.,1.))
 if steer>0:Input.action_release("right");Input.action_press("left",steer)
 else:Input.action_release("left");Input.action_press("right",-steer)
 if not pending.is_empty() and drive_time>=pending[0][2]:
  var shot=pending.pop_front()
  game.view=shot[1];game.camera_snap=true
  call_deferred("capture",shot[0])
 if pending.is_empty() and drive_time>12.5:
  Input.action_release("gas");Input.action_release("left");Input.action_release("right")
  stage="done"
  print("SHOWCASE done %s speed %.1f m/s contacts %d"%[game.vehicle.spec.id,speed,car.wheel_contact])
  game.request_game_close(0)

func capture(name:String):
 await get_tree().process_frame
 await get_tree().process_frame
 var image=get_viewport().get_texture().get_image()
 image.save_jpg(out.path_join("%s_%s.jpg"%[game.vehicle.spec.id,name]),.88)
 print("SHOWCASE %s %s"%[game.vehicle.spec.id,name])
