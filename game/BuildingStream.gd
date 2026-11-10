extends RefCounted

# Overture observations arrive after the base driving tile. Terrain, the current
# car, and previously downloaded imagery remain active while the buildings cook.
const TILE = preload("res://WorldTile.gd")
var worker: Thread
var pending_id: Vector2i
var pending_instance = 0
var next_check = 0
var next_roof_check = 0
var roof_job = false
var layout_job = false
var layout_visits:Dictionary={}
# Mapped lane counts the helper did not have cached when it built the tile
# (road_lanes status "pending"): fetched here, off the first tile's critical
# path, then the markings are repainted (LaneMarkings.gd).
var lanes_job = false
var lane_visits:Dictionary={}
var pending_patch = Vector2i.ZERO

static func number(value)->bool:
 return (value is int or value is float) and is_finite(float(value))

static func rows(data:Dictionary,key:String)->Array:
 var value=data.get(key)
 return value if value is Array else []

static func matched_index(record,buildings:Array)->int:
 if not record is Dictionary:return -1
 var value=record.get("baseIndex")
 if not number(value) or value<0 or value>=buildings.size() or value!=int(value):return -1
 var index=int(value)
 var center=record.get("center")
 if not center is Array or center.size()!=2:return -1
 for axis in 2:
  if not number(center[axis]) or absf(float(center[axis])-float(buildings[index][4+axis]))>.00001:return -1
 return index

static func valid_finish(style)->bool:
 if not style is Dictionary or style.is_empty():return false
 for key in style:
  var value=style[key]
  if key=="across":
   if not value is bool:return false
  elif key=="roof_height":
   if not number(value) or value<=0 or value>40:return false
  elif key in ["wall_material","roof_material","wall_color","roof_color","shape"]:
   if not value is String or value.length()>32:return false
  else:return false
 return true

static func admitted_footprints(data:Dictionary,info:Dictionary)->Dictionary:
 var fingerprint=info.get("footprint_survey","")
 if not fingerprint is String or fingerprint.length()!=24:return {}
 if data.get("additionsStatus","unavailable")=="unavailable":
  var previous=info.get("corroborated_footprints",{}).duplicate()
  previous.merge(info.get("surveyed_footprints",{}),true)
  return previous
 if data.get("baselineFingerprint","")!=fingerprint:return {}
 var accepted={}
 for entry in rows(data,"additions"):
  if not entry is Dictionary or accepted.size()>=64:continue
  var id=entry.get("id");var ring=entry.get("ring");var center=entry.get("center")
  var corroborated=entry.get("geometryStatus","")=="corroborated-outline"
  var height=entry.get("displayHeightM") if corroborated else entry.get("heightM")
  if corroborated and (entry.get("heightStatus","") not in ["source-model","unknown"] or entry.get("eavesM")!=null):continue
  if not id is String or id.length()!=36 or accepted.has(id) or not number(height) or height<2. or height>85.:continue
  if not ring is Array or ring.size()<3 or ring.size()>96 or not center is Array or center.size()!=2:continue
  if not number(center[0]) or not number(center[1]):continue
  var polygon=PackedVector2Array()
  for p in ring:
   if not p is Array or p.size()!=2 or not number(p[0]) or not number(p[1]):break
   if minf(p[0],p[1])<=0. or maxf(p[0],p[1])>=1.:break
   polygon.append(Vector2(p[0],p[1]))
  if polygon.size()!=ring.size():continue
  var triangles=Geometry2D.triangulate_polygon(polygon)
  if triangles.size()!=(ring.size()-2)*3:continue
  accepted[id]={"building":[[ring],Array(triangles),height,0.,center[0],center[1],"corroborated_footprint" if corroborated else "surveyed_footprint",""],"observation":entry}
 return accepted

