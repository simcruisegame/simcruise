extends Node3D
# Display-only terrain in the same east / south frame as the driving surface.
const RADIUS_M := 45000.0
const EMPTY := Vector2i(-1,-1)
const SHADER = preload("res://RegionalTerrain.gdshader")
var ridge=preload("res://RidgeView.gd").new()
var summit=preload("res://SummitSnow.gd").new()
var snow=preload("res://RegionalSnow.gd").new()
var worker: Thread
var requested := EMPTY
var displayed := EMPTY
var failed := EMPTY
var retry_after := 0
var next_poll := 0
var instance := MeshInstance3D.new()
var finish: ShaderMaterial
var last_result := {}
var enabled := true
var last_sun := Vector3.ZERO
var last_haze_m := 0.0

func _ready():
 finish = ShaderMaterial.new()
 finish.shader = SHADER
 instance.material_override = finish
 instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
 instance.gi_mode = GeometryInstance3D.GI_MODE_DISABLED
 add_child(instance)

func read_region(helper: String, directory: String, region: Vector2i) -> Dictionary:
 var lines := []
 if OS.execute(helper, ["landscape", directory, str(region.x), str(region.y)], lines, true) != 0 or lines.is_empty():
  return {}
 var data = JSON.parse_string(lines[0])
 if not data is Dictionary or data.get("status") != "available":
  return {"requests":int(data.get("requests",0))} if data is Dictionary else {}
 var coords=data.get("region",[])
 if coords.size()!=2 or coords[0]!=region.x or coords[1]!=region.y:
  return {}
 var expected := "relief-tin-v4-%d-%d.bin" % [region.x,region.y]
 if data.get("grid") != expected or data.get("radius") != RADIUS_M: return {}
 var file := FileAccess.open(directory.path_join(expected),FileAccess.READ)
 if not file or file.get_length()<8 or file.get_length()>5000000: return {}
 var vertex_count := file.get_32()
 var index_count := file.get_32()
 if vertex_count<3 or vertex_count>250000 or index_count<3 or index_count>440000 or index_count%3!=0: return {}
 if file.get_length()!=8+vertex_count*12+index_count*4: return {}
 var positions := file.get_buffer(vertex_count*12).to_vector3_array()
 var indices := file.get_buffer(index_count*4).to_int32_array()
 for index in indices:
  if index<0 or index>=vertex_count:return {}
 var image_file := "relief-aerial-v1-%d-%d.jpg" % [region.x,region.y]
 if data.get("aerial","") == image_file:
  var photo := Image.load_from_file(directory.path_join(image_file))
  if photo and photo.get_size() == Vector2i(512,512):
   photo.generate_mipmaps()
   data["photo"] = photo
 var middle_name := "relief-middle-v1-%d-%d.jpg" % [region.x,region.y]
 if data.get("middle","")==middle_name:
  var img:=Image.load_from_file(directory.path_join(middle_name))
  if img and img.get_size()==Vector2i(1536,1536):
   img.generate_mipmaps()
   data["middle_photo"]=img
 var coast_name:="relief-ocean-v1-%d-%d.png"%[region.x,region.y]
 if data.get("seaMask","")==coast_name:
  var sea:=Image.load_from_file(directory.path_join(coast_name))
  if sea and sea.get_size()==Vector2i(513,513):
   sea.convert(Image.FORMAT_L8)
   sea.generate_mipmaps()
   data["sea_mask"]=sea
 var mask_name:="relief-road-v1-%d-%d.png" % [region.x,region.y]
 if data.get("roadMask","")==mask_name:
  var mask:=Image.load_from_file(directory.path_join(mask_name))
  if mask and mask.get_size()==Vector2i(2048,2048):
   mask.convert(Image.FORMAT_L8)
   mask.generate_mipmaps()
   data["road_mask"]=mask
 var images:Array[Image]=[]
 var regions:=PackedVector4Array()
 for item in data.get("photos",[]):
  if images.size()>=4 or not item is Dictionary:break
  var name=str(item.get("name",""))
  var uv=item.get("uv")
  if name.get_file()!=name or not name.begins_with("relief-detail-v1-") or not name.ends_with(".jpg") or not uv is Array or uv.size()!=4:continue
  if not (uv[2]>0.0 and uv[3]>0.0 and uv[2]<2.0 and uv[3]<2.0):continue
  var img:=Image.load_from_file(directory.path_join(name))
  if not img or img.get_size()!=Vector2i(1024,1024):continue
  img.convert(Image.FORMAT_RGB8)
  img.generate_mipmaps()
  images.append(img)
  regions.append(Vector4(uv[0],uv[1],uv[2],uv[3]))
 data["photos"]=images
 data["photo_uv"]=regions
 var arrays := []
 arrays.resize(Mesh.ARRAY_MAX)
 arrays[Mesh.ARRAY_VERTEX] = positions
 arrays[Mesh.ARRAY_INDEX] = indices
 data["arrays"] = arrays
 return data

