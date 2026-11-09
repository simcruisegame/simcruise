extends RefCounted

# Photographic color is usable only near the short, checked camera track.
const SUPPORT_M = 85.0
const MAX_CACHED = 24
var enabled = true
var worker: Thread
var pending: Vector2i
var pending_uv: Vector2
# ReferencePose uses the surveyed camera point; ordinary play uses the car.
var qa_camera_uv = null
var qa_camera_tile = null
var qa_reference_id := ""
var records = {}
var next_request_ms = 0
var next_sky_update_ms = 0
var sky_active = false

func fetch(helper: String, cache_dir: String, id: Vector2i, anchor: Vector2) -> Dictionary:
 var lines = []
 var rc = OS.execute(helper, ["street-sample", cache_dir, str(id.x), str(id.y),str(anchor.x),str(anchor.y)], lines, true)
 if rc == 0 and not lines.is_empty():
  var data = JSON.parse_string(str(lines[0]))
  if data is Dictionary:
   var tile = data.get("tile", [])
   if tile is Array and tile.size() == 2 and int(tile[0]) == id.x and int(tile[1]) == id.y:
    return data
 return {"status":"unavailable"}

func uv_for(frame: Dictionary, id: Vector2i) -> Vector2:
 var x = (float(frame.lon) + 180.) / 360. * 65536. - id.x
 var y = (1. - asinh(tan(deg_to_rad(float(frame.lat)))) / PI) * 32768. - id.y
 return Vector2(x, y)

func material_color(category, fallback: Vector3) -> Vector3:
 if category is Dictionary:
  var c = category.get("linear", [])
  if c.size() == 3:
   return Vector3(c[0], c[1], c[2])
 return fallback

func strength(category) -> float:
 return clampf(float(category.get("confidence", 0.)), 0., 1.) if category is Dictionary else 0.

func apply_tile(id: Vector2i, tile, side: float):
 var mat: ShaderMaterial = tile.streaming_ground
 if mat == null: return
 mat.set_shader_parameter("photo_strength", Vector2.ZERO)
 if not enabled: return
 var best = INF
 var chosen = {}
 var track = []
 for key in records:
  if absi(id.x-key.x)>1 or absi(id.y-key.y)>1: continue
  var r = records[key]
  var appearance = r.get("appearance", {})
  if not appearance is Dictionary or appearance.get("status") != "accepted": continue
  var frames = r.get("frames", [])
  if frames.size() != 3: continue
  var points = []
  var distance_to_tile = INF
  var distance_to_center = INF
  for frame in frames:
   var p = uv_for(frame, id)
   points.append(p)
   distance_to_tile = minf(distance_to_tile, p.distance_to(p.clamp(Vector2.ZERO, Vector2.ONE))*side)
   distance_to_center = minf(distance_to_center, p.distance_squared_to(Vector2(.5,.5)))
  if distance_to_tile <= SUPPORT_M and distance_to_center < best:
   best = distance_to_center
   chosen = appearance
   track = points
 if chosen.is_empty(): return
 var road = chosen.get("road")
 var green = chosen.get("vegetation")
 for i in track.size():
  mat.set_shader_parameter("photo_point_"+str(i), track[i])
 mat.set_shader_parameter("photo_pavement", material_color(road, Vector3.ONE))
 mat.set_shader_parameter("photo_green", material_color(green, Vector3.ONE))
 mat.set_shader_parameter("photo_strength", Vector2(strength(road), strength(green)))

func reapply(game):
 for id in game.tiles:
  apply_tile(id, game.tiles[id], game.side)

func update_sky(game):
 var sky = null
 var fade = 0.
 if enabled and game.ready_drive and not game.live_weather:
  var id=game.tile_id(game.pos)
  var record=records.get(id,{})
  var appearance=record.get("appearance")
  if appearance is Dictionary and appearance.get("clearSky") is Dictionary:
   var frames=record.get("frames",[])
   if frames.size()==3:
    var uv=(game.pos-game.tile_offset(id))/game.side
    var a=uv_for(frames[0],id);var b=uv_for(frames[1],id);var c=uv_for(frames[2],id)
    var dist=minf(uv.distance_to(Geometry2D.get_closest_point_to_segment(uv,a,b)),uv.distance_to(Geometry2D.get_closest_point_to_segment(uv,b,c)))*game.side
    sky=appearance.clearSky
    fade=(1.-smoothstep(25.,80.,dist))*clampf(float(sky.get("confidence",0.))*1.25,0.,1.)
 if fade>0. and sky.upper.size()==3 and sky.lower.size()==3:
  RenderingServer.global_shader_parameter_set("road_sky_high",Vector4(sky.upper[0],sky.upper[1],sky.upper[2],fade))
  RenderingServer.global_shader_parameter_set("road_sky_low",Vector4(sky.lower[0],sky.lower[1],sky.lower[2],float(sky.heading)/360.))
  sky_active=true
 elif sky_active:
  RenderingServer.global_shader_parameter_set("road_sky_high",Vector4.ZERO)
  sky_active=false

