extends CanvasLayer
signal search_for(query)
signal choose(lon,lat,title,street,bearing)
signal camera
signal reset
signal sound
# HUD levels, cycled with F2 or the Xbox left stick and remembered as the typed
# "hud_level" preference. Full keeps the original opaque header and footer.
# Glance shows a translucent place tag and the minimap, fading to GLANCE_FADED
# while rolling and brightening when the speed changes or a message arrives,
# plus a controls legend that hides itself LEGEND_S after a mode or view
# change. Off keeps only the prompts, which stay visible at every level.
const OFF=0
const GLANCE=1
const FULL=2
const LEVEL_NAMES=["Off","Glance","Full"]
const GLANCE_FADED=.4
const GLANCE_FADE_S=.35
const SPEED_WAKE_MPH=3
const WAKE_S=2.5
const LEGEND_S=6.0
var game
var root=Control.new()
var top=HBoxContainer.new()
var driving_panels:Array[Control]=[]
var clean_view=true
var level=GLANCE
var header:PanelContainer
var bottom:PanelContainer
var location:VBoxContainer
var mapbox:PanelContainer
var streetbg:PanelContainer
var tag:PanelContainer
var tag_road=Label.new()
var tag_title=Label.new()
var tag_speed=Label.new()
var tag_units:Label
var legend:PanelContainer
var legend_text=Label.new()
var legend_time=0.0
var hint=""
var glance_alpha=1.0
var wake_time=0.0
var shown_view=-1
var shown_mph=0
var moving=false
var hud_button
var sound_pref_button
var motion_slider:HSlider
var place=Label.new()
var speed=Label.new()
var gear=Label.new()
var street=Label.new()
var trip=Label.new()
var note=Label.new()
var map
var atlas_map=preload("res://RoadMap.gd").new()
var modal=PanelContainer.new()
var field=LineEdit.new()
var choices=VBoxContainer.new()
var feedback=Label.new()
var instructions:Label
var mode_button
var sound_button
var font=preload("res://art/ui.ttf")
var display=preload("res://art/title.ttf")
var audio_on=false
const INK=Color("#15221f")
const GLASS=Color("#15221f9e")
const WHITE=Color("#f4efdb")
const LIME=Color("#c6ef96")
func style(bg:Color,roundness=9,margin=10):
 var s=StyleBoxFlat.new();s.bg_color=bg;s.set_corner_radius_all(roundness)
 s.content_margin_left=margin;s.content_margin_right=margin;s.content_margin_top=margin*.55;s.content_margin_bottom=margin*.55;return s
func label(text,px,color=WHITE):
 var l=Label.new();l.text=text;l.add_theme_font_size_override("font_size",px);l.modulate=color;return l
func button(text,action):
 var b=Button.new();b.text=text;b.focus_mode=Control.FOCUS_NONE;b.add_theme_stylebox_override("normal",style(Color("#32443b"),5,12));b.add_theme_stylebox_override("hover",style(Color("#4d6250"),5,12));b.add_theme_stylebox_override("pressed",style(Color("#405844"),5,12));b.pressed.connect(action);return b
func anchored(c,l,t,r,b,x1,y1,x2,y2):
 root.add_child(c);c.anchor_left=l;c.anchor_top=t;c.anchor_right=r;c.anchor_bottom=b;c.offset_left=x1;c.offset_top=y1;c.offset_right=x2;c.offset_bottom=y2
