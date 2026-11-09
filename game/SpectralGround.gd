extends RefCounted
# Experimental opt-in. Driving tiles and vector pavement never wait for this job.
var enabled:=false
var worker:Thread
var requested_id:Vector2i
var requested_instance:int
var next_check:int=0

func load_cover(helper:String,cache:String,id:Vector2i)->Dictionary:
 var lines=[]
 var result={"status":"unavailable"}
 if OS.execute(helper,["ground-cover",cache,str(id.x),str(id.y)],lines,true)!=0 or lines.is_empty():return result
 var value=JSON.parse_string(lines[0])
 if not value is Dictionary:return result
 var key=value.get("id")
 if not key is Array or key.size()!=2 or key[0]!=id.x or key[1]!=id.y:return result
 result=value
 if result.get("status")=="available":
  var expected="naip-low-v1-%d-%d.png"%[id.x,id.y]
  if result.get("png")!=expected:return {"status":"unavailable"}
  var image=Image.load_from_file(cache.path_join(expected))
  if not image or image.get_size()!=Vector2i(256,256):return {"status":"unavailable"}
  image.generate_mipmaps()
  result["image"]=image
 return result

func set_enabled(game,value:bool):
 enabled=value
 next_check=0
 if not value:
  for tile in game.tiles.values():
   if tile.streaming_ground and tile.info.has("low_cover_date"):tile.streaming_ground.set_shader_parameter("has_low_cover",false)

func label(game)->String:
 var tile=game.tiles.get(game.tile_id(game.pos))
 if game.live_weather:return "USDA ground cover paused for live weather"
 if tile:
  if tile.info.has("low_cover_date"):
   return "USDA NAIP "+str(tile.info.low_cover_date) if tile.streaming_ground.get_shader_parameter("has_low_cover") else "USDA ground cover paused"
  if tile.info.get("low_cover_status") in ["unavailable","unsupported"]:return "No USDA ground-cover observation here"
 return "Waiting for USDA ground cover"

func tick(game):
 var now=Time.get_ticks_msec()
 if worker:
  if worker.is_alive():return
  var data=worker.wait_to_finish()
  worker=null
  game.calls+=int(data.get("requests",0))
  var tile=game.tiles.get(requested_id)
  if tile and tile.get_instance_id()==requested_instance:
   tile.info["low_cover_status"]=data.get("status","unavailable")
   tile.info["low_cover_retry"]=now+(15000 if data.get("status")=="waiting_canopy" else 900000)
   if data.has("image"):
    tile.info["low_cover_date"]=data.date
    tile.streaming_ground.set_shader_parameter("low_cover_map",ImageTexture.create_from_image(data.image))
 if not enabled or not game.ready_drive or now<next_check or not game.jump.is_empty():return
 next_check=now+1000
 var current=game.tile_id(game.pos)
 var active=game.tiles.get(current)
 if not active or not active.streaming_ground:return
 # The underlying optical observation describes growing-season land cover.
 # Leave reference winter, live weather and cold-season street exposures alone.
 var stamp=game.daylight.applied_epoch
 var month=int(Time.get_datetime_dict_from_unix_time(stamp).month) if stamp>0 else 6
 var summer=not game.live_weather and month>=5 and month<=10 and active.info.get("region","")!="Raven"
 for tile in game.tiles.values():
  if tile.streaming_ground and tile.info.has("low_cover_date") and tile.streaming_ground.get_shader_parameter("has_low_cover")!=summer:
   tile.streaming_ground.set_shader_parameter("has_low_cover",summer)
 if game.hud.atlas_map.visible:game.hud.atlas_map.header()
 if not summer or active.info.has("low_cover_date") or not active.info.get("canopy_ready",false):return
 if now<int(active.info.get("low_cover_retry",0)):return
 requested_id=current;requested_instance=active.get_instance_id()
 worker=Thread.new()
 if worker.start(load_cover.bind(game.helper,game.cache,current))!=OK:worker=null

func shutdown():
 if worker and worker.is_started():worker.wait_to_finish()
