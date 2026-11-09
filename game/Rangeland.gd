extends Node3D
# Low shrubs and field grass. Satellite models estimate stand density and
# height. Local aerial texture locates likely vegetation within each stand at
# the driving camera. Each plant is a textured blade clump (crossed cards)
# that sways in Rangeland.gdshader; beside a mapped road the clumps are mown.
const NONE := Vector2i(-1,-1)
const RADIUS := 96.
const NEAR_RADIUS := 23.
# Orthographic union of the triangles in the authored canopy at unit scale.
const CROWN_AREA := 2.3101
const BLOCKS := 16
# Mown verge: full height returns between these distances from the asphalt edge.
const MOWN_EDGE_M := 3.0
const ROUGH_EDGE_M := 8.0
const CLUMP_SHADER := preload("res://Rangeland.gdshader")
const FLOATS_PER_INSTANCE := 20
var distant_instances:=MultiMesh.new()
var worker:Thread
var requested:=NONE
var observations:Dictionary={}
var retry:Dictionary={}
var patches:Dictionary={}
var mesh_instances:=MultiMesh.new()
var enabled:=true
var last_pos:=Vector2.INF
var last_refresh:=0
var incomplete:=true
var survey_focus:=Vector2.INF
var tile_versions:Dictionary={}
var draws:=0
var tested_cells:=0
var clump_material:ShaderMaterial
var clump_texture:ImageTexture

func stamp(image:Image,x:float,y:float,radius:float,color:Color):
 var lo:=Vector2i(floori(x-radius),floori(y-radius)).clamp(Vector2i.ZERO,Vector2i(image.get_width()-1,image.get_height()-1))
 var hi:=Vector2i(ceili(x+radius),ceili(y+radius)).clamp(Vector2i.ZERO,Vector2i(image.get_width()-1,image.get_height()-1))
 for py in range(lo.y,hi.y+1):
  for px in range(lo.x,hi.x+1):
   if Vector2(px+.5-x,py+.5-y).length()<=radius:image.set_pixel(px,py,color)

# A fan of curved blades drawn once in code: no downloaded art. Tips lighten,
# the transparent background keeps a leaf tone so filtered edges stay green.
func make_clump_texture()->ImageTexture:
 var size:=128
 var image:=Image.create_empty(size,size,false,Image.FORMAT_RGBA8)
 image.fill(Color(.33,.42,.17,0.))
 var rng:=RandomNumberGenerator.new()
 rng.seed=4477
 for blade in 60:
  var root_x:=size*.5+rng.randf_range(-size*.16,size*.16)
  var lean:=rng.randf_range(-.95,.95)
  var curve:=rng.randf_range(-.55,.55)
  var height:=rng.randf_range(size*.42,size*.97)
  var width:=rng.randf_range(1.1,2.8)
  var color:=Color.from_hsv(rng.randf_range(.17,.26),rng.randf_range(.42,.70),rng.randf_range(.50,.98))
  var steps:=int(height)
  for s in steps:
   var t:=float(s)/steps
   var x:=root_x+(lean*t+curve*t*t)*size*.34
   var y:=size-1.-t*height
   stamp(image,x,y,width*(1.-t*.82),color.lightened(t*.22))
 image.fix_alpha_edges()
 image.generate_mipmaps()
 return ImageTexture.create_from_image(image)

func material()->ShaderMaterial:
 if clump_material:return clump_material
 clump_texture=make_clump_texture()
 clump_material=ShaderMaterial.new()
 clump_material.shader=CLUMP_SHADER
 clump_material.set_shader_parameter("clump",clump_texture)
 return clump_material

# One vertical card through the origin, split into rows so the shader can bend
# the upper rows. UV.y is 1 at the root and 0 at the tip.
func card(st:SurfaceTool,angle:float,half_width:float,height:float,rows:int):
 var across:=Vector3(cos(angle),0.,sin(angle))*half_width
 var normal:=Vector3(-sin(angle),0.,cos(angle))
 for i in rows:
  var y0:=height*float(i)/rows;var y1:=height*float(i+1)/rows
  var v0:=1.-float(i)/rows;var v1:=1.-float(i+1)/rows
  var a:=-across+Vector3.UP*y0;var b:=across+Vector3.UP*y0
  var c:=across+Vector3.UP*y1;var d:=-across+Vector3.UP*y1
  for corner in [[a,Vector2(0,v0)],[b,Vector2(1,v0)],[c,Vector2(1,v1)],[a,Vector2(0,v0)],[c,Vector2(1,v1)],[d,Vector2(0,v1)]]:
   st.set_color(Color.WHITE);st.set_normal(normal);st.set_uv(corner[1]);st.add_vertex(corner[0])

