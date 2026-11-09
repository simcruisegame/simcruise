extends SceneTree
# Emulate a mapped Xbox left stick at the normal project physics rate, recording the physical chassis.
var car
var tick=0
var trial=0
var target_mph=40.
var first_input=.68
var second_input=0.
var mirror=1.
var braking=0.
var sweep=6
var supports_pad_filter=false
var trials=[]
var frame_file
var destination=""

func _initialize():
 var args=OS.get_cmdline_user_args()
 destination=args[0]
 frame_file=FileAccess.open(destination+".csv",FileAccess.WRITE)
 frame_file.store_line("trial,t,input,mapped,demand,rackDeg,yawRateDegS,headingDeg,sideSlipDeg,speedMPS,posX,posZ,contacts,flDeg,frDeg,flN,frN,rlN,rrN")
 var smooth="--sweep" in args
 var amplitudes=[.7,.85] if smooth else [.5,.7,.85]
 var durations=[6,12] if smooth else [0]
 for mph in [25.,40.,55.]:
  for amplitude in amplitudes:
   var corrections=[0.,-amplitude] if smooth else [0.,.14,-.14,-amplitude]
   for second in corrections:
    for sign_L in [-1.,1.]:
     for ramp in durations:trials.append([mph,amplitude,second,sign_L,0.,ramp])
 var braking_corrections=[0.,-.7] if smooth else [0.,-.14,-.7]
 for second in braking_corrections:
  for sign_L in [-1.,1.]:trials.append([40.,.70,second,sign_L,.22,6 if smooth else 0])
 InputMap.add_action("clutch")
 for binding in [["pad_left",-1.],["pad_right",1.]]:
  var action=binding[0];InputMap.add_action(action,.12)
  var event=InputEventJoypadMotion.new();event.device=-1;event.axis=JOY_AXIS_LEFT_X;event.axis_value=binding[1]
  InputMap.action_add_event(action,event)
 var floor_body=StaticBody3D.new();var shape=CollisionShape3D.new();var box=BoxShape3D.new()
 box.size=Vector3(500,1,600);shape.shape=box;floor_body.position.y=-.5;floor_body.add_child(shape);root.add_child(floor_body)
 car=preload("res://Powertrain.gd").new();car.setup();root.add_child(car);car.set_manual(true)
 for method in car.get_method_list():
  if method.name=="drive":supports_pad_filter=method.args.size()>=6
 next_trial()

func stick(value):
 var e=InputEventJoypadMotion.new();e.device=7;e.axis=JOY_AXIS_LEFT_X;e.axis_value=-value
 Input.parse_input_event(e);Input.flush_buffered_events()

func next_trial():
 var cfg=trials[trial];target_mph=cfg[0];first_input=cfg[1];second_input=cfg[2];mirror=cfg[3];braking=cfg[4];sweep=cfg[5]
 car.restart(Vector3(0,.10,0),0.);car.select_ratio(0);stick(0.);tick=0

func _physics_process(dt):
 tick+=1
 if tick==86:
  var v=target_mph*.44704
  car.linear_velocity=-car.global_basis.z*v
  car.omega.fill(v/car.R)
 # Each mirrored trial coasts in neutral from the same seed velocity, with four actual tire contacts.
 var f=tick-100
 var raw=0.
 if f>=0 and f<36:raw=first_input*mirror*minf(1.,float(f+1)/maxi(1,sweep))
 elif f>=36 and f<66:raw=mirror*lerpf(first_input,second_input,minf(1.,float(f-35)/maxi(1,sweep)))
 elif f>=66 and f<66+sweep:raw=second_input*mirror*(1.-float(f-65)/sweep)
 stick(raw)
 var pad=Input.get_action_strength("pad_left")-Input.get_action_strength("pad_right")
 var speed=-car.linear_velocity.dot(car.global_basis.z)
 var cmd=car.thumbstick_command(pad,speed)
 var brakes=braking if f>=36 and f<66 else 0.
 if supports_pad_filter:car.drive(dt,0.,brakes,cmd,false,absf(pad)>0.)
 else:car.drive(dt,0.,brakes,cmd,false)
 if f>=0:
  var heading=atan2(-(-car.global_basis.z).x,-(-car.global_basis.z).z)
  var angle=atan2(car.linear_velocity.dot(car.global_basis.x),speed)
  var sample=[trial,(f-36)*dt,raw,pad,cmd,rad_to_deg(car.steering),rad_to_deg(car.angular_velocity.y),rad_to_deg(heading),rad_to_deg(angle),speed,car.position.x,car.position.z,car.wheel_contact,rad_to_deg(car.wheel_heading[0]),rad_to_deg(car.wheel_heading[1]),car.spring_load[0],car.spring_load[1],car.spring_load[2],car.spring_load[3]]
  frame_file.store_line(",".join(sample.map(func(n):return str(n))))
 if f==128:
  trial+=1
  if trial==trials.size():
   frame_file.close();var out=FileAccess.open(destination+".json",FileAccess.WRITE);out.store_string(JSON.stringify(trials));out.close()
   print("COUNTERSTEER ",trial," mirrored trials at ",Engine.physics_ticks_per_second," Hz");quit()
  else:next_trial()
 return false
