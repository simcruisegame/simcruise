extends Node3D
# Measured crowns keep their ground anchors, diameters and heights across detail levels.
const STREET_OAK = preload("res://art/oak0.glb")
const CROWN_SHADER = preload("res://Canopy.gdshader")
var bark_material = ShaderMaterial.new()
var sprays = {}
var crowns = {}
var rings = []
var trunks = []
var last = Vector2.INF
var last_revision = -1
var last_view_pos=Vector3.INF
var last_forward=Vector3.ZERO
var last_fov=0.
var municipal_crowns=preload("res://MunicipalCrownDisplay.gd").new()
var observed_forest=preload("res://SurveyForest.gd").new()

func photo(file_path: String, jpeg := false) -> Texture2D:
 var im = Image.new()
 var bytes = FileAccess.get_file_as_bytes("res://data/" + file_path)
 var result = im.load_jpg_from_buffer(bytes) if jpeg else im.load_png_from_buffer(bytes)
 if result != OK:
  push_error("Unable to read tree texture " + file_path)
  return null
 if not jpeg: im.fix_alpha_edges()
 im.generate_mipmaps()
 return ImageTexture.create_from_image(im)

func mesh_data(file_name: String) -> ArrayMesh:
 var parser = GLTFDocument.new()
 var state = GLTFState.new()
 var bytes = FileAccess.get_file_as_bytes("res://data/" + file_name + ".meshdata")
 if parser.append_from_buffer(bytes,"",state) != OK:
  push_error("Unable to read tree mesh " + file_name)
  return ArrayMesh.new()
 var root = parser.generate_scene(state)
 var holder = root if root is MeshInstance3D else root.find_children("*","MeshInstance3D",true,false)[0]
 var mesh = holder.mesh
 root.free()
 return mesh

func summer_mesh(species: String, level: int) -> ArrayMesh:
 var foliage = mesh_data("crown-%s-%d" % [species,level])
 var kind = "pine" if species == "pine" else "leaf"
 if level > 0:
  foliage.surface_set_material(0,bark_material)
  foliage.surface_set_material(1,crowns[kind])
  return foliage
 var detailed: ArrayMesh
 if species == "oak":
  var scene = STREET_OAK.instantiate()
  var node = scene if scene is MeshInstance3D else scene.find_children("*","MeshInstance3D",true,false)[0]
  var source = node.mesh
  var arrays = source.surface_get_arrays(0)
  var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
  for i in vertices.size(): vertices[i] /= Vector3(7.75,8.61,7.75)
  arrays[Mesh.ARRAY_VERTEX] = vertices
  detailed = ArrayMesh.new()
  detailed.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,arrays)
  detailed.surface_set_material(0,source.surface_get_material(0))
  scene.free()
 else:
  detailed = mesh_data("wood-%s-0" % species)
  detailed.surface_set_material(0,bark_material)
  detailed.surface_set_material(1,sprays[kind])
 var surface = detailed.get_surface_count()
 detailed.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,foliage.surface_get_arrays(0))
 detailed.surface_set_material(surface,crowns[kind])
 return detailed

# The canopy shader darkens toward the trunk and fades leaves near the eye in
# mesh units, so each crown material learns the extents of the surface it covers.
func fit_crown(material: ShaderMaterial, mesh: ArrayMesh) -> void:
 for surface in mesh.get_surface_count():
  if mesh.surface_get_material(surface) != material: continue
  var vertices: PackedVector3Array = mesh.surface_get_arrays(surface)[Mesh.ARRAY_VERTEX]
  if vertices.is_empty(): return
  var bounds := AABB(vertices[0], Vector3.ZERO)
  for vertex in vertices: bounds = bounds.expand(vertex)
  material.set_shader_parameter("crown_centre", bounds.get_center())
  material.set_shader_parameter("crown_radii", (bounds.size * 0.5).max(Vector3.ONE * 0.05))
  return

func _ready():
 bark_material.shader = preload("res://Bark.gdshader")
 bark_material.set_shader_parameter("bark",photo("tree-bark.jpg",true))
 for kind in ["leaf","pine"]:
  var branch = StandardMaterial3D.new()
  branch.albedo_texture = photo("spray-%s.png" % kind)
  branch.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
  branch.alpha_scissor_threshold = .30
  branch.cull_mode = BaseMaterial3D.CULL_DISABLED
  branch.roughness = .96
  sprays[kind] = branch
  var crown = ShaderMaterial.new()
  crown.shader = CROWN_SHADER
  crown.set_shader_parameter("foliage",photo("cluster-%s.png" % kind))
  crowns[kind] = crown
 for species in ["oak","leaf","pine","winter"]:
  for level in 3:
   var mesh: ArrayMesh
   if species == "winter":
    mesh = mesh_data("wood-winter-%d" % level)
    mesh.surface_set_material(0,bark_material)
   else:
    mesh = summer_mesh(species,level)
   var batch = MultiMesh.new()
   batch.transform_format = MultiMesh.TRANSFORM_3D
   batch.use_custom_data = true
   batch.mesh = mesh
   var chunk = MultiMeshInstance3D.new()
   chunk.multimesh = batch
   if level == 2: chunk.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
   add_child(chunk)
   rings.append(batch)
 fit_crown(crowns["leaf"], rings[3].mesh)
 fit_crown(crowns["pine"], rings[6].mesh)
 var thicket = mesh_data("forest-understory")
 thicket.surface_set_material(0,crowns["leaf"])
 var plants = MultiMesh.new()
 plants.transform_format = MultiMesh.TRANSFORM_3D
 plants.use_custom_data = true
 plants.mesh = thicket
 var node = MultiMeshInstance3D.new()
 node.multimesh = plants
 node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
 add_child(node)
 rings.append(plants)
 for i in 32:
  var body = StaticBody3D.new()
  var collision = CollisionShape3D.new()
  var cylinder = CylinderShape3D.new()
  cylinder.radius = 1.
  cylinder.height = 2.
  collision.shape = cylinder
  body.add_child(collision)
  body.collision_layer = 0
  add_child(body)
  trunks.append(body)