# Detailed: three cards with two rows (36 vertices). Distant: two cards, one
# row (12 vertices). The previous tuft geometry used 1284 and 426 vertices.
func plant_mesh(detailed:bool=true)->ArrayMesh:
 var st:=SurfaceTool.new()
 st.begin(Mesh.PRIMITIVE_TRIANGLES)
 st.set_material(material())
 var cards:=3 if detailed else 2
 for i in cards:
  card(st,PI*float(i)/cards+.21,1.,1.,2 if detailed else 1)
 return st.commit()

# Instance layout: 3x4 transform, aerial tint colour, custom (sway phase, mown).
# Fixed at construction so a buffer written before _ready has the same stride.
func _init():
 for target in [mesh_instances,distant_instances]:
  target.transform_format=MultiMesh.TRANSFORM_3D
  target.use_colors=true
  target.use_custom_data=true

func create_batch(target:MultiMesh,detailed:bool):
 target.mesh=plant_mesh(detailed)
 var rendered:=MultiMeshInstance3D.new()
 rendered.multimesh=target
 rendered.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_ON if detailed else GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
 add_child(rendered)

func _ready():
 create_batch(mesh_instances,true)
 create_batch(distant_instances,false)

func load_district(helper:String,cache:String,id:Vector2i)->Dictionary:
 var lines: Array=[]
 if OS.execute(helper,["shrub-cover",cache,str(id.x),str(id.y)],lines,true)!=0 or lines.is_empty():return {}
 var result=JSON.parse_string(lines[0])
 if not result is Dictionary:return {}
 var coords=result.get("id",[])
 if coords.size()!=2 or int(coords[0])!=id.x or int(coords[1])!=id.y:return {}
 var name:="shrub-mask-v1-2025-%d-%d.png"%[id.x,id.y]
 if result.get("png")==name:
  var im:=Image.load_from_file(cache.path_join(name))
  if im and im.get_size()==Vector2i(256,256):result["image"]=im
 return result

# Metres from the nearest mapped pavement edge, using the tile's 24 m road
# grid and its neighbours; INF when no road is near.
func verge_distance(tile,local:Vector2)->float:
 var closest:=INF
 var cell:=Vector2i((local/24.).floor())
 for dy in range(-1,2):
  for dx in range(-1,2):
   for seg in tile.road_grid.get(cell+Vector2i(dx,dy),[]):
    var q:Vector2=Geometry2D.get_closest_point_to_segment(local,seg[0],seg[1])
    closest=minf(closest,q.distance_to(local)-float(seg[3])*.5)
 return closest

# Tint relative to the card colours: the aerial pixel's hue and brightness.
func plant_tint(pixel:Color)->Color:
 var light:=maxf(pixel.get_luminance(),.02)
 var brightness:=clampf(.72+light*1.25,.62,1.32)
 var hue:=Color(pixel.r/light,pixel.g/light,pixel.b/light).lerp(Color.WHITE,.55)
 return Color(hue.r*brightness,hue.g*brightness,hue.b*brightness,1.)

