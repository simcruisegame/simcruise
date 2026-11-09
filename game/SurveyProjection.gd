extends RefCounted
## Apply photographic color only to a previously registered survey mesh.
const SHADER = preload("res://survey-projection.gdshader")

static func vector(value: Array) -> Vector3:
 return Vector3(value[0], value[1], value[2])

static func texture_from_data(filename: String, size: Vector2i, mipmaps: bool) -> ImageTexture:
 if filename != filename.get_file() or filename.get_extension() != "png":
  return null
 var path := "res://data/" + filename
 if not FileAccess.file_exists(path):
  return null
 var img := Image.new()
 if img.load_png_from_buffer(FileAccess.get_file_as_bytes(path)) != OK or img.get_size() != size:
  return null
 if mipmaps:
  img.generate_mipmaps()
 return ImageTexture.create_from_image(img)

static func material(tile, profile: Dictionary, base: float, kind: String, fallback) -> String:
 var data: Dictionary = profile.get("projection", {})
 if data.is_empty():
  return fallback
 var identifier := str(data.file).get_basename()
 var key := "survey_projection_%s_%s_%s" % [identifier, kind, fallback]
 if tile.materials.has(key):
  return key
 var channels = data.get("wall_srgb", []) if kind == "wall" else profile.get("color_observations", {}).get(kind, [])
 var tint := Color("#8d8981")
 if channels.size() == 3:
  tint = Color8(int(channels[0]), int(channels[1]), int(channels[2]))
 var material_key := "survey_sandstone_" + tint.to_html(false)
 if kind == "wall" and not tile.materials.has(material_key):
  var stone := StandardMaterial3D.new()
  stone.albedo_color = tint
  stone.roughness = .94
  tile.materials[material_key] = stone
 var plain = material_key if kind == "wall" else fallback
 var size := Vector2i(int(data["size"][0]), int(data["size"][1]))
 if size.x <= 0 or size.y <= 0 or size.x > 2048 or size.y > 2048:
  return plain
 var cache_key := "survey_projection_" + identifier.sha256_text()
 if not tile.has_meta(cache_key):
  tile.set_meta(cache_key, [texture_from_data(data.file, size, true), texture_from_data(data.depth, size, false)])
 var maps: Array = tile.get_meta(cache_key)
 if maps[0] == null or maps[1] == null:
  return plain
 var mat := ShaderMaterial.new()
 mat.shader = SHADER
 mat.set_shader_parameter("field", maps[0])
 mat.set_shader_parameter("range_map", maps[1])
 mat.set_shader_parameter("tint", tint)
 mat.set_shader_parameter("image_size", Vector2(size))
 mat.set_shader_parameter("principal", Vector2(data.principal[0], data.principal[1]))
 mat.set_shader_parameter("focal_pixels", float(data.focal))
 mat.set_shader_parameter("optical_center", vector(data.camera) + Vector3.UP * base)
 mat.set_shader_parameter("camera_right", vector(data.right))
 mat.set_shader_parameter("camera_up", vector(data.up))
 mat.set_shader_parameter("camera_forward", vector(data.forward))
 var backing = tile.materials.get(fallback)
 if kind in ["0", "wall"] and backing is StandardMaterial3D:
  if kind == "0":
   mat.set_shader_parameter("tint", backing.albedo_color)
  mat.set_shader_parameter("preserve_wall_value", kind == "wall")
  if backing.albedo_texture:
   mat.set_shader_parameter("fallback_texture", backing.albedo_texture)
   mat.set_shader_parameter("has_fallback_texture", true)
 tile.materials[key] = mat
 return key
