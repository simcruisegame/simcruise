extends RefCounted
# One optional regional observation serves sixteen zoom-16 driving tiles.
var worker: Thread
var observations := {}
var next_check := 0

func read_region(helper: String,cache: String,region: Vector2i):
 var lines=[]
 var code=OS.execute(helper,["forest-form",cache,str(region.x),str(region.y)],lines,true)
 var doc=JSON.parse_string(str(lines[0])) if code==0 and lines.size() else null
 if not doc is Dictionary or doc.get("status","") not in ["available","no_coverage","unsupported","unavailable"]:
  doc={"status":"unavailable"}
 return {"region":region,"doc":doc}

func tick(game):
 var now=Time.get_ticks_msec()
 if worker:
  if worker.is_alive():return
  var response=worker.wait_to_finish()
  worker=null
  var doc:Dictionary=response.doc
  game.calls+=int(doc.get("requests",0))
  observations.erase(response.region)
  observations[response.region]={"doc":doc,"until":now+(1800000 if doc.status=="unavailable" else 10800000)}
  if observations.size()>16:observations.erase(observations.keys()[0])
  next_check=0
 if now<next_check or not game.ready_drive or not game.jump.is_empty():return
 next_check=now+750
 var best=INF
 var target=Vector2i(-1,-1)
 for id in game.tiles:
  var tile=game.tiles[id]
  if not tile.info.get("live_canopy",false) or tile.info.get("field",false) or not tile.info.get("canopy_ready",false):continue
  if tile.unsurveyed_canopy.is_empty() or tile.forest_form!=null:continue
  if now<int(tile.info.get("forest_retry",0)):continue
  var region=Vector2i(id.x>>2,id.y>>2)
  var saved=observations.get(region,{})
  if not saved.is_empty() and now<int(saved.until):
   var record:Dictionary=saved.doc
   tile.info["forest_form_status"]=record.status
   tile.info["forest_form_years"]=[record.get("evtYear"),record.get("nlcdYear")]
   tile.info["forest_retry"]=saved.until
   if record.status=="available":
    tile.forest_form=preload("res://ForestForm.gd").from_document(id,record)
    if tile.forest_form and tile.apply_forest_form()>0:game.tile_count+=1
   continue
  var distance=(game.tile_offset(id)+Vector2.ONE*game.side*.5).distance_squared_to(game.pos)
  if distance<best:best=distance;target=region
 if target.x>=0:
  worker=Thread.new()
  if worker.start(read_region.bind(game.helper,game.cache,target))!=OK:worker=null

func shutdown():
 if worker and worker.is_started():worker.wait_to_finish()
