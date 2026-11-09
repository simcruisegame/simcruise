extends Node
# Standalone QA switch. Freeze each car pose only for photography, then drive
# with the same public inputs as the player.
var game
var write_failed = false
var trip_meter_ok = false
var captures=""
var started=0
var stable=0
var qa_cam=Camera3D.new()
func _ready():
 game=get_parent()
 var args=OS.get_cmdline_user_args()
 var index=args.find("--car-audit")
 if index < 0 or index+1 >= args.size() or not args[index+1].is_absolute_path():
  push_error("Car appearance check needs an absolute output directory")
  game.request_game_close(2)
  set_process(false)
  return
 captures=args[index+1]
 if DirAccess.make_dir_recursive_absolute(captures)!=OK:
  push_error("Could not create capture directory")
  game.request_game_close(2)
  set_process(false)
  return
 add_child(qa_cam);started=Time.get_ticks_msec()
func _process(_dt):
 if Time.get_ticks_msec()-started>45000:set_process(false);print("Audit timed out");game.request_game_close(3);return
 if not game.ready_drive or game.tiles.size()<3 or game.vehicle.wheel_contact!=4:
  stable=0
  return
 stable+=1
 if stable<32:return
 set_process(false)
 await capture()
func settle_save(view):
 for i in 14:
  await get_tree().process_frame
  await RenderingServer.frame_post_draw
 var status=get_viewport().get_texture().get_image().save_png(captures.path_join(view+".png"))
 write_failed = write_failed or status != OK
 print("CAR VIEW ",view," result=",status)
# Likeness frames for laying beside the reference photographs: a 50 mm lens on
# a 36 x 24 mm frame (27 degrees vertical) from the photographers' positions,
# saved as JPEG.
func settle_save_jpg(view):
 for i in 14:
  await get_tree().process_frame
  await RenderingServer.frame_post_draw
 var status=get_viewport().get_texture().get_image().save_jpg(captures.path_join(view+".jpg"),.92)
 write_failed = write_failed or status != OK
 print("CAR VIEW ",view," result=",status)
func capture():
 game.view=1;game.hud.visible=false
 await get_tree().process_frame
 game.set_process(false);game.set_physics_process(false);game.vehicle.freeze=true
 var t=game.vehicle.global_transform
 measure_car()
 game.camera.current=true
 game.cockpit.set_view(true,game.camera,game.vehicle,0.,.016,160.933)
 var lower_bound = game.cockpit.shown_tenths == 0 and game.cockpit.trip_label.text.ends_with("0.0")
 game.cockpit.set_view(true,game.camera,game.vehicle,0.,.016,160.935)
 var upper_bound = game.cockpit.shown_tenths == 1 and game.cockpit.trip_label.text.ends_with("0.1")
 game.cockpit.set_view(true,game.camera,game.vehicle,0.,.016,0.)
 trip_meter_ok = lower_bound and upper_bound and game.cockpit.shown_tenths==0
 game.camera.global_position=t*game.cockpit.EYE;game.camera.look_at(t*game.cockpit.SIGHT,t.basis.y)
 await settle_save("cockpit")
 qa_cam.current=true
 qa_cam.near=.03;qa_cam.far=1500.
 qa_cam.cull_mask=1|4;qa_cam.fov=32.
 qa_cam.global_position=t*Vector3(-.337,.784,.24)
 qa_cam.look_at(t*Vector3(-.337,.738,-.196),t.basis.y)
 await settle_save("gauges")
 game.cockpit.wheel.visible=false
 qa_cam.fov=30.;qa_cam.global_position=t*Vector3(-.337,.756,.270)
 qa_cam.look_at(t*Vector3(-.337,.748,-.196),t.basis.y)
 await settle_save("instrument-bench")
 game.cockpit.wheel.visible=true
 game.cockpit.set_view(false,game.camera,game.vehicle,0,.016)
 qa_cam.cull_mask=1|2;qa_cam.fov=40.
 for data in [["head-on",Vector3(0,.80,-5.65),Vector3(0,.48,-.90)],["front",Vector3(-4.,1.75,-4.85),Vector3(0,.6,0)],["rear",Vector3(-3.8,1.68,4.8),Vector3(0,.56,.05)],["wheel",Vector3(-2.4,.53,-1.38),Vector3(-.707,.32,-1.13)]]:
  qa_cam.global_position=t*data[1];qa_cam.look_at(t*data[2],t.basis.y)
  await settle_save(data[0])
 print("CAR LIKENESS VIEWS ",captures)
 qa_cam.fov=27.;qa_cam.keep_aspect=Camera3D.KEEP_HEIGHT
 for data in [["front-quarter",Vector3(-3.55,1.45,-5.35),Vector3(0,.52,-.25)],["side",Vector3(6.4,1.,0.),Vector3(0,.55,0.)],["rear-quarter",Vector3(-4.,1.35,5.3),Vector3(0,.55,.25)],["tail",Vector3(-.55,1.05,5.7),Vector3(0,.62,1.55)]]:
  qa_cam.global_position=t*data[1];qa_cam.look_at(t*data[2],t.basis.y)
  await settle_save_jpg(data[0])
 qa_cam.fov=40.
 game.set_process(true);game.set_physics_process(true);game.vehicle.freeze=false
 game.camera.current=true;game.view=1
 await control_check()