func build(g):
 game=g;add_child(root);root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT);root.mouse_filter=Control.MOUSE_FILTER_IGNORE
 var theme=Theme.new();theme.default_font=font;theme.default_font_size=18;root.theme=theme
 header=PanelContainer.new();header.add_theme_stylebox_override("panel",style(INK,0,20));anchored(header,0,0,1,0,0,0,0,51);header.add_child(top);top.add_theme_constant_override("separation",14)
 var title=label("SimCruise",31);title.add_theme_font_override("font",display);top.add_child(title)
 var div=label(" / ",20,Color("#84978a"));top.add_child(div);place.text=g.current_title;place.size_flags_horizontal=Control.SIZE_EXPAND_FILL;place.text_overrun_behavior=TextServer.OVERRUN_TRIM_ELLIPSIS;top.add_child(place)
 mode_button=button("Chase view   C",func():camera.emit());top.add_child(mode_button)
 sound_button=button("Sound off   M",func():sound.emit());top.add_child(sound_button)
 var find=button("Find a place   /",open_search);find.add_theme_stylebox_override("normal",style(LIME,5,14));find.add_theme_color_override("font_color",INK);top.add_child(find)
 bottom=PanelContainer.new();bottom.add_theme_stylebox_override("panel",style(INK,0,20));anchored(bottom,0,1,1,1,0,-78,0,0)
 var row=HBoxContainer.new();row.add_theme_constant_override("separation",14);bottom.add_child(row)
 speed.add_theme_font_size_override("font_size",46);speed.custom_minimum_size.x=51;speed.horizontal_alignment=HORIZONTAL_ALIGNMENT_RIGHT;row.add_child(speed)
 var units=VBoxContainer.new();units.alignment=BoxContainer.ALIGNMENT_CENTER;units.add_theme_constant_override("separation",-3);row.add_child(units);units.add_child(label("MPH",16,LIME));gear.add_theme_font_size_override("font_size",13);units.add_child(gear)
 row.add_child(VSeparator.new())
 var carinfo=VBoxContainer.new();carinfo.alignment=BoxContainer.ALIGNMENT_CENTER;carinfo.add_theme_constant_override("separation",-4);row.add_child(carinfo);carinfo.add_child(label("1999 Mazda MX-5 Miata",22));carinfo.add_child(label("British racing green",15,Color("#b1c2ad")))
 var spacer=Control.new();spacer.size_flags_horizontal=Control.SIZE_EXPAND_FILL;row.add_child(spacer)
 instructions=label("WASD / arrows  Drive     Space  Brake     R  Recover",16,Color("#c7d1bd"));row.add_child(instructions);row.add_child(VSeparator.new());trip.custom_minimum_size.x=78;row.add_child(trip)
 location=VBoxContainer.new();location.add_theme_constant_override("separation",0);anchored(location,0,1,0,1,20,-246,240,-94)
 mapbox=PanelContainer.new();mapbox.clip_contents=true;mapbox.add_theme_stylebox_override("panel",style(INK,4,0));location.add_child(mapbox)
 map=preload("res://MapHUD.gd").new();map.game=g;map.custom_minimum_size=Vector2(175,100);mapbox.add_child(map)
 streetbg=PanelContainer.new();streetbg.add_theme_stylebox_override("panel",style(INK,2,7));location.add_child(streetbg);street.add_theme_font_size_override("font_size",17);streetbg.add_child(street)
 # Glance: a translucent place tag at the top left and a self-hiding legend.
 tag=PanelContainer.new();tag.add_theme_stylebox_override("panel",style(GLASS,7,9));anchored(tag,0,0,0,0,16,14,16,14);tag.mouse_filter=Control.MOUSE_FILTER_IGNORE
 var tag_row=HBoxContainer.new();tag_row.add_theme_constant_override("separation",14);tag.add_child(tag_row)
 var tag_lines=VBoxContainer.new();tag_lines.add_theme_constant_override("separation",-2);tag_row.add_child(tag_lines)
 tag_road.text=" ";tag_road.add_theme_font_size_override("font_size",18);tag_lines.add_child(tag_road)
 tag_title.text=g.current_title;tag_title.add_theme_font_size_override("font_size",13);tag_title.modulate=Color("#b1c2ad");tag_lines.add_child(tag_title)
 tag_speed.add_theme_font_size_override("font_size",30);tag_speed.vertical_alignment=VERTICAL_ALIGNMENT_CENTER;tag_row.add_child(tag_speed)
 tag_units=label("mph",12,LIME);tag_units.vertical_alignment=VERTICAL_ALIGNMENT_CENTER;tag_row.add_child(tag_units)
 legend=PanelContainer.new();legend.add_theme_stylebox_override("panel",style(GLASS,7,14));anchored(legend,.5,1,.5,1,0,-24,0,-24);legend.grow_horizontal=Control.GROW_DIRECTION_BOTH;legend.grow_vertical=Control.GROW_DIRECTION_BEGIN;legend.mouse_filter=Control.MOUSE_FILTER_IGNORE
 legend_text.add_theme_font_size_override("font_size",15);legend_text.modulate=Color("#c7d1bd");legend_text.horizontal_alignment=HORIZONTAL_ALIGNMENT_CENTER;legend.add_child(legend_text);legend.visible=false
 var status=PanelContainer.new();status.name="Status";status.add_theme_stylebox_override("panel",style(Color("#24362ee8"),5,12));anchored(status,.5,0,.5,0,-185,65,185,95);note.horizontal_alignment=HORIZONTAL_ALIGNMENT_CENTER;status.add_child(note)
 driving_panels.assign([header,bottom,location])
 anchored(modal,.5,.5,.5,.5,-242,-182,242,175);modal.add_theme_stylebox_override("panel",style(INK,12,22));modal.visible=false
 var v=VBoxContainer.new();v.add_theme_constant_override("separation",8);modal.add_child(v)
 var h=HBoxContainer.new();v.add_child(h);var head=label("Where to?",34);head.add_theme_font_override("font",display);head.size_flags_horizontal=Control.SIZE_EXPAND_FILL;h.add_child(head);h.add_child(button("Close  Esc",close_search))
 var fields=HBoxContainer.new();v.add_child(fields);field.placeholder_text="Street, city, landmark, or latitude, longitude";field.size_flags_horizontal=Control.SIZE_EXPAND_FILL;fields.add_child(field);fields.add_child(button("Search",submit));field.text_submitted.connect(func(_q):submit())
 feedback.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;feedback.add_theme_font_size_override("font_size",16);v.add_child(feedback)
 var scroll=ScrollContainer.new();scroll.custom_minimum_size.y=164;scroll.horizontal_scroll_mode=ScrollContainer.SCROLL_MODE_DISABLED;v.add_child(scroll)
 choices.size_flags_horizontal=Control.SIZE_EXPAND_FILL;scroll.add_child(choices)
 var credits=label("Map © OpenStreetMap contributors · OpenMapTiles · OpenFreeMap\nVirginia imagery, roads & buildings © VGIN · Elevation USGS 3DEP / Mapzen\nStreet photos © Grab and KartaView Contributors · CC BY-SA 4.0\nBuildings · Overture / OSM · ORNL / FEMA · USACE NSI · Miami-Dade lidar",13,Color("#b1c2ad"));v.add_child(credits)
 root.add_child(atlas_map);atlas_map.setup(g)
 build_settings(g)
 set_level(g.driving_preferences.values.hud_level,false)
 return self
