extends RefCounted

const DIRECTORY = "res://data/"
const WALL = preload("res://photo-facade.gdshader")
var initialized = false
var record = {}
var masonry_texture: ImageTexture

func find(tile, building, measured = {}) -> Dictionary:
 if not initialized:
  initialized = true
  var id = tile.info.id
  var path = DIRECTORY + "photo-facades-%d-%d.json" % [id[0],id[1]]
  if FileAccess.file_exists(path):
   var parsed = JSON.parse_string(FileAccess.get_file_as_string(path))
   if parsed is Dictionary:
    record = parsed
 if not record.is_empty() and not measured.is_empty() and absf(float(measured.eave_m)-record.eave_above_center)>.55:
  return {}
 if record.is_empty() or building[0].size() != 1 or absf(float(building[3]))>.15:
  return {}
 var ring = building[0][0]
 if ring.size() != record.ring.size() or tile.point(record.center).distance_to(tile.point([building[4],building[5]])) > .4:
  return {}
 # Mapped vertex order is part of a photographed face's identity. Reject revisions
 # that reverse, reorder, move, or split walls until re-registered.
 for i in ring.size():
  if tile.point(ring[i]).distance_to(tile.point(record.ring[i])) > .3:
   return {}
 return record

func draw_edge(tile, photo, index, next, base, top, normal) -> bool:
 for face in photo.faces:
  var left = int(face.a)
  var right = int(face.b)
  if not ((left == index and right == next) or (left == next and right == index)):
   continue
  var key = str(face.file)
  if not tile.materials.has(key):
   var image = Image.new()
   if image.load_png_from_buffer(FileAccess.get_file_as_bytes(DIRECTORY + key)) != OK:
    return false
   image.generate_mipmaps()
   var material = ShaderMaterial.new()
   material.shader = WALL
   material.set_shader_parameter("observed",ImageTexture.create_from_image(image))
   if masonry_texture==null:
    var masonry_image=Image.new()
    if masonry_image.load_png_from_buffer(FileAccess.get_file_as_bytes(DIRECTORY+photo.masonry_file))!=OK:
     return false
    masonry_image.generate_mipmaps()
    masonry_texture=ImageTexture.create_from_image(masonry_image)
   material.set_shader_parameter("masonry",masonry_texture)
   material.set_shader_parameter("face_size",Vector2(face.width_m,top-base-photo.bottom_above_center))
   tile.materials[key] = material
  var a = tile.point(photo.ring[left])
  var b = tile.point(photo.ring[right])
  var lo = base + photo.bottom_above_center
  var st = tile.tool(key)
  tile.tri(st,tile.vert(a,lo),tile.vert(b,lo),tile.vert(b,top),Vector2(0,1),Vector2(1,1),Vector2(1,0),normal)
  tile.tri(st,tile.vert(a,lo),tile.vert(b,top),tile.vert(a,top),Vector2(0,1),Vector2(1,0),Vector2(0,0),normal)
  if face.coping_height_m>0.:
   var cap_key="photographed-aluminum-coping"
   if not tile.materials.has(cap_key):
    var cap=StandardMaterial3D.new()
    cap.albedo_color=Color(.82,.81,.77)
    cap.roughness=.76
    tile.materials[cap_key]=cap
   var outward=Vector2(normal.x,normal.z)*face.coping_depth_m
   var low=top-face.coping_height_m
   var p=a+outward
   var q=b+outward
   tile.quad(cap_key,tile.vert(p,low),tile.vert(q,low),tile.vert(q,top),tile.vert(p,top),normal)
   tile.quad(cap_key,tile.vert(a,top),tile.vert(b,top),tile.vert(q,top),tile.vert(p,top),Vector3.UP)
   tile.quad(cap_key,tile.vert(a,low),tile.vert(p,low),tile.vert(q,low),tile.vert(b,low),Vector3.DOWN)
  return true
 return false

func draw_entrance(tile, photo, base):
 if not photo.has("entrance_roof"):return
 var data=photo.entrance_roof
 var corners=[]
 for a in data.corners:
  corners.append(tile.vert(tile.point(a),base+float(a[2])))
 for component in ["fascia","top","soffit","pier"]:
  var key="entrance-survey-"+component
  var color=data.get(component+"_srgb",photo.brick_srgb)
  if not tile.materials.has(key):
   var mat=StandardMaterial3D.new()
   mat.albedo_color=Color(color[0],color[1],color[2])
   mat.roughness=.89
   tile.materials[key]=mat
 var depth=float(data.fascia_height_m)*Vector3.DOWN
 var lip=float(data.top_lip_m)*Vector3.DOWN
 var a=corners[0];var b=corners[1];var c=corners[2];var d=corners[3]
 tile.quad("entrance-survey-top",a,b,c,d,Vector3.UP)
 tile.quad("entrance-survey-soffit",a+depth,d+depth,c+depth,b+depth,Vector3.DOWN)
 var center=(a+b+c+d)*.25
 for i in 4:
  var p:Vector3=corners[i];var q:Vector3=corners[(i+1)%4]
  var n=(q-p).cross(Vector3.UP).normalized()
  if n.dot((p+q)*.5-center)<0:n=-n
  tile.quad("entrance-survey-fascia",p+depth,q+depth,q+lip,p+lip,n)
  tile.quad("entrance-survey-top",p+lip,q+lip,q,p,n)
 for pier in data.piers:
  for i in 4:
   var p=tile.point(pier[i]);var q=tile.point(pier[(i+1)%4])
   var t=(q-p).normalized()
   var bottom=minf(tile.height_at(p),tile.height_at(q))-.1
   tile.quad("entrance-survey-pier",tile.vert(p,bottom),tile.vert(q,bottom),tile.vert(q,base+pier[(i+1)%4][2]),tile.vert(p,base+pier[i][2]),Vector3(-t.y,0,t.x))