func fetch(helper,cache,id,info,side,materials,segments):
 var lines=[]
 var code=OS.execute(helper,["buildings",cache,str(id.x),str(id.y)],lines,true)
 var data=JSON.parse_string(str(lines[0])) if code==0 and not lines.is_empty() else null
 if not data is Dictionary or not data.get("updates") is Array:
  return {"status":"unavailable"}
 var returned=data.get("id",[])
 if not returned is Array or returned.size()!=2 or not number(returned[0]) or not number(returned[1]) or returned[0]!=id.x or returned[1]!=id.y:
  return {"status":"unavailable"}
 var result={"status":data.get("status","unavailable"),"uses_status":data.get("usesStatus","unavailable"),"requests":data.get("requests",0),"release":data.get("release"),"count":0}
 var original=info.get("_base_structures",info.buildings)
 var buildings=original.duplicate()
 var observations={}
 for change in data.updates:
  var index=matched_index(change,original)
  if index<0 or observations.has(index):continue
  var previous=buildings[index]
  var h=change.get("heightM")
  var base_height=change.get("baseHeightM")
  if not number(h) or not number(base_height) or h<.8 or h>650. or absf(float(base_height)-float(previous[2]))>.02:continue
  var changed=previous.duplicate()
  changed[2]=h
  buildings[index]=changed
  observations[index]=change
 var uses={}
 for record in rows(data,"uses"):
  var index=matched_index(record,original)
  if index<0 or uses.has(index):continue
  if record.get("use") in ["single_family","duplex","multi_family","nonresidential"]:uses[index]=record
 var finishes={}
 for row in rows(data,"finishes"):
  var index=matched_index(row,original)
  if index<0 or finishes.has(index) or not valid_finish(row.get("style")):continue
  finishes[index]=row
 if result.status=="unavailable" and finishes.is_empty():finishes=info.get("enriched_finishes",{})
 if data.get("usesStatus")=="unavailable":uses=info.get("enriched_uses",{})
 if result.status=="unavailable" and info.has("enriched_heights"):
  observations=info.enriched_heights
  buildings=info.buildings.slice(0,original.size())
 var additional=admitted_footprints(data,info)
 var surveyed={}
 var corroborated={}
 for identifier in additional:
  var destination=corroborated if additional[identifier].observation.get("geometryStatus")=="corroborated-outline" else surveyed
  destination[identifier]=additional[identifier]
 if surveyed==info.get("surveyed_footprints",{}) and corroborated==info.get("corroborated_footprints",{}) and observations==info.get("enriched_heights",{}) and uses==info.get("enriched_uses",{}) and finishes==info.get("enriched_finishes",{}):return result
 info["surveyed_footprints"]=surveyed
 info["corroborated_footprints"]=corroborated
 var addition_ids=additional.keys()
 addition_ids.sort()
 for identifier in addition_ids:buildings.append(additional[identifier].building)
 info["enriched_finishes"]=finishes
 info["enriched_uses"]=uses
 info.buildings=buildings
 info["enriched_heights"]=observations
 var layer=TILE.new()
 layer.build_structures(info,side,materials,segments)
 result["layer"]=layer
 result["count"]=observations.size()
 return result

