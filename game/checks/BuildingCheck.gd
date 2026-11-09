extends Node
# godot --path game -- --building-check miami   (or cambridge)
const ROOFS = preload("res://LidarRoof.gd")
const SITES = {
 "lovelock":[-118.4740171,40.1809656,Vector2i(11200,24767)],
 "colby":[-101.0592574,39.3914620,Vector2i(14370,24954)],
 "anchorage":[-149.8922,61.2162,Vector2i(5480,18580)],
 "honolulu":[-157.8581,21.3071,Vector2i(4030,28796)],
 "cambridge":[-71.134311,42.365183,Vector2i(19818,24238)],
 "miami":[-80.208896,25.748401,Vector2i(18166,27914)]
}
var game
var site = "cambridge"
var started = 0
var phase = 0
var initial_layer = 0
var contact_frames = 0

func _ready():
 game = get_parent()
 started = Time.get_ticks_msec()
 var args = OS.get_cmdline_user_args()
 var i = args.find("--building-check")
 if i+1<args.size() and not args[i+1].begins_with("--"):
  site=args[i+1].to_lower()

func finish(ok, message):
 print(("BUILDINGS PASSED " if ok else "BUILDINGS FAILED ")+site+" "+message)
 set_process(false)
 if ok and site=="miami" and "--miami-reference" in OS.get_cmdline_user_args():
  game.add_child(preload("res://checks/MiamiReferenceCapture.gd").new())
  return
 game.request_game_close(0 if ok else 1)

func _process(_dt):
 if not SITES.has(site):finish(false,"unknown scenario");return
 if Time.get_ticks_msec()-started>120000:finish(false,"timeout in phase "+str(phase));return
 if not game.ready_drive or not game.jump.is_empty():return
 var spec=SITES[site]
 if phase==0:
  game.choose_place(spec[0],spec[1],"Building QA "+site)
  phase=1;return
 if game.current_title!="Building QA "+site:return
 var id=game.tile_id(game.pos)
 if id!=spec[2]:finish(false,"unexpected spawn tile "+str(id));return
 var tile=game.tiles.get(id)
 if not tile or not tile.structure_layer:return
 if not tile.info.has("building_status"):
  initial_layer=tile.structure_layer.get_instance_id();return
 if phase==1:
  if initial_layer==0 or tile.structure_layer.get_instance_id()==initial_layer:
   finish(false,"background structure layer was never replaced");return
  var error=""
  if site in ["lovelock","colby"]:error=check_corroboration(tile)
  elif site in ["honolulu","anchorage"]:error=check_property_surfaces(tile)
  else:error=check_meshes(tile)
  if not error.is_empty():finish(false,error);return
  phase=2
 if game.vehicle.wheel_contact==4:contact_frames+=1
 else:contact_frames=0
 if contact_frames>=4:finish(true,"contacts=4 release="+str(tile.info.get("building_release","")))

# Batches share materials across footprints. Check the concrete final vertex
# arrays in tile-local metres with a one-centimetre spatial hash.
func index_vertices(layer, materials):
 var surfaces={"roof":{},"glass":{}}
 for node in layer.find_children("*","MeshInstance3D",true,false):
  for s in node.mesh.get_surface_count():
   var mat=node.mesh.surface_get_material(s)
   var kind=""
   if mat==materials.get("streamed_roof") or mat==materials.get("roof"):kind="roof"
   if mat==materials.get("house_glass"):kind="glass"
   if kind.is_empty():continue
   var a=node.mesh.surface_get_arrays(s)
   if a.is_empty() or a[Mesh.ARRAY_VERTEX]==null:continue
   for v in a[Mesh.ARRAY_VERTEX]:
    var key=Vector3i((v*100.).round())
    surfaces[kind][key]=v
 return surfaces

func contains_vertex(index, target:Vector3):
 var key=Vector3i((target*100.).round())
 if index.has(key):return true
 for x in range(-1,2):
  for y in range(-1,2):
   for z in range(-1,2):
    var v=index.get(key+Vector3i(x,y,z))
    if v is Vector3 and v.distance_squared_to(target)<.000225:return true
 return false

