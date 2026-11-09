extends SceneTree
# This fixture launches the production driving scene, selects ordinary coordinates, and waits for its optional worker.
var game
var selected=false
var completed=false
var start=0
var contacts=0
var screenshot=""

func _initialize():
 start=Time.get_ticks_msec()
 var args=OS.get_cmdline_user_args()
 var ix=args.find("--forest-cockpit")
 if ix>=0 and ix+1<args.size():screenshot=args[ix+1]
 game=load("res://Drive.tscn").instantiate()
 root.add_child(game)

func _physics_process(_dt):
 if completed:return false
 if Time.get_ticks_msec()-start>115000:
  completed=true;printerr("FOREST LIVE timeout. Position ",game.pos," title ",game.current_title)
  for t in game.tiles.values():print("tile ",t.info.get("id")," status ",t.info.get("forest_form_status")," unsurveyed ",t.unsurveyed_canopy.size())
  game.request_game_close(3);return false
 if not game.ready_drive or not game.jump.is_empty():return false
 if not selected:
  game.choose_place(-121.36822,47.71318,"Northeast Stevens Pass Highway near US 2 in Skykomish, Washington")
  selected=true
  return false
 if game.current_title!="Northeast Stevens Pass Highway near US 2 in Skykomish, Washington":return false
 var id=game.tile_id(game.pos)
 var tile=game.tiles.get(id)
 if not tile:return false
 if not screenshot.is_empty():game.view=1
 if game.vehicle.wheel_contact==4:contacts+=1
 var support=0
 var eligible=0
 var statuses={}
 for other in game.tiles.values():
  if other.info.has("forest_form_status"):
   statuses[str(other.info.id)]=[other.info.forest_form_status,other.info.get("forest_form_crowns",0)]
   support+=int(other.info.get("forest_form_crowns",0))
   eligible+=other.unsurveyed_canopy.size()
 if contacts<20 or support==0 or not tile.info.has("forest_form_status"):return false
 var region=Vector2i(id.x>>2,id.y>>2)
 var doc=game.forest_stream.observations.get(region,{}).get("doc",{})
 completed=true
 print("FOREST LIVE ",JSON.stringify({"cameraTile":[id.x,id.y],"region":[region.x,region.y],"years":[doc.get("evtYear"),doc.get("nlcdYear")],"forestRequests":doc.get("requests"),"eligiblePeaks":eligible,"changed":support,"contactTicks":contacts,"helper":game.helper,"cache":game.cache,"tiles":statuses,"elapsedSec":(Time.get_ticks_msec()-start)*.001}))
 if screenshot.is_empty():game.request_game_close(0)
 else:save_cockpit.call_deferred()
 return false

func save_cockpit():
 for i in 10:await RenderingServer.frame_post_draw
 var result=root.get_texture().get_image().save_jpg(screenshot,.91)
 print("FOREST COCKPIT ",screenshot," result=",result," view=",game.view)
 game.request_game_close(0 if result==OK else 4)
