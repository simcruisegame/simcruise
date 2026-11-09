extends Node3D
var grass:MultiMesh
var scrub:MultiMesh
var last=Vector2.INF
var revision=-1
func batch(mesh,use_color):
 var mm=MultiMesh.new()
 mm.transform_format=MultiMesh.TRANSFORM_3D;mm.use_colors=use_color;mm.mesh=mesh
 var node=MultiMeshInstance3D.new();node.multimesh=mm
 node.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
 add_child(node)
 return mm
func grass_mesh():
 var grass_material=StandardMaterial3D.new()
 grass_material.vertex_color_use_as_albedo=true;grass_material.roughness=.98;grass_material.metallic_specular=.06
 grass_material.cull_mode=BaseMaterial3D.CULL_DISABLED
 var st=SurfaceTool.new();st.begin(Mesh.PRIMITIVE_TRIANGLES);st.set_material(grass_material)
 var rng=RandomNumberGenerator.new();rng.seed=81301
 for i in 46:
  var angle=rng.randf()*TAU;var across=Vector3(cos(angle),0,sin(angle))
  var base=Vector3(rng.randf_range(-.47,.47),0,rng.randf_range(-.47,.47))
  var height=rng.randf_range(.09,.31);var width=rng.randf_range(.006,.017)
  var tip=base+Vector3.UP*height+across*.07;var normal=(Vector3.UP*.6+Vector3(-across.z,0,across.x)).normalized()
  var points=[base-across*width,base+across*width,tip]
  for j in 3:
   st.set_color(Color(.68,.68,.68) if j<2 else Color.WHITE)
   st.set_normal(normal);st.add_vertex(points[j])
 return st.commit()
func _ready():
 grass=batch(grass_mesh(),true)
 var state=GLTFState.new();var parser=GLTFDocument.new()
 if parser.append_from_buffer(FileAccess.get_file_as_bytes("res://data/roadside-shrub.meshdata"),"",state)!=OK:return
 var root=parser.generate_scene(state)
 var holder=root if root is MeshInstance3D else root.find_children("*","MeshInstance3D",true,false)[0]
 var mesh=holder.mesh
 var im=Image.new();im.load_png_from_buffer(FileAccess.get_file_as_bytes("res://data/shrub-color.png"))
 im.fix_alpha_edges();im.generate_mipmaps()
 var m=StandardMaterial3D.new();m.albedo_texture=ImageTexture.create_from_image(im)
 m.transparency=BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR;m.alpha_scissor_threshold=.32
 m.cull_mode=BaseMaterial3D.CULL_DISABLED;m.roughness=.96
 mesh.surface_set_material(0,m);scrub=batch(mesh,false);root.free()
func write(mm,rows):
 if not mm:return
 if mm.instance_count<rows.size():mm.instance_count=ceili(rows.size()/32.)*32
 mm.visible_instance_count=rows.size()
 for i in rows.size():
  mm.set_instance_transform(i,rows[i][0])
  if mm.use_colors:mm.set_instance_color(i,rows[i][1])
