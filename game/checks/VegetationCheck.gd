extends SceneTree
# Headless regression check for the vegetation and ground-surface materials.
#   godot --headless --path game --script res://checks/VegetationCheck.gd
# The dummy renderer still parses every shader (servers/rendering/dummy/
# storage/material_storage.cpp shader_set_code), so a shader that fails to
# compile prints SHADER ERROR and exposes no uniforms. The check lists each
# shader's uniforms, the plant and crown geometry budgets and the procedural
# asphalt normal map, then quits with 0 (pass) or 1 (fail). Nodes added in
# _initialize only become ready once the root enters the tree, so everything
# runs on the first process frame and prints once.
const SHADERS={
 "res://Canopy.gdshader":["foliage","transmission","inner_occlusion","crown_centre","crown_radii","albedo_gain","fade_start","fade_end"],
 "res://Bark.gdshader":["bark"],
 "res://FieldTerrain.gdshader":["photo","pavement","asphalt_wear","asphalt_normal","has_asphalt_normal","grass_detail","verge_mown_m"],
 "res://terrain.gdshader":["surface_map","ortho","road_edges","asphalt_wear","asphalt_normal","has_asphalt_normal","grass_detail"],
 "res://Rangeland.gdshader":["clump","sway","wind","fade_start","fade_end"],
}
# Vertex budgets: the former tuft plant used 1284 (near) and 426 (far).
const PLANT_VERTEX_LIMIT={"near":48,"far":16}
var frames=0
var failures=[]
var grove

func fail(reason:String):
 failures.append(reason)
 print("VEGETATION FAIL ",reason)

func shader_uniforms():
 for path in SHADERS:
  var shader=load(path)
  if shader==null:fail("cannot load "+path);continue
  var names=[]
  for p in RenderingServer.get_shader_parameter_list(shader.get_rid()):names.append(str(p.name))
  names.sort()
  print("UNIFORMS ",path.get_file()," count=",names.size()," ",", ".join(names))
  if names.is_empty():fail(path.get_file()+" exposes no uniforms (did it fail to compile?)")
  for expected in SHADERS[path]:
   if expected not in names:fail(path.get_file()+" lacks uniform "+expected)

func vertex_count(mesh:Mesh)->int:
 var total=0
 for s in mesh.get_surface_count():total+=mesh.surface_get_arrays(s)[Mesh.ARRAY_VERTEX].size()
 return total

func plants():
 var rangeland=load("res://Rangeland.gd").new()
 var started=Time.get_ticks_usec()
 var near=rangeland.plant_mesh(true);var far=rangeland.plant_mesh(false)
 print("PLANT build_ms=",snappedf((Time.get_ticks_usec()-started)/1000.,.1)," (clump texture plus both meshes, once per run)")
 var texture:ImageTexture=rangeland.clump_texture
 var image=texture.get_image()
 var opaque=0;var samples=0
 for y in range(0,image.get_height(),2):
  for x in range(0,image.get_width(),2):
   samples+=1
   if image.get_pixel(x,y).a>.42:opaque+=1
 print("PLANT near_vertices=",vertex_count(near)," far_vertices=",vertex_count(far)," clump_texture=",image.get_width(),"x",image.get_height()," mips=",image.get_mipmap_count()," opaque_fraction=",snappedf(float(opaque)/samples,.001)," material=",near.surface_get_material(0).shader.resource_path.get_file())
 if vertex_count(near)>PLANT_VERTEX_LIMIT.near:fail("near plant exceeds %d vertices"%PLANT_VERTEX_LIMIT.near)
 if vertex_count(far)>PLANT_VERTEX_LIMIT.far:fail("far plant exceeds %d vertices"%PLANT_VERTEX_LIMIT.far)
 if float(opaque)/samples<.15 or float(opaque)/samples>.75:fail("clump silhouette covers an implausible fraction of its card")
 rangeland.free()

# Minimal stand-ins for WorldTile and Drive so the placement code runs without
# streaming: one 7 m road along y = 240 m across a 480 m tile.
class StubTile:
 var side:=480.
 var shrub_aerial:Image
 var road_grid:={}
 var learned_edges=null
 func _init():
  shrub_aerial=Image.create_empty(256,256,false,Image.FORMAT_RGB8)
  for y in 256:
   for x in 256:
    shrub_aerial.set_pixel(x,y,Color(.30+.08*sin(x*.3),.36+.10*cos(y*.21),.18))
  var segment=[Vector2(0,240),Vector2(480,240),"Test Road",7.,false,"secondary"]
  for cell_x in 20:road_grid[Vector2i(cell_x,10)]=[segment]
 func surface_grip(p:Vector2)->float:return 1. if absf(p.y-240.)<3.5 else .5
 func occupied(_p:Vector2)->bool:return false
 func height_at(_p:Vector2)->float:return 0.

class StubGame:
 var tiles:={}
 func tile_offset(_id)->Vector2:return Vector2.ZERO

