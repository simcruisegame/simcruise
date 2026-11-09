extends RefCounted
# Survey peaks describe crown locations. Only existing game bodies provide trunk or ground contact.
const EMPTY_REGION=Vector2i(-1,-1)
const VISIBILITY_M=1750.
const INDEX="res://data/als-forest-index.json"
var tiles={}
var ground_nodes={}
var disabled=false
var region=EMPTY_REGION
var index=null
var terrain_material:ShaderMaterial
var thread:Thread
var loading=EMPTY_REGION
var pending_ground=[]
var pending_crowns={}
var local_surveys_read=0
var current_side=0.

func eligible(location:Vector2,side:float)->Vector2i:
 if index==null:
  index=[]
  var catalog=JSON.parse_string(FileAccess.get_file_as_string(INDEX))
  if catalog is Dictionary and int(catalog.get("version",0))==1:
   index=catalog.get("surveys",[])
 for site in index:
  var b=site.get("bounds",[])
  var key=site.get("region",[])
  if b.size()!=4 or key.size()!=2:continue
  var closest=location.clamp(Vector2(b[0],b[1]),Vector2(b[2],b[3]))
  if location.distance_to(closest)*side<=VISIBILITY_M:return Vector2i(int(key[0]),int(key[1]))
 return EMPTY_REGION

func apply(game):
 var location:Vector2=Vector2(game.origin)+game.pos/game.side
 var key=EMPTY_REGION if disabled else eligible(location,game.side)
 if key==region:return
 region=key
 current_side=game.side
 tiles={};pending_ground=[];pending_crowns={}
 for node in ground_nodes.values():
  node.hide()
  node.queue_free()
 ground_nodes={}
 terrain_material=null
 game.grove.release_far_groups()
 if region!=EMPTY_REGION and thread==null:start_read()

func start_read():
 loading=region
 var worker=Thread.new()
 if worker.start(read_survey.bind(loading,current_side))==OK:
  thread=worker
  local_surveys_read+=1

func poll(grove)->bool:
 if thread!=null and not thread.is_alive():
  var result=thread.wait_to_finish()
  thread=null
  if loading==region and result is Dictionary:
   pending_crowns=result.get("crowns",{})
   pending_ground=result.get("ground",[])
  elif region!=EMPTY_REGION:
   start_read()
 if not pending_ground.is_empty():
  var tile=pending_ground.pop_back()
  if terrain_material==null:
   terrain_material=ShaderMaterial.new()
   terrain_material.shader=preload("res://SurveyForestGround.gdshader")
  var mesh=ArrayMesh.new()
  mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,tile[1])
  mesh.surface_set_material(0,terrain_material)
  var node=MeshInstance3D.new()
  node.mesh=mesh
  node.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
  node.hide()
  grove.add_child(node)
  ground_nodes[tile[0]]=node
 if pending_ground.is_empty() and not pending_crowns.is_empty():
  tiles=pending_crowns
  pending_crowns={}
  return true
 return false

func shutdown():
 if thread!=null:
  thread.wait_to_finish()
  thread=null

static func read_survey(key:Vector2i,side:float)->Dictionary:
 var path="res://data/als-forest-%d-%d.json"%[key.x,key.y]
 var stream=FileAccess.open(path,FileAccess.READ)
 if not stream or stream.get_length()>400000:return {}
 var doc=JSON.parse_string(stream.get_as_text())
 if not doc is Dictionary or int(doc.get("version",0))!=1 or doc.get("region",[]).size()!=2:return {}
 if Vector2i(int(doc.region[0]),int(doc.region[1]))!=key:return {}
 var crowns={}
 for batch in doc.get("tiles",[]):
  if batch.size()!=3:continue
  var id=Vector2i(int(batch[0]),int(batch[1]))
  var rows=[]
  for row in batch[2]:
   if row.size()!=10 or row[3]<35. or row[3]>650. or row[4]<17. or row[4]>200.:continue
   var uv=Vector2(row[0],row[1])/65535.
   var rgb=Color(float(row[7])/255.,float(row[8])/255.,float(row[9])/255.)
   rows.append([uv,float(row[2])*.1,float(row[3])*.1,float(row[4])*.1,int(row[5]),float(row[6])*.01,rgb])
  if not rows.is_empty():crowns[id]=rows
 var ground_path="res://data/als-ground-%d-%d.meshdata"%[key.x,key.y]
 var ground=FileAccess.open(ground_path,FileAccess.READ)
 if not ground or ground.get_length()>1500000:return {}
 var arrays=decode_ground(ground.get_buffer(ground.get_length()),doc.get("groundTiles",[]),side)
 if arrays.is_empty():return {}
 return {"crowns":crowns,"ground":arrays}

