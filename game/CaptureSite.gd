extends RefCounted

# Measured photo surfaces are tile-local. A tile without a capture record uses
# its ordinary map materials and never requests a street photograph.
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
 if not parsed is Dictionary or not parsed.has("fixtures"): return
 record = parsed
 var turf: Array = record.get("turf", [.36,.43,.19])
 turf_color=Color(turf[0],turf[1],turf[2])
 for name in ["exxon-short","exxon-long","bp-short","bp-long","bp-prices","exxon-prices","exxon-dispenser","mart-brick","pizza-front","pizza-side"]:
  var image := Image.new()
  var path := "res://data/capture-%s.jpg" % name
  if image.load_jpg_from_buffer(FileAccess.get_file_as_bytes(path)) != OK: continue
  image.generate_mipmaps()
  var mat := StandardMaterial3D.new()
  mat.albedo_texture = ImageTexture.create_from_image(image)
  mat.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
  mat.cull_mode = BaseMaterial3D.CULL_DISABLED
  mat.roughness = 0.88
  t.materials["cap_" + name] = mat

func bind_road(t, material: ShaderMaterial) -> void:
 if record.is_empty(): return
 var road: Dictionary = record.road
 var image := Image.new()
 if image.load_png_from_buffer(FileAccess.get_file_as_bytes("res://data/" + str(road.image))) != OK: return
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

func dispenser(t, position_2d, across, floor_y):
 if not t.materials.has("cap_exxon-dispenser"): return
 var front: Vector2 = across if across.y>0 else -across
 var along := Vector2(front.y,-front.x)
 var p: Vector2 = position_2d+front*.36
 var left: Vector2 = p+along*.52; var right: Vector2 = p-along*.52
 face(t,"cap_exxon-dispenser",t.vert(left,floor_y+.23),t.vert(right,floor_y+.23),t.vert(right,floor_y+1.80),t.vert(left,floor_y+1.80),Vector3(front.x,0,front.y))

func fixture(t,key):
 var row: Dictionary = record.fixtures[key]
 var p = t.point(row.point)
 return t.vert(p,t.height_at(p)+float(row.height))

func pylon(t,key):
 var top: Vector3 = fixture(t,key+"_sign")
 var site := Vector2(top.x,top.z)
 var floor_y: float = t.height_at(site)
 var normal := Vector2(-.955,-.296)
 var right := Vector2(normal.y,-normal.x)
 var h := top.y-floor_y
 var panel_h := h*(.74 if key=="exxon" else .63)
 var width := panel_h*(.45 if key=="exxon" else .42)
 var base_y := top.y-panel_h
 var steel = "fuel_white"
 for signum in [-1.,1.]:
  var foot: Vector2 = site+right*(width*.45)*signum
  t.post(t.vert(foot,floor_y),t.vert(foot,base_y+.04),.075,.065,steel,8)
 var y=(base_y+top.y)*.5
 var box_center=t.vert(site,y)
 t.JUNCTION.box(t,box_center,right,Vector3(width,panel_h,.21),steel)
 var offset := normal*.116
 var left = site+offset-right*width*.5; var r = site+offset+right*width*.5
 if t.materials.has("cap_"+key+"-prices"):
  face(t,"cap_"+key+"-prices",t.vert(left,base_y),t.vert(r,base_y),t.vert(r,top.y),t.vert(left,top.y),Vector3(normal.x,0,normal.y))

