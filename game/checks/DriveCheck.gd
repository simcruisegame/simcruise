extends Node
var game
var lamp_poses=[]
var lamp_drift=0.
var lamp_angle=0.
var wheel_travel=0.
var brake_glow=false
var lights_released=false
var stage=0
var now=0.
var total=0.
var previous=0
var frames=[]
var seen={}
var max_tiles=0
var peak=0.
var initial_heading=0.
var distance_before=0.
var collision=false
var out=""
var finished=false
func _ready():
 out=OS.get_environment("DRIVE_QA");game=get_parent()
 if out=="":out=ProjectSettings.globalize_path("user://road-test")
 DirAccess.make_dir_recursive_absolute(out)
func change(n):
 stage=n;now=0;print("stage ",n);var f=FileAccess.open(out+"/stage.txt",FileAccess.WRITE);f.store_string(str(n))
func snap(filename):
 var im=get_tree().root.get_texture().get_image();im.resize(1360,816,Image.INTERPOLATE_LANCZOS);im.save_jpg(out+"/"+filename+".jpg",.93)
# End the test once: one line, no further _process, then the graceful close
# that lets tile workers return instead of quitting under them.
func finish(code:int,line:String=""):
 if finished:return
 finished=true
 set_process(false)
 if line!="":print(line)
 game.request_game_close(code)
func _process(dt):
 if finished or game.closing:return
 advance(dt)
