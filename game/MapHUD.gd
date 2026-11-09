extends Control
const SCALES=[1.45,.95,.52,.28,.14]
var level=1
var game
var overview={}
var smaller=Button.new()
var larger=Button.new()

func _ready():
 mouse_filter=Control.MOUSE_FILTER_STOP
 tooltip_text="Open road map (Tab or Xbox Menu)"
 mouse_default_cursor_shape=Control.CURSOR_POINTING_HAND
 overview=JSON.parse_string(FileAccess.get_file_as_string("res://data/map-overview.json"))
 for b in [smaller,larger]:
  b.text="−" if b==smaller else "+"
  b.tooltip_text="Zoom out (D-pad down)" if b==smaller else "Zoom in (D-pad up)"
  b.focus_mode=Control.FOCUS_NONE
  b.add_theme_font_size_override("font_size",16)
  add_child(b)
  b.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
  b.offset_left=-50 if b==smaller else -26
  b.offset_right=b.offset_left+22;b.offset_top=3;b.offset_bottom=25
  b.pressed.connect(zoom.bind(1 if b==smaller else -1))
 zoom(0)

func zoom(direction:int):
 level=clampi(level+direction,0,SCALES.size()-1)
 smaller.disabled=level==SCALES.size()-1;larger.disabled=level==0
 queue_redraw()

func _gui_input(event):
 if event is InputEventMouseButton and event.pressed:
  if event.button_index==MOUSE_BUTTON_LEFT:game.hud.atlas_map.open_map();accept_event()
  elif event.button_index==MOUSE_BUTTON_WHEEL_UP:zoom(-1);accept_event()
  elif event.button_index==MOUSE_BUTTON_WHEEL_DOWN:zoom(1);accept_event()

func road(a:Vector2,b:Vector2,width:float,scale_m:float):
 var start=a*scale_m+size*.5;var end=b*scale_m+size*.5
 if Rect2(Vector2.ZERO,size).grow(5).intersects(Rect2(start,end-start).abs().grow(1)):
  draw_line(start,end,Color("#788c81") if width>9 else Color("#536c60"),clampf(width*.39*scale_m,1.,8.),true)

func _draw():
 if not game or not game.is_node_ready():return
 draw_rect(Rect2(Vector2.ZERO,size),Color("#192725"))
 var scale_m:float=SCALES[level]
 for tile in game.tiles.values():
  var origin=Vector2(tile.position.x,tile.position.z)-game.pos
  for segment in tile.segments:road(origin+segment[0],origin+segment[1],segment[3],scale_m)
 if level>=2:
  var radius=size*.5/scale_m
  var lo=game.tile_id(game.pos-radius);var hi=game.tile_id(game.pos+radius)
  for x in range(lo.x,hi.x+1):
   for y in range(lo.y,hi.y+1):
    var id=Vector2i(x,y)
    if game.tiles.has(id):continue
    var offset=game.tile_offset(id)-game.pos
    for feature in overview.get("%d_%d"%[x,y],[]):
     var points=feature[1]
     for i in range(1,points.size()):
      var a=Vector2(points[i-1][0],points[i-1][1])*game.side
      var b=Vector2(points[i][0],points[i][1])*game.side
      road(offset+a,offset+b,feature[0],scale_m)
 var c=size*.5;var d=Vector2(-sin(game.yaw),-cos(game.yaw));var r=Vector2(-d.y,d.x)
 draw_circle(c,8,Color("#14392b"));draw_colored_polygon(PackedVector2Array([c+d*8,c-d*5+r*5,c-d*3,c-d*5-r*5]),Color("#c9f68d"))