func check_property_surfaces(tile) -> String:
 var observations=tile.info.get("enriched_finishes",{})
 var layer=tile.structure_layer
 var kind=4 if site=="anchorage" else 0
 var slot="tag_wall_" if site=="anchorage" else "tag_roof_"
 var used={}
 for key in layer.materials:
  var material=layer.materials[key]
  if str(key).begins_with(slot) and material is ShaderMaterial and material.get_shader_parameter("kind")==kind:
   used[material]=true
 var triangles=0
 for node in layer.find_children("*","MeshInstance3D",true,false):
  for surface in node.mesh.get_surface_count():
   if not used.has(node.mesh.surface_get_material(surface)):continue
   var arrays=node.mesh.surface_get_arrays(surface)
   if arrays.is_empty():continue
   var normals=arrays[Mesh.ARRAY_NORMAL]
   for j in range(0,normals.size(),3):
    var rise=absf(normals[j].y)
    if (site=="honolulu" and rise>.05 and rise<.999) or (site=="anchorage" and rise<.01):triangles+=1
 if observations.is_empty() or triangles==0:return "attributed geometry or final surface is missing"
 print("BUILDING FINISH ",site," observed_footprints=",observations.size()," corresponding_triangles=",triangles)
 return ""

func check_meshes(tile) -> String:
 var layer=tile.structure_layer
 var heights=tile.info.get("enriched_heights",{})
 var uses=tile.info.get("enriched_uses",{})
 if heights.is_empty() or uses.is_empty():return "missing height or independent use records"
 if heights!=layer.info.get("enriched_heights") or uses!=layer.info.get("enriched_uses"):return "rebuild omitted source records"
 var bodies=layer.find_children("*","StaticBody3D",true,false)
 if bodies.is_empty() or not tile.streamed_roof:return "missing building collision or roof material"
 var index=index_vertices(layer,layer.materials)
 var roof_matches=0
 var tall_matches=0
 for i in heights:
  var h=heights[i];var b=tile.info.buildings[i]
  if absf(h.heightM-b[2])>.02:return "applied height differs from inventory"
  if absf(h.heightM-h.baseHeightM)<.08:continue
  var corner=tile.point(b[0][0][0]);var y=tile.height_at(tile.point(h.center))+h.heightM
  if contains_vertex(index.roof,Vector3(corner.x,y,corner.y)):
   roof_matches+=1
   if h.heightM>=14. and absf(h.heightM-h.baseHeightM)>=1.:tall_matches+=1
 if roof_matches==0:return "no reported height change found on actual roof surface"
 if site=="cambridge" and tall_matches==0:return "no confirmed changed Cambridge roof elevation in mesh"
 var independent=0
 var residential_windows=0
 var previously_generic=0
 for i in uses:
  var u=uses[i];var b=tile.info.buildings[i]
  if u.get("source")!="USACE NSI" or tile.point(u.center).distance_to(tile.point([b[4],b[5]]))>.01:return "mismatched NSI center or source"
  if not heights.has(i):independent+=1
  if site=="miami" and u.use in ["single_family","duplex"] and b[2]<=15.:
   if has_residential_window(tile,b,index.glass):
    residential_windows+=1
    var original=tile.info._base_structures[i]
    var area=0.
    for k in original[0][0].size():
     var loop=original[0][0]
     area+=tile.point(loop[k]).cross(tile.point(loop[(k+1)%loop.size()]))
    if original[6] in ["","building","yes"] and original[7] in ["","building","yes"] and (absf(area)*.5>=260. or original[2]>10.):
     previously_generic+=1
 var addition_error=check_footprint_additions(tile,index.roof,bodies)
 if not addition_error.is_empty():return addition_error
 if independent==0:return "no occupancy evidence independent of a height adjustment"
 if site=="miami":
  if previously_generic==0:return "no formerly generic facade gained verified residential glazing"
  var error=check_lidar(tile,index.roof)
  if not error.is_empty():return error
 print("BUILDING GEOMETRY heights=",heights.size()," changed roofs=",roof_matches," tall=",tall_matches," uses=",uses.size()," independent uses=",independent," residential glazing=",residential_windows," formerly generic=",previously_generic," colliders=",bodies.size())
 return ""

func check_footprint_additions(tile,roof_vertices,bodies)->String:
 var measured=tile.info.get("surveyed_footprints",{})
 var original=tile.info.get("_base_structures",[])
 var expected=1
 if measured.size()!=expected or tile.info.buildings.size()!=original.size()+expected:return "verified footprints absent or duplicated"
 var faces={}
 for body in bodies:
  for node in body.find_children("*","CollisionShape3D",true,false):
   if node.shape is ConcavePolygonShape3D:
    for point in node.shape.get_faces():faces[Vector3i((point*100.).round())]=point
 for record in measured.values():
  var b=record.building;var p=tile.point([b[4],b[5]]);var y=tile.height_at(p)+b[2]
  if not tile.occupied(p):return "verified structure missing its ground footprint"
  for xy in b[0][0]:
   var corner=tile.vert(tile.point(xy),y)
   if not contains_vertex(roof_vertices,corner):return "verified footprint missing its measured roof"
   if not contains_vertex(faces,corner):return "verified footprint missing its wall collider"
 print("ADDITIONAL FOOTPRINTS ",measured.size()," measured roofs and wall colliders, original indices retained ",original.size())
 return ""