static func decode_ground(bytes:PackedByteArray,ground_tiles:Array,side:float)->Array:
 var result=[]
 for row in ground_tiles:
  if row.size()!=5:continue
  var id=Vector2i(int(row[0]),int(row[1]));var offset=int(row[2]);var nv=int(row[3]);var ni=int(row[4])
  if nv<3 or nv>65535 or ni<3 or ni%3 or offset<0 or offset+nv*10+ni*2>bytes.size():continue
  var vs=PackedVector3Array();var cs=PackedColorArray();var ix=PackedInt32Array();vs.resize(nv);cs.resize(nv);ix.resize(ni)
  for i in nv:
   var j=offset+i*10
   vs[i]=Vector3(bytes.decode_u16(j)*side/65535.,bytes.decode_s16(j+4)*.1,bytes.decode_u16(j+2)*side/65535.)
   cs[i]=Color(bytes[j+6]/255.,bytes[j+7]/255.,bytes[j+8]/255.,float(bytes[j+9]))
  var valid=true
  for i in ni:
   var k=bytes.decode_u16(offset+nv*10+i*2)
   if k>=nv:valid=false;break
   ix[i]=k
  if not valid:continue
  var normals=PackedVector3Array();normals.resize(nv)
  for i in range(0,ni,3):
   var a=ix[i];var b=ix[i+1];var c=ix[i+2];var n=(vs[c]-vs[a]).cross(vs[b]-vs[a]).normalized()
   if n.y<0:n=-n
   normals[a]+=n;normals[b]+=n;normals[c]+=n
  for i in nv:normals[i]=normals[i].normalized()
  var arr=[];arr.resize(Mesh.ARRAY_MAX);arr[Mesh.ARRAY_VERTEX]=vs;arr[Mesh.ARRAY_NORMAL]=normals;arr[Mesh.ARRAY_COLOR]=cs;arr[Mesh.ARRAY_INDEX]=ix
  result.append([id,arr])
 return result

func fill(game,lists,cut):
 terrain_material.set_shader_parameter("extinction",game.world_environment.fog_density if game.live_weather else .00020)
 terrain_material.set_shader_parameter("haze_color",game.world_environment.fog_light_color)
 for id in ground_nodes:
  var node=ground_nodes[id];var at=game.tile_offset(id);node.position=Vector3(at.x,0.,at.y)
  node.visible=not game.tiles.has(id)
 var camera=game.get_viewport().get_camera_3d()
 var eye=Vector3(game.pos.x,0.,game.pos.y) if not camera else camera.global_position
 var dir=Vector3(0,0,-1) if not camera else -camera.global_basis.z
 var viewport=game.get_viewport().get_visible_rect().size
 var focal=viewport.y/(2.*tan(deg_to_rad(62.)/2.))
 if camera:focal=(viewport.x if camera.keep_aspect==Camera3D.KEEP_WIDTH else viewport.y)/(2.*tan(deg_to_rad(camera.fov)/2.))
 for id in tiles:
  var offset=game.tile_offset(id)
  var tile=game.tiles.get(id)
  var originals={}
  if tile:
   for old in tile.grove:
    var cell=Vector2i((Vector2(old[0].x,old[0].z)/12.).floor())
    if not originals.has(cell):originals[cell]=[]
    originals[cell].append(old)
  for r in tiles[id]:
   var loc:Vector2=r[0]*game.side
   var p:Vector2=loc+offset;var dist=p.distance_to(game.pos)
   if dist>VISIBILITY_M:continue
   # In a live city an authoritative tree inventory takes precedence.
   if tile and tile.info.get("tree_inventories",{}).size()>0:continue
   if tile and (tile.occupied(loc) or tile.surface_grip(loc)>.80):continue
   var basis=Basis(Vector3.UP,fposmod(p.x*.77+p.y*3.01,TAU))*Basis.from_scale(Vector3(r[3],r[2],r[3]))
   # The position stores absolute surveyed ground plus a fitted canopy-top height.
   var root=Vector3(p.x,r[1],p.y)
   var to_tree=root+Vector3.UP*r[2]*.6-eye
   var range_m=maxf(1.,to_tree.length());var high_px=r[2]*focal/range_m
   var visible=to_tree.dot(dir)>range_m*.70
   var lod=0 if visible and range_m<240 and high_px>180 else (1 if visible and range_m<410 and high_px>65 else 2)
   var slot=13+lod
   if r[4]==0:
    # A mixed stand does not decide tree species. Reuse its existing local form when the proposed crown matches.
    # Otherwise the generic far mesh only represents the measured taper.
    var kind=-1
    var best=clampf(r[3]*.55,2.5,6.5)
    if dist<=360:
     var c=Vector2i((loc/12.).floor())
     for j in range(-1,2):
      for i in range(-1,2):
       for old in originals.get(c+Vector2i(i,j),[]):
        var delta=loc.distance_to(Vector2(old[0].x,old[0].z))
        if delta<best:kind=int(old[5]);best=delta
    if kind<0:slot=17 if r[5]<.60 else 16
    elif kind!=4:slot=kind*3+lod
   if range_m>410 or (range_m>240 and high_px<52):slot=17 if r[4]==1 or r[5]<.60 else 16
   if slot>=16:slot=game.grove.far_group(id,slot-16,lists)
   lists[slot].append([Transform3D(basis,root),r[6] if slot>=16 else fposmod(p.x*.62+p.y*.723,1.)])
   if dist<=530:
    var cell=Vector2i((p/10.).floor())
    if not cut.has(cell):cut[cell]=[]
    cut[cell].append([p,r[3]*.65])
func covered(pos:Vector2,cut:Dictionary)->bool:
 var at=Vector2i((pos/10.).floor())
 for oy in range(-1,2):
  for ox in range(-1,2):
   for q in cut.get(at+Vector2i(ox,oy),[]):
    if pos.distance_squared_to(q[0])<q[1]*q[1]:return true
 return false