func tick(game):
 var now = Time.get_ticks_msec()
 if now>=next_sky_update_ms:
  next_sky_update_ms=now+200
  update_sky(game)
 if worker:
  if worker.is_alive(): return
  var data = worker.wait_to_finish()
  worker = null
  game.calls += int(data.get("requests", 0))
  var catalog=data.get("panoramax",{})
  var retry=catalog.get("status")=="unavailable" or (data.get("status")=="unavailable" and data.get("references",[]).is_empty())
  var attempts=mini(int(records.get(pending,{}).get("attempts",0))+1,4) if retry else 0
  data["attempts"]=attempts
  data["anchor"]=pending_uv
  data["until"]=now+([0,15000,30000,60000,120000][attempts] if retry else 3600000)
  records.erase(pending)
  if records.size() >= MAX_CACHED: records.erase(records.keys()[0])
  records[pending] = data
  reapply(game)
 if not enabled or not game.ready_drive or not game.jump.is_empty() or now < next_request_ms: return
 var id = game.tile_id(game.pos) if qa_camera_tile==null else qa_camera_tile
 var tile = game.tiles.get(id)
 if not tile or not tile.info.get("live_canopy", false): return
 var anchor:Vector2=(game.pos-game.tile_offset(id))/game.side if qa_camera_uv==null else qa_camera_uv
 var old=records.get(id,{})
 if old.get("until",0)>now:
  var scoped=old.get("panoramax",{}).get("status") in ["available","empty"]
  if not scoped or anchor.distance_to(old.get("anchor",anchor))*game.side<clampf(game.side*.13,25.,75.):return
 pending = id
 pending_uv=anchor
 next_request_ms = now + 12000
 worker = Thread.new()
 worker.start(fetch.bind(game.helper, game.cache, id, anchor))

# Use a precise, licensed observation nearest the car. The distance and capture
# epoch also bound the photographic-lighting reconstruction.
func nearby_reference(game) -> Dictionary:
 var id=game.tile_id(game.pos) if qa_camera_tile==null else qa_camera_tile
 var uv:Vector2=(game.pos-game.tile_offset(id))/game.side if qa_camera_uv==null else qa_camera_uv
 return reference_at(id,uv,game.side,qa_reference_id)

func reference_at(id:Vector2i, uv:Vector2, side:float, requested_id:String="") -> Dictionary:
 if not enabled:return {}
 var best=145.
 var chosen={}
 for neighbor in records:
  if absi(id.x-neighbor.x)>1 or absi(id.y-neighbor.y)>1:continue
  for ref in records[neighbor].get("references",[]):
   if not requested_id.is_empty() and ref.get("id")!=requested_id:continue
   var d=uv.distance_to(uv_for(ref,id))*side
   if d>=145.:continue
   if d<best-.05 or (absf(d-best)<=.05 and float(ref.get("captureUnix",0))>float(chosen.get("captureUnix",0))):
    best=d;chosen=ref
 if chosen.is_empty():return {}
 return {"frame":chosen,"distance_m":best,"fade":1.-smoothstep(40.,145.,best)}

func current_frame(game) -> Dictionary:
 var id = game.tile_id(game.pos)
 var tile = game.tiles.get(id)
 if tile and tile.info.get("region","")=="Chula":
  return {"author":"miken","sourceUrl":"https://kartaview.org/details/3690649/2059"}
 var frames = records.get(id, {}).get("frames", [])
 return nearby_reference(game).get("frame",{}) if frames.is_empty() else frames[0]

func open_credit(game):
 var frame = current_frame(game)
 if frame.is_empty(): return
 var source = str(frame.get("sourceUrl",""))
 if source.begins_with("https://kartaview.org/details/") or source.begins_with("https://panoramax.xyz/#focus=pic&pic="):
  OS.shell_open(source)

func label(game):
 if not enabled: return "Street photos off"
 var id = game.tile_id(game.pos)
 var tile = game.tiles.get(id)
 if not tile: return "Loading area"
 if not tile.info.get("live_canopy", false):
  return "Chula survey · June 2021" if tile.info.get("region","")=="Chula" else "Bundled scenery"
 var result = records.get(id, {})
 if result.is_empty(): return "Checking street photos…" if worker and pending == id else "Waiting for street photos"
 var nearby=nearby_reference(game)
 if not nearby.is_empty() and result.get("frames",[]).is_empty():
  var f=nearby.frame
  return "Panoramax · "+str(f.capturedAt).left(10)+" · "+str(f.author)+" · "+str(f.license)
 if result.get("panoramax",{}).get("status")=="unavailable":
  return "Street-photo catalog unavailable · retrying"
 match result.get("status",""):
  "no_coverage":
   return "No usable street photo nearby"
  "available":
   var appearance = result.get("appearance", {})
   var frame=current_frame(game)
   var raw_date=frame.get("capturedAt")
   var date=str(raw_date).left(10) if raw_date is String else "capture date unknown"
   if appearance is Dictionary and appearance.get("status")=="accepted":
    var track=result.get("frames",[])
    if track.size()!=3:return "Photo date · "+date
    var p=(game.pos-game.tile_offset(id))/game.side
    var a=uv_for(track[0],id)
    var b=uv_for(track[1],id)
    var c=uv_for(track[2],id)
    var d=minf(p.distance_to(Geometry2D.get_closest_point_to_segment(p,a,b)),p.distance_to(Geometry2D.get_closest_point_to_segment(p,b,c)))*game.side
    var detail="Photo colors" if d<SUPPORT_M else "Nearby photo track"
    return "%s · %s · %s / KartaView" % [detail,date,frame.get("author","Contributor")]
   return "Photos found · "+date+" · color uncertain"
 return "Street photos unavailable"

func shutdown():
 if worker:
  worker.wait_to_finish()
  worker = null