func forest_floor(tile, row, offset, lists):
 if not tile.surface_mask or tile.info.get("region","") == "leaf-off": return
 var trunk = Vector2(row[0].x,row[0].z)
 for i in 2:
  var phase = row[2] + i*2.38
  var local = trunk + Vector2(cos(phase),sin(phase))*minf(3.6,row[3]*.22)
  if local.x < 0. or local.y < 0. or local.x >= tile.side or local.y >= tile.side: continue
  var mask = tile.sample_surface(tile.surface_mask,local)
  if mask.b < .60 or mask.r > .08 or mask.a > .60: continue
  if tile.surface_grip(local) > .82 or tile.occupied(local): continue
  var size = clampf(row[1]*(.20+.12*fposmod(phase*3.7,1.)),1.15,6.2)
  var scale = Vector3(size*1.46,size,size)
  var basis = Basis(Vector3.UP,phase)*Basis.from_scale(scale)
  var root = Vector3(local.x+offset.x,tile.height_at(local),local.y+offset.y)
  lists[12].append([Transform3D(basis,root),fposmod(phase*8.34,1.)])

func add_forest_conifers():
 var material = ShaderMaterial.new()
 material.shader = CROWN_SHADER
 material.set_shader_parameter("foliage",photo("forest-needle-fan.png"))
 for level in 3:
  var mesh=mesh_data("forest-conifer-%d"%level)
  mesh.surface_set_material(0,bark_material)
  mesh.surface_set_material(1,material)
  mesh.surface_set_material(2,crowns["pine"])
  if level==0:fit_crown(material,mesh)
  var batch=MultiMesh.new()
  batch.transform_format=MultiMesh.TRANSFORM_3D
  batch.use_custom_data=true
  batch.mesh=mesh
  var chunk=MultiMeshInstance3D.new()
  chunk.multimesh=batch
  if level==2:chunk.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
  add_child(chunk)
  rings.append(batch)

var survey_foliage_materials=[]
var forest_groups={}
var far_nodes=[]
func far_foliage(tex):
 var mat=ShaderMaterial.new()
 mat.shader=preload("res://SurveyFarCrown.gdshader")
 mat.set_shader_parameter("foliage",tex)
 survey_foliage_materials.append(mat)
 return mat

func build_far_crowns():
 var conifer=mesh_data("als-far-forest-conifer-2")
 for pointed in [false,true]:
  var source=conifer if pointed else mesh_data("als-far-crown-leaf-2")
  var mesh=ArrayMesh.new()
  mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,conifer.surface_get_arrays(0))
  mesh.surface_set_material(0,bark_material)
  mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,source.surface_get_arrays(1))
  mesh.surface_set_material(1,far_foliage(photo("forest-needle-fan.png" if pointed else "cluster-leaf.png")))
  if pointed:
   mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,source.surface_get_arrays(2))
   mesh.surface_set_material(2,far_foliage(photo("cluster-pine.png")))
  var mm=MultiMesh.new();mm.transform_format=MultiMesh.TRANSFORM_3D;mm.use_custom_data=true;mm.mesh=mesh
  var node=MultiMeshInstance3D.new();node.multimesh=mm;node.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF;add_child(node);far_nodes.append(node);rings.append(mm)

func far_group(id,kind,lists):
 var key=Vector3i(id.x,id.y,kind)
 if forest_groups.has(key):return forest_groups[key]
 var mm=MultiMesh.new();mm.transform_format=MultiMesh.TRANSFORM_3D;mm.use_custom_data=true
 mm.mesh=rings[16+kind].mesh
 var node=MultiMeshInstance3D.new();node.multimesh=mm;node.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
 add_child(node);far_nodes.append(node);var slot=rings.size();rings.append(mm);lists.append([])
 forest_groups[key]=slot;return slot

func release_far_groups():
 if far_nodes.is_empty():return
 for node in far_nodes:
  node.hide()
  node.queue_free()
 far_nodes.clear()
 forest_groups.clear()
 survey_foliage_materials.clear()
 rings.resize(16)

