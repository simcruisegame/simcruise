extends RefCounted

# Optional measured photo surfaces for one tile, read from data/capture-X-Y.json:
#  road         {image, origin, axis, extent}: a registered pavement image (UV frame)
#  photos       {name: file}: photo textures, used as materials "cap_<name>"
#               (fuel canopy fascia "<brand>-short"/"<brand>-long", "dispenser")
#  roof_height  canopy deck height read by Junction.gd
#  turf, rails, curbed_turf: verge colour, observed guard rails, curbed islands
# No capture record ships at the moment, so every tile keeps record empty and
# its ordinary map materials: each entry point below returns early, and callers
# (WorldTile, Junction, SignalControls, Roadside) test record.is_empty().
var record: Dictionary = {}
var mask: Image
var turf_color := Color("#617331")
var origin: Vector2
var axis: Vector2
var extent: Vector2

func vec(pair) -> Vector2:
 return Vector2(pair[0], pair[1])

func load_tile(t) -> void:
 var file := "res://data/capture-%d-%d.json" % [int(t.info.id[0]), int(t.info.id[1])]
 if not FileAccess.file_exists(file): return
 var parsed = JSON.parse_string(FileAccess.get_file_as_string(file))
 if not parsed is Dictionary or not parsed.get("road") is Dictionary: return
 record = parsed
 var turf: Array = record.get("turf", [.36,.43,.19])
 turf_color=Color(turf[0],turf[1],turf[2])
 var photos = record.get("photos", {})
 if not photos is Dictionary: return
 for name in photos:
  var image := load_image("res://data/" + str(photos[name]))
  if image == null: continue
  image.generate_mipmaps()
  var mat := StandardMaterial3D.new()
  mat.albedo_texture = ImageTexture.create_from_image(image)
  mat.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
  mat.cull_mode = BaseMaterial3D.CULL_DISABLED
  mat.roughness = 0.88
  t.materials["cap_" + str(name)] = mat

# A bundled JPEG or PNG, or null when it is missing or unreadable.
func load_image(path: String) -> Image:
 if not FileAccess.file_exists(path): return null
 var image := Image.new()
 var bytes := FileAccess.get_file_as_bytes(path)
 var ok := image.load_png_from_buffer(bytes) if path.get_extension().to_lower() == "png" else image.load_jpg_from_buffer(bytes)
 return image if ok == OK else null

func bind_road(t, material: ShaderMaterial) -> void:
 if record.is_empty(): return
 var road: Dictionary = record.road
 var image := load_image("res://data/" + str(road.get("image","")))
 if image == null: return
 origin = vec(road.origin)
 axis = vec(road.axis)
 extent = vec(road.extent)
 # CPU surface classification also follows the measured pavement.
 mask = image.duplicate()
 mask.resize(maxi(1, image.get_width() / 4), maxi(1, image.get_height() / 4), Image.INTERPOLATE_BILINEAR)
 image.generate_mipmaps()
 material.set_shader_parameter("capture_road", ImageTexture.create_from_image(image))
 material.set_shader_parameter("captured_turf", turf_color)
 material.set_shader_parameter("capture_origin", origin)
 material.set_shader_parameter("capture_axis", axis)
 material.set_shader_parameter("capture_extent", extent)
 material.set_shader_parameter("has_capture_road", true)

func road_confidence(uv: Vector2) -> float:
 if mask == null: return 0.
 var p := uv - origin
 var tex := Vector2(p.dot(axis), p.dot(Vector2(-axis.y, axis.x))) / extent
 if tex.x < 0 or tex.y < 0 or tex.x >= 1 or tex.y >= 1: return 0.
 return mask.get_pixel(int(tex.x * mask.get_width()), int(tex.y * mask.get_height())).a

func face(t, material, bottom_left, bottom_right, top_right, top_left, normal):
 t.quad(material, bottom_right, bottom_left, top_left, top_right, normal)

# Photographed fascia bands on a mapped fuel canopy (Junction.gd), when the record has them.
func fascia(t, f, y):
 var brand := str(f.brand).to_lower()
 if not t.materials.has("cap_" + brand + "-short"): return
 var a = t.point(f.ring[0]); var b = t.point(f.ring[3]); var c = t.point(f.ring[2])
 var center = (a+c)*.5
 for band in [[a,b,"-short"],[b,c,"-long"]]:
  var left: Vector2 = band[0]; var right: Vector2 = band[1]
  var normal := (right-left).orthogonal().normalized()
  if normal.dot((right+left)*.5-center) < 0: normal = -normal
  # Include the slight overhang on the stripe of the stock canopy geometry.
  var dl = left+normal*.085; var dr = right+normal*.085
  face(t,"cap_"+brand+band[2],t.vert(dl,y-.32),t.vert(dr,y-.32),t.vert(dr,y+.315),t.vert(dl,y+.315),Vector3(normal.x,0,normal.y))

