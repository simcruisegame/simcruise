extends RefCounted
# Fetch ahead separately. The road constructor only consumes a completed cached
# surface, and a live tile and its collider retain their original elevation.
var worker:Thread
var pending_id:Vector2i
var checked={}
var next_check:=0
var enabled:=true
func fetch_one(helper:String,cache:String,id:Vector2i):
 var lines=[]
 if OS.execute(helper,["road-banks",cache,str(id.x),str(id.y)],lines,true)==0 and lines.size():
  var value=JSON.parse_string(lines[0])
  if value is Dictionary:return value
 return {"status":"unavailable","requests":0}
func tick(game):
 if worker:
  if worker.is_alive():return
  var result=worker.wait_to_finish();worker=null
  game.calls+=int(result.get("requests",0))
  checked[pending_id]=Time.get_ticks_msec()+(900000 if result.get("status")=="unavailable" else 86400000)
 if not enabled or not game.jump.is_empty() or not game.ready_drive or Time.get_ticks_msec()<next_check:return
 next_check=Time.get_ticks_msec()+3500
 var active=game.tiles.get(game.tile_id(game.pos))
 if not active or not active.info.get("live_canopy",false) or active.info.get("field",false):return
 var road=active.nearest(game.pos-game.tile_offset(game.tile_id(game.pos)),false,"")
 if road.size()<3 or road[2]>40 or road[1][5] not in ["motorway","trunk","primary","secondary","tertiary"]:return
 var direction=Vector2(-sin(game.yaw),-cos(game.yaw))
 var speed=Vector2(game.vehicle.linear_velocity.x,game.vehicle.linear_velocity.z).length()
 var candidates=[]
 if speed>2.5:
  # Already-visible neighbors can be built hundreds of metres before the car
  # reaches them. Start one or two cells ahead to beat normal geometry streaming.
  for step in [1.25,2.10,.75]:
   var id=game.tile_id(game.pos+direction*game.side*step)
   if not game.tiles.has(id) and not candidates.has(id):candidates.append(id)
 candidates.append(game.tile_id(game.pos))
 for id in candidates:
  if FileAccess.file_exists("res://data/%d_%d.json"%[id.x,id.y]) or Time.get_ticks_msec()<int(checked.get(id,0)):continue
  if checked.size()>128:
   var oldest=checked.keys()[0]
   checked.erase(oldest)
  pending_id=id;worker=Thread.new()
  if worker.start(fetch_one.bind(game.helper,game.cache,id))!=OK:worker=null
  return
func shutdown():
 if worker and worker.is_started():worker.wait_to_finish()