# Finished-car dimensions in millimetres above the ground under the loaded
# tyres (chassis frame, -Z forward), for comparison with the GLB audit.
func measure_car():
 var vehicle=game.vehicle
 var inv=vehicle.global_transform.affine_inverse()
 var ground=0.
 for w in game.wheels:ground+=w[0].position.y-vehicle.R
 ground/=game.wheels.size()
 var mm=func(y:float)->float:return roundf((y-ground)*1000.)
 var pts=func(node:MeshInstance3D)->PackedVector3Array:
  var out=PackedVector3Array()
  var xf:Transform3D=inv*node.global_transform
  for s in node.mesh.get_surface_count():
   for p in node.mesh.surface_get_arrays(s)[Mesh.ARRAY_VERTEX]:out.append(xf*p)
  return out
 var centre=func(node:MeshInstance3D)->Vector3:
  var box:AABB=(inv*node.global_transform)*node.get_aabb()
  return box.get_center()
 var result={"ground_y_chassis":snappedf(ground,.0001)}
 var model=game.vehicle.get_child(0) if game.vehicle.get_child_count()>0 else null
 for child in game.vehicle.get_children():
  if child.find_child("body_British racing green",true,false):model=child
 var body:MeshInstance3D=model.find_child("body_British racing green",true,false)
 var shell=pts.call(body)
 var low=INF
 var rear=-INF;var rear_y=0.
 var deck={}
 for p in shell:
  if p.y-ground<.05 or absf(p.x)>1.2:continue
  low=minf(low,p.y)
  if p.z>rear:rear=p.z;rear_y=p.y
  if absf(p.x)<.1 and p.z>1.1:
   var k=snappedf(p.z,.1)
   deck[k]=maxf(deck.get(k,-INF),p.y)
 result["ground_clearance"]=mm.call(low)
 result["rearmost_point_height"]=mm.call(rear_y)
 var profile={}
 for k in deck:profile[str(k)]=mm.call(deck[k])
 result["deck_centreline_height_by_z"]=profile
 for nm in ["Headlamp 1","rear_light_1","Plate","MirrorR","HandleR","ReflectorR","body_red"]:
  var node=model.find_child(nm,true,false)
  if node is MeshInstance3D:
   var c:Vector3=centre.call(node)
   var box:AABB=(inv*node.global_transform)*node.get_aabb()
   result[nm]={"centre_height":mm.call(c.y),"x":roundf(c.x*1000.),"z":roundf(c.z*1000.),"size_xyz":[roundf(box.size.x*1000.),roundf(box.size.y*1000.),roundf(box.size.z*1000.)]}
 var hub_heights=[]
 for w in game.wheels:hub_heights.append(mm.call(w[0].position.y))
 result["wheel_centre_heights"]=hub_heights
 var file=FileAccess.open(captures.path_join("car-measure.json"),FileAccess.WRITE)
 if file:file.store_string(JSON.stringify(result,"  "))
 print("CAR MEASURE ",JSON.stringify(result))