func advance(dt):
 now+=dt;total+=dt
 if total>235:finish(2,"TIMEOUT %d %s %s"%[stage,str(game.pos),str(game.velocity)]);return false
 if not game.ready_drive:return false
 if lamp_poses.size()>0:
  for i in game.lamp_nodes.size():
   var pose=game.vehicle.global_transform.affine_inverse()*game.lamp_nodes[i].global_transform
   lamp_drift=maxf(lamp_drift,pose.origin.distance_to(lamp_poses[i].origin))
   lamp_angle=maxf(lamp_angle,pose.basis.get_rotation_quaternion().angle_to(lamp_poses[i].basis.get_rotation_quaternion()))
  wheel_travel+=absf(game.vehicle.omega[2])*dt
  if game.lamp_on and game.lamp_materials[0].emission_energy_multiplier>1:brake_glow=true
  if brake_glow and not game.lamp_on and game.lamp_materials[0].emission_energy_multiplier==0:lights_released=true
 var us=Time.get_ticks_usec()
 if stage==1 and previous>0:frames.append((us-previous)/1000.)
 previous=us
 match stage:
  0:
   if now>6.6 and not game.job and game.tiles.size()>=4:
    for light in game.lamp_nodes:lamp_poses.append(game.vehicle.global_transform.affine_inverse()*light.global_transform)
    print("lamp meshes=",lamp_poses.size()," roller children=",game.wheels[2][1].get_child_count())
    print("loaded rendered ",get_tree().root.size," tiles=",game.tiles.size()," wheel contacts=",game.vehicle.wheel_contact);game.hud.message("");snap("carytown");initial_heading=game.yaw;Input.action_press("gas");change(1)
  1:
   if now>1.8 and now<2.1:Input.action_press("right",.12)
   else:Input.action_release("right")
   if now>2.6:
    print("throttle ",game.velocity," yaw change=",game.yaw-initial_heading," rpm=",game.vehicle.rpm);Input.action_release("gas");Input.action_press("handbrake");change(2)
  2:
   if now>1.8:
    snap("braking");print("braked ",game.velocity," wheel contacts=",game.vehicle.wheel_contact);Input.action_release("handbrake");Input.action_press("brake");change(3)
  3:
   if now>2:
    print("reverse ",game.velocity," reverse gear=",game.vehicle.in_reverse);Input.action_release("brake");game.recover()
    var rr=game.nearest_world();var dir=(rr[0][1][1]-rr[0][1][0]).normalized();var toward=Vector2(-dir.y,dir.x)
    game.vehicle.restart(Vector3(game.pos.x,game.ground(game.pos)+.18,game.pos.y),atan2(-toward.x,-toward.y));game.vehicle.contact_monitor=true;game.vehicle.max_contacts_reported=8
    game.vehicle.body_entered.connect(func(_o):collision=true);Input.action_press("gas");change(4)
  4:
   if now>5:
    snap("impact");print("impact contact=",collision," speed=",game.velocity);Input.action_release("gas");game.hud.open_search();game.hud.field.text="37.779225, -100.168317";game.hud.submit();game.hud.choices.get_child(0).pressed.emit();change(5)
  5:
   if game.jump.is_empty() and "37.779" in game.current_title:
    print("KANSAS ",game.origin," cache requests=",game.calls);Input.action_press("gas");change(6)
  6:
   var speed=game.velocity
   Input.action_release("gas")
   if speed<25.3:Input.action_press("gas",clampf((25.3-speed)*.48+.105,0,1))
   peak=maxf(peak,speed);seen[game.tile_id(game.pos)]=true;max_tiles=maxi(game.tiles.size(),max_tiles)
   # Follow the surveyed highway for the stream test using normal steering inputs.
   var best=null;var gap=INF;var offset=Vector2.ZERO
   for id in game.tiles:
    var t=game.tiles[id];var o=game.tile_offset(id)
    for sg in t.segments:
     if sg[5]!="trunk":continue
     var q=Geometry2D.get_closest_point_to_segment(game.pos-o,sg[0],sg[1]);var d=q.distance_to(game.pos-o)
     if d<gap:gap=d;best=[q,sg];offset=o
   if best!=null:
    var f=(best[1][1]-best[1][0]).normalized();var cur=Vector2(-sin(game.yaw),-cos(game.yaw));if f.dot(cur)<0:f=-f
    var aim=best[0]+offset+f*maxf(11,absf(speed)*.74)+Vector2(-f.y,f.x)*1.6-game.pos;var target=atan2(-aim.x,-aim.y);var ang=angle_difference(game.yaw,target)
    Input.action_release("left");Input.action_release("right")
    if absf(ang)>.012:Input.action_press("left" if ang>0 else "right",minf(1,absf(ang)*2.5))
   if int(now*4)%20==0 and dt>.001 and Engine.get_process_frames()%10==0:print("sample ",snappedf(now,.1)," ",game.distance," m ",speed," m/s gear ",game.vehicle.gear+1," rpm ",roundi(game.vehicle.rpm))
   if (game.distance>3200 and seen.size()>=6) or now>185:
    Input.action_release("gas");snap("kansas");print("LAMPS max_body_drift_m=",lamp_drift," max_angle_rad=",lamp_angle," wheel_revs=",wheel_travel/TAU," turned_on=",brake_glow," turned_off=",lights_released);frames.sort();print("RESULT continuous_m=",game.distance," visited=",seen.size()," peak_mps=",peak," max_active=",max_tiles," local_origin=",game.origin," provider_calls=",game.calls," median_ms=",frames[int(frames.size()*.5)]," p95_ms=",frames[int(frames.size()*.95)]);var pass_check=seen.size()>=6 and game.distance>3200 and lamp_poses.size()==3 and lamp_drift<.005 and lamp_angle<.004 and brake_glow and lights_released and wheel_travel>TAU*8
    var report=FileAccess.open(out+"/drive-result.json",FileAccess.WRITE);report.store_string(JSON.stringify({"metres":game.distance,"tiles":seen.size(),"active_max":max_tiles,"source_requests":game.calls,"frame_median_ms":frames[int(frames.size()*.5)],"frame_p95_ms":frames[int(frames.size()*.95)],"peak_speed_mps":peak,"lamp_drift_m":lamp_drift,"lamp_rotation_rad":lamp_angle,"wheel_revolutions":wheel_travel/TAU,"lamps_illuminated":brake_glow,"lamps_released":lights_released,"road_test_passed":pass_check,"end_origin_tile":[game.origin.x,game.origin.y]}, "  "));report.close()
    finish(0 if pass_check else 1,"ROAD TEST PASSED" if pass_check else "ROAD TEST FAILED continuous_m=%.0f visited=%d lamps=%d drift=%.4f angle=%.4f glow=%s released=%s revs=%.1f"%[game.distance,seen.size(),lamp_poses.size(),lamp_drift,lamp_angle,str(brake_glow),str(lights_released),wheel_travel/TAU])
 return false
