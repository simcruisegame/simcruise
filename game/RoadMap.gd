extends Control
const SCALES=[1.3,.74,.37,.185,.0925,.046,.023,.0115,.00575,.002875,.00144]
var level=4
var game
var center=Vector2.ZERO
var pinned=false
var pin=Vector2.ZERO
var dragging=false
var press_at=Vector2.ZERO
var moved=false
var status:Label
var coords:Label
var transmission_button:Button
var car_button:Button
var photos_button:Button
var ground_cover_button:Button
var photo_status:Label
var images={}
var retries={}
var worker:Thread
var pending=""
var next=[]
var idle=0.
var font=preload("res://art/ui.ttf")
func setup(g):
 game=g;hide();clip_contents=true;mouse_filter=Control.MOUSE_FILTER_STOP
 set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
 var bar=PanelContainer.new();add_child(bar);bar.set_anchors_and_offsets_preset(Control.PRESET_TOP_WIDE);bar.add_theme_stylebox_override("panel",g.hud.style(Color("#172522"),8,10))
 var controls=VBoxContainer.new();controls.add_theme_constant_override("separation",4);bar.add_child(controls)
 var row=HBoxContainer.new();row.add_theme_constant_override("separation",12);controls.add_child(row)
 var labels=VBoxContainer.new();labels.size_flags_horizontal=SIZE_EXPAND_FILL;row.add_child(labels)
 status=g.hud.label("",21);coords=g.hud.label("",14,Color("#b2c3b9"));labels.add_child(status);labels.add_child(coords)
 status.text_overrun_behavior=TextServer.OVERRUN_TRIM_ELLIPSIS
 coords.text_overrun_behavior=TextServer.OVERRUN_TRIM_ELLIPSIS
 for spec in [["−",func():zoom(1)],["+",func():zoom(-1)],["Recenter",recenter],["Back to drive",close_map]]:
  var button=g.hud.button(spec[0],spec[1]);row.add_child(button)
 var settings=HFlowContainer.new();settings.add_theme_constant_override("h_separation",8);settings.add_theme_constant_override("v_separation",4);controls.add_child(settings)
 car_button=g.hud.button("Car · "+g.vehicle.spec.name,g.cycle_car);settings.add_child(car_button)
 transmission_button=g.hud.button("",g.toggle_manual);settings.add_child(transmission_button)
 photos_button=g.hud.button("",g.toggle_street_detail);settings.add_child(photos_button)
 settings.add_child(g.hud.button("Source photo ↗",func():g.street_detail.open_credit(g)))
 settings.add_child(g.hud.button("Imagery",g.toggle_recent));settings.add_child(g.hud.button("Weather",g.toggle_weather))
 ground_cover_button=g.hud.button("",g.toggle_spectral_ground);settings.add_child(ground_cover_button)
 ground_cover_button.tooltip_text="Use USDA near-infrared aerial imagery to identify low vegetation near the car."
 photo_status=g.hud.label("",14,Color("#b2c3b9"))
 photo_status.text_overrun_behavior=TextServer.OVERRUN_TRIM_ELLIPSIS
 controls.add_child(photo_status)
 var reference=g.hud.button("Compare Street View ↗",open_reference)
 add_child(reference)
 reference.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_RIGHT)
 reference.offset_left=-216;reference.offset_right=-16;reference.offset_top=-78;reference.offset_bottom=-39
 if not visibility_changed.is_connected(_visibility):visibility_changed.connect(_visibility)
func _visibility():
 if not visible:dragging=false
func open_map():
 if not game.ready_drive:return
 visible=true;move_to_front();recenter()
func open_reference():
 var point=coordinates(pin if pinned else game.pos)
 var heading=fposmod(-rad_to_deg(game.yaw),360.)
 OS.shell_open("https://www.google.com/maps/@?api=1&map_action=pano&viewpoint=%.7f,%.7f&heading=%.1f&pitch=0&fov=80"%[point.x,point.y,heading])
func close_map():
 hide()
func recenter():
 center=game.pos;pinned=false;refresh()
func zoom(step:int,anchor=Vector2.INF):
 var before:float=SCALES[level];level=clampi(level+step,0,SCALES.size()-1)
 if anchor.is_finite():center+=(anchor-size*.5)*(1./before-1./float(SCALES[level]))
 refresh()
func coordinates(p):
 var x=float(game.origin.x)+float(p.x)/game.side
 var y=float(game.origin.y)+float(p.y)/game.side
 return Vector2(rad_to_deg(atan(sinh(PI*(1.-2.*y/65536.)))),x/65536.*360.-180.)