# Cockpit preferences live beside the other settings on the road atlas (Tab).
func build_settings(g):
 var settings=atlas_map.transmission_button.get_parent()
 hud_button=button("",toggle_driving_panels);hud_button.tooltip_text="F2 or the Xbox left stick cycles Off, Glance and Full";settings.add_child(hud_button)
 sound_pref_button=button("",func():sound.emit());sound_pref_button.tooltip_text="M toggles the engine, tyre and wind sound";settings.add_child(sound_pref_button)
 var motion=HBoxContainer.new();motion.add_theme_constant_override("separation",8);settings.add_child(motion)
 motion.add_child(label("Head motion",16,Color("#b2c3b9")))
 motion_slider=HSlider.new();motion_slider.min_value=0;motion_slider.max_value=1;motion_slider.step=.05;motion_slider.custom_minimum_size=Vector2(130,0);motion_slider.focus_mode=Control.FOCUS_NONE;motion_slider.size_flags_vertical=Control.SIZE_SHRINK_CENTER
 motion_slider.tooltip_text="Driver view: 0 welds the camera to the chassis, 1 is the full head model"
 motion_slider.value=g.driving_preferences.values.head_motion;g.cockpit.head_motion=motion_slider.value
 motion_slider.value_changed.connect(set_head_motion);motion.add_child(motion_slider)
func set_head_motion(value:float):
 game.cockpit.head_motion=value
 game.driving_preferences.remember("head_motion",float(value))
func set_level(new_level:int,announce:=true):
 level=clampi(new_level,OFF,FULL);clean_view=level!=FULL
 var full=level==FULL
 header.visible=full;bottom.visible=full;location.visible=level!=OFF;tag.visible=level==GLANCE
 mapbox.add_theme_stylebox_override("panel",style(INK if full else GLASS,4,0))
 streetbg.add_theme_stylebox_override("panel",style(INK if full else GLASS,2,7))
 location.offset_top=-246 if full else -176;location.offset_bottom=-94 if full else -24
 location.modulate.a=1.0;tag.modulate.a=1.0;glance_alpha=1.0;wake_time=WAKE_S
 if announce and level==GLANCE:show_legend()
 legend.visible=level==GLANCE and legend_time>0
func toggle_driving_panels():
 set_level((level+1)%3)
 if game:game.driving_preferences.remember("hud_level",level)
func show_legend(seconds:=LEGEND_S):
 legend_time=seconds;legend.visible=level==GLANCE
func _process(dt):
 if legend_time>0:
  legend_time-=dt
  if legend_time<=0:legend.visible=false
 if wake_time>0:wake_time-=dt
 if level!=GLANCE:return
 var awake=wake_time>0 or not moving or note.get_parent().visible or legend_time>0
 glance_alpha+=((1.0 if awake else GLANCE_FADED)-glance_alpha)*(1.-exp(-dt/GLANCE_FADE_S))
 tag.modulate.a=glance_alpha;location.modulate.a=glance_alpha
