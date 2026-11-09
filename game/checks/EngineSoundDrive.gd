extends SceneTree
var car
var voice
var muted := false
var tick := 0
var rows := []
var pcm := PackedByteArray()
var dest := ""
var recording := false
func _initialize():
 var args:=OS.get_cmdline_user_args()
 if args.is_empty():quit(2);return
 dest=args[0]
 muted="--muted" in args
 recording="--record" in args
 if not InputMap.has_action("clutch"):InputMap.add_action("clutch")
 var ground:=StaticBody3D.new();var shape:=CollisionShape3D.new();var box:=BoxShape3D.new()
 box.size=Vector3(1600,1,1600);ground.position.y=-.5;shape.shape=box;ground.add_child(shape);root.add_child(ground)
 car=preload("res://Powertrain.gd").new()
 car.setup();root.add_child(car);car.restart(Vector3(0,.10,0),0.)
 if not muted:voice=preload("res://InlineFourSound.gd").new()
func _physics_process(dt):
 tick+=1
 var throttle:=0.;var brake:=0.
 if tick>110 and tick<=520:throttle=.89
 if tick>610 and tick<795:brake=1.
 if tick==661:car.set_manual(true)
 if tick==662:car.select_ratio(5)
 if tick==817:
  car.select_ratio(0)
  car.start_engine()
 car.drive(dt,throttle,brake,0.,false)
 var key=[tick,car.rpm,car.gear,car.stalled,car.position.x,car.position.y,car.position.z,car.linear_velocity.z,car.wheel_contact,car.shift_time]
 var sound=[car.sound_throttle,car.sound_combustion,car.sound_shaft_load]
 rows.append([key,sound])
 if voice:
  var frames:=368 if tick%2==0 else 367
  var sample=voice.render(frames,car.rpm,sound[1],sound[2],car.stalled,true,sound[0])
  if recording:
   var start:=pcm.size();pcm.resize(start+frames*2)
   for i in frames:pcm.encode_s16(start+i*2,roundi(32767.*sample[i].x))
 if tick==900:
  var f:=FileAccess.open(dest+".json",FileAccess.WRITE);f.store_string(JSON.stringify(rows));f.close()
  if not pcm.is_empty():
   var wav:=AudioStreamWAV.new();wav.format=AudioStreamWAV.FORMAT_16_BITS;wav.mix_rate=22050;wav.data=pcm;wav.save_to_wav(dest+".wav")
  quit()
 return false