func has_residential_window(tile,b,index):
 var center=tile.point([b[4],b[5]]);var base=tile.height_at(center)
 for ring in b[0]:
  for i in ring.size():
   var a=tile.point(ring[i]);var d=tile.point(ring[(i+1)%ring.size()])
   var length=a.distance_to(d)
   if length<3.1:continue
   var line=(d-a)/length;var out=line.orthogonal()
   if ((a+d)*.5-center).dot(out)<0:out=-out
   var count=clampi(roundi(length/3.9),1,8)
   var edge=a.lerp(d,.5/count)+out*.038-line*.49
   if contains_vertex(index,Vector3(edge.x,base+.975,edge.y)):return true
 return false

func check_lidar(tile,roof_vertices) -> String:
 var file="res://data/lidar-roofs-18166-27914.json"
 var doc=JSON.parse_string(FileAccess.get_file_as_string(file))
 if not doc is Dictionary or doc.get("roofs",[]).is_empty():return "missing Miami survey"
 var matched=0;var found_vertices=0;var expected_vertices=0
 for b in tile.info.buildings:
  var survey=ROOFS.find(tile,b)
  if survey.is_empty():continue
  matched+=1
  var shifted=b.duplicate(true)
  shifted[0][0][0][0]=float(shifted[0][0][0][0])+.0005
  if not ROOFS.find(tile,shifted).is_empty():return "survey accepted an altered footprint signature"
  var base=tile.height_at(tile.point([b[4],b[5]]))
  var checked={}
  for raw_idx in survey.f:
   var i=int(raw_idx)
   if checked.has(i):continue
   checked[i]=true;expected_vertices+=1
   var v=survey.v[i];var p=Vector2(v[0],v[1])*tile.side/100000.
   var target=Vector3(p.x,base+float(v[2])*.01,p.y)
   if not contains_vertex(roof_vertices,target):return "missing measured roof vertex on profile "+str(matched)+": "+str(target)
   found_vertices+=1
 if matched!=doc.roofs.size() or found_vertices<30:return "survey footprint count or geometry coverage mismatch"
 print("MIAMI LIDAR surveyed roofs=",matched," final roof vertices=",found_vertices,"/",expected_vertices," altered signatures rejected=",matched)
 return ""

func check_corroboration(tile)->String:
 var additions=tile.info.get("corroborated_footprints",{})
 var original=tile.info.get("_base_structures",[])
 var expected=19 if site=="lovelock" else 1
 if additions.size()!=expected or tile.info.buildings.size()!=original.size()+expected:return "outline count or deduplication failed"
 if not tile.info.get("surveyed_footprints",{}).is_empty():return "inferred elevation entered surveyed roof inventory"
 for i in original.size():
  if original[i][0]!=tile.info.buildings[i][0]:return "original building indices changed"
 var layer=tile.structure_layer
 var roof=index_vertices(layer,layer.materials).roof
 var walls={}
 for node in layer.find_children("*","CollisionShape3D",true,false):
  if node.shape is ConcavePolygonShape3D:
   for vertex in node.shape.get_faces():walls[Vector3i((vertex*100.).round())]=vertex
 var inferred=0
 var assumed=0
 for key in additions:
  var entry=additions[key];var observation=entry.observation;var b=entry.building
  if b[6]!="corroborated_footprint" or observation.get("eavesM")!=null or observation.has("heightM"):return "unknown eaves labeled measured"
  if observation.heightStatus=="source-model" and observation.displayHeightSource=="Microsoft ML Buildings":inferred+=1
  elif observation.heightStatus=="unknown" and is_equal_approx(observation.displayHeightM,3.):assumed+=1
  else:return "invalid display-height provenance"
  var h=tile.height_at(tile.point([b[4],b[5]]))+b[2]
  for xy in b[0][0]:
   var corner=tile.vert(tile.point(xy),h)
   if not contains_vertex(roof,corner) or not contains_vertex(walls,corner):return "roof or wall does not match outline"
  var middle=Vector2.ZERO
  for j in 3:middle+=tile.point(b[0][0][int(b[1][j])])/3.
  if not tile.occupied(middle):return "addition missing its actual ground occupancy"
 if inferred!=(16 if site=="lovelock" else 0) or assumed!=(3 if site=="lovelock" else 1):return "unexpected provenance counts"
 print("CORROBORATED OUTLINES ",site," original=",original.size()," model_height=",inferred," unknown_height=",assumed)
 return ""