func signal_arm(t, label):
 var top: Vector3 = fixture(t,label+"_mast"); var end: Vector3 = fixture(t,label+"_end")
 var site := Vector2(top.x,top.z); var floor_y = t.height_at(site)
 t.post(t.vert(site,floor_y),top+Vector3.UP*.12,.19,.105,"signal_steel",12)
 var mid := top.lerp(end,.46)+Vector3.UP*.19
 t.post(top,mid,.12,.082,"signal_steel",10)
 t.post(mid,end,.084,.046,"signal_steel",10)
 # Westbound heads at the near mast face east. Opposite heads face west.
 var direction := Vector2(.956,.293) * (1. if label=="nw" else -1.)
 var front := Vector3(direction.x,0,direction.y)
 for fraction in [.46,.66,.86]:
  var hook := top.lerp(end,fraction)+Vector3.UP*(sin(fraction*PI)*.19)
  var body := hook-Vector3.UP*.26
  t.JUNCTION.box(t,body,direction,Vector3(.29,.97,.34),"signal_yellow")
  var face_center := body+front*.155
  for lens in 3:
   var lens_center := face_center+Vector3.UP*(.295-.29*lens)
   t.post(lens_center,lens_center+front*.105,.124,.118,"signal_dark",12)
   if lens==2: t.post(lens_center+front*.107,lens_center+front*.11,.089,.089,"signal_go",12)
 var backplate := top.lerp(end,.28)+Vector3.UP*.10
 t.JUNCTION.box(t,backplate,direction,Vector3(.09,.67,.87),"fuel_white")

func build_fixtures(t):
 if record.is_empty(): return
 preload("res://CapturedWoodland.gd").build(t)
 for brand in ["exxon","bp"]:pylon(t,brand)
 for label in ["nw","se"]:signal_arm(t,label)
 for key in ["rail_zinc","rail_post"]:
  var mat:=StandardMaterial3D.new()
  mat.albedo_color=Color("#92988e") if key=="rail_zinc" else Color("#665e50")
  mat.metallic=.54 if key=="rail_zinc" else .15
  mat.roughness=.55 if key=="rail_zinc" else .84
  mat.cull_mode=BaseMaterial3D.CULL_DISABLED
  t.materials[key]=mat
 for path in record.get("rails",[]):observed_barrier(t,path)
 for path in record.get("curbed_turf",[]):raised_island(t,path)

func store(t,b,idx) -> bool:
 if record.is_empty(): return false
 if idx==10: return pizza(t,b)
 if idx!=13: return false
 var center=t.point([b[4],b[5]]);var base=t.height_at(center);var ceiling=base+4.9
 var outline=PackedVector2Array()
 for v in b[0][0]: outline.append(t.point(v))
 var bounds=Rect2(outline[0],Vector2.ZERO)
 for p in outline:bounds=bounds.expand(p)
 t.footprints.append([bounds,outline])
 for i in outline.size():
  var a=outline[i];var d=outline[(i+1)%outline.size()]
  var n=(d-a).orthogonal().normalized()
  if n.dot((a+d)*.5-center)<0:n=-n
  var normal=Vector3(n.x,0,n.y)
  var low=minf(base,minf(t.height_at(a),t.height_at(d)))-.25
  var k="cap_mart-brick" if t.materials.has("cap_mart-brick") else "siding0"
  t.quad(k,t.vert(a,low),t.vert(d,low),t.vert(d,ceiling),t.vert(a,ceiling),normal,Vector2(maxf(1.,a.distance_to(d)/6.),1))
  t.collision_faces.append_array(PackedVector3Array([t.vert(a,low),t.vert(d,ceiling),t.vert(d,low),t.vert(a,low),t.vert(a,ceiling),t.vert(d,ceiling)]))
  if n.y>.7 and a.distance_to(d)>6.:
   var start=a.lerp(d,.06);var finish=a.lerp(d,.94)
   t.quad("house_glass",t.vert(start+n*.025,base+.54),t.vert(finish+n*.025,base+.54),t.vert(finish+n*.025,base+2.52),t.vert(start+n*.025,base+2.52),normal)
   for j in 8:
    var mullion=start.lerp(finish,j/7.)+n*.045
    t.post(t.vert(mullion,base+.54),t.vert(mullion,base+2.52),.043,.043,"iron",4)
   var front=n*.77
   t.quad("iron",t.vert(a+front,base+2.75),t.vert(d+front,base+2.75),t.vert(d,base+3.28),t.vert(a,base+3.28),Vector3.UP)
   t.quad("trim",t.vert(a+front,base+2.60),t.vert(d+front,base+2.60),t.vert(d+front,base+2.75),t.vert(a+front,base+2.75),normal)
 var roof=t.tool("field_roof")
 for j in range(0,b[1].size(),3):
  var a=outline[int(b[1][j])];var d=outline[int(b[1][j+1])];var f=outline[int(b[1][j+2])]
  t.tri(roof,t.vert(a,ceiling),t.vert(d,ceiling),t.vert(f,ceiling),a/t.side,d/t.side,f/t.side,Vector3.UP)
 return true

