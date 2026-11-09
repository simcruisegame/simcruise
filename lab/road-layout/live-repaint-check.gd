extends SceneTree

# Exercise the regular place picker, live district fetch and paint swap. Give
# this script a scratch map cache that has not read Portland's lane district.
const ID=Vector2i(10434,23439)
var game
var began:int
var first_tile:int
var first_surface:int
var first_layer:int
var collider_ids:Array=[]
var first_vertices:int
var histogram=[0,0,0,0,0]
var settled=false
var post_frames=0
var starts_without_layout=false

func _initialize():
 run.call_deferred()

func run():
 var args=OS.get_cmdline_user_args()
 if args.is_empty():
  push_error("Pass a scratch map-cache directory")
  quit(1)
  return
 game=load("res://Drive.tscn").instantiate()
 root.add_child(game)
 game.cache=args[0]
 game.choose_place(-122.68297298,45.52254037,"Portland","Southwest Harvey Milk Street")
 began=Time.get_ticks_msec()
 physics_frame.connect(record_frame)

func vertices(node)->int:
 var result=0
 for stripe in node.get_children():
  for i in stripe.mesh.get_surface_count():
   result+=stripe.mesh.surface_get_array_len(i)
 return result

func ground_bodies(tile)->Array:
 var ids=[]
 for child in tile.get_children():
  if child is StaticBody3D:ids.append(child.get_instance_id())
 return ids

func record_frame():
 if Time.get_ticks_msec()-began>85000:
  print("REPAINT TIMEOUT ",game.ready_drive," ",game.tiles.keys())
  quit(1)
  return
 var tile=game.tiles.get(ID)
 if not game.ready_drive or not tile or tile.lane_paint_layer==null:return
 if first_tile==0:
  first_tile=tile.get_instance_id()
  first_layer=tile.lane_paint_layer.get_instance_id()
  first_surface=tile.streaming_ground.get_instance_id()
  first_vertices=vertices(tile.lane_paint_layer)
  collider_ids=ground_bodies(tile)
  starts_without_layout=not tile.info.has("road_layout")
  print("REPAINT FIRST ",first_vertices," ",starts_without_layout)
 if not settled and game.vehicle.wheel_contact==4:settled=true
 if settled:histogram[clampi(game.vehicle.wheel_contact,0,4)]+=1
 if not tile.info.has("road_layout") or tile.lane_paint_layer.get_instance_id()==first_layer:return
 post_frames+=1
 if post_frames<90:return
 var count=vertices(tile.lane_paint_layer)
 var receipt={"cold":starts_without_layout,"sameTile":first_tile==tile.get_instance_id(),"sameGroundMaterial":first_surface==tile.streaming_ground.get_instance_id(),"sameGroundColliders":not collider_ids.is_empty() and ground_bodies(tile)==collider_ids,"beforeVertices":first_vertices,"afterVertices":count,"contacts":histogram,"spans":tile.info.road_layout.spans.size(),"postFrames":post_frames}
 print("LIVE PAINT ",JSON.stringify(receipt))
 quit(0 if receipt.cold and receipt.sameTile and receipt.sameGroundMaterial and receipt.sameGroundColliders and count<first_vertices and receipt.spans>0 and histogram.slice(0,4).max()==0 else 1)
