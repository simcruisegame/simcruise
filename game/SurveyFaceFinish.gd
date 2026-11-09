extends RefCounted

static func palette(tile, profile: Dictionary) -> Dictionary:
 var colors = profile.get("color_observations", {})
 var result := {}
 for code in colors:
  var channels: Array = colors[code]
  if channels.size() != 3: continue
  var color := Color8(clampi(roundi(channels[0]),0,255), clampi(roundi(channels[1]),0,255), clampi(roundi(channels[2]),0,255))
  var key = "measured_face_" + color.to_html(false)
  if not tile.materials.has(key):
   var mat := StandardMaterial3D.new()
   mat.albedo_color = color
   mat.roughness = .93
   mat.metallic_specular = .16
   mat.cull_mode = BaseMaterial3D.CULL_DISABLED
   tile.materials[key] = mat
  result[code]=key
 return result