func refresh(game):
 if game.tile_count==revision and last.distance_squared_to(game.pos)<25:return
 last=game.pos;revision=game.tile_count
 var blades=[];var shrubs=[]
 var origin=Vector2i((game.pos/1.45).floor())
 for dx in range(-19,20):
  for dz in range(-19,20):
   if dx*dx+dz*dz>361:continue
   var cell=origin+Vector2i(dx,dz)
   var tile_id=game.tile_id(Vector2(cell)*1.45)
   var rng=absi(hash(Vector4i(tile_id.x,tile_id.y,cell.x,cell.y)))
   var pos=(Vector2(cell)+Vector2(rng%101/101.,(rng/101)%101/101.))*1.45
   var id=game.tile_id(pos);var t=game.tiles.get(id)
   if not t:continue
   var p=pos-game.tile_offset(id)
   var photo=t.photo_sample
   if not photo:continue
   var rgb=t.sample_surface(photo,p)
   var green=rgb.g>rgb.r*.99 and rgb.g>rgb.b*1.12
   var cover=t.sample_surface(t.surface_mask,p) if t.surface_mask else Color(0,1 if green else 0,0,.5)
   if cover.r>.08 or cover.a>.6 or t.surface_grip(p)>.82:continue
   var forest=cover.b>.53
   if cover.g<.16 and not forest:continue
   if t.occupied(p):continue
   var loc=Vector3(pos.x,t.height_at(p),pos.y)
   var winter=t.info.get("region","")=="leaf-off"
   var angle=(rng%360)*PI/180.
   var scale=.58+(rng%31)*.027
   if forest and rng%9==0:
    if not winter or green:
     var size=(2.2+(rng%37)*.09)
     shrubs.append([Transform3D(Basis(Vector3.UP,angle)*Basis.from_scale(Vector3(size*1.7,size,size*1.7)),loc)])
   elif not forest or (not winter and rng%3==0):
    var color=(Color("#97896a") if winter else Color("#899150")).lerp(rgb,.28)
    var blade_height=scale
    if not t.capture.record.is_empty() and not forest:
     # The photographed station verges are mown. At road distance their
     # subpixel blades blend into the measured ground cover.
     var viewing_distance=pos.distance_to(game.pos)
     if viewing_distance>18.:continue
     color=t.capture.turf_color.lightened(.17).lerp(color,.20)
     blade_height*=.31*(1.-smoothstep(7.,18.,viewing_distance))
    blades.append([Transform3D(Basis(Vector3.UP,angle)*Basis.from_scale(Vector3(scale,blade_height,scale)),loc),color.srgb_to_linear()])
 measured_plants(game,shrubs)
 write(grass,blades);write(scrub,shrubs)

# Low vegetation inferred from independent classified airborne and multispectral
# observations. Blank cells, skipped regions and mapped interruptions stay blank.
var measured_tile_cache := {}

func measured_plants(game, rows: Array) -> void:
 if not scrub or not scrub.mesh:return
 var bounds := scrub.mesh.get_aabb()
 var half_width := .5*sqrt(bounds.size.x*bounds.size.x+bounds.size.z*bounds.size.z)
 var mesh_origin := bounds.get_center()
 mesh_origin.y=bounds.position.y
 for id in measured_tile_cache.keys():
  if not game.tiles.has(id):measured_tile_cache.erase(id)
 for id in game.tiles:
  var tile=game.tiles[id]
  var offset: Vector2=game.tile_offset(id)
  var nearest: Vector2 = game.pos.clamp(offset,offset+Vector2.ONE*tile.side)
  if nearest.distance_squared_to(game.pos)>65.*65.:continue
  if not measured_tile_cache.has(id):
   var path="res://data/low-vegetation-%d-%d.json"%[id.x,id.y]
   measured_tile_cache[id]=[]
   if not FileAccess.file_exists(path):continue
   var document=JSON.parse_string(FileAccess.get_file_as_string(path))
   if document is Dictionary and int(document.get("version",0)) == 1:
    var value: Array=document.get("tile",[])
    if value.size()==2 and Vector2i(int(value[0]),int(value[1]))==id:
     measured_tile_cache[id]=document.get("cells",[])
  for row in measured_tile_cache.get(id,[]):
   var p=Vector2(row[0],row[1])*tile.side
   var pos=offset+p
   if pos.distance_squared_to(game.pos)>65.*65. or tile.occupied(p):continue
   var yaw=(hash(Vector2i(roundi(row[0]*1e5),roundi(row[1]*1e5)))%360)*PI/180.
   var basis=Basis(Vector3.UP,yaw)*Basis.from_scale(Vector3(row[3]/half_width,row[2]/bounds.size.y,row[3]/half_width))
   var loc=Vector3(pos.x,tile.height_at(p),pos.y)-basis*mesh_origin
   rows.append([Transform3D(basis,loc)])