func header():
 transmission_button.text="Transmission · "+("Manual" if game.vehicle.manual else "Automatic")
 photos_button.text="Street photos · "+("On" if game.street_detail.enabled else "Off")
 ground_cover_button.text="USDA ground cover · "+("On" if game.spectral_ground.enabled else "Off")
 photo_status.text=game.street_detail.label(game)
 if game.spectral_ground.enabled:photo_status.text+="   ·   "+game.spectral_ground.label(game)
 var tile=game.tiles.get(game.tile_id(game.pos))
 if tile and tile.info.has("building_status"):
  var heights=tile.info.get("enriched_heights",{}).size()
  var uses=tile.info.get("enriched_uses",{}).size()
  if heights or uses:photo_status.text+="   ·   Buildings near car: %d height updates, %d uses"%[heights,uses]
 if tile and not tile.info.get("live_roof_profiles",{}).is_empty():
  photo_status.text+="   ·   %d lidar roof shapes near car" % tile.info.live_roof_profiles.size()
 photo_status.tooltip_text=photo_status.text
 if tile:
  var project=tile.info.get("roof_observation",{}).get("project")
  if project is Dictionary:
   photo_status.tooltip_text+="\nUSGS 3DEP · %s · %s to %s" % [project.get("name",""),project.get("start",""),project.get("end","")]
 var loc=coordinates(pin if pinned else game.pos)
 status.text=("Pinned spot" if pinned else "You are on "+game.road_name)
 coords.text="%.5f°, %.5f°   ·   %s"%[loc.x,loc.y,game.current_title]+"   ·   "+game.weather_label()+"   ·   "+game.recent_label()
func refresh():
 if not visible or game==null or not is_instance_valid(status):return
 header();next.clear()
 var z=clampi(roundi(16.-log(460./(game.side*SCALES[level]))/log(2.)),4,14)
 var count=1<<(16-z);var step=game.side*count
 var ext=size*.5/SCALES[level]
 var base=Vector2(game.origin)*game.side
 var lo=Vector2i(((base+center-ext)/step).floor());var hi=Vector2i(((base+center+ext)/step).floor())
 for x in range(lo.x,hi.x+1):
  for y in range(lo.y,hi.y+1):
   if x<0 or y<0 or x>=(1<<z) or y>=(1<<z):continue
   var id=Vector3i(x,y,z);next.append(id)
 next.sort_custom(func(a,b):return tile_center(a).distance_squared_to(center)<tile_center(b).distance_squared_to(center))
 queue_redraw()
func tile_center(id):
 var count=1<<(16-id.z)
 return Vector2(Vector2i(id.x,id.y)*count-game.origin)*game.side+Vector2.ONE*game.side*count*.5
func _notification(what):
 if what==NOTIFICATION_RESIZED and visible:call_deferred("refresh")
func _process(dt):
 if worker and not worker.is_alive():
  var result=worker.wait_to_finish();worker=null
  if result.has("im"):
   if images.size()>=64:images.erase(images.keys()[0])
   images[pending]=[ImageTexture.create_from_image(result.im),result.names]
  else:retries[pending]=Time.get_ticks_msec()+60000
  queue_redraw()
 if not visible:return
 idle+=dt
 if idle>.5:header();idle=0.
 var pads=Input.get_connected_joypads()
 if pads.size():
  var v=Vector2(Input.get_joy_axis(pads[0],JOY_AXIS_LEFT_X),Input.get_joy_axis(pads[0],JOY_AXIS_LEFT_Y))
  if v.length()>.19:center+=v*dt*430./SCALES[level];refresh()
 if worker:return
 for id in next:
  var key="%d_%d_%d"%[id.z,id.x,id.y]
  if images.has(key) or retries.get(key,0)>Time.get_ticks_msec():continue
  pending=key;worker=Thread.new();worker.start(load_map.bind(key,id,game.helper,game.cache));break
func _exit_tree():
 if worker:worker.wait_to_finish()
func svg_points(points):
 var arr=PackedStringArray()
 for p in points:arr.append("%d,%d"%[p[0],p[1]])
 return " ".join(arr)
func load_map(key,id,helper,cache):
 var path="res://data/map-"+key+".json";var raw=""
 if FileAccess.file_exists(path):raw=FileAccess.get_file_as_string(path)
 else:
  var result=[];var code=OS.execute(helper,["map",cache,str(id.z),str(id.x),str(id.y)],result,true)
  if code!=0 or result.is_empty():return {}
  raw=str(result[0])
 var d=JSON.parse_string(raw)
 if not d is Dictionary or not d.has("lines"):return {}
 var svg=PackedStringArray(['<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 4096 4096"><rect width="4096" height="4096" fill="#22332e"/>'])
 for f in d.fills:
  var paths=PackedStringArray()
  for r in f[1]:paths.append("M"+svg_points(r)+"Z")
  svg.append('<path fill="%s" fill-rule="evenodd" d="%s"/>'%["#294c5a" if f[0]==0 else "#2c4435"," ".join(paths)])
 var colors=["#626a61","#54675e","#66796c","#849385","#b5b798","#cfa96b"]
 var widths=[6,7,9,12,16,22]
 for f in d.lines:
  svg.append('<polyline fill="none" stroke="%s" stroke-width="%d" stroke-linejoin="round" points="%s"/>'%[colors[int(f[0])],widths[int(f[0])],svg_points(f[1])])
 svg.append("</svg>");var im=Image.new()
 if im.load_svg_from_string("".join(svg))!=OK:return {}
 im.generate_mipmaps();return {"im":im,"names":d.names}