func observed_patch(tile,tx:int,ty:int,image:Image,id:Vector2i)->Array:
 var items:Array=[]
 var aerial:Image=tile.shrub_aerial
 if not aerial:return items
 var code:=image.get_pixel(posmod(id.x,16)*16+tx,posmod(id.y,16)*16+ty)
 var fraction:float=code.r8*.01
 var height:float=code.g8*.01
 if fraction<.04 or height<.08:return items
 var span:float=tile.side/BLOCKS
 # Wider clumps than the former tufts cover the same fraction with fewer plants.
 var radius:float=clampf(height*2.05,.48,2.3)*1.3
 var estimated_count:float=fraction*span*span/(CROWN_AREA*radius*radius)
 var cells:=clampi(ceili(sqrt(estimated_count*2.1)),8,24)
 tested_cells+=cells*cells
 var step:=span/cells
 var candidates:Array=[]
 for x in cells:
  for y in cells:
   var seed_value:=absi(hash(Vector4i(id.x,id.y,tx*32+x,ty*32+y)))
   var local:=(Vector2(x+.06+.88*float(seed_value%1000)*.001,y+.06+.88*float((seed_value/1000)%1000)*.001)*step)+Vector2(tx,ty)*span
   if tile.surface_grip(local)>.69 or tile.occupied(local):continue
   var px:=Vector2i((local/tile.side*256.).floor()).clamp(Vector2i(3,3),Vector2i(252,252))
   var pixel:=aerial.get_pixelv(px)
   var center:=pixel.get_luminance()
   var around:float=(aerial.get_pixelv(px+Vector2i(3,0)).get_luminance()+aerial.get_pixelv(px-Vector2i(3,0)).get_luminance()+aerial.get_pixelv(px+Vector2i(0,3)).get_luminance()+aerial.get_pixelv(px-Vector2i(0,3)).get_luminance())*.25
   var scale:float=.74+float(seed_value%511)/1000.
   var noise:float=float((seed_value/100000)%1000)*.001
   # Favor darker local structure. Coarse survey support controls the total
   # crown cover, including pale dormant shrubs with little optical contrast.
   candidates.append([around-center+.035*(noise-.5),local,scale,float(seed_value%6283)*.001,pixel,noise])
 candidates.sort_custom(func(l,r):return l[0]>r[0])
 var target:float=fraction*float(candidates.size())*step*step
 var filled:=0.
 for item in candidates:
  if filled>=target:break
  var local:Vector2=item[1]
  var scale:float=item[2]
  var mown:float=1.-smoothstep(MOWN_EDGE_M,ROUGH_EDGE_M,verge_distance(tile,local))
  var cut_height:float=.10+.15*item[5]
  var axes:=Vector3(radius*scale,lerpf(height*scale,minf(height*scale,cut_height),mown),radius*scale*lerpf(1.,.7,mown))
  var transform3:=Transform3D(Basis(Vector3.UP,item[3])*Basis.from_scale(axes),Vector3(local.x,tile.height_at(local),local.y))
  items.append([transform3,plant_tint(item[4]),Color(item[3]/TAU,mown,0.,0.)])
  filled+=CROWN_AREA*axes.x*axes.z
 return items

func refresh_instances(game,focus:Vector2):
 var out:=PackedFloat32Array();var far:=PackedFloat32Array();var used:Dictionary={};var pending:=false
 var loaded:Dictionary={}
 var allowance:=3
 for id in game.tiles:
  var tile=game.tiles[id]
  var district:=Vector2i(id.x>>4,id.y>>4)
  if not tile.shrub_aerial or not observations.has(district):continue
  var offset:Vector2=game.tile_offset(id)
  var relative:Vector2=focus-offset
  var span:float=tile.side/BLOCKS
  if (relative-relative.clamp(Vector2.ZERO,Vector2.ONE*tile.side)).length()>RADIUS:continue
  var version:Vector2i=Vector2i(tile.get_instance_id() & 0x7fffffff,int(tile.learned_edges!=null))
  loaded[id]=version
  if tile_versions.get(id)!=version:
   for old:Vector4i in patches.keys():
    if old.x==id.x and old.y==id.y:patches.erase(old)
  var lo:=Vector2i(((relative-Vector2.ONE*RADIUS)/span).floor()).clamp(Vector2i.ZERO,Vector2i(BLOCKS-1,BLOCKS-1))
  var hi:=Vector2i(((relative+Vector2.ONE*RADIUS)/span).floor()).clamp(Vector2i.ZERO,Vector2i(BLOCKS-1,BLOCKS-1))
  for i in range(lo.x,hi.x+1):
   for j in range(lo.y,hi.y+1):
    var box:=Vector2(i,j)*span
    if relative.distance_squared_to(relative.clamp(box,box+Vector2.ONE*span))>RADIUS*RADIUS:continue
    var key:=Vector4i(id.x,id.y,i,j);used[key]=true
    if not patches.has(key):
     if allowance==0:pending=true;continue
     patches[key]=observed_patch(tile,i,j,observations[district],id);allowance-=1
    for plant in patches[key]:
     var local:Transform3D=plant[0]
     var v:=local.origin+Vector3(offset.x,0,offset.y)
     if focus.distance_squared_to(Vector2(v.x,v.z))>RADIUS*RADIUS:continue
     var b:=local.basis;var tint:Color=plant[1];var custom:Color=plant[2]
     # MultiMesh buffer order: 3x4 transform, instance colour, custom data.
     var record=PackedFloat32Array([b.x.x,b.y.x,b.z.x,v.x,b.x.y,b.y.y,b.z.y,v.y,b.x.z,b.y.z,b.z.z,v.z,tint.r,tint.g,tint.b,tint.a,custom.r,custom.g,custom.b,custom.a])
     if focus.distance_squared_to(Vector2(v.x,v.z))<=NEAR_RADIUS*NEAR_RADIUS:out.append_array(record)
     else:far.append_array(record)
 for key in patches.keys():
  if not used.has(key):patches.erase(key)
 tile_versions=loaded
 mesh_instances.instance_count=out.size()/FLOATS_PER_INSTANCE
 if not out.is_empty():mesh_instances.buffer=out
 distant_instances.instance_count=far.size()/FLOATS_PER_INSTANCE
 if not far.is_empty():distant_instances.buffer=far
 draws=(out.size()+far.size())/FLOATS_PER_INSTANCE
 last_pos=focus;last_refresh=Time.get_ticks_msec()
 incomplete=pending