func plants_in_field():
 var rangeland=load("res://Rangeland.gd").new()
 var tile=StubTile.new()
 var mask=Image.create_empty(256,256,false,Image.FORMAT_RGB8)
 mask.fill(Color8(40,60,0))
 var items=rangeland.observed_patch(tile,0,8,mask,Vector2i.ZERO)
 # Within 3 m of the asphalt edge plants are cut to 10-25 cm; beyond 8 m they
 # keep the 0.6 m survey height times 0.74-1.25; between, heights blend.
 var mown=0;var tall=0;var bad=0
 for plant in items:
  var height:float=plant[0].basis.y.length()
  if plant[2].g>.95:
   mown+=1
   if height>.26:bad+=1
  elif plant[2].g<.05:
   tall+=1
   if height<.40:bad+=1
 print("FIELD patch_plants=",items.size()," mown=",mown," tall=",tall," blended=",items.size()-mown-tall," height_violations=",bad," first_tint=",items[0][1] if items.size() else null)
 if items.size()<8:fail("synthetic stand placed too few plants")
 if mown==0 or tall==0:fail("verge mowing did not split the stand into cut and uncut plants")
 if bad>0:fail("mown plants taller than 26 cm or natural plants shorter than 40 cm")
 var game=StubGame.new();game.tiles[Vector2i.ZERO]=tile
 rangeland.observations[Vector2i.ZERO]=mask
 rangeland.refresh_instances(game,Vector2(15.,255.))
 var near:int=rangeland.mesh_instances.instance_count;var far:int=rangeland.distant_instances.instance_count
 var stride_ok=rangeland.mesh_instances.buffer.size()==near*20 and rangeland.distant_instances.buffer.size()==far*20
 print("FIELD instances near=",near," far=",far," draws=",rangeland.draws," buffer_stride_ok=",stride_ok," patches=",rangeland.patches.size())
 if near+far==0:fail("refresh placed no instances around the synthetic stand")
 if not stride_ok:fail("multimesh buffers do not match the 20-float instance stride")
 rangeland.free()

func crowns():
 var names=["oak","leaf","pine","winter"]
 var total=0
 for i in grove.rings.size():
  var mesh:Mesh=grove.rings[i].mesh
  total+=vertex_count(mesh)
 print("CROWNS rings=",grove.rings.size()," total_vertices=",total)
 for kind in ["leaf","pine"]:
  var material:ShaderMaterial=grove.crowns[kind]
  var centre=material.get_shader_parameter("crown_centre");var radii=material.get_shader_parameter("crown_radii")
  print("CROWN_FIT ",kind," centre=",centre," radii=",radii," foliage=",material.get_shader_parameter("foliage")!=null)
  if centre==null or radii==null:fail(kind+" crown material has no fitted extents");continue
  if radii.x<.3 or radii.x>1. or radii.y<.2 or radii.y>.8 or centre.y<.3 or centre.y>.9:fail(kind+" crown fit outside the expected unit-mesh range")
 var draws=0
 for child in grove.get_children():
  if child is MultiMeshInstance3D:draws+=1
 print("CROWNS multimesh_nodes=",draws," (one draw call per populated ring; unchanged by the shader work)")
 for i in [0,3,6,9,12]:print("RING ",i," ",names[i/3] if i<12 else "understory"," vertices=",vertex_count(grove.rings[i].mesh))

func ground():
 var finish=load("res://GroundFinish.gd")
 var started=Time.get_ticks_usec()
 var texture:ImageTexture=finish.asphalt_normal_texture()
 var elapsed=(Time.get_ticks_usec()-started)/1000.
 var image=texture.get_image()
 var sum=Vector3.ZERO;var n=0;var min_z=1.
 for y in range(0,image.get_height(),4):
  for x in range(0,image.get_width(),4):
   var c=image.get_pixel(x,y);sum+=Vector3(c.r,c.g,c.b);n+=1;min_z=minf(min_z,c.b)
 var mean=sum/n
 print("ASPHALT_NORMAL size=",image.get_width(),"x",image.get_height()," mips=",image.get_mipmap_count()," mean=",mean," min_z=",snappedf(min_z,.001)," generate_ms=",snappedf(elapsed,.1)," cached_again=",finish.asphalt_normal_texture()==texture)
 if image.get_width()!=256 or image.get_mipmap_count()<6:fail("asphalt normal map has the wrong size or no mipmaps")
 if absf(mean.x-.5)>.06 or absf(mean.y-.5)>.06 or mean.z<.80:fail("asphalt normal map is not centred on the flat normal")
 for path in ["res://FieldTerrain.gdshader","res://terrain.gdshader"]:
  var material=ShaderMaterial.new();material.shader=load(path)
  finish.bind(material,{},[19827,24228],611.0)
  var bound=material.get_shader_parameter("asphalt_normal")
  var origin=material.get_shader_parameter("detail_origin")
  print("BIND ",path.get_file()," asphalt_normal=",bound is Texture2D," has_asphalt_normal=",material.get_shader_parameter("has_asphalt_normal")," asphalt_wear=",material.get_shader_parameter("asphalt_wear") is Texture2D," detail_origin=",origin)
  if not bound is Texture2D or material.get_shader_parameter("has_asphalt_normal")!=true:fail(path.get_file()+" did not receive the asphalt normal map")

func _initialize():
 grove=load("res://Grove.gd").new()
 root.add_child(grove)

func _process(_dt):
 frames+=1
 if frames>3:fail("check did not finish");quit(1);return false
 if frames!=1:return false
 shader_uniforms()
 crowns()
 plants()
 plants_in_field()
 ground()
 if failures.is_empty():print("VEGETATION PASS")
 else:print("VEGETATION FAILED ",failures.size()," check(s)")
 grove.queue_free()
 quit(0 if failures.is_empty() else 1)
 return false
