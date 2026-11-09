extends RefCounted

const DIRECTORY = "res://data/"
const RECORDS = {
 Vector2i(16104,24411): "survey-wall-16104-24411.json",
 Vector2i(15788,23579): "survey-wall-15788-23579.json",
}
var loaded = false
var record: Dictionary

func add_if_matched(tile, building, top:float):
 if not loaded:
  loaded = true
  var id = Vector2i(int(tile.info.id[0]),int(tile.info.id[1]))
  if RECORDS.has(id):
   var source = DIRECTORY + str(RECORDS[id])
   if FileAccess.file_exists(source):
    var item = JSON.parse_string(FileAccess.get_file_as_string(source))
    if item is Dictionary and item.get("schema") == 1 and item.get("tile") is Array and item.tile.size() == 2 and Vector2i(int(item.tile[0]),int(item.tile[1])) == id:
     record = item
 if record.is_empty() or building[0].size() != 1 or absf(building[3]) > .15:
  return
 var ring = building[0][0]
 if ring.size() != record.ring.size() or tile.point(record.center).distance_to(tile.point([building[4],building[5]])) > .35:
  return
 # A changed outline, part subdivision, or reversed vertex order invalidates registration.
 for i in ring.size():
  if tile.point(ring[i]).distance_to(tile.point(record.ring[i])) > .3:
   return
 var image = Image.new()
 var path = DIRECTORY + str(record.file)
 if not FileAccess.file_exists(path) or image.load_png_from_buffer(FileAccess.get_file_as_bytes(path)) != OK:
  return
 var base:float = tile.height_at(tile.point(record.ground_uv))
 var lower = base + float(record.z_m[0])
 var upper = base + float(record.z_m[1])
 var clipped = minf(upper,top-.18)
 if clipped <= lower:
  return
 image.generate_mipmaps()
 var material = StandardMaterial3D.new()
 material.albedo_texture = ImageTexture.create_from_image(image)
 material.roughness = 1.
 material.metallic_specular = 0.
 material.cull_mode = BaseMaterial3D.CULL_DISABLED
 material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
 var index = int(record.edge)
 var first:Vector2 = tile.point(ring[index])
 var last:Vector2 = tile.point(ring[(index+1)%ring.size()])
 var normal = Vector2(record.normal[0],record.normal[1])
 var a = first.lerp(last,record.u[0]) + normal*.055
 var b = first.lerp(last,record.u[1]) + normal*.055
 var vertices = [Vector3(a.x,lower,a.y),Vector3(b.x,lower,b.y),Vector3(b.x,clipped,b.y),Vector3(a.x,clipped,a.y)]
 var clipped_v = (upper-clipped)/(upper-lower)
 var uvs = [Vector2(0,1),Vector2(1,1),Vector2(1,clipped_v),Vector2(0,clipped_v)]
 var st = SurfaceTool.new()
 st.begin(Mesh.PRIMITIVE_TRIANGLES)
 st.set_material(material)
 for corner in [0,2,1,0,3,2]:
  st.set_normal(Vector3(normal.x,0,normal.y))
  st.set_uv(uvs[corner])
  st.add_vertex(vertices[corner])
 var patch = MeshInstance3D.new()
 patch.name = "ObservedWallPatch"
 patch.mesh = st.commit()
 patch.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
 tile.add_child(patch)
