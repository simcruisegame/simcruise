extends SceneTree
const PLACE="North Highway 89 input check"
var game
var phase=0
var started=0
var progress=0
var trial=0
var trials=[0.,-.70,-.14]
var rows=[]
var out
var contacts=4
var start_pos=Vector2.ZERO
var direction=0.

func _initialize():
 started=Time.get_ticks_msec()
 var args=OS.get_cmdline_user_args()
 out=FileAccess.open(args[0],FileAccess.WRITE)
 out.store_line("trial,frame,t,input,action,angle,yaw,heading,speed,contacts,position_x,position_z,wheel_grip")
 game=load("res://Drive.tscn").instantiate();root.add_child(game)

func hardware(x):
 var event=InputEventJoypadMotion.new()
 event.device=7;event.axis=JOY_AXIS_LEFT_X;event.axis_value=-x
 Input.parse_input_event(event);Input.flush_buffered_events()

func _physics_process(dt):
 if phase==6:return false
 if Time.get_ticks_msec()-started>140000:
  phase=6;printerr("ROAD CORRECTION timed out ",trial," ",phase," ",game.road_name);game.request_game_close(7);return false
 if phase==0 and game.ready_drive:
  game.choose_place(-110.622981566,43.753607726,PLACE,"North Highway 89",220.1754)
  phase=1
  return false
 if phase<=1 and (not game.ready_drive or not game.jump.is_empty() or game.current_title!=PLACE):return false
 if phase==1:
  if game.tiles.size()<6 or game.vehicle.wheel_contact!=4:return false
  progress+=1
  if progress<24:return false
  phase=2;game.vehicle.set_manual(false);start_pos=game.pos
  Input.action_press("gas",.60)
  return false
 var vehicle=game.vehicle;var speed=-vehicle.linear_velocity.dot(vehicle.global_basis.z)
 if phase==2:
  if speed<25.*.44704:return false
  Input.action_release("gas");progress=0;phase=3
 if phase==3:
  var t=progress-36
  var value=.70 if progress<36 else trials[trial]
  if t>=24:value=0.
  hardware(value);contacts=mini(contacts,vehicle.wheel_contact)
  var heading=fposmod(-rad_to_deg(vehicle.rotation.y),360.)
  if t==0:direction=heading
  var grip=1.0
  for v in vehicle.surface_grip:grip=minf(grip,v)
  out.store_line(",".join([trial,progress,t*dt,value,Input.get_action_strength("pad_left")-Input.get_action_strength("pad_right"),rad_to_deg(vehicle.steering),rad_to_deg(vehicle.angular_velocity.y),heading,speed,vehicle.wheel_contact,game.pos.x,game.pos.y,grip].map(func(v):return str(v))))
  if t==36:
   print("US89 trial=",trial," road=",game.road_name," wheelContacts=",contacts," travelledM=",game.pos.distance_to(start_pos)," speedMPS=",speed)
   if contacts<4 or game.road_name!="North Highway 89":phase=6;game.request_game_close(4);return false
   trial+=1;hardware(0.)
   if trial==trials.size():out.close();print("US89 CORRECTIONS COMPLETE");phase=6;game.request_game_close(0);return false
   progress=0;contacts=4;phase=0
  else:progress+=1
 return false