func tick(game):
 if not enabled or not game.ready_drive or not game.jump.is_empty():return
 var focus:Vector2=survey_focus if survey_focus.is_finite() else game.pos
 var here:Vector2i=game.tile_id(focus)
 var region:=Vector2i(here.x>>4,here.y>>4)
 var now:=Time.get_ticks_msec()
 if worker and not worker.is_alive():
  var reply=worker.wait_to_finish();worker=null
  if "--shrub-reference" in OS.get_cmdline_user_args():print("SHRUB SOURCE ",requested," ",reply.get("status")," image=",reply.has("image"))
  game.calls+=int(reply.get("requests",0))
  if reply.has("image"):
   observations[requested]=reply.image
   for key in observations.keys():
    if (key-region).length_squared()>2:observations.erase(key)
   incomplete=true
  else:retry[requested]=now+180000
 var longitude:float=(region.x+.5)/4096.*360.-180.
 var latitude:=rad_to_deg(atan(sinh(PI*(1.-(region.y+.5)/2048.))))
 var eligible:=longitude>=-125. and longitude<=-87.54 and latitude>=24. and latitude<=50.
 if retry.size()>12:
  for key in retry.keys():
   if (key-region).length_squared()>2:retry.erase(key)
 if eligible and not observations.has(region) and now>int(retry.get(region,0)) and not worker:
  requested=region;worker=Thread.new()
  if worker.start(load_district.bind(game.helper,game.cache,region),Thread.PRIORITY_LOW)!=OK:worker=null;retry[region]=now+20000
 if not observations.has(region) and patches.is_empty() and draws==0:return
 if now-last_refresh>=60 and (incomplete or last_pos.distance_squared_to(focus)>16. or now-last_refresh>1500):
  refresh_instances(game,focus)

func shutdown():
 if worker:worker.wait_to_finish();worker=null
func save_stems(game,path:String):
 var rows: Array=[]
 for key:Vector4i in patches:
  var id:=Vector2i(key.x,key.y)
  var tile=game.tiles.get(id)
  if not tile:continue
  var offset:Vector2=game.tile_offset(id)
  for plant in patches[key]:
   var s:Transform3D=plant[0]
   var lp:=Vector2(s.origin.x,s.origin.z)
   if (lp+offset).distance_to(survey_focus if survey_focus.is_finite() else game.pos)>RADIUS:continue
   rows.append([id.x,id.y,lp.x/tile.side,lp.y/tile.side,s.basis.get_scale().y,s.basis.get_scale().x,atan2(s.basis.z.x,s.basis.z.z),lp.distance_to((survey_focus if survey_focus.is_finite() else game.pos)-offset)])
 var out=FileAccess.open(path,FileAccess.WRITE)
 if out:out.store_string(JSON.stringify(rows))