func tick(game):
 if not enabled or not is_instance_valid(game.vehicle): return
 var id:Vector2i = game.tile_id(game.pos)
 var here := Vector2i(floori(id.x/8.0),floori(id.y/8.0))
 var now := Time.get_ticks_msec()
 snow.tick(game,displayed,finish)
 ridge.tick(game,self)
 summit.tick(game,self)
 if displayed != EMPTY:
  var light_direction:Vector3 = game.sun.global_basis.z.normalized()
  if not light_direction.is_equal_approx(last_sun):
   last_sun = light_direction
   finish.set_shader_parameter("sun_from", light_direction)
  # U.S. METAR reports stop at 10 statute miles on otherwise clear days.
  var reported := float(game.observation.get("visibility_m",16093))
  var haze_m := maxf(100.,reported / 3.912) if game.live_weather and reported >= 0. and reported < 16090. else 55000.
  if haze_m != last_haze_m:
   last_haze_m = haze_m
   finish.set_shader_parameter("visibility_m", haze_m)
  var centre:Vector2 = game.tile_offset(displayed*8+Vector2i(4,4))
  var desired := Vector3(centre.x,0,centre.y)
  if instance.position!=desired:instance.position=desired
  var show_mesh: bool = centre.distance_to(game.pos) < 12000.0 and game.jump.is_empty()
  if instance.visible != show_mesh:instance.visible = show_mesh
 if worker and not worker.is_alive():
  var data = worker.wait_to_finish()
  worker = null
  game.calls += int(data.get("requests",0))
  if requested == here and data.has("arrays"):
   var mesh := ArrayMesh.new()
   mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,data["arrays"])
   instance.mesh = mesh
   var centre:Vector2 = game.tile_offset(requested*8+Vector2i(4,4))
   instance.position = Vector3(centre.x,0,centre.y)
   instance.visible = true
   if data.has("photo"):
    finish.set_shader_parameter("photo",ImageTexture.create_from_image(data.photo))
   finish.set_shader_parameter("has_photo",data.has("photo"))
   finish.set_shader_parameter("has_middle",data.has("middle_photo"))
   if data.has("middle_photo"):finish.set_shader_parameter("middle_photo",ImageTexture.create_from_image(data.middle_photo))
   finish.set_shader_parameter("has_ocean",data.has("sea_mask"))
   if data.has("sea_mask"):finish.set_shader_parameter("sea_mask",ImageTexture.create_from_image(data.sea_mask))
   finish.set_shader_parameter("has_roads",data.has("road_mask"))
   if data.has("road_mask"):finish.set_shader_parameter("mapped_roads",ImageTexture.create_from_image(data.road_mask))
   var images:Array[Image]=data.photos
   var regions:PackedVector4Array=data.photo_uv
   finish.set_shader_parameter("detail_count",0)
   if images.size()>0:
    var texture:=Texture2DArray.new()
    if texture.create_from_images(images)==OK:
     regions.resize(4)
     finish.set_shader_parameter("detail_colors",texture)
     finish.set_shader_parameter("detail_uv",regions)
     finish.set_shader_parameter("detail_count",images.size())
   finish.set_shader_parameter("has_snow",false)
   ridge.cells.clear()
   finish.set_shader_parameter("has_fine_0",false)
   finish.set_shader_parameter("has_fine_1",false)
   finish.set_shader_parameter("ridge_count",0)
   displayed = requested
   failed = EMPTY
   last_result = {"region":displayed,"coarse":data.coarse,"detail":data.detail,"aerial":data.has("photo"),"requests":data.requests,"triangles":int(data.indices)/3,"tolerance_m":data.maxError,"detail_images":data.photos.size(),"middle":data.has("middle_photo"),"road_segments":data.roadSegments}
  elif requested == here:
   failed=requested; retry_after=now+300000
 if not game.ready_drive or game.hud.modal.visible or game.hud.atlas_map.visible or not game.jump.is_empty() or displayed == here or worker or (failed==here and now<retry_after):return
 if now<next_poll: return
 next_poll=now+900
 if game.tiles.size()<4: return
 requested=here
 worker=Thread.new()
 if worker.start(read_region.bind(game.helper,game.cache,here),Thread.PRIORITY_LOW)!=OK:
  worker=null
  failed=here;retry_after=now+10000

func shutdown():
 snow.shutdown()
 ridge.shutdown()
 summit.shutdown()
 if worker:
  worker.wait_to_finish()
  worker=null
