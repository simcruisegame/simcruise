extends RefCounted

const FINISH = preload("res://ObservedFinish.gdshader")
# A material supplies a representative swatch only if OSM does not specify a color.
const SURFACES = {
 "brick":[1,"aa6a50",.94], "bricks":[1,"aa6a50",.94],
 "wood":[2,"977958",.91], "timber":[2,"977958",.91], "weatherboard":[2,"ad9b83",.91],
 "clapboard":[2,"b2afa1",.93], "vinyl_siding":[2,"c5c4b6",.88], "siding":[2,"b5b2a6",.91],
 "metal":[3,"aaaeb0",.59], "metal_sheet":[3,"aaaeb0",.59], "copper":[3,"96816d",.62],
 "steel":[3,"9b9e9d",.61], "corrugated_metal":[3,"a5a6a4",.68],
 "glass":[4,"78929e",.23], "concrete":[0,"aaa9a3",.93], "stone":[0,"a49d8d",.95],
 "sandstone":[0,"c1a98c",.95], "limestone":[0,"c5bdac",.94], "stucco":[0,"c9c2ad",.96],
 "plaster":[0,"c9c2ad",.96], "slate":[6,"676a6e",.91], "asphalt_shingle":[6,"6e6b68",.95],
 "shingle":[6,"777470",.94], "roof_tiles":[6,"ad755b",.87], "tiles":[6,"ad755b",.87],
 "tar_paper":[0,"625e59",.96], "bitumen":[0,"585555",.96], "gravel":[0,"9e9a90",.96]
}
const COLOR_NAMES = {
 "grey":"9b9b98","gray":"9b9b98","light_gray":"c3c2bd","light_grey":"c3c2bd","dark_gray":"60615e",
 "dark_grey":"60615e","black":"3c3e3d","white":"e0ded5","brown":"846348","red":"a35446",
 "green":"657b62","blue":"6989a3","yellow":"d8bf72","orange":"cd895f","pink":"cc9e98",
 "beige":"c6b79d","silver":"b3b5b3","tan":"bea585","cream":"ddd6be","maroon":"86494b"
}

static func area(poly:PackedVector2Array)->float:
 var total=0.
 for i in poly.size():total+=poly[i].cross(poly[(i+1)%poly.size()])
 return absf(total)*.5

static func polygon(tile,ring)->PackedVector2Array:
 var points=PackedVector2Array()
 for xy in ring:points.append(tile.point(xy))
 return points

static func bounds(poly:PackedVector2Array)->Rect2:
 var rect=Rect2(poly[0],Vector2.ZERO)
 for p in poly:rect=rect.expand(p)
 return rect

static func find(tile,index:int)->Dictionary:
 var data=tile.info.get("street_objects",{}).get("building_finishes",[])
 if data.is_empty():return {}
 if not tile.has_meta("observed_finishes"):
  var candidates=[]
  for entry in data:
   var outline=polygon(tile,entry.ring)
   if outline.size()<3:continue
   candidates.append([outline,bounds(outline),area(outline),entry])
  var possible={}
  var hits={}
  for i in tile.info.buildings.size():
   var shape=tile.info.buildings[i]
   # Separate parts and courtyard structures may share a containing outline.
   if shape[0].size()!=1 or float(shape[3])>0.1:continue
   var target=polygon(tile,shape[0][0])
   if target.size()<3:continue
   var rect=bounds(target);var target_area=area(target)
   if target_area<3.:continue
   var matches=[]
   for item in candidates:
    var common=rect.intersection(item[1]).get_area()
    if common<maxf(rect.get_area(),item[1].get_area())*.76:continue
    if minf(target_area,item[2])<maxf(target_area,item[2])*.82:continue
    var shared_area=0.
    for part in Geometry2D.intersect_polygons(target,item[0]):shared_area+=area(part)
    var union=target_area+item[2]-shared_area
    if union<=0. or shared_area/union<.82:continue
    matches.append(item[3])
    hits[item[3].way]=int(hits.get(item[3].way,0))+1
   if matches.size()==1:possible[i]=matches[0]
  var accepted={}
  for i in possible:
   var item=possible[i]
   if hits[item.way]==1:accepted[i]=item
  tile.set_meta("observed_finishes",accepted)
 return tile.get_meta("observed_finishes").get(index,{})