func tick(game):
 if worker:
  if worker.is_alive():return
  var result=worker.wait_to_finish()
  worker=null
  var tile=game.tiles.get(pending_id)
  game.calls+=int(result.get("requests",0))
  if tile and tile.get_instance_id()==pending_instance:
   if lanes_job:
    var doc=result.get("road_lanes")
    if doc is Dictionary and doc.get("status")=="checked":
     tile.info["road_lanes"]=doc
     tile.install_marking_update(result.get("paint_update",{}))
    else:lane_visits[pending_id]=Time.get_ticks_msec()+600000
   elif layout_job:
    tile.info["road_layout_download"]=result.get("status","unavailable")
    if result.get("layout") is Dictionary:
     tile.info["road_layout"]=result.layout
     tile.install_lane_update(result.get("paint_update",{}))
   elif roof_job:
    var visits=tile.info.get("live_roof_visits",{})
    visits[pending_patch]=Time.get_ticks_msec()+(30000 if result.status=="pending" else 600000 if result.status=="unavailable" else 3600000)
    tile.info["live_roof_visits"]=visits
    tile.info["roof_observation"]={"status":result.status,"project":result.get("project"),"requests":result.get("requests",0),"patch":pending_patch,"cached":result.get("cached",false)}
    if result.has("layer"):tile.replace_structure_layer(result.layer)
   else:
    tile.info["building_status"]=result.status
    tile.info["building_uses_status"]=result.get("uses_status","unavailable")
    var retry_soon=result.status=="unavailable" or tile.info.building_uses_status=="unavailable"
    tile.info["building_retry"]=Time.get_ticks_msec()+(150000 if retry_soon else 3600000)
    if result.has("layer"):
     tile.replace_structure_layer(result.layer)
     tile.info["building_release"]=result.release
  elif result.has("layer"):result.layer.free()
  layout_job=false
  lanes_job=false
 var now=Time.get_ticks_msec()
 if not game.ready_drive or not game.jump.is_empty() or now<next_check:return
 next_check=now+800
 var current=game.tile_id(game.pos)
 var velocity=Vector2(game.vehicle.linear_velocity.x,game.vehicle.linear_velocity.z)
 var ahead=game.tile_id(game.pos+velocity*5.)
 # Lane counts first: the markings are in view at once, and one request
 # serves a 2 x 2 group of tiles.
 for id in [current,ahead]:
  var tile=game.tiles.get(id)
  if not tile or not tile.info.get("live_canopy",false):continue
  var lanes=tile.info.get("road_lanes")
  if not lanes is Dictionary or lanes.get("status")!="pending" or lane_visits.get(id,0)>now:continue
  if lane_visits.size()>=96:lane_visits.erase(lane_visits.keys()[0])
  lane_visits[id]=now+60000
  pending_id=id;pending_instance=tile.get_instance_id();lanes_job=true;layout_job=false;roof_job=false
  var observations=[tile.paint_evidence,tile.measured_section,tile.road_survey,tile.surveyed_outcrop,tile.crossfall_surface,tile.road_surface]
  worker=Thread.new()
  worker.start(fetch_lanes.bind(game.helper,game.cache,id,tile.info.duplicate(),game.side,tile.materials.duplicate(),observations))
  return
 for id in [current,ahead]:
  var tile=game.tiles.get(id)
  if tile==null or tile.structure_layer==null:continue
  if tile.info.buildings.is_empty() and str(tile.info.get("footprint_survey","")).is_empty():continue
  if tile.info.get("building_retry",0)>now:continue
  pending_id=id
  pending_instance=tile.get_instance_id()
  roof_job=false
  worker=Thread.new()
  worker.start(fetch.bind(game.helper,game.cache,id,tile.info.duplicate(),game.side,tile.materials.duplicate(),tile.segments.duplicate()))
  return
 # Use this existing background worker even in districts without building
 # footprints. New and revisited tiles can read the one shared 2x2 district.
 for id in [current,ahead]:
  if "--without-road-layout" in OS.get_cmdline_user_args():break
  var tile=game.tiles.get(id)
  if not tile or not tile.info.get("live_canopy",false) or tile.info.has("road_layout"):continue
  if layout_visits.get(id,0)>now:continue
  if not tile.info.roads.any(func(r):return r[1] and r[4]>=7.3 and r[2]!="" and r[0] in ["motorway","trunk","primary","secondary","tertiary"] and (r.size()<6 or r[5]!="unpaved")):continue
  if layout_visits.size()>=96:layout_visits.erase(layout_visits.keys()[0])
  layout_visits[id]=now+6*3600000
  pending_id=id;pending_instance=tile.get_instance_id();layout_job=true;roof_job=false
  var observations=[tile.paint_evidence,tile.measured_section,tile.road_survey,tile.surveyed_outcrop,tile.crossfall_surface,tile.road_surface]
  worker=Thread.new()
  worker.start(fetch_layout.bind(game.helper,game.cache,id,tile.info.duplicate(),game.side,tile.materials.duplicate(),observations))
  return
 if now<next_roof_check or "--without-streamed-roofs" in OS.get_cmdline_user_args():return
 var focus=game.pos+velocity*2.5
 var id=game.tile_id(focus)
 var tile=game.tiles.get(id)
 if tile==null or tile.structure_layer==null or tile.info.buildings.is_empty() or not tile.info.get("live_canopy",false):return
 var local=focus-game.tile_offset(id)
 var patch=Vector2i((local/tile.side*4.).floor()).clamp(Vector2i.ZERO,Vector2i(3,3))
 if tile.info.get("live_roof_visits",{}).get(patch,0)>now:return
 var eligible=false
 var center=(Vector2(patch)+Vector2(.5,.5))*.25
 for row in tile.info.buildings:
  if row[3]<.1 and center.distance_squared_to(Vector2(row[4],row[5]))<.26*.26:eligible=true;break
 if not eligible:return
 pending_id=id
 pending_patch=patch
 pending_instance=tile.get_instance_id()
 roof_job=true
 next_roof_check=now+30000
 worker=Thread.new()
 worker.start(fetch_roofs.bind(game.helper,game.cache,id,patch,tile.height_at(local),tile.info.duplicate(),game.side,tile.materials.duplicate(),tile.segments.duplicate()))