func submit():
 if field.text.strip_edges()!="":search_for.emit(field.text.strip_edges())
func open_search():
 modal.visible=true;field.grab_focus();field.select_all();presets()
func close_search():
 modal.visible=false;field.release_focus()
func clear_choices():
 for b in choices.get_children():choices.remove_child(b);b.queue_free()
static func survey_approach():
 return {"lon":-77.90848160,"lat":37.36914673,"street":"Patrick Henry Highway","title":"US 360 at Chula Road · eastbound"}
static func princeton_gravel():
 return {"lon":-96.5909,"lat":40.56724,"street":"Princeton Road","bearing":90.,"title":"Princeton Road · Lancaster County, NE"}
static func davidson_hall():
 return {"lon":-70.79325725,"lat":44.40776522,"street":"Church Street","bearing":341.23,"title":"Davidson Hall · Bethel, ME"}
static func monument_valley():
 return {"lon":-109.990814,"lat":37.101444,"bearing":230.,"title":"Monument Valley · US 163, UT"}
func presets():
 results([
  survey_approach(),
  monument_valley(),
  {"lon":-104.81944137,"lat":41.13906643,"street":"Capitol Avenue","bearing":334.,"title":"Capitol Avenue · Cheyenne, WY"},
  {"lon":-81.630658,"lat":38.348449,"street":"Lee Street East","bearing":136.44,"title":"Clay Center · Charleston, WV"},
  princeton_gravel(),
  davidson_hall(),
  {"lon":-96.675002778,"lat":40.808013889,"street":"J Street","bearing":91.,"title":"J Street · Lincoln, NE"},
  {"lon":-80.208896,"lat":25.748401,"title":"South Miami Avenue, Miami · lidar roofs","street":"South Miami Avenue","bearing":52.21},
  {"lon":-71.134311,"lat":42.365183,"title":"Cambridge, MA · surveyed building heights"},
  {"lon":-77.90694508,"lat":37.37026814,"street":"Chula Road","title":"Chula Road, Amelia County, VA"},
  {"lon":-77.6244769,"lat":37.3850801,"street":"Raven Wing Drive","title":"Raven Wing Drive, Chesterfield County, VA"},
  {"lon":-77.47826,"lat":37.55230,"title":"Carytown, Richmond · Byrd Theatre"}
 ],"Pick a starting street or search for a place.")
func results(arr,message):
 clear_choices();feedback.text=message
 for item in arr:
  var b=button(item.title,func():choose.emit(item.lon,item.lat,item.title,item.get("street",""),item.get("bearing")));b.alignment=HORIZONTAL_ALIGNMENT_LEFT;b.text_overrun_behavior=TextServer.OVERRUN_TRIM_ELLIPSIS;choices.add_child(b)
func message(t):
 note.text=t;note.get_parent().visible=t!=""
 if t!="":wake_time=maxf(wake_time,3.0)
func refresh(mph,g,dist,road,view):
 speed.text=str(mph);gear.text=g;trip.text="%.2f mi"%dist;street.text="  "+road;mode_button.text=["Chase view   C","Driver view   C","Scenic view   C"][view];sound_button.text="Sound on   M" if audio_on else "Sound off   M";map.queue_redraw()
 tag_road.text=road;tag_title.text=game.current_title;tag_speed.text=str(mph);tag_speed.visible=view!=1;tag_units.visible=view!=1
 if hud_button:hud_button.text="HUD · "+LEVEL_NAMES[level]
 if sound_pref_button:sound_pref_button.text="Sound · "+("On" if audio_on else "Off")
 if absi(mph-shown_mph)>=SPEED_WAKE_MPH:shown_mph=mph;wake_time=maxf(wake_time,WAKE_S)
 moving=mph>=SPEED_WAKE_MPH
 if view!=shown_view:shown_view=view;show_legend()

func update_drive_hint():
 var pad=not Input.get_connected_joypads().is_empty()
 var text=("Auto  ·  B for manual    A  E-brake" if pad else "Auto  ·  T for manual    H  E-brake")
 if game.vehicle.manual:
  text=("Manual  ·  B for auto    LB  Clutch    RB / X  Shift    A  E-brake" if pad else "Manual  ·  T for auto    Shift  Clutch    E / Q  Shift    H  E-brake")
  if game.vehicle.stalled:text="Clutch + right stick  Restart" if pad else "Clutch + I  Restart"
 if text==hint:return
 hint=text;instructions.text=text
 legend_text.text=text+("     Y  Camera     Menu  Map     L3  HUD" if pad else "     C  Camera     Tab  Map     F2  HUD     M  Sound")
 if shown_view!=-1:show_legend()
