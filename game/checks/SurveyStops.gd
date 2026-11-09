extends Node

const ROOF = preload("res://LidarRoof.gd")
const STOPS = [
 {"button":"Clay Center · Charleston, WV","road":"Lee Street East","coordinate":Vector2(38.348449,-81.630658)},
 {"button":"J Street · Lincoln, NE","road":"J Street","coordinate":Vector2(40.808013889,-96.675002778)},
 {"button":"Princeton Road · Lancaster County, NE","road":"Princeton Road","coordinate":Vector2(40.56724,-96.5909)}
]
var game
var stop_index=0
var selected=false
var began=0

func _ready():
 game=get_parent()
 began=Time.get_ticks_msec()

func finish(ok,reason):
 set_process(false)
 print(("SURVEY STOPS PASSED " if ok else "SURVEY STOPS FAILED ")+reason)
 game.request_game_close(0 if ok else 1)

func _process(_dt):
 if Time.get_ticks_msec()-began>90000:
  finish(false,"timeout "+str(stop_index));return
 if not game.ready_drive or not game.jump.is_empty():return
 var stop=STOPS[stop_index]
 if not selected:
  game.hud.open_search()
  for item in game.hud.choices.get_children():
   if item is Button and item.text==stop.button:
    selected=true;item.pressed.emit();return
  finish(false,"missing menu stop "+stop.button);return
 if game.current_title!=stop.button or game.road_name!=stop.road:return
 var position=game.player_coordinates()
 if position.distance_to(stop.coordinate)>.0012:
  finish(false,"incorrect street position "+str(position));return
 var active=game.tiles.get(game.tile_id(game.pos))
 if not active or game.vehicle.wheel_contact!=4:return
 var detail=""
 if stop_index==0:
  if not active.structure_layer:return
  var count=0
  for footprint in active.info.buildings:
   var measured=ROOF.find(active.structure_layer,footprint)
   if not measured.is_empty() and not str(measured.get("face_finish","")).is_empty():count+=1
  if count<1:return
  detail=" surveyed_landmarks="+str(count)
 elif stop_index==1:
  var counts=active.info.get("tree_inventories",{})
  if counts.is_empty():return
  var n=0
  for value in counts.values():n+=int(value)
  if n<1:return
  detail=" retained_inventory_trees="+str(n)
 else:
  var bearing=fposmod(rad_to_deg(-game.yaw),360.)
  if absf(angle_difference(deg_to_rad(bearing),PI*.5))>deg_to_rad(3.):return
  if not active.has_mapped_unpaved:return
  for grip in game.vehicle.surface_grip:
   if not is_equal_approx(float(grip),.76):return
  detail=" mapped_gravel=4 eastbound_deg="+str(snappedf(bearing,.01))
 print("SURVEY STOP ",stop.button," road=",game.road_name," coordinate=",position," tires=",game.vehicle.wheel_contact,detail)
 stop_index+=1;selected=false
 if stop_index==STOPS.size():finish(true,"normal menu selection, locations and measured scenery")
