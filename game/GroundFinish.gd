extends RefCounted

static var wear_reference: WeakRef
static var normal_reference: WeakRef
static var last_normal_ms: float = 0.0

# Procedural asphalt aggregate normals: rounded stones from inverted cellular
# distance plus fine simplex grain, converted with Image.bump_map_to_normal_map.
# Seamless noise keeps the 0.55 m repeat invisible; 256 px gives 2 mm texels.
# Nothing is downloaded, so no licence note is needed beyond the project MIT.
static func asphalt_normal_texture() -> ImageTexture:
 var cached: ImageTexture = normal_reference.get_ref() if normal_reference else null
 if cached:
  return cached
 var started := Time.get_ticks_usec()
 var size := 256
 var stones := FastNoiseLite.new()
 stones.noise_type = FastNoiseLite.TYPE_CELLULAR
 stones.cellular_return_type = FastNoiseLite.RETURN_DISTANCE
 stones.cellular_distance_function = FastNoiseLite.DISTANCE_EUCLIDEAN
 stones.frequency = 0.09
 stones.seed = 7
 var grain := FastNoiseLite.new()
 grain.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
 grain.frequency = 0.05
 grain.fractal_octaves = 4
 grain.seed = 11
 var cells := stones.get_seamless_image(size, size, false, false, 0.1, true)
 var fine := grain.get_seamless_image(size, size, false, false, 0.1, true)
 var height := Image.create_empty(size, size, false, Image.FORMAT_RF)
 for y in size:
  for x in size:
   var stone := 1.0 - cells.get_pixel(x, y).r
   var relief := clampf(stone * stone * 0.72 + fine.get_pixel(x, y).r * 0.28, 0.0, 1.0)
   height.set_pixel(x, y, Color(relief, 0.0, 0.0))
 height.bump_map_to_normal_map(4.0)
 height.generate_mipmaps()
 var texture := ImageTexture.create_from_image(height)
 normal_reference = weakref(texture)
 last_normal_ms = (Time.get_ticks_usec() - started) / 1000.0
 return texture

static func bind(material: ShaderMaterial, assets: Dictionary, tile_id, width_m: float) -> void:
 var asphalt_wear: ImageTexture = wear_reference.get_ref() if wear_reference else null
 if asphalt_wear == null:
  var image = Image.new()
  if image.load_png_from_buffer(FileAccess.get_file_as_bytes("res://data/asphalt-wear.png")) == OK:
   image.generate_mipmaps()
   asphalt_wear = ImageTexture.create_from_image(image)
   wear_reference = weakref(asphalt_wear)
 if asphalt_wear:
  material.set_shader_parameter("asphalt_wear", asphalt_wear)
 material.set_shader_parameter("asphalt_normal", asphalt_normal_texture())
 material.set_shader_parameter("has_asphalt_normal", true)
 if assets.has("grass_detail"):
  material.set_shader_parameter("grass_detail", assets.grass_detail)
 if assets.has("litter"):
  material.set_shader_parameter("litter", assets.litter)
 # All material wavelengths divide 384 m. Reduce the absolute coordinate in
 # double precision before sending it to the GPU so adjacent tiles join.
 material.set_shader_parameter("detail_origin", Vector2(
  fposmod(float(tile_id[0]) * width_m, 384.0),
  fposmod(float(tile_id[1]) * width_m, 384.0)))