func check_wheel_mounts() -> bool:
 if game.wheels.size()!=4:return false
 var shared_mesh=preload("res://SportWheel.gd").wheel()
 for i in 4:
  var hub:Node3D=game.wheels[i][0]
  var roller:Node3D=game.wheels[i][1]
  var mounted:MeshInstance3D=roller.get_child(roller.get_child_count()-1)
  var caliper:MeshInstance3D=hub.get_child(hub.get_child_count()-1)
  var side_sign=-1. if i%2==0 else 1.
  if mounted.mesh!=shared_mesh or signf(mounted.transform.basis.x.x)!=side_sign:return false
  if hub.get_parent()!=game.vehicle or roller.get_parent()!=hub or caliper.get_parent()!=hub:return false
  if game.wheels[i][2]!=(i<2) or signf(hub.position.x)!=side_sign:return false
  if absf(hub.position.z-game.vehicle.MOUNTS[i].z)>.001:return false
  if game.lamp_nodes.any(func(lamp):return hub.is_ancestor_of(lamp)):return false
 return true

func control_check():
 var references=[]
 for l in game.lamp_nodes:references.append(game.vehicle.global_transform.affine_inverse()*l.global_transform)
 var max_tach_degrees=0.
 var max_speed_degrees=0.
 var first_tach=game.cockpit.tach.rotation.z
 var first_speed=game.cockpit.speedometer.rotation.z
 var rotation=0.
 var max_angle=0.
 var lighting=false
 var released=false
 var error=0.
 var peak_mps=0.
 var peak_rpm=0.
 var max_steer=0.
 for i in 240:
  if i==0:Input.action_press("gas",.7)
  if i==40:Input.action_press("right",.16)
  if i==64:Input.action_release("right")
  if i==120:Input.action_release("gas");Input.action_press("brake",.75)
  if i==190:Input.action_release("brake")
  await get_tree().physics_frame
  max_tach_degrees=maxf(max_tach_degrees,rad_to_deg(absf(first_tach-game.cockpit.tach.rotation.z)))
  max_speed_degrees=maxf(max_speed_degrees,rad_to_deg(absf(first_speed-game.cockpit.speedometer.rotation.z)))
  rotation+=absf(game.vehicle.omega[2])/Engine.physics_ticks_per_second
  max_steer=maxf(max_steer,absf(game.vehicle.steering))
  peak_mps=maxf(peak_mps,absf(game.velocity))
  peak_rpm=maxf(peak_rpm,game.vehicle.rpm)
  lighting=lighting or (game.lamp_on and game.lamp_materials[0].emission_energy_multiplier>1.)
  released=released or (i>190 and not game.lamp_on and game.lamp_materials[0].emission_energy_multiplier==0.)
  for j in references.size():
   var p=game.vehicle.global_transform.affine_inverse()*game.lamp_nodes[j].global_transform
   error=maxf(error,p.origin.distance_to(references[j].origin))
   max_angle=maxf(max_angle,p.basis.get_rotation_quaternion().angle_to(references[j].basis.get_rotation_quaternion()))
 var mounts_ok=check_wheel_mounts()
 var result={"four_wheel_mounts":mounts_ok,"trip_mile_threshold":trip_meter_ok,"tach_sweep_degrees":max_tach_degrees,"speed_sweep_degrees":max_speed_degrees,"fixed_lamps":references.size(),"chassis_lamp_drift_m":error,"chassis_lamp_rotation_rad":max_angle,"tyre_revolutions":rotation/TAU,"brake_light_on":lighting,"brake_light_released":released,"peak_speed_mps":peak_mps,"peak_rpm":peak_rpm,"steering_rad":max_steer,"wheelbase_m":game.vehicle.WHEELBASE_M,"tyre_radius_m":game.vehicle.R,"wheel_contacts_end":game.vehicle.wheel_contact,"renderer":RenderingServer.get_video_adapter_name()}
 var file=FileAccess.open(captures.path_join("controls.json"),FileAccess.WRITE)
 if file:
  file.store_string(JSON.stringify(result,"  "))
 else:
  write_failed=true
 print("CAR CONTROL ",JSON.stringify(result))
 var lamps_ok = references.size()==3 and error<.001 and max_angle<.003 and released and lighting
 var controls_ok = peak_mps>4.5 and max_steer>.08 and rotation>3.*TAU and game.vehicle.wheel_contact==4
 var gauges_ok = max_speed_degrees>15. and max_tach_degrees>40. and trip_meter_ok
 game.request_game_close(0 if mounts_ok and lamps_ok and controls_ok and gauges_ok and not write_failed else 5)