func pizza(t,b):
 var outline=PackedVector2Array()
 for uv in b[0][0]:outline.append(t.point(uv))
 var center=t.point([b[4],b[5]]);var floor_y=t.height_at(center)
 var left=outline[4];var right=outline[5]
 var span=(right-left).normalized()
 var offset=(left+right).dot(span)*.5
 var width=left.distance_to(right)*.5
 var bounds=Rect2(outline[0],Vector2.ZERO)
 for pt in outline:bounds=bounds.expand(pt)
 t.footprints.append([bounds,outline])
 var eave=floor_y+3.03;var rise=2.10
 var base_y=floor_y-.10
 for i in outline.size():
  var a=outline[i];var d=outline[(i+1)%outline.size()]
  var normal2=(d-a).orthogonal().normalized()
  if normal2.dot((a+d)*.5-center)<0:normal2=-normal2
  var normal=Vector3(normal2.x,0,normal2.y)
  var va=t.vert(a,base_y);var vd=t.vert(d,base_y)
  var ea=t.vert(a,eave);var ed=t.vert(d,eave)
  t.collision_faces.append_array(PackedVector3Array([va,ed,vd,va,ea,ed]))
  if i==4 and t.materials.has("cap_pizza-front"):
   var st=t.tool("cap_pizza-front");var peak=t.vert((a+d)*.5,eave+rise)
   t.tri(st,va,vd,ed,Vector2(0,1),Vector2(1,1),Vector2(1,.41),normal)
   t.tri(st,va,ed,ea,Vector2(0,1),Vector2(1,.41),Vector2(0,.41),normal)
   t.tri(st,ea,ed,peak,Vector2(0,.41),Vector2(1,.41),Vector2(.55,0),normal)
  else:
   var wall="cap_pizza-side" if i==3 else "cap_mart-brick"
   t.quad(wall,va,vd,ed,ea,normal,Vector2(maxf(1.,a.distance_to(d)/8.),1))
   var ha=t.roof_height(a,span,offset,width,eave,rise);var hd=t.roof_height(d,span,offset,width,eave,rise)
   if maxf(ha,hd)>eave+.02:t.quad("cap_mart-brick",ea,ed,t.vert(d,hd),t.vert(a,ha),normal)
 var roof=t.tool("field_roof")
 for j in range(0,b[1].size(),3):
  var verts=[outline[int(b[1][j])],outline[int(b[1][j+1])],outline[int(b[1][j+2])]]
  for signum in [-1.,1.]:
   var panel=t.roof_half(verts,span,offset,signum)
   for i in range(1,panel.size()-1):
    var a=panel[0];var d=panel[i];var f=panel[i+1]
    var va=t.vert(a,t.roof_height(a,span,offset,width,eave,rise));var vd=t.vert(d,t.roof_height(d,span,offset,width,eave,rise));var vf=t.vert(f,t.roof_height(f,span,offset,width,eave,rise))
    var n=(vd-va).cross(vf-va).normalized()
    if n.y<0:n=-n
    t.tri(roof,va,vd,vf,a/t.side,d/t.side,f/t.side,n)
 return true

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
