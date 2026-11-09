extends RefCounted
## Two independent camera stations and USGS flight lines constrain this solid.
## UVs project screened exposed rock from each station. Unseen rock uses local grain.
const FILE:="res://data/needles-granite"
const SHADER=preload("res://MeasuredGranite.gdshader")

static func photograph(suffix:String):
 var image=Image.new()
 if image.load_webp_from_buffer(FileAccess.get_file_as_bytes(FILE+suffix+".meshdata"))!=OK:return null
 image.generate_mipmaps()
 return ImageTexture.create_from_image(image)

static func attach(tile)->void:
 var id=tile.info.id
 if int(id[0])!=13919 or int(id[1])!=23870:return
 var f=FileAccess.open(FILE+".meshdata",FileAccess.READ)
 if not f:return
 var version=f.get_32()
 if version not in [3,4]:return
 var origin=Vector2(f.get_float(),f.get_float())*tile.side
 var source_side=f.get_float()
 var count=f.get_32()
 var indices=f.get_32()
 if source_side<=0. or count<=0 or count>20000 or indices<=0 or indices>120000 or indices%3!=0 or f.get_length()!=24+44*count+indices*4+(4 if version==4 else 0):return
 var xyz=f.get_buffer(count*12).to_float32_array()
 var normal=f.get_buffer(count*12).to_float32_array()
 var a=f.get_buffer(count*8).to_float32_array()
 var b=f.get_buffer(count*8).to_float32_array()
 var photo=f.get_buffer(count*4)
 var index=f.get_buffer(4*indices).to_int32_array()
 var vertices:=PackedVector3Array()
 var normals:=PackedVector3Array()
 var uv_a:=PackedVector2Array()
 var uv_b:=PackedVector2Array()
 var colors:=PackedColorArray()
 vertices.resize(count);normals.resize(count);uv_a.resize(count);uv_b.resize(count);colors.resize(count)
 var ground=tile.base_height(origin)
 if version==4 and tile.surveyed_outcrop:
  ground=f.get_float()+tile.surveyed_outcrop.vertical_offset
 var scale=tile.side/source_side
 for i in count:
  vertices[i]=Vector3(origin.x+xyz[i*3]*scale,ground+xyz[i*3+1],origin.y+xyz[i*3+2]*scale)
  normals[i]=Vector3(normal[i*3],normal[i*3+1],normal[i*3+2])
  uv_a[i]=Vector2(a[i*2],a[i*2+1]);uv_b[i]=Vector2(b[i*2],b[i*2+1])
  colors[i]=Color(photo[i*4]/255.,photo[i*4+1]/255.,0,1)
 for value in index:
  if value<0 or value>=count:return
 var arrays=[]
 arrays.resize(Mesh.ARRAY_MAX)
 arrays[Mesh.ARRAY_VERTEX]=vertices;arrays[Mesh.ARRAY_NORMAL]=normals;arrays[Mesh.ARRAY_INDEX]=index
 arrays[Mesh.ARRAY_TEX_UV]=uv_a;arrays[Mesh.ARRAY_TEX_UV2]=uv_b;arrays[Mesh.ARRAY_COLOR]=colors
 var left=photograph("-a");var right=photograph("-b")
 var grain=Image.new()
 if not left or not right or grain.load_png_from_buffer(FileAccess.get_file_as_bytes(FILE+"-detail.png"))!=OK:return
 grain.generate_mipmaps()
 var mat=ShaderMaterial.new();mat.shader=SHADER
 mat.set_shader_parameter("stone_a",left);mat.set_shader_parameter("stone_b",right);mat.set_shader_parameter("grain",ImageTexture.create_from_image(grain))
 var mesh=ArrayMesh.new()
 mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,arrays);mesh.surface_set_material(0,mat)
 tile.mesh_node(mesh);tile.collider(mesh.create_trimesh_shape())
 tile.info["measured_granite_faces"]=indices/3