# A photographed pump face beside a mapped dispenser (Junction.gd), when the record has one.
func dispenser(t, position_2d, across, floor_y):
 if not t.materials.has("cap_dispenser"): return
 var front: Vector2 = across if across.y>0 else -across
 var along := Vector2(front.y,-front.x)
 var p: Vector2 = position_2d+front*.36
 var left: Vector2 = p+along*.52; var right: Vector2 = p-along*.52
 face(t,"cap_dispenser",t.vert(left,floor_y+.23),t.vert(right,floor_y+.23),t.vert(right,floor_y+1.80),t.vert(left,floor_y+1.80),Vector3(front.x,0,front.y))

func build_fixtures(t):
 if record.is_empty(): return
 for key in ["rail_zinc","rail_post"]:
  var mat:=StandardMaterial3D.new()
  mat.albedo_color=Color("#92988e") if key=="rail_zinc" else Color("#665e50")
  mat.metallic=.54 if key=="rail_zinc" else .15
  mat.roughness=.55 if key=="rail_zinc" else .84
  mat.cull_mode=BaseMaterial3D.CULL_DISABLED
  t.materials[key]=mat
 for path in record.get("rails",[]):observed_barrier(t,path)
 for path in record.get("curbed_turf",[]):raised_island(t,path)

# Buildings keep the ordinary map model; a capture record has no bespoke buildings.
func store(_t,_b,_idx) -> bool:
 return false

func observed_barrier(t,points):
 # The W profile, end cap and post spacing give the rail depth at road height.
 var path=PackedVector2Array()
 for uv in points:path.append(t.point(uv))
 var profile=[Vector2(.015,.49),Vector2(-.062,.54),Vector2(-.082,.585),Vector2(-.016,.642),Vector2(-.076,.70),Vector2(-.064,.746),Vector2(.011,.79)]
 var distance=0.
 for i in range(1,path.size()):
  var a=path[i-1];var b=path[i];var along=(b-a).normalized();var across=along.orthogonal();var length=a.distance_to(b)
  var divisions=maxi(1,ceili(length/3.5))
  for j in divisions:
   var start=a.lerp(b,float(j)/divisions);var end=a.lerp(b,float(j+1)/divisions)
   var y1=t.height_at(start);var y2=t.height_at(end)
   for k in range(1,profile.size()):
    var lo=profile[k-1];var hi=profile[k]
    var v1=t.vert(start+across*lo.x,y1+lo.y);var v2=t.vert(end+across*lo.x,y2+lo.y)
    var v3=t.vert(end+across*hi.x,y2+hi.y);var v4=t.vert(start+across*hi.x,y1+hi.y)
    var normal=(v3-v1).cross(v2-v1).normalized()
    if Vector2(normal.x,normal.z).dot(across)>0:normal=-normal
    t.quad("rail_zinc",v1,v2,v3,v4,normal)
   var base1=t.vert(start,y1+.40);var base2=t.vert(end,y2+.40)
   t.collision_faces.append_array(PackedVector3Array([base1,base2+Vector3.UP*.41,base2,base1,base1+Vector3.UP*.41,base2+Vector3.UP*.41]))
  var next=fposmod(-distance,1.92)
  while next<length:
   var post_point=a+along*next+across*.116
   t.JUNCTION.box(t,t.vert(post_point,t.height_at(post_point)+.352),along,Vector3(.116,.70,.15),"rail_post")
   next+=1.92
  distance+=length

func raised_island(t,points):
 var poly=PackedVector2Array()
 for uv in points:poly.append(t.point(uv))
 var center=Vector2.ZERO
 for p in poly:center+=p/poly.size()
 # Follow the measured ground slope. Keep reference UVs on the turf top so
 # grass and road materials agree at the edge of the adjoining terrain.
 t.materials["island_turf"]=t.terrain_material
 var cap=t.tool("island_turf");var top=func(p):return t.vert(p,t.height_at(p)+.126)
 var ix=Geometry2D.triangulate_polygon(poly)
 for i in range(0,ix.size(),3):
  var a=poly[ix[i]];var b=poly[ix[i+1]];var c=poly[ix[i+2]]
  t.tri(cap,top.call(a),top.call(b),top.call(c),a/t.side,b/t.side,c/t.side,Vector3.UP)
  t.collision_faces.append_array(PackedVector3Array([top.call(a),top.call(b),top.call(c)]))
 for i in poly.size():
  var a=poly[i];var b=poly[(i+1)%poly.size()]
  var outward=(b-a).orthogonal().normalized()
  if outward.dot((a+b)*.5-center)<0:outward=-outward
  var n=Vector3(outward.x,0,outward.y)
  var at=top.call(a);var bt=top.call(b)
  var edge=Vector3(outward.x,0,outward.y)*.13
  var ao=t.vert(a,t.height_at(a)+.012)+edge*1.15
  var bo=t.vert(b,t.height_at(b)+.012)+edge*1.15
  t.quad("curb",at+edge,bt+edge,bt,at,Vector3.UP)
  t.quad("curb",ao,bo,bt+edge,at+edge,n)
  t.collision_faces.append_array(PackedVector3Array([ao,bo,bt+edge,ao,bt+edge,at+edge,at+edge,bt+edge,bt,at+edge,bt,at]))