func fetch_layout(helper,cache,id:Vector2i,info:Dictionary,side:float,materials:Dictionary,observations:Array)->Dictionary:
 var args=PackedStringArray(["road-layout",cache,str(id.x),str(id.y)])
 var roads=JSON.stringify(info.roads).to_utf8_buffer()
 if roads.size()<=60000:args.append(Marshalls.raw_to_base64(roads))
 var lines=[]
 var rc=OS.execute(helper,args,lines,true)
 var result=JSON.parse_string(str(lines[0])) if rc==0 and not lines.is_empty() else null
 if not result is Dictionary:return {"status":"unavailable","requests":0}
 var layout=result.get("layout")
 if layout is Dictionary and not layout.get("spans",[]).is_empty():
  info["road_layout"]=layout
  var scratch=TILE.new()
  result["paint_update"]=scratch.prepare_lane_update(info,side,materials,observations)
  scratch.free()
 return result

func fetch_lanes(helper,cache,id:Vector2i,info:Dictionary,side:float,materials:Dictionary,observations:Array)->Dictionary:
 # The road rows go through a file: a Windows command line holds 32767 characters.
 var file=FileAccess.open(cache.path_join("lane-roads-%d-%d.json"%[id.x,id.y]),FileAccess.WRITE)
 if not file:return {"status":"unavailable","requests":0}
 file.store_string(JSON.stringify(info.roads));file.close()
 var lines=[]
 var rc=OS.execute(helper,["lane-counts",cache,str(id.x),str(id.y)],lines,true)
 var result=JSON.parse_string(str(lines[0])) if rc==0 and not lines.is_empty() else null
 if not result is Dictionary:return {"status":"unavailable","requests":0}
 var doc=result.get("road_lanes")
 if doc is Dictionary and doc.get("status")=="checked":
  info["road_lanes"]=doc
  var scratch=TILE.new()
  result["paint_update"]=scratch.prepare_marking_update(info,side,materials,observations)
  scratch.free()
 return result

func fetch_roofs(helper,cache,id,patch:Vector2i,ground:float,info,side,materials,segments):
 var lines=[]
 var rc=OS.execute(helper,["roofs",cache,str(id.x),str(id.y),str(patch.x),str(patch.y),str(ground)],lines,true)
 var data=JSON.parse_string(str(lines[0])) if rc==0 and not lines.is_empty() else null
 if not data is Dictionary:return {"status":"unavailable"}
 var at=rows(data,"id")
 var part=rows(data,"patch")
 if at.size()!=2 or part.size()!=2 or at[0]!=id.x or at[1]!=id.y or part[0]!=patch.x or part[1]!=patch.y:return {"status":"unavailable"}
 var result={"status":data.get("status","unavailable"),"project":data.get("project"),"requests":data.get("requests",0),"cached":data.get("cached",false)}
 var profiles=info.get("live_roof_profiles",{}).duplicate()
 var count=profiles.size()
 var base=info.get("_base_structures",info.buildings)
 for profile in rows(data,"profiles"):
  if not profile is Dictionary or not number(profile.get("index")):continue
  var index=int(profile.index)
  if index<0 or index>=base.size() or profiles.has(index):continue
  var building=base[index]
  var footprint=profile.get("footprint",[])
  if footprint.size()!=building[0].size():continue
  var same=true
  for i in footprint.size():
   if not preload("res://LidarRoof.gd")._ring_matches(building[0][i],footprint[i]):same=false;break
  if same:profiles[index]=profile
 if count!=profiles.size():
  info["live_roof_profiles"]=profiles
  var layer=TILE.new()
  layer.build_structures(info,side,materials,segments)
  result["layer"]=layer
 return result

func shutdown():
 if worker:
  var result=worker.wait_to_finish()
  if result.has("layer"):result.layer.free()
  worker=null