func screen(p):return (p-center)*SCALES[level]+size*.5
func _draw():
 if not visible or game==null:return
 draw_rect(Rect2(Vector2.ZERO,size),Color("#22332e"))
 var spots=[];var used={}
 for id in next:
  var key="%d_%d_%d"%[id.z,id.x,id.y]
  if not images.has(key):continue
  var width=game.side*(1<<(16-id.z))*SCALES[level];var pt=screen(tile_center(id))-Vector2.ONE*width*.5
  draw_texture_rect(images[key][0],Rect2(pt,Vector2.ONE*(width+.25)),false)
  for name in images[key][1]:
   var p=pt+Vector2(name[1],name[2])*width/4096.
   if p.x<22 or p.x>size.x-22 or p.y<92 or p.y>size.y-54:continue
   spots.append([name[3],name[0],p,name[4]])
 if level<5:
  for tile in game.tiles.values():
   var o=Vector2(tile.position.x,tile.position.z)
   for road in tile.segments:
    var a=screen(o+road[0]);var b=screen(o+road[1]);var viewport=Rect2(Vector2.ZERO,size)
    if not viewport.intersects(Rect2(a,b-a).abs().grow(2)):continue
    draw_line(a,b,Color("#a4ac91"),clampf(road[3]*SCALES[level]*.7,1.2,6.),true)
    if road[2]!="" and road[2]!=game.road_name and a.distance_to(b)>28:spots.append([5,road[2],(a+b)*.5,0])
 spots.sort_custom(func(a,b):return a[0]<b[0]);var occupied=[];var car=screen(game.pos)
 occupied.append(Rect2(car-Vector2(74,24),Vector2(148,50)))
 for spot in spots:
  var title=str(spot[1]);if used.has(title) or occupied.size()>65:continue
  var p:Vector2=spot[2];var px=15 if spot[3] else 12
  var w=font.get_string_size(title,HORIZONTAL_ALIGNMENT_LEFT,-1,px).x;var box=Rect2(p-Vector2(w*.5+5,px),Vector2(w+10,px+5))
  if box.position.y<80:continue
  var overlap=false
  for other in occupied:
   if other.intersects(box):overlap=true;break
  if overlap:continue
  occupied.append(box);used[title]=true;var at=p-Vector2(w*.5,0)
  draw_string_outline(font,at,title,HORIZONTAL_ALIGNMENT_LEFT,-1,px,4,Color("#1c2925"));draw_string(font,at,title,HORIZONTAL_ALIGNMENT_LEFT,-1,px,Color("#e0e6d7"))
 var dir=Vector2(-sin(game.yaw),-cos(game.yaw));var right=Vector2(-dir.y,dir.x)
 draw_circle(car,14,Color("#173824"));draw_colored_polygon(PackedVector2Array([car+dir*11,car-dir*7+right*6,car-dir*4,car-dir*7-right*6]),Color("#c8f692"))
 draw_string_outline(font,car+Vector2(18,5),"You",HORIZONTAL_ALIGNMENT_LEFT,-1,16,4,Color("#15221f"));draw_string(font,car+Vector2(18,5),"You",HORIZONTAL_ALIGNMENT_LEFT,-1,16,Color.WHITE)
 if pinned:draw_circle(screen(pin),7,Color("#edb66e"));draw_arc(screen(pin),11,0,TAU,24,Color("#edb66e"),2,true)
 var scale_m=10.;while scale_m*SCALES[level]<55:scale_m*=2.
 var bx=Vector2(24,size.y-28);draw_line(bx,bx+Vector2.RIGHT*scale_m*SCALES[level],Color.WHITE,2)
 draw_string(font,bx-Vector2(0,7),("%.1f mi"%(scale_m/1609.344)) if scale_m>=400 else ("%d ft"%roundi(scale_m*3.28084)),HORIZONTAL_ALIGNMENT_LEFT,-1,13,Color.WHITE)
 draw_string(font,Vector2(size.x-284,size.y-15),"N ↑    © OpenStreetMap contributors",HORIZONTAL_ALIGNMENT_LEFT,-1,13,Color("#c1cdc4"))
func _gui_input(e):
 if e is InputEventMouseButton:
  if e.button_index==MOUSE_BUTTON_WHEEL_DOWN and e.pressed:zoom(1,e.position)
  elif e.button_index==MOUSE_BUTTON_WHEEL_UP and e.pressed:zoom(-1,e.position)
  elif e.button_index==MOUSE_BUTTON_LEFT:
   if e.pressed:dragging=true;press_at=e.position;moved=false
   elif dragging:
    dragging=false
    if not moved:pin=center+(e.position-size*.5)/SCALES[level];pinned=true;refresh()
  accept_event()
 elif e is InputEventMouseMotion and dragging:
  if press_at.distance_to(e.position)>3:moved=true
  if moved:center-=e.relative/SCALES[level];refresh()
  accept_event()
