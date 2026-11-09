extends Node
## --screenshots OUTPUT_DIRECTORY [--only=name,name]: visits the README shots,
## waits for each place to stream, hides the HUD, optionally drives forward,
## and saves a 1600 px JPEG per shot. Run windowed (a GPU, or software Vulkan in
## CI or a container) with a pinned sun, for example:
##   godot --path game -- --screenshots /abs/out --solar-epoch 1792096200
## Live weather is off so captures are repeatable; nothing prints per frame.
const SHOTS=[
 # name, title, lon, lat, street, bearing, view (0 chase, 1 driver, 2 scenic), drive seconds, atlas
 # Shots stand still: a scripted throttle without steering leaves a curving road.
 ["dragon","Tail of the Dragon · US 129, NC",-83.9202491,35.4667227,"Calderwood Highway",315.,1,0.,false],
 ["monument","Monument Valley · US 163, UT",-109.990814,37.101444,"",230.,0,0.,false],
 ["viaduct","I-180 viaduct · Cheyenne, WY",-104.81243019,41.13262168,"I-180",165.,0,0.,false],
 ["capitol","Capitol Avenue · Cheyenne, WY",-104.81944137,41.13906643,"Capitol Avenue",334.,0,0.,false],
 ["atlas","Road atlas · Deals Gap, NC",-83.9202491,35.4667227,"Calderwood Highway",315.,1,0.,true],
]
var SETTLE_SECONDS=float(OS.get_environment("SCREENSHOT_SETTLE")) if OS.has_environment("SCREENSHOT_SETTLE") else 45.   # wall clock after the drive is ready
const TIMEOUT_SECONDS=300.
var game
var out=""
var queue=[]
var shot=[]
var stage=""
var clock=0.
var ticks=0

func _ready():
 game=get_parent()
 var args=OS.get_cmdline_user_args()
 out=args[args.find("--screenshots")+1]
 DirAccess.make_dir_recursive_absolute(out)
 var only=[]
 for a in args:if a.begins_with("--only="):only=Array(a.trim_prefix("--only=").split(",",false))
 for s in SHOTS:if only.is_empty() or s[0] in only:queue.append(s)
 game.live_weather=false
 call_deferred("next")

func next():
 Input.action_release("gas");Input.action_release("brake")
 if queue.is_empty():
  print("SCREENSHOTS done in ",out)
  game.request_game_close(0);return
 shot=queue.pop_front()
 if game.hud.atlas_map.visible:game.hud.atlas_map.close_map()
 game.choose_place(shot[2],shot[3],shot[1],shot[4],shot[5])
 stage="load";clock=0.

var status_clock=0.
var last_wall=0.
func _process(_dt):
 # Wall clock, not frame time: software rendering can run below 1 fps.
 var now=Time.get_ticks_msec()/1000.
 var dt=now-last_wall;last_wall=now
 status_clock+=dt
 if status_clock>20.:
  status_clock=0.
  print("SCREENSHOT status %s stage=%s fps=%d ready=%s jump=%s tiles=%d"%[shot[0] if shot.size() else "-",stage,Engine.get_frames_per_second(),game.ready_drive,str(game.jump.keys()),game.tiles.size()])
 if stage=="":return
 clock+=dt
 match stage:
  "load":
   if clock>TIMEOUT_SECONDS:print("SCREENSHOT %s timed out loading"%shot[0]);stage="";call_deferred("next");return
   if game.ready_drive and game.jump.is_empty():
    game.view=shot[6];game.camera_snap=true
    game.hud.set_level(game.hud.OFF,false)
    stage="settle";clock=0.;ticks=0
    print("SCREENSHOT %s ready after %.0f s, fps %d"%[shot[0],Time.get_ticks_msec()/1000.,Engine.get_frames_per_second()])
  "settle":
   if clock>=SETTLE_SECONDS:
    print("SCREENSHOT %s settled, fps %d"%[shot[0],Engine.get_frames_per_second()])
    if shot[7]>0.:Input.action_press("gas");stage="drive"
    else:stage="frame"
    clock=0.;ticks=0
  "frame":
   ticks+=1
   if shot[8] and ticks==1:game.hud.atlas_map.open_map()
   if ticks>=(12 if shot[8] else 4):capture()

func _physics_process(_dt):
 if stage!="drive":return
 ticks+=1
 if ticks>=int(shot[7]*60.):
  Input.action_release("gas")
  game.hud.set_level(game.hud.OFF,false)
  stage="frame";ticks=0;clock=0.

func capture():
 stage=""
 var image=get_viewport().get_texture().get_image()
 if image.get_width()>1600:image.resize(1600,int(1600.*image.get_height()/image.get_width()),Image.INTERPOLATE_LANCZOS)
 var path=out.path_join(shot[0]+".jpg")
 image.save_jpg(path,.9)
 print("SCREENSHOT %s %dx%d %s"%[shot[0],image.get_width(),image.get_height(),path])
 call_deferred("next")