func _process(_delta):
 if municipal_crowns.poll(self):last_revision=-1
 if observed_forest.poll(self):last_revision=-1

func _exit_tree():
 municipal_crowns.shutdown()
 observed_forest.shutdown()

func refresh(game):
 var camera=game.get_viewport().get_camera_3d()
 # Historical source dates also apply when an archived nighttime sun angle is unusable.
 var survey_epoch=game.daylight.previous_epoch if game.daylight.previous_weight>0. else 0
 var changed_view=municipal_crowns.set_epoch(survey_epoch)
 if camera and not observed_forest.tiles.is_empty():
  var dir=-camera.global_basis.z
  changed_view=changed_view or last_view_pos.distance_squared_to(camera.global_position)>16. or dir.dot(last_forward)<.91 or absf(camera.fov-last_fov)>3.
  if changed_view:last_view_pos=camera.global_position;last_forward=dir;last_fov=camera.fov
 if not changed_view and game.tile_count==last_revision and last.distance_squared_to(game.pos)<16:return
 last_revision=game.tile_count;last=game.pos
 municipal_crowns.begin_frame()
 var lists=[];var contacts=[]
 for batch in rings:lists.append([])
 var cut={}
 observed_forest.apply(game)
 if not observed_forest.tiles.is_empty():
  if rings.size()==13:add_forest_conifers()
  if rings.size()==16:build_far_crowns()
  while lists.size()<rings.size():lists.append([])
  for mat in survey_foliage_materials:
   var loss=game.world_environment.fog_density if game.live_weather else .00020
   mat.set_shader_parameter("extinction",loss);mat.set_shader_parameter("haze_color",game.world_environment.fog_light_color)
  observed_forest.fill(game,lists,cut)
 for id in game.tiles:
  var tile=game.tiles[id];var o=game.tile_offset(id)
  for record in tile.grove:
   var row:Array=record
   var recovered=record.size()>7
   if recovered:
    var fallback:Array=record[7]
    if not municipal_crowns.dated(record[6]):
     if fallback.is_empty():continue
     row=fallback;recovered=false
    elif not fallback.is_empty():
     var previous_p:Vector3=fallback[0]+Vector3(o.x,0,o.y)
     var previous_d=Vector2(previous_p.x,previous_p.z).distance_squared_to(last)
     if previous_d<28*28:contacts.append([previous_d,previous_p,fallback[4],fallback[1]])
   var p=row[0]+Vector3(o.x,0,o.y);var d=Vector2(p.x,p.z).distance_squared_to(last)
   if d>485*485:continue
   if not cut.is_empty() and tile.info.get("tree_inventories",{}).is_empty() and observed_forest.covered(Vector2(p.x,p.z),cut):
    if not recovered and d<28*28:contacts.append([d,p,row[4],row[1]])
    continue
   var slot=0 if d<33*33 else (1 if d<92*92 else 2)
   var species=int(row[5])
   if species==4:
    if rings.size()==13:
     add_forest_conifers()
     for k in 3:lists.append([])
    slot+=13
   else:slot+=species*3
   var pigment=fposmod(row[2]*27.13,1.)
   var size := Vector3(row[3],row[1],row[3])
   var b=Basis(Vector3.UP,row[2])*Basis.from_scale(size)
   var shape_drawn=false
   if row.size()>6 and row[6] is Dictionary and d<125.*125. and slot<9:
    shape_drawn=municipal_crowns.offer(rings[slot].mesh,row,slot,Transform3D(b,p),pigment,d)
   if not shape_drawn:lists[slot].append([Transform3D(b,p),pigment])
   if d<175*175:forest_floor(tile,row,o,lists)
   if not recovered and d<28*28:contacts.append([d,p,row[4],row[1]])
 municipal_crowns.end_frame()
 for k in rings.size():
  var mm=rings[k];var count=lists[k].size()
  if mm.instance_count<count:mm.instance_count=ceili(count/32.)*32
  mm.visible_instance_count=count
  if k>=18 and count>0:
   # Keep unused preallocated slots out of the spatial bounds. Godot can then cull full tile batches, including in mirrors.
   var origin:Vector3=lists[k][0][0].origin
   var bounds=AABB(origin,Vector3.ZERO)
   for record in lists[k]:
    var t:Transform3D=record[0]
    bounds=bounds.expand(t.origin).expand(t.origin+Vector3.UP*t.basis.y.length())
   mm.custom_aabb=bounds.grow(10.)
  for i in count:
   mm.set_instance_transform(i,lists[k][i][0])
   mm.set_instance_custom_data(i,lists[k][i][1] if lists[k][i][1] is Color else Color(lists[k][i][1],0,0,0))
 contacts.sort_custom(func(a,b):return a[0]<b[0])
 for i in trunks.size():
  var b=trunks[i]
  if i>=contacts.size():b.collision_layer=0;continue
  var row=contacts[i];var shape=b.get_child(0).shape;shape.height=clampf(row[3]*.36,1.5,5.);shape.radius=clampf(row[2]*.5,.04,.68)
  b.position=row[1]+Vector3.UP*shape.height*.5;b.collision_layer=1
