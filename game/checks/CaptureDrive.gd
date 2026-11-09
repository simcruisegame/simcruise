extends Node
var game
var stage=0
var elapsed=0.
var total=0.
var start=Vector2.ZERO
var origin_tile
var times=[]
var prev_time=0
var minimum_contacts=4
var maximum_roll_deg=0.
var peak_roll_condition=""
var settled_seconds=0.
var out=""
var film=false

func _ready():
 game=get_parent()
 game.helper="/usr/bin/false"
 var args=OS.get_cmdline_user_args()
 out=args[args.find("--capture-drive")+1]
 film="--film" in args
 DirAccess.make_dir_recursive_absolute(out)

func fail(message):
 push_error(message)
 game.request_game_close(1)

func _process(dt):
 elapsed+=dt; total+=dt
 if total>65:fail("Capture approach timed out at "+str(stage));return
 if not game.ready_drive or not game.jump.is_empty():
  settled_seconds=0.
  return
 if stage<2:
  var car=game.vehicle
  var settled=car.wheel_contact==4 and absf(car.linear_velocity.y)<.18 and car.angular_velocity.length()<.12
  settled_seconds=settled_seconds+dt if settled else 0.
 match stage:
  0:
   if elapsed<2.2 or settled_seconds<.55:return
   var forward=-game.vehicle.global_basis.z
   if game.view!=1 or forward.z<=0.:fail("Expected a seated view facing the Chula junction");return
   game.hud.open_search();game.hud.field.text="Chula Gas Mart";game.hud.submit()
   if game.hud.choices.get_child_count()!=1:fail("Capture approach search");return
   game.hud.choices.get_child(0).pressed.emit();stage=1;elapsed=0.;settled_seconds=0.
  1:
   if elapsed<2.8 or settled_seconds<.55 or game.tiles.size()<4:return
   var tile=game.tiles[game.tile_id(game.pos)]
   if game.road_name!="Patrick Henry Highway" or tile.capture.record.is_empty() or tile.capture.mask==null:fail("No measured surfaces at approach");return
   if (-game.vehicle.global_basis.z).x<=0.:fail("Incorrect direction on eastbound carriageway");return
   start=game.pos;origin_tile=game.tile_id(game.pos);game.hud.message("")
   if film and not game.sound_enabled:game.toggle_sound()
   print("DRIVE START ticks=",Engine.get_process_frames())
   Input.action_press("gas",.59);stage=2;elapsed=0.
  2:
   minimum_contacts=mini(minimum_contacts,game.vehicle.wheel_contact)
   var roll_deg=absf(rad_to_deg(game.vehicle.rotation.z))
   if roll_deg>maximum_roll_deg:
    maximum_roll_deg=roll_deg
    peak_roll_condition="at %.2fs, coordinate %s, steering %.3f"%[elapsed,game.player_coordinates(),game.vehicle.steering_input]
   var now=Time.get_ticks_usec()
   if prev_time>0:times.append((now-prev_time)/1000.)
   prev_time=now
   if film:
    if elapsed>3. and not game.hud.clean_view:game.hud.toggle_driving_panels()
    # Use the same right-lane steering as the Chula road acceptance drive.
    var local_tile=game.tiles.get(game.tile_id(game.pos))
    if local_tile:
     var basis=game.vehicle.global_basis
     var heading=Vector2(-basis.z.x,-basis.z.z).normalized()
     var ofs=game.tile_offset(game.tile_id(game.pos))
     var probe_point=game.pos+heading*12.
     var hit=local_tile.nearest(probe_point-ofs,true,"Patrick Henry Highway")
     if hit.size():
      var tangent=(hit[1][1]-hit[1][0]).normalized()
      if tangent.dot(heading)<0.:tangent=-tangent
      var delta=ofs+hit[0]+Vector2(-tangent.y,tangent.x)*1.45-game.pos
      var lateral=delta.dot(Vector2(basis.x.x,basis.x.z))
      var command=clampf(-atan2(4.532*lateral,maxf(1.,delta.length_squared()))/game.vehicle.available_steering(game.velocity),-1.,1.)
      Input.action_release("left");Input.action_release("right")
      if absf(command)>.001:Input.action_press("left" if command>0 else "right",absf(command))
    Input.action_press("gas",clampf((14.-game.velocity)*.17,.04,.59))
    if elapsed<14.5:return
   elif elapsed<6.2:return
   Input.action_release("gas")
   if minimum_contacts!=4 or maximum_roll_deg>4.:
    fail("Survey roadbed lost contact or banked excessively: contacts=%d roll=%.2f %s"%[minimum_contacts,maximum_roll_deg,peak_roll_condition]);return
   if game.pos.distance_to(start)<28:fail("Eastbound drive did not advance");return
   if DisplayServer.get_name()!="headless":get_viewport().get_texture().get_image().save_jpg(out+"/US360-cockpit.jpg",.94)
   times.sort();print("CAPTURE DRIVE metres=",game.pos.distance_to(start)," road=",game.road_name," direction=",-game.vehicle.global_basis.z," wheels=",game.vehicle.wheel_contact," min_contacts=",minimum_contacts," max_roll_deg=",maximum_roll_deg," tiles=",game.tiles.size()," helper_requests=",game.calls," frames=",times.size()," median_ms=",times[times.size()/2]," p95_ms=",times[int(times.size()*.95)])
   print("CAPTURE DRIVE PASSED")
   game.request_game_close(0)
