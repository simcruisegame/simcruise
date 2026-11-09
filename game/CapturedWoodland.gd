extends RefCounted

# Uncolored surfaces outside the observed geometry retain the game's regional woodland.
static func build(tile) -> void:
 for frame in [2060,2061]:
  var mesh_path := "res://data/scan-%d.meshdata" % frame
  var tex_path := "res://data/scan-%d.png" % frame
  var gltf := GLTFDocument.new()
  var state := GLTFState.new()
  if gltf.append_from_buffer(FileAccess.get_file_as_bytes(mesh_path),"",state) != OK: continue
  var scene := gltf.generate_scene(state)
  var holder = scene if scene is MeshInstance3D else scene.find_children("*","MeshInstance3D",true,false)[0]
  var photo := Image.new()
  if photo.load_png_from_buffer(FileAccess.get_file_as_bytes(tex_path)) != OK:
   scene.free()
   continue
  photo.fix_alpha_edges()
  photo.generate_mipmaps()
  var material := StandardMaterial3D.new()
  material.albedo_texture = ImageTexture.create_from_image(photo)
  material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
  material.cull_mode = BaseMaterial3D.CULL_BACK
  material.roughness = 1.
  # The camera already recorded shadows within the crown. A modest diffuse contribution
  # keeps this surface responsive to the scene light without shading it twice at noon.
  material.emission_enabled = true
  material.emission_texture = material.albedo_texture
  material.emission = Color.BLACK
  material.emission_energy_multiplier = .22
  material.albedo_color = Color(.52,.52,.52)
  holder.mesh.surface_set_material(0,material)
  holder.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
  tile.add_child(scene)