static func material(tile,observed:Dictionary,part:String,fallback):
 if observed.is_empty():return fallback
 var surface=str(observed.get(part+"_material",""))
 var style=SURFACES.get(surface,[])
 var color=str(observed.get(part+"_color","")).replace(" ","_").replace("-","_")
 if color.begins_with("#"):
  color=color.substr(1)
  if color.length()==3:color=color[0]+color[0]+color[1]+color[1]+color[2]+color[2]
  if color.length()!=6 or not color.is_valid_hex_number():color=""
 else:color=COLOR_NAMES.get(color,"")
 if color.is_empty() and style.is_empty():return fallback
 if style.is_empty():style=[0,"",.91]
 if color.is_empty():color=style[1]
 var key="tag_%s_%s_%s_%s_%s"%[part,fallback,color,style[0],style[2]]
 if tile.materials.has(key):return key
 var mat=ShaderMaterial.new();mat.shader=FINISH
 mat.set_shader_parameter("surface_color",Color("#"+color))
 mat.set_shader_parameter("kind",style[0]);mat.set_shader_parameter("roughness",style[2])
 mat.set_shader_parameter("roof",part=="roof")
 var backing=tile.materials.get(fallback)
 if part=="wall" and backing is StandardMaterial3D and backing.albedo_texture:
  mat.set_shader_parameter("facade",backing.albedo_texture);mat.set_shader_parameter("use_facade",true)
 tile.materials[key]=mat
 return key

static func roof_axes(outline:PackedVector2Array,observed:Dictionary)->Array:
 var axis=Vector2.RIGHT;var longest=0.
 for i in outline.size():
  var edge=outline[(i+1)%outline.size()]-outline[i]
  if edge.length_squared()>longest:
   longest=edge.length_squared();axis=edge.orthogonal().normalized()
 if observed.get("across",false):axis=axis.orthogonal()
 var lo=INF;var hi=-INF
 for p in outline:lo=minf(lo,p.dot(axis));hi=maxf(hi,p.dot(axis))
 return [axis,(lo+hi)*.5,maxf(.1,(hi-lo)*.5)]

static func gable(tile,building,axes:Array,eave:float,rise:float,roof_key,wall_key)->void:
 var axis:Vector2=axes[0];var center:float=axes[1];var half:float=axes[2]
 var wall=tile.tool(wall_key);var roof=tile.tool(roof_key);var pts=[]
 for ring in building[0]:
  for i in ring.size():
   var a=tile.point(ring[i]);var b=tile.point(ring[(i+1)%ring.size()]);pts.append(a)
   if a.distance_squared_to(b)<.0001:continue
   var cuts=[a];var u=a.dot(axis)-center;var v=b.dot(axis)-center
   if u*v<0.:cuts.append(a.lerp(b,u/(u-v)))
   cuts.append(b)
   var n=(b-a).normalized().orthogonal()
   if ((a+b)*.5-tile.point([building[4],building[5]])).dot(n)<0.:n=-n
   var normal=Vector3(n.x,0,n.y)
   for k in range(cuts.size()-1):
    var l=cuts[k];var r=cuts[k+1];var yl=tile.roof_height(l,axis,center,half,eave,rise);var yr=tile.roof_height(r,axis,center,half,eave,rise)
    if maxf(yl,yr)<eave+.002:continue
    tile.tri(wall,tile.vert(l,eave),tile.vert(r,eave),tile.vert(r,yr),Vector2.ZERO,Vector2.RIGHT,Vector2.ONE,normal)
    tile.tri(wall,tile.vert(l,eave),tile.vert(r,yr),tile.vert(l,yl),Vector2.ZERO,Vector2.ONE,Vector2.DOWN,normal)
 for j in range(0,building[1].size(),3):
  var face=[pts[int(building[1][j])],pts[int(building[1][j+1])],pts[int(building[1][j+2])]]
  for sign_side in [-1.,1.]:
   var panel=tile.roof_half(face,axis,center,sign_side)
   for k in range(1,panel.size()-1):
    var a=panel[0];var b=panel[k];var c=panel[k+1]
    var va=tile.vert(a,tile.roof_height(a,axis,center,half,eave,rise));var vb=tile.vert(b,tile.roof_height(b,axis,center,half,eave,rise));var vc=tile.vert(c,tile.roof_height(c,axis,center,half,eave,rise))
    var normal=(vb-va).cross(vc-va).normalized()
    if normal.y<0.:normal=-normal
    tile.tri(roof,va,vb,vc,a/tile.side,b/tile.side,c/tile.side,normal)
