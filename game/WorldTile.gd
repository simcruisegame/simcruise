extends Node3D
const LIDAR_ROOF=preload("res://LidarRoof.gd")
const JUNCTION=preload("res://Junction.gd")
var info:Dictionary
var side:float
var batches={}
var paint_evidence
var road_layout
var lane_paint_layer:Node3D
var road_survey
var measured_section
var segments=[]
var footprints=[]
var collision_faces=PackedVector3Array()
var materials:Dictionary
var terrain_material:ShaderMaterial
var structure_layer:Node3D
var streamed_roof:ShaderMaterial
var streaming_ground:ShaderMaterial
const WALL=[25,37,53,63,68,73,21,33,69]
# The generic entrance set uses unbranded door and window pixels. Fractions
# locate the top of each usable section within its CC0 photograph.
const SHOP=[14,80,82,91]
const SHOP_CROP={14:.52,80:.25,82:0.,91:.31}
var road_prior:Image
var shrub_aerial:Image
var aerial_pixels:Image
var learned_edges:Image
var photographs=preload("res://PhotoFacades.gd").new()
var wall_patches=preload("res://ObservedWallPatches.gd").new()
var capture=preload("res://CaptureSite.gd").new()
var surface_mask:Image
var photo_sample:Image
var grid_steps=32
var surveyed_outcrop
var crossfall_surface
var road_surface   # RoadSurface.gd ribbons, or null
func height_at(p:Vector2)->float:
 if road_surface:
  var on_road=road_surface.height(p)
  if not is_nan(on_road):return on_road
 var original=base_height(p)
 if crossfall_surface:return crossfall_surface.elevation(p,original)
 return surveyed_outcrop.elevation(p,original) if surveyed_outcrop else original
# Deck height of a mapped bridge at p (tile metres), or NAN off every deck.
# The terrain under a bridge is the valley, so placing the car, its shadow and
# recovery use this first (BridgeDeck.gd builds the matching surface).
func deck_at(p:Vector2)->float:
 var best=NAN
 for row in info.get("bridges",[]):
  var pts:Array=row[2];var heights:Array=row[4];var half=float(row[3])*.5+.6
  for k in range(1,pts.size()):
   var a=Vector2(pts[k-1][0],pts[k-1][1])*side;var b=Vector2(pts[k][0],pts[k][1])*side
   var ab=b-a;var l2=ab.length_squared()
   if l2<1e-4:continue
   var t=clampf((p-a).dot(ab)/l2,0.,1.)
   if p.distance_to(a+ab*t)>half:continue
   var z=lerpf(float(heights[k-1]),float(heights[k]),t)
   if is_nan(best) or z>best:best=z
 return best
func base_height(p:Vector2)->float:
 var g=(p/side*grid_steps).clamp(Vector2.ZERO,Vector2.ONE*grid_steps)
 var x=mini(int(g.x),grid_steps-1);var y=mini(int(g.y),grid_steps-1)
 var stride=grid_steps+1;var i=y*stride+x;var h=info.h
 if info.get("road_grade",{}).has("source"):
  var u=g.x-x
  var v=g.y-y
  if u+v<=1.:
   return h[i]+(h[i+1]-h[i])*u+(h[i+stride]-h[i])*v
  return h[i+stride+1]+(h[i+1]-h[i+stride+1])*(1.-v)+(h[i+stride]-h[i+stride+1])*(1.-u)
 return lerpf(lerpf(h[i],h[i+1],g.x-x),lerpf(h[i+stride],h[i+stride+1],g.x-x),g.y-y)
# Road-corridor smoothing. The ground grid point-samples the elevation model,
# so under a road one node sits on the pavement, the next on the shoulder and
# the next in the ditch. Each triangle then tilts differently and every grid
# edge is a slope step for the tyres. A robust local-linear fit of the
# centreline profile (Gaussian window, ±3 sigma ≈ 27 m) keeps genuine grades,
# rejects ditch and bank outliers, and the corridor nodes are blended onto that
# profile plus a clamped cross slope. Authored roadbed nodes, surveyed bank
# nodes and tile-boundary nodes never move, so a shared tile edge stays
# identical on both sides. checks/RoadProfile.gd measures the result.
const SMOOTH_SIGMA_M=9.
const SMOOTH_STEP_M=2.
const SMOOTH_SHOULDER_M=4.
const SMOOTH_FALLOFF_M=8.
const SMOOTH_CAP_M=1.5
const SMOOTH_LATERAL=.05
const SMOOTH_LATERAL_OFFSET_M=8.
const SMOOTH_SEAM_REACH_M=3.*SMOOTH_SIGMA_M+2.*SMOOTH_STEP_M
static func sample_grid(h:PackedFloat64Array,n:int,width:float,p:Vector2)->float:
 var g=(p/width*n).clamp(Vector2.ZERO,Vector2.ONE*n)
 var x=mini(int(g.x),n-1);var y=mini(int(g.y),n-1);var i=y*(n+1)+x
 return lerpf(lerpf(h[i],h[i+1],g.x-x),lerpf(h[i+n+1],h[i+n+2],g.x-x),g.y-y)
# Weighted local-linear (first-order Savitzky-Golay / LOESS) estimate at
# station s. S holds ascending stations; NAN when the window holds no sample.
static func local_linear(S:PackedFloat64Array,Z:PackedFloat64Array,W:PackedFloat64Array,s:float)->float:
 var lo=S.bsearch(s-3.*SMOOTH_SIGMA_M);var hi=mini(S.bsearch(s+3.*SMOOTH_SIGMA_M,false),S.size())
 var sw=0.;var swx=0.;var swy=0.;var swxx=0.;var swxy=0.
 for i in range(lo,hi):
  var x=S[i]-s;var w=W[i]*exp(-.5*x*x/(SMOOTH_SIGMA_M*SMOOTH_SIGMA_M))
  if w<=0.:continue
  sw+=w;swx+=w*x;swy+=w*Z[i];swxx+=w*x*x;swxy+=w*x*Z[i]
 if sw<=0.:return NAN
 var det=sw*swxx-swx*swx
 if det<=1e-9*maxf(sw*swxx,1e-12):return swy/sw
 return (swy-swx*(sw*swxy-swx*swy)/det)/sw
func smooth_road_corridors()->void:
 var roads:Array=info.get("roads",[])
 var n=grid_steps;var count=(n+1)*(n+1);var h=info.h
 if roads.is_empty() or h.size()!=count or info.get("survey_ground_nodes",0)>0 or "--without-road-smoothing" in OS.get_cmdline_user_args():return
 # The helper already built road benches from world-anchored data (road-grade.mjs v2).
 if int(info.get("road_grade",{}).get("version",0))>=2:return
 var started=Time.get_ticks_usec()
 var fixed={}
 var roadbed:Array=info.get("roadbed",[])
 for i in range(0,roadbed.size()-1,2):fixed[int(roadbed[i])]=true
 for i in info.get("bank_detail_nodes",PackedInt32Array()):fixed[int(i)]=true
 var original=PackedFloat64Array(h)
 var sum_w=PackedFloat64Array();sum_w.resize(count)
 var sum_wz=PackedFloat64Array();sum_wz.resize(count)
 var max_w=PackedFloat64Array();max_w.resize(count)
 var cell=side/n
 var roads_done=0
 for road in roads:
  var pts=metres(road[3])
  if pts.size()<2:continue
  var half=float(road[4])*.5
  # Station of every polyline vertex.
  var starts=PackedFloat64Array();var length=0.
  for k in pts.size():
   if k>0:length+=pts[k].distance_to(pts[k-1])
   starts.append(length)
  if length<2.*SMOOTH_STEP_M:continue
  # Centreline samples every SMOOTH_STEP_M: station, height and cross slope.
  var S=PackedFloat64Array();var Z=PackedFloat64Array();var RC=PackedFloat64Array();var ONES=PackedFloat64Array()
  var seg=0;var s=0.
  while s<=length:
   while seg<pts.size()-2 and s>starts[seg+1]:seg+=1
   var a=pts[seg];var t=(pts[seg+1]-a).normalized();var p=a+t*(s-starts[seg])
   var left=Vector2(-t.y,t.x)*SMOOTH_LATERAL_OFFSET_M
   S.append(s);Z.append(sample_grid(original,n,side,p));ONES.append(1.)
   RC.append((sample_grid(original,n,side,p+left)-sample_grid(original,n,side,p-left))/(2.*SMOOTH_LATERAL_OFFSET_M))
   s+=SMOOTH_STEP_M
  # Two robustness passes: residuals beyond six median absolute deviations lose their weight.
  var W=ONES.duplicate()
  for pass_index in 2:
   var spread=PackedFloat64Array()
   for i in S.size():spread.append(absf(Z[i]-local_linear(S,Z,W,S[i])))
   var ranked=spread.duplicate();ranked.sort()
   var scale=maxf(6.*1.4826*ranked[ranked.size()>>1],.12)
   for i in S.size():
    var r=spread[i]/scale
    W[i]=(1.-r*r)*(1.-r*r) if r<1. else 0.
  # Grid nodes inside the corridor, keyed by their nearest centreline point.
  var reach=half+SMOOTH_SHOULDER_M+SMOOTH_FALLOFF_M
  var nearest_point={}
  for k in pts.size()-1:
   var a=pts[k];var ab=pts[k+1]-a;var l2=ab.length_squared()
   if l2<1e-6:continue
   var left=Vector2(-ab.y,ab.x)/sqrt(l2)
   var lo=((a.min(pts[k+1])-Vector2.ONE*reach)/cell).floor();var hi=((a.max(pts[k+1])+Vector2.ONE*reach)/cell).ceil()
   for gy in range(maxi(1,int(lo.y)),mini(n-1,int(hi.y))+1):
    for gx in range(maxi(1,int(lo.x)),mini(n-1,int(hi.x))+1):
     var q=Vector2(gx,gy)*cell;var f=clampf((q-a).dot(ab)/l2,0.,1.);var d=q.distance_to(a+ab*f)
     if d>reach:continue
     var idx=gy*(n+1)+gx
     if nearest_point.has(idx) and nearest_point[idx][0]<=d:continue
     nearest_point[idx]=[d,starts[k]+f*sqrt(l2),(q-a).dot(left)]
  for idx in nearest_point:
   if fixed.has(idx):continue
   var row=nearest_point[idx];var w=1.-smoothstep(half+SMOOTH_SHOULDER_M,reach,row[0])
   if w<=0.:continue
   var z=local_linear(S,Z,W,row[1])
   if is_nan(z):continue
   var cross=clampf(local_linear(S,RC,ONES,row[1]),-SMOOTH_LATERAL,SMOOTH_LATERAL)
   var change=SMOOTH_CAP_M*tanh((z+cross*row[2]-original[idx])/SMOOTH_CAP_M)
   sum_w[idx]+=w;sum_wz[idx]+=w*(original[idx]+change);max_w[idx]=maxf(max_w[idx],w)
  roads_done+=1
 var seams=seam_boundaries(original,fixed,sum_w,sum_wz,max_w)
 var moved=0;var largest=0.
 for idx in count:
  if sum_w[idx]<=0.:continue
  var value=original[idx]+(sum_wz[idx]/sum_w[idx]-original[idx])*max_w[idx]
  if absf(value-original[idx])>.0005:moved+=1;largest=maxf(largest,absf(value-original[idx]))
  h[idx]=value
 info.h=h
 info["road_smoothing"]={"roads":roads_done,"nodes":moved,"max_m":largest,"seams":seams,"ms":(Time.get_ticks_usec()-started)/1000.}
# Road polyline in tile metres without repeated vertices.
func metres(route:Array)->PackedVector2Array:
 var pts=PackedVector2Array()
 for a in route:
  var p=point(a)
  if pts.is_empty() or p.distance_to(pts[-1])>=.01:pts.append(p)
 return pts
# Number of roads with an end within two centimetres of p.
func roads_ending_at(roads:Array,p:Vector2)->int:
 var found=0
 for road in roads:
  var pts=metres(road[3])
  if pts.size()>=2 and (pts[0].distance_to(p)<.02 or pts[-1].distance_to(p)<.02):found+=1
 return found
# Follow a polyline from its first vertex for at most SMOOTH_SEAM_REACH_M.
static func truncated(pts:PackedVector2Array)->PackedVector2Array:
 var out=PackedVector2Array([pts[0]]);var walked=0.
 for k in range(1,pts.size()):
  var step=pts[k].distance_to(out[-1])
  if walked+step>SMOOTH_SEAM_REACH_M:
   out.append(out[-1]+(pts[k]-out[-1])*((SMOOTH_SEAM_REACH_M-walked)/step));break
  out.append(pts[k]);walked+=step
 return out
# A bundled neighbour across one tile edge: heights after its own roadbed, roads, fixed nodes.
func bundled_neighbour(offset:Vector2i)->Dictionary:
 var id=Vector2i(int(info.id[0]),int(info.id[1]))+offset
 var path="res://data/%d_%d.json"%[id.x,id.y]
 if not FileAccess.file_exists(path) or FileAccess.file_exists("res://data/district-surface-%d-%d.json"%[id.x,id.y]):return {}
 var data=JSON.parse_string(FileAccess.get_file_as_string(path))
 if not data is Dictionary or int(data.get("grid",32))!=grid_steps or not data.get("h") is Array or not data.get("roads") is Array:return {}
 var h=PackedFloat64Array(data.h)
 if h.size()!=(grid_steps+1)*(grid_steps+1):return {}
 var fixed={}
 var roadbed:Array=data.get("roadbed",[])
 for i in range(0,roadbed.size()-1,2):
  var k=int(roadbed[i])
  if k>=0 and k<h.size():h[k]=float(roadbed[i+1]);fixed[k]=true
 return {"h":h,"roads":data.roads,"fixed":fixed}
# Height from whichever of the two tiles holds p (a millimetre of tolerance so
# both tiles make the same choice); NAN outside both.
func seam_sample(p:Vector2,original:PackedFloat64Array,other:PackedFloat64Array,shift:Vector2)->float:
 if p.x>=-1e-3 and p.y>=-1e-3 and p.x<=side+1e-3 and p.y<=side+1e-3:return sample_grid(original,grid_steps,side,p)
 var q=p-shift
 if q.x>=-1e-3 and q.y>=-1e-3 and q.x<=side+1e-3 and q.y<=side+1e-3:return sample_grid(other,grid_steps,side,q)
 return NAN
# Boundary nodes shared with a bundled neighbour. The road is chained across the
# edge for SMOOTH_SEAM_REACH_M on both sides and sampled at stations anchored on
# the crossing, so both tiles work from the same points and compute the same
# height for every shared node. The local pass never touches boundary nodes.
func seam_boundaries(original:PackedFloat64Array,fixed:Dictionary,sum_w:PackedFloat64Array,sum_wz:PackedFloat64Array,max_w:PackedFloat64Array)->int:
 if not info.has("id"):return 0
 # Chain boundary profiles only when the neighbour can do the same from its side.
 # A streamed tile next to a bundled one would move shared nodes that the bundled
 # tile never moves back, opening a step at the seam.
 if not FileAccess.file_exists("res://data/%d_%d.json"%[int(info.id[0]),int(info.id[1])]):return 0
 var n=grid_steps;var cell=side/n;var roads:Array=info.get("roads",[]);var touched={}
 var neighbours={}
 var window=3.*SMOOTH_SIGMA_M;var steps=ceili(window/SMOOTH_STEP_M)
 for road in roads:
  var own=metres(road[3])
  if own.size()<2:continue
  for end in [0,own.size()-1]:
   var p:Vector2=own[end];var uv=p/side
   var on_x=-1 if uv.x<1e-4 else (1 if uv.x>1.-1e-4 else 0)
   var on_y=-1 if uv.y<1e-4 else (1 if uv.y>1.-1e-4 else 0)
   if (on_x==0)==(on_y==0) or roads_ending_at(roads,p)!=1:continue
   var offset=Vector2i(on_x,on_y)
   if not neighbours.has(offset):neighbours[offset]=bundled_neighbour(offset)
   var nb:Dictionary=neighbours[offset]
   if nb.is_empty():continue
   var shift=Vector2(offset)*side;var across=p-shift
   # Exactly one road ends here on each side and both have the same width: the
   # neighbour applies the same rule, so both tiles either chain or both skip.
   var matches=[]
   for other in nb.roads:
    var pts=metres(other[3])
    if pts.size()<2:continue
    if pts[-1].distance_to(across)<.02:pts.reverse()
    if pts[0].distance_to(across)<.02:matches.append([pts,float(other[4])])
   if matches.size()!=1 or absf(matches[0][1]-float(road[4]))>.01:continue
   # Chain: our side walked back from the crossing, then the neighbour's side.
   var back=own.duplicate()
   if end!=0:back.reverse()
   back=truncated(back);back.reverse()
   var forward=truncated(matches[0][0])
   var chain=back.duplicate()
   for k in range(1,forward.size()):chain.append(forward[k]+shift)
   var starts=PackedFloat64Array();var length=0.
   for k in chain.size():
    if k>0:length+=chain[k].distance_to(chain[k-1])
    starts.append(length)
   var crossing=starts[back.size()-1]
   # Samples anchored on the crossing, identical from either tile.
   var S=PackedFloat64Array();var Z=PackedFloat64Array();var RC=PackedFloat64Array();var ONES=PackedFloat64Array()
   for k in range(-steps,steps+1):
    var s=crossing+k*SMOOTH_STEP_M
    if s<0. or s>length:continue
    var seg=0
    while seg<chain.size()-2 and s>starts[seg+1]:seg+=1
    var t=(chain[seg+1]-chain[seg]).normalized();var q=chain[seg]+t*(s-starts[seg])
    # The chain lies in our tile before the crossing and in the neighbour after it.
    var z=sample_grid(original,n,side,q) if s<=crossing else sample_grid(nb.h,n,side,q-shift)
    var left=Vector2(-t.y,t.x)*SMOOTH_LATERAL_OFFSET_M
    var zl=seam_sample(q+left,original,nb.h,shift);var zr=seam_sample(q-left,original,nb.h,shift)
    S.append(s);Z.append(z);ONES.append(1.);RC.append(0. if is_nan(zl) or is_nan(zr) else (zl-zr)/(2.*SMOOTH_LATERAL_OFFSET_M))
   if S.size()<4:continue
   var W=ONES.duplicate()
   for pass_index in 2:
    var spread=PackedFloat64Array()
    for i in S.size():spread.append(absf(Z[i]-local_linear(S,Z,W,S[i])))
    var ranked=spread.duplicate();ranked.sort()
    var scale=maxf(6.*1.4826*ranked[ranked.size()>>1],.12)
    for i in S.size():
     var r=spread[i]/scale
     W[i]=(1.-r*r)*(1.-r*r) if r<1. else 0.
   var half=float(road[4])*.5;var reach=half+SMOOTH_SHOULDER_M+SMOOTH_FALLOFF_M
   for j in range(1,n):
    var gx=(n if on_x>0 else 0) if on_x!=0 else j;var gy=(n if on_y>0 else 0) if on_y!=0 else j
    var idx=gy*(n+1)+gx
    var mirror=((n if on_x<0 else 0) if on_x!=0 else gx)+(n+1)*((n if on_y<0 else 0) if on_y!=0 else gy)
    if fixed.has(idx) or nb.fixed.has(mirror):continue
    var q=Vector2(gx,gy)*cell;var best=INF;var station=0.;var lateral=0.
    for k in chain.size()-1:
     var a=chain[k];var ab=chain[k+1]-a;var l2=ab.length_squared()
     if l2<1e-6:continue
     var f=clampf((q-a).dot(ab)/l2,0.,1.);var d=q.distance_to(a+ab*f)
     if d<best:best=d;station=starts[k]+f*sqrt(l2);lateral=(q-a).dot(Vector2(-ab.y,ab.x)/sqrt(l2))
    if best>reach or absf(station-crossing)>window:continue
    var z=local_linear(S,Z,W,station)
    if is_nan(z):continue
    var w=1.-smoothstep(half+SMOOTH_SHOULDER_M,reach,best)
    if w<=0.:continue
    var cross=clampf(local_linear(S,RC,ONES,station),-SMOOTH_LATERAL,SMOOTH_LATERAL)
    var change=SMOOTH_CAP_M*tanh((z+cross*lateral-original[idx])/SMOOTH_CAP_M)
    sum_w[idx]+=w;sum_wz[idx]+=w*(original[idx]+change);max_w[idx]=maxf(max_w[idx],w);touched[idx]=true
 return touched.size()
func point(a)->Vector2:return Vector2(a[0],a[1])*side
func vert(p:Vector2,h:float)->Vector3:return Vector3(p.x,h,p.y)
func tool(k):
 if not batches.has(k):
  var st=SurfaceTool.new();st.begin(Mesh.PRIMITIVE_TRIANGLES);st.set_material(materials[k]);batches[k]=st
 return batches[k]
func tri(st,a,b,c,uv1,uv2,uv3,n):
 var ps=[a,b,c];var uv=[uv1,uv2,uv3]
 if (b-a).cross(c-a).dot(n)>0:ps=[a,c,b];uv=[uv1,uv3,uv2]
 for i in 3:
  st.set_normal(n);st.set_uv(uv[i]);st.add_vertex(ps[i])
func quad(k,a,b,c,d,n,tiles=Vector2.ONE):
 var st=tool(k)
 tri(st,a,b,c,Vector2(tiles.x,tiles.y),Vector2(0,tiles.y),Vector2(0,0),n)
 tri(st,a,c,d,Vector2(tiles.x,tiles.y),Vector2(0,0),Vector2(tiles.x,0),n)
func mesh_node(mesh):
 var obj=MeshInstance3D.new();obj.mesh=mesh;add_child(obj);return obj
func svg_path(points:Array)->String:
 var row=PackedStringArray()
 for a in points:row.append("%.3f,%.3f"%[a[0]*side,a[1]*side])
 return "M"+" L".join(row)
func stroke(path:String,color:String,width:float,extra="")->String:
 return '<path d="%s" fill="none" stroke="%s" stroke-width="%.2f" stroke-linejoin="round" %s/>'%[path,color,width,extra]
func make_ground():
 if info.get("field",false):
  var image=Image.new()
  var path="res://data/field-photo-%d-%d.jpg"%[info.id[0],info.id[1]]
  if image.load_jpg_from_buffer(FileAccess.get_file_as_bytes(path))==OK:
   photo_sample=image.duplicate()
   photo_sample.resize(96,96,Image.INTERPOLATE_BILINEAR)
   image.generate_mipmaps()
   var photo=ImageTexture.create_from_image(image)
   var mat=ShaderMaterial.new();mat.shader=preload("res://FieldTerrain.gdshader")
   capture.bind_road(self,mat)
   mat.set_shader_parameter("photo",photo);mat.set_shader_parameter("grain",preload("res://art/asphalt.jpg"));mat.set_shader_parameter("meters",side)
   if materials.has("grass_detail"):mat.set_shader_parameter("grass_detail",materials.grass_detail)
   mat.set_shader_parameter("winter",info.get("region","")=="leaf-off")
   if materials.has("litter"):mat.set_shader_parameter("litter",materials.litter)
   var mask=Image.new()
   if mask.load_png_from_buffer(FileAccess.get_file_as_bytes("res://data/field-pavement-%d-%d.png"%[info.id[0],info.id[1]]))==OK:
    surface_mask=mask
    mask.generate_mipmaps()
    mat.set_shader_parameter("pavement",ImageTexture.create_from_image(mask))
   var roof=StandardMaterial3D.new();roof.albedo_texture=photo;roof.roughness=.94;roof.cull_mode=BaseMaterial3D.CULL_DISABLED;materials["field_roof"]=roof
   for road in info.roads:
    for i in range(1,road[3].size()):
     var a=point(road[3][i-1]);var b=point(road[3][i])
     if a.distance_squared_to(b)>.01:segments.append([a,b,road[2],road[4],road[1],road[0]])
   ground_mesh(mat)
   return

 var im=street_image()
 var mat=ShaderMaterial.new();mat.shader=preload("res://terrain.gdshader");mat.set_shader_parameter("surface_map",ImageTexture.create_from_image(im));mat.set_shader_parameter("grit",preload("res://art/asphalt.jpg"));mat.set_shader_parameter("meters",side)
 if materials.has("mineral_detail"):
  mat.set_shader_parameter("mineral_detail",materials.mineral_detail)
  mat.set_shader_parameter("mineral_origin",Vector2(fposmod(float(info.id[0])*side,2.53),fposmod(float(info.id[1])*side,2.53)))
 if measured_section:measured_section.bind(mat)
 streaming_ground=mat
 if info.get("live_canopy",false):
  streamed_roof=ShaderMaterial.new()
  streamed_roof.shader=preload("res://SurveyRoof.gdshader")
  streamed_roof.set_shader_parameter("tile_side",side)
  streamed_roof.set_shader_parameter("parent_quadrant",Vector2(int(info.id[0])%4,int(info.id[1])%4))
  materials["streamed_roof"]=streamed_roof
 if info.get("live_canopy",false) and info.get("aerial_key","")!="":
  road_prior=preload("res://RoadEdges.gd").geometric_prior(info.roads,side)
 var mx=floori(float(info.id[0])/4);var my=floori(float(info.id[1])/4);var resource="res://art/a-%d-%d.jpg"%[mx,my];var photo=null
 if ResourceLoader.exists(resource):photo=load(resource)
 elif info.get("aerial","")!="":
  var directory=info.get("_source_cache",ProjectSettings.globalize_path("user://map-cache"))
  var imap=read_aerial(directory,str(info.aerial))
  if imap and not imap.is_empty():
   prepare_pixels(imap)
   imap.generate_mipmaps();photo=ImageTexture.create_from_image(imap)
  else:info["aerial"]=""
 if photo:
  update_roof_photo(photo)
  mat.set_shader_parameter("ortho",photo);mat.set_shader_parameter("quadrant",Vector2(int(info.id[0])%4,int(info.id[1])%4));mat.set_shader_parameter("imagery",true)
 ground_mesh(mat)
# Generates the semantic pavement mask and centerline segments together. Both
# the first tile and a later measured-paint update use this geometry.
func street_image()->Image:
 var svg='<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 %f %f"><rect width="100%%" height="100%%" fill="#429a31"/>'%[side,side]
 for area in info.land:
  var color="#429a31" if area[0]!="water" else "#4e7782"
  var paths=""
  for r in area[2]:paths+=svg_path(r)+" Z "
  svg+='<path d="%s" fill="%s" fill-rule="evenodd"/>'%[paths,color]
 # Reserve magenta in this semantic texture for mapped outdoor paths. Motor
 # roads are drawn afterward, including intersections with pedestrian lines.
 for walk in info.get("pedestrian",[]):
  svg+=stroke(svg_path(walk[2]),"#ff00ff",float(walk[1]),'stroke-linecap="butt"')
 var pavements="";var curbs="";var asphalt="";var lines="";var crosswalks=""
 var junctions={}
 var loose_strokes=""
 var paved_strokes=""
 for road in info.roads:
  var route=road[3];var w=float(road[4]);var path=svg_path(route);var main=road[0] in ["primary","secondary","trunk","motorway","tertiary"]
  # A bridge's pavement and paint are on its deck (BridgeDeck.gd), not on the ground below.
  var on_deck=road.size()>6 and road[6]=="bridge" and not info.get("bridges",[]).is_empty()
  if on_deck:
   for j in range(1,route.size()):
    var a=point(route[j-1]);var b=point(route[j])
    if a.distance_to(b)>=.1:segments.append([a,b,road[2],w,road[1],road[0]])
   continue
  var loose_surface=road.size()>5 and road[5]=="unpaved"
  if not loose_surface and not info.get("live_canopy",false) and road[0] not in ["track","service"]:
   pavements+=stroke(path,"#a7a39a",w+5.0,'stroke-linecap="round"');curbs+=stroke(path,"#c9c4ae",w+.65,'stroke-linecap="round"')
  var surface_code="#00ffff" if loose_surface else ("#a0947a" if road[0]=="track" else "#484a49")
  var surface_stroke=stroke(path,surface_code,w,'stroke-linecap="round"')
  if loose_surface:loose_strokes+=surface_stroke
  else:
   asphalt+=surface_stroke
   if road.size()>5 and road[5]=="paved":paved_strokes+=surface_stroke
  for j in range(1,route.size()):
   var a=point(route[j-1]);var b=point(route[j])
   if a.distance_to(b)<.1:continue
   var segment=[a,b,road[2],w,road[1],road[0]]
   if road.size()>5 and road[5] in ["paved","unpaved"]:
    segment.append(road[5])
    if road[5]=="unpaved":has_mapped_unpaved=true
   segments.append(segment)
   if main and not loose_surface and a.distance_to(b)>22:
    var t=(b-a).normalized();var n=Vector2(-t.y,t.x);var inset=minf(8,a.distance_to(b)*.2)
    var aa=a+t*inset;var bb=b-t*inset;var p='M%.2f,%.2f L%.2f,%.2f'%[aa.x,aa.y,bb.x,bb.y]
    if road[1]:
     if paint_evidence!=null and road[2]==paint_evidence.road_name:lines+=paint_evidence.dashed_stroke(self,aa,bb)
     elif measured_section and road[2]==measured_section.road_name:lines+=measured_section.dashed_stroke(self,aa,bb)
     elif road_layout and road_layout.applies(road[2]):lines+=road_layout.dashed_stroke(self,aa,bb,road[2])
     else:lines+=stroke(p,"#e2dfcd",.18,'stroke-dasharray="3 7"')
    else:
     for off in [-.17,.17]:
      var v=n*off
      var parts=road_survey.remaining(aa+v,bb+v) if road_survey else [[aa+v,bb+v]]
      for part in parts:
       lines+=stroke('M%.2f,%.2f L%.2f,%.2f'%[part[0].x,part[0].y,part[1].x,part[1].y],"#d5ac62",.13)
    for end in [a,b]:
     var id=Vector2i((end*2).round());junctions[id]=junctions.get(id,0)+1
 for seg in segments:
  if seg[5] not in ["primary","secondary","tertiary"]:continue
  var a:Vector2=seg[0];var b:Vector2=seg[1];if a.distance_to(b)<30:continue
  var t=(b-a).normalized();var n=Vector2(-t.y,t.x)
  if info.get("live_canopy",false) or junctions.get(Vector2i((a*2).round()),0)<2:continue
  var center=a+t*7
  for j in range(-4,5):
   var m=center+n*j*(seg[3]/10.0)
   crosswalks+=stroke('M%.2f,%.2f L%.2f,%.2f'%[m.x-t.x*1.1,m.y-t.y*1.1,m.x+t.x*1.1,m.y+t.y*1.1],"#d3ceba",.48)
 # At mixed junctions the confirmed paved road stays visible across the gravel.
 if not loose_strokes.is_empty():asphalt+=loose_strokes+paved_strokes
 svg+=pavements+curbs+asphalt+lines+crosswalks+"</svg>"
 var im=Image.new();im.load_svg_from_string(svg);im.generate_mipmaps()
 return im

func take_lane_meshes()->Array:
 var meshes=[]
 for key in ["whitepaint","yellowpaint"]:
  if batches.has(key):
   meshes.append(batches[key].commit())
   batches.erase(key)
 return meshes

func install_lane_meshes(meshes:Array):
 var replacement=Node3D.new()
 for mesh in meshes:
  var stripe=MeshInstance3D.new()
  stripe.mesh=mesh
  replacement.add_child(stripe)
 add_child(replacement)
 if lane_paint_layer:
  remove_child(lane_paint_layer)
  lane_paint_layer.queue_free()
 lane_paint_layer=replacement

# The worker owns this temporary tile. It never builds a collider or replaces
# any driving, aerial imagery, tree, building or camera state.
func prepare_lane_update(data:Dictionary,size_m:float,mats:Dictionary,observations:Array)->Dictionary:
 info=data
 side=size_m
 materials=mats
 grid_steps=int(data.get("grid",32))
 paint_evidence=observations[0]
 measured_section=observations[1]
 road_survey=observations[2]
 surveyed_outcrop=observations[3]
 crossfall_surface=observations[4]
 road_layout=preload("res://RoadLayout.gd").for_tile(self)
 if not road_layout:return {}
 var surface=street_image()
 paint_lane_marks()
 return {"surface":ImageTexture.create_from_image(surface),"meshes":take_lane_meshes(),"layout":road_layout}

func install_lane_update(update:Dictionary):
 if update.is_empty() or not streaming_ground:return
 streaming_ground.set_shader_parameter("surface_map",update.surface)
 install_lane_meshes(update.meshes)
 road_layout=update.layout

func apply_recent(texture,soil=null):
 if streaming_ground:
  streaming_ground.set_shader_parameter("has_soil",texture!=null and soil!=null)
  streaming_ground.set_shader_parameter("bare_mask",soil)
 if not terrain_material:return
 terrain_material.set_shader_parameter("use_recent",texture!=null)
 if texture:terrain_material.set_shader_parameter("recent_image",texture)
func ground_mesh(mat):
 terrain_material=mat
 if road_surface:
  var ribbon=road_surface.mesh(mat,side);mesh_node(ribbon);collider(ribbon.create_trimesh_shape())
 preload("res://GroundFinish.gd").bind(mat,materials,info.id,side)
 mat.set_shader_parameter("recent_quadrant",Vector2(int(info.id[0])%4,int(info.id[1])%4))
 # A 128 grid is drawn adaptively: cells split only where the detail differs
 # from the coarse surface (road benches, surveyed banks), so flat or untouched
 # ground costs what a 64 grid did.
 if info.get("bank_detail_active",false) or (grid_steps==128 and info.h.size()==129*129):
  var fine=preload("res://RoadBankMesh.gd").make(info.h,side,mat)
  if surveyed_outcrop:
   make_outcrop_ground(fine.surface_get_arrays(0),mat)
  elif crossfall_surface:
   make_crossfall_ground(fine.surface_get_arrays(0),mat)
  else:
   mesh_node(fine)
   collider(fine.create_trimesh_shape())
  return
 var plane=PlaneMesh.new();plane.size=Vector2(side,side);plane.subdivide_width=grid_steps-1;plane.subdivide_depth=grid_steps-1
 var ar=plane.get_mesh_arrays();var v:PackedVector3Array=ar[Mesh.ARRAY_VERTEX];var u=PackedVector2Array()
 for i in v.size():
  var p=Vector2(v[i].x+side/2,v[i].z+side/2);v[i]=vert(p,height_at(p));u.append(p/side)
 var normals=PackedVector3Array()
 var delta=side/grid_steps
 for vertex in v:
  var p=Vector2(vertex.x,vertex.z)
  var slope=Vector3(height_at(p-Vector2.RIGHT*delta)-height_at(p+Vector2.RIGHT*delta),2*delta,height_at(p-Vector2.DOWN*delta)-height_at(p+Vector2.DOWN*delta))
  normals.append(slope.normalized())
 ar[Mesh.ARRAY_NORMAL]=normals;ar[Mesh.ARRAY_VERTEX]=v;ar[Mesh.ARRAY_TEX_UV]=u
 if surveyed_outcrop:
  make_outcrop_ground(ar,mat)
  return
 if crossfall_surface:
  make_crossfall_ground(ar,mat)
  return
 var ground=ArrayMesh.new();ground.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,ar);ground.surface_set_material(0,mat);mesh_node(ground)
 collider(ground.create_trimesh_shape())
func make_crossfall_ground(arrays:Array,mat:ShaderMaterial)->void:
 var ground=ArrayMesh.new()
 ground.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,crossfall_surface.refine(self,arrays))
 if not crossfall_surface.active:crossfall_surface=null
 ground.surface_set_material(0,mat);mesh_node(ground);collider(ground.create_trimesh_shape())
func make_outcrop_ground(arrays:Array,mat:ShaderMaterial)->void:
 var ground=ArrayMesh.new()
 ground.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,surveyed_outcrop.refine(self,arrays))
 ground.surface_set_material(0,mat);mesh_node(ground);collider(ground.create_trimesh_shape())
 preload("res://MeasuredGranite.gd").attach(self)
var road_grid={}
var has_mapped_unpaved=false
func road_index():
 for seg in segments:
  var reach=Vector2.ONE*(float(seg[3])*.5+2.2);var lo=Vector2i(((seg[0].min(seg[1])-reach)/24.).floor());var hi=Vector2i(((seg[0].max(seg[1])+reach)/24.).floor())
  for x in range(lo.x,hi.x+1):
   for y in range(lo.y,hi.y+1):
    var key=Vector2i(x,y)
    if not road_grid.has(key):road_grid[key]=[]
    road_grid[key].append(seg)
# Wheel-only adjustment. Land-cover users still identify the full roadway for
# placement exclusions. The source gives a surface class, not a measured tire mu.
func wheel_grip(p:Vector2):
 if measured_section:
  var measured:float=measured_section.surface(p,side)
  if measured>=0.:return measured
 var base=surface_grip(p)
 if not has_mapped_unpaved:return base
 var closest=INF
 var loose=false
 var core=false
 for seg in road_grid.get(Vector2i((p/24.).floor()),[]):
  if seg.size()<7:continue
  var q=Geometry2D.get_closest_point_to_segment(p,seg[0],seg[1])
  var distance=q.distance_to(p)
  if distance>float(seg[3])*.5 or distance>closest:continue
  if is_equal_approx(distance,closest) and seg[6]!="paved":continue
  closest=distance
  loose=seg[6]=="unpaved"
  core=distance<float(seg[3])*.35
 if not loose:return base
 return .76 if core else minf(base,.76)

func surface_grip(p:Vector2):
 if capture.road_confidence(p/side)>.52:return 1.
 if learned_edges:
  var c=sample_surface(learned_edges,p)
  if c.g>.03:return 1. if c.r>.5 else .65
 if surface_mask:
  var c=sample_surface(surface_mask,p)
  if c.r>.55 or c.a>.86:return 1.
  if c.a>.63:return .76
 var best=.65
 for seg in road_grid.get(Vector2i((p/24.).floor()),[]):
  var q=Geometry2D.get_closest_point_to_segment(p,seg[0],seg[1])
  if q.distance_to(p)<=seg[3]*.5+(.25 if info.get("field",false) else 2.1):
   if seg[5]!="track":return 1.
   best=.76
 return best
func paint_lane_marks():
 for seg in segments:
  if seg.size()>6 and seg[6]=="unpaved":continue
  if seg[5] not in ["motorway","trunk","primary","secondary","tertiary"] or seg[3]<7.3:continue
  var a:Vector2=seg[0];var b:Vector2=seg[1];var length=a.distance_to(b);if length<5:continue
  var t=(b-a)/length;var n=Vector2(-t.y,t.x)
  var offsets=[0.] if seg[4] else [-.115,.115]
  var dash=3. if seg[4] else 5.;var step=9. if seg[4] else 5.
  for off in offsets:
   var k=2.5
   while k<length-2.5:
    var p=a+t*k+n*off;var q=a+t*minf(length-2.5,k+dash)+n*off;var l=n*.067
    if paint_evidence!=null and seg[4] and seg[2]==paint_evidence.road_name:
     for part in paint_evidence.remaining(p,q):
      if part.size()>=2:raised_stripe("whitepaint",part[0],part[-1],l)
    elif measured_section and seg[4] and seg[2]==measured_section.road_name:
     for part in measured_section.remaining(p,q):
      if part.size()>1:raised_stripe("whitepaint",part[0],part[-1],l)
    elif road_layout and seg[4] and road_layout.applies(seg[2]):
     for part in road_layout.remaining(p,q,seg[2]):raised_stripe("whitepaint",part[0],part[1],l)
    elif road_survey and not seg[4]:
     for part in road_survey.remaining(p,q):raised_stripe("yellowpaint",part[0],part[1],l)
    else:raised_stripe("whitepaint" if seg[4] else "yellowpaint",p,q,l)
    k+=step
func raised_stripe(material_key:String,p:Vector2,q:Vector2,half_width:Vector2):
 quad(material_key,vert(p-half_width,height_at(p-half_width)+.048),vert(p+half_width,height_at(p+half_width)+.048),vert(q+half_width,height_at(q+half_width)+.048),vert(q-half_width,height_at(q-half_width)+.048),Vector3.UP)
func nearest(p:Vector2,main_only=false,street=""):
 var best=INF;var out=[]
 for seg in segments:
  if street!="" and str(seg[2]).nocasecmp_to(street)!=0:continue
  if main_only and seg[5] in ["service","track"]:continue
  var q=Geometry2D.get_closest_point_to_segment(p,seg[0],seg[1]);var d=p.distance_squared_to(q)
  if d<best:best=d;out=[q,seg,sqrt(d)]
 return out
func make_building(b,idx):
 if JUNCTION.contains_canopy(self,b) or capture.store(self,b,idx):return
 var observed=preload("res://ObservedBuildings.gd").find(self,idx)
 var archived=info.get("enriched_finishes",{}).get(idx,{})
 if not archived.is_empty():
  var combined=archived.style.duplicate()
  combined.merge(observed,true)
  observed=combined
 var measured=LIDAR_ROOF.find(self,b)
 var photo=photographs.find(self,b,measured)
 var survey_use=info.get("enriched_uses",{}).get(idx,{}).get("use","")
 var detached=false
 if info.get("live_canopy",false) and b[3]<.1 and b[2]<=10.:
  var ring=b[0][0]
  var area=0.
  for i in ring.size():area+=point(ring[i]).cross(point(ring[(i+1)%ring.size()]))
  var use=str(b[6]) if b.size()>6 else ""
  var subtype=str(b[7]) if b.size()>7 else ""
  detached=(use in ["house","residential","detached"] or subtype in ["house","residential","detached"] or (absf(area)*.5<260. and use in ["","building","yes"] and subtype in ["","building","yes"]))
 if survey_use!="":detached=survey_use in ["single_family","duplex"] and b[2]<=15. and b[3]<.1
 if photo.is_empty() and (info.get("field",false) or detached):
  survey_building(b,idx,observed,measured)
  return
 var rings=b[0];var c=Vector2(b[4],b[5])*side;var base=height_at(c);var top=base+clampf(b[2],.8 if info.get("enriched_heights",{}).has(idx) else 2.8,650.);var bottom=base+float(b[3]);var seed_id=absi(hash(Vector3i(info.id[0],info.id[1],idx)))
 if not photo.is_empty():photographs.draw_entrance(self,photo,base)
 var fac=WALL[seed_id%WALL.size()];var shop=SHOP[seed_id%SHOP.size()]
 var road=nearest(c,true);var commercial=road.size()>0
 if survey_use!="" and survey_use!="nonresidential":commercial=false
 if b.size()>6 and b[6] in ["surveyed_footprint","corroborated_footprint"]:
  # The outline adds no evidence for a facade finish or an active storefront.
  commercial=false;fac="siding0"
 var pts=[];var outline=PackedVector2Array()
 for a in rings[0]:outline.append(point(a))
 var bounds=Rect2(outline[0],Vector2.ZERO)
 for p in outline:bounds=bounds.expand(p)
 commercial=commercial and preload("res://MappedStorefront.gd").accepts(b,info.get("pois",[]),outline,bounds,side)
 if b[3]<1:footprints.append([bounds,outline])
 var finishes=preload("res://ObservedBuildings.gd")
 if not observed.is_empty():
  fac=finishes.material(self,observed,"wall",fac)
  shop=finishes.material(self,observed,"wall",shop)
 if measured.has("projection"):
  commercial=false
  fac=preload("res://SurveyProjection.gd").material(self,measured,base,"wall",fac)
 var roof_key=finishes.material(self,observed,"roof",roof_base_key(b))
 var roof_axes=[]
 var roof_rise=0.
 if not measured.is_empty():top=base+float(measured.eave_m)
 elif not photo.is_empty():top=base+photo.eave_above_center
 elif observed.get("shape","")=="gabled":
  roof_axes=finishes.roof_axes(outline,observed)
  roof_rise=clampf(float(observed.get("roof_height",roof_axes[2]*.45)),0.,maxf(0.,top-bottom-2.5))
  top-=roof_rise
 if photo.is_empty() and not measured.has("projection") and not observed.has("wall_material") and not observed.has("wall_color"):
  wall_patches.add_if_matched(self,b,top)
 var historic=preload("res://HistoricAnchorage.gd")
 var dated_wall=not measured.is_empty() and historic.matches(info.id,b,measured) and historic.enabled(OS.get_cmdline_user_args())
 var outer:Array=rings[0];var front=-1;var closest=INF
 if commercial:
  for j in outer.size():
   var aa=point(outer[j]);var bb=point(outer[(j+1)%outer.size()]);if aa.distance_to(bb)<8:continue
   var nr=nearest((aa+bb)/2,true);if nr.size() and nr[2]<closest:closest=nr[2];front=j
 for ring in rings:
  for j in ring.size():
   var a=point(ring[j]);var d=point(ring[(j+1)%ring.size()]);pts.append(vert(a,top));var length=a.distance_to(d);if length<.02:continue
   var vec=(d-a)/length;var norm=Vector3(vec.y,0,-vec.x)
   if b[3]<2:
    var v0=vert(a,bottom-2);var v1=vert(d,bottom-2);var v2=vert(d,top);var v3=vert(a,top)
    collision_faces.append_array(PackedVector3Array([v0,v2,v1,v0,v3,v2]))
   if not photo.is_empty() and ring==rings[0] and photographs.draw_edge(self,photo,j,(j+1)%ring.size(),base,top,norm):continue
   var lo=minf(bottom,height_at(a))-1.3 if b[3]<1 else bottom
   var hi=top-.17;var storefront=commercial
   var out=Vector2(norm.x,norm.z);if ((a+d)/2-c).dot(out)<0:out=-out
   var local_front=road.size()>0 and (road[0]-(a+d)*.5).dot(out)>0
   storefront=storefront and ring==outer and j==front and local_front
   var low_top=minf(hi-.01,maxf(base+3.4,minf(base+4.1,top-1.3)))
   if storefront:
    quad(shop,vert(a,lo),vert(d,lo),vert(d,low_top),vert(a,low_top),norm,Vector2(maxi(1,roundi(length/8)),1))
    lo=low_top
    if local_front and length>4 and length<45:
     var lip=out*.21;var y=low_top+.055
     quad("trim",vert(a+lip,y-.14),vert(d+lip,y-.14),vert(d+lip,y),vert(a+lip,y),norm)
     quad("cap",vert(a,y),vert(d,y),vert(d+lip,y),vert(a+lip,y),Vector3.UP)
     if seed_id%4==0:
      var aa=a.lerp(d,.07);var bb=a.lerp(d,.93);var e=out*.73;var shade="awning"+str(seed_id%3)
      quad(shade,vert(aa+e,y-.45),vert(bb+e,y-.45),vert(bb,y),vert(aa,y),Vector3.UP)
      quad(shade,vert(aa+e,y-.62),vert(bb+e,y-.62),vert(bb+e,y-.45),vert(aa+e,y-.45),norm)

   var surface=fac
   var repeats=Vector2(maxi(1,roundi(length/9)),maxi(1,roundi((hi-lo)/6)))
   if not (dated_wall and ring==outer and historic.draw(self,j,a,d,norm,lo,hi,top,surface,repeats)):
    quad(surface,vert(a,lo),vert(d,lo),vert(d,hi),vert(a,hi),norm,repeats)
   quad("cap",vert(a,hi),vert(d,hi),vert(d,top),vert(a,top),norm)
 if not measured.is_empty():
  LIDAR_ROOF.draw(self,measured,base,roof_key,fac)
  return
 if roof_rise>.01:
  finishes.gable(self,b,roof_axes,top,roof_rise,roof_key,fac)
  return
 var triangles=b[1];var st=tool(roof_key)
 for j in range(0,triangles.size(),3):
  tri(st,pts[int(triangles[j])],pts[int(triangles[j+1])],pts[int(triangles[j+2])],Vector2.ZERO,Vector2.RIGHT,Vector2.ONE,Vector3.UP)
func occupied(p:Vector2):
 for f in footprints:
  if f[0].has_point(p) and Geometry2D.is_point_in_polygon(p,f[1]):return true
 return false
var grove=[]
var forest_form
var unsurveyed_canopy := PackedInt32Array()
var crowns_waiting_for_edges: Array = []
func sample_surface(image:Image,p:Vector2)->Color:
 return image.get_pixel(clampi(floori(p.x/side*image.get_width()),0,image.get_width()-1),clampi(floori(p.y/side*image.get_height()),0,image.get_height()-1))
func foliage(p:Vector2,h:float,idx,dbh=0.0,species="",crown_width=0.0):
 var seed_id=absi(hash(Vector3i(roundi(p.x*8),roundi(p.y*8),idx)))
 var width=h*lerpf(.70,1.11,(seed_id%100)/100.)
 var sp=str(species).to_lower()
 if "crape" in sp or "dogwood" in sp:width*=1.10
 elif "ginkgo" in sp or "pear" in sp:width*=.73
 var kind=0
 if info.get("field",false) or crown_width>0:
  var roll=seed_id%100
  kind=0 if roll<65 else 1
  if info.get("region","")=="leaf-off":
   var color=sample_surface(photo_sample,p) if photo_sample else Color.GRAY
   var evergreen=color.g>color.r*1.07 and color.g>color.b*1.08
   kind=2 if evergreen or roll<11 else 3
  elif roll>89 and h>6:kind=2
 if crown_width>0:width=crown_width
 grove.append([vert(p,height_at(p)),h,fmod(seed_id*.013,TAU),width,dbh if dbh>0 else h*.032,kind])
func replace_crowns(rows):
 if road_prior!=null and learned_edges==null:crowns_waiting_for_edges=rows
 grove.clear()
 unsurveyed_canopy.clear()
 var survey_counts: Dictionary = {}
 for i in rows.size():
  var rec=rows[i]
  var p=point(rec)
  var surveyed=rec.size()>=12
  var recovered=false
  var fallback:Array=[]
  if surveyed and surface_grip(p)>.8 and preload("res://SurveyedTreeWell.gd").permits(self,p,rec):
   recovered=true
   var peak=point([rec[7],rec[8]])
   if not occupied(peak) and surface_grip(peak)<=.8:
    foliage(peak,clampf(rec[2],2.5,78.),i,0.0,"",rec[3])
    fallback=grove.pop_back()
  if surveyed and (occupied(p) or surface_grip(p)>.8 and not recovered):
   if rec[10]!="matched":continue
   # A map disagreement cannot move the measured trunk into traffic or a wall.
   p=point([rec[7],rec[8]])
   surveyed=false
  if occupied(p) or surface_grip(p)>.8 and not recovered:continue
  if surveyed:
   foliage(p,clampf(rec[2],2.5,78.),i,rec[4],rec[5],rec[3])
   grove[-1][5]=int(rec[6])
   if rec.size()==13 and rec[10]=="matched" and rec[12] is Dictionary:
    grove[-1].append(rec[12])
    if recovered:grove[-1].append(fallback)
   var provider=str(rec[11])
   survey_counts[provider]=int(survey_counts.get(provider,0))+1
  else:
   foliage(p,clampf(rec[2],2.5,78.),i,0.0,"",rec[3])
   if rec.size()==4:unsurveyed_canopy.append(grove.size()-1)
 apply_forest_form()
 info["tree_inventories"]=survey_counts
func apply_forest_form()->int:
 if forest_form==null:return 0
 var changed:=0
 for index in unsurveyed_canopy:
  var row:Array=grove[index]
  var uv=Vector2(row[0].x,row[0].z)/side
  # A mapped stand describes a majority; retain the existing form on one in eight peaks.
  var anchor_key=Vector2i(roundi(uv.x*16384.),roundi(uv.y*16384.))
  if row[5]!=4 and forest_form.conifer(uv) and absi(hash(anchor_key))%8!=0:
   row[5]=4
   changed+=1
 info["forest_form_crowns"]=changed
 return changed

func build(data:Dictionary,size_m:float,mats:Dictionary):
 preload("res://SurveyGroundPatch.gd").apply(data)
 preload("res://RoadBankMesh.gd").apply(data)
 info=data;side=size_m;materials=mats.duplicate();grid_steps=int(data.get("grid",32))
 if info.get("live_canopy",false) and not info.get("field",false) and info.has("id"):
  forest_form=preload("res://ForestForm.gd").for_tile(Vector2i(int(info.id[0]),int(info.id[1])))
  if forest_form:
   info["forest_form_status"]="bundled"
   info["forest_form_years"]=forest_form.years
 # A surveyed field tile can carry a sparse common road grade ("roadbed") for both
 # the textured mesh and its physics collider. Other tiles use their source DEM.
 var roadbed: Array = data.get("roadbed",[])
 for i in range(0,roadbed.size()-1,2):
  info.h[int(roadbed[i])] = roadbed[i+1]
 smooth_road_corridors()
 # Engineered road ribbons (RoadSurface.gd): the grid is pushed below them first.
 road_surface=preload("res://RoadSurface.gd").for_tile(self)
 if road_surface:info["road_surface_lowered"]=road_surface.lower_grid(info.h,grid_steps,side)
 surveyed_outcrop=preload("res://SurveyOutcropGround.gd").for_tile(self)
 if not surveyed_outcrop:crossfall_surface=preload("res://RoadCrossfallGround.gd").for_tile(self)
 capture.load_tile(self)
 measured_section=preload("res://MeasuredSection.gd").for_tile(self)
 road_survey=preload("res://RoadSurvey.gd").for_tile(self)
 paint_evidence=preload("res://PaintEvidence.gd").for_tile(self)
 road_layout=preload("res://RoadLayout.gd").for_tile(self)
 make_ground();road_index()
 # Decks for mapped bridges; the ground under them stays the valley or road below.
 preload("res://BridgeDeck.gd").build(self)
 # Guard rails where the ground drops away beside a road ribbon (GuardRails.gd).
 preload("res://GuardRails.gd").build(self)
 if not info.get("field",false):
  paint_lane_marks()
  if info.get("live_canopy",false):install_lane_meshes(take_lane_meshes())
  if road_layout:road_layout.draw(self)
  if paint_evidence!=null:paint_evidence.draw(self)
  if road_survey:road_survey.draw(self)
  if measured_section:measured_section.draw(self)
 else:survey_paint()
 if info.get("live_canopy",false):
  info["_base_structures"]=info.buildings
  structure_layer=get_script().new()
  structure_layer.build_structures(info,side,materials,segments)
  add_child(structure_layer)
  footprints=structure_layer.footprints
 else:
  for i in info.buildings.size():make_building(info.buildings[i],i)
 JUNCTION.build(self)
 var tree_list=info.trees
 for i in tree_list.size():
  var t=tree_list[i];foliage(Vector2(t[0],t[1])*side,clampf(t[2],3,36),i,t[3] if t.size()>3 else 0,t[4] if t.size()>4 else "",t[5] if t.size()>5 else 0.)
 if info.has("crowns"):replace_crowns(info.crowns)
 # Trees without a city inventory are illustrative and stay clear of building footprints.
 if tree_list.is_empty() and not info.get("field",false) and not info.get("live_canopy",false):
  for i in range(0,segments.size(),2):
   var s=segments[i];if s[5] not in ["minor","secondary","tertiary"]:continue
   var t=(s[1]-s[0]).normalized();var n=Vector2(-t.y,t.x)
   for m in range(14,int(s[0].distance_to(s[1]))-6,24):
    var p=s[0]+t*m+n*(s[3]/2+2.5)*(-1 if (m+i)%3==0 else 1)
    if not occupied(p):foliage(p,6.2+float(i%5),i)
 if not info.get("field",false) and not info.get("live_canopy",false):street_details()
 finish_geometry()

func build_structures(data,size_m,shared_materials,road_segments):
 info=data
 side=size_m
 materials=shared_materials.duplicate()
 grid_steps=int(info.get("grid",32))
 segments=road_segments
 for i in info.buildings.size():make_building(info.buildings[i],i)
 finish_geometry()

func replace_structure_layer(replacement):
 var previous=structure_layer
 if previous:
  remove_child(previous)
  previous.queue_free()
 add_child(replacement)
 structure_layer=replacement
 footprints=replacement.footprints
 info.buildings=replacement.info.buildings
 info["live_roof_profiles"]=replacement.info.get("live_roof_profiles",{})
 info["enriched_heights"]=replacement.info.get("enriched_heights",{})
 info["enriched_uses"]=replacement.info.get("enriched_uses",{})
 info["enriched_finishes"]=replacement.info.get("enriched_finishes",{})
 info["surveyed_footprints"]=replacement.info.get("surveyed_footprints",{})
 info["corroborated_footprints"]=replacement.info.get("corroborated_footprints",{})

func finish_geometry():
 if collision_faces.size()>0:
  var shape=ConcavePolygonShape3D.new();shape.backface_collision=true;shape.set_faces(collision_faces);collider(shape);collision_faces.clear()
 for st in batches.values():mesh_node(st.commit())
 batches.clear()

func post(a:Vector3,b:Vector3,r1:float,r2:float,key,num=7):
 var axis=(b-a).normalized();var x=axis.cross(Vector3.FORWARD)
 if x.length_squared()<.05:x=axis.cross(Vector3.RIGHT)
 x=x.normalized();var y=axis.cross(x).normalized()
 for j in num:
  var n=x*cos(TAU*j/num)+y*sin(TAU*j/num);var m=x*cos(TAU*(j+1)/num)+y*sin(TAU*(j+1)/num)
  quad(key,a+n*r1,a+m*r1,b+m*r2,b+n*r2,(n+m).normalized())
func street_details():
 # Inventory streetlights in the playable map. The fixtures use a shared batched mesh.
 for rec in info.get("lamps",[]):
  var p=point(rec);var nr=nearest(p,true);if nr.is_empty() or nr[2]>16:continue
  var d=(nr[0]-p).normalized();if d.length_squared()<.5:continue
  # Align measured fixtures to the modeled sidewalk where road-width estimates overlap them.
  if nr[2]<nr[1][3]*.5+.48:p=nr[0]-d*(nr[1][3]*.5+.48)
  var h=height_at(p);var o=vert(p,h)
  var toward=Vector3(d.x,0,d.y)
  post(o,o+Vector3.UP*.56,.128,.09,"iron")
  post(o+Vector3.UP*.55,o+Vector3.UP*5.2,.072,.042,"iron")
  var a=o+Vector3.UP*5.05;var b=a+toward*.53+Vector3.UP*.30;var end=b+toward*.67
  post(a,b,.047,.041,"iron");post(b,end,.042,.042,"iron")
  post(end-Vector3.UP*.09,end+Vector3.UP*.035,.21,.14,"iron");post(end-Vector3.UP*.13,end-Vector3.UP*.09,.12,.19,"globe")
 for idx in segments.size():
  var seg=segments[idx];if seg[5] not in ["primary","secondary","tertiary","minor"]:continue
  var a:Vector2=seg[0];var b:Vector2=seg[1];var length=a.distance_to(b);if length<22:continue
  var t=(b-a)/length;var n=Vector2(-t.y,t.x);var wide=seg[3]/2
  for signum in [-1,1]:
   var out=n*signum;var offset=out*wide;var inner=out*.16;var endcap=minf(length*.13,7)
   var aa=a+t*endcap+offset;var bb=b-t*endcap+offset
   for j in range(ceili(aa.distance_to(bb)/5)):
    var step=5/maxf(.01,aa.distance_to(bb));var u=aa.lerp(bb,minf(1,j*step));var v=aa.lerp(bb,minf(1,(j+1)*step));var hu=height_at(u)+.14;var hv=height_at(v)+.14
    quad("curb",vert(u,hu-.15),vert(v,hv-.15),vert(v,hv),vert(u,hu),Vector3(-out.x,0,-out.y))
    var curb0=vert(u,hu-.16);var curb1=vert(v,hv-.16);var curb2=vert(v,hv);var curb3=vert(u,hu)
    var outeru=vert(u+out*2.05,hu);var outerv=vert(v+out*2.05,hv)
    collision_faces.append_array(PackedVector3Array([curb0,curb1,curb2,curb0,curb2,curb3,curb3,curb2,outerv,curb3,outerv,outeru]))
    quad("walk",vert(u,hu),vert(v,hv),vert(v+out*2.05,hv),vert(u+out*2.05,hu),Vector3.UP,Vector2(3,1))
   if seg[5]=="minor" or length<36:continue
   for j in range(16+idx%9,int(length)-10,27+(idx%3)*5):
    var p=a+t*j+out*(wide-1.05)
    if occupied(p):continue
    var own=nearest(p,true);if own.size() and own[2]<wide-1.7:continue
    var rnd=absi(hash(Vector3i(info.id[0]+idx,info.id[1]+j,signum)))
    # Detail cars are pooled around the driver. More distant streets use the ground texture.
    parked.append([p,atan2(t.x,t.y)+(PI if signum<0 and seg[4]==0 else 0),rnd%5])
var parked=[]

func collider(shape):
 var immovable=StaticBody3D.new();var col=CollisionShape3D.new();col.shape=shape;immovable.add_child(col);add_child(immovable)

func roof_height(p,axis,offset,width,eave,rise):
 return eave+rise*clampf(1.-absf(p.dot(axis)-offset)/width,0,1)

func roof_half(vertices,axis,offset,sign_side):
 var clipped=[]
 for i in vertices.size():
  var a:Vector2=vertices[i];var b:Vector2=vertices[(i+1)%vertices.size()]
  var da=(a.dot(axis)-offset)*sign_side;var db=(b.dot(axis)-offset)*sign_side
  if da>=0:clipped.append(a)
  if (da>0 and db<0) or (da<0 and db>0):clipped.append(a.lerp(b,da/(da-db)))
 return clipped

func survey_building(b,idx,observed={},measured={}):
 var center=point([b[4],b[5]]);var base=height_at(center);var outline=PackedVector2Array()
 for uv in b[0][0]:outline.append(point(uv))
 var bounds=Rect2(outline[0],Vector2.ZERO)
 var axis=Vector2.RIGHT;var longest=0.
 for i in outline.size():
  bounds=bounds.expand(outline[i])
  var edge=outline[(i+1)%outline.size()]-outline[i]
  if edge.length_squared()>longest:longest=edge.length_squared();axis=edge.orthogonal().normalized()
 footprints.append([bounds,outline])
 if observed.get("across",false):axis=axis.orthogonal()
 var low=INF;var high=-INF
 for pt in outline:
  low=minf(low,pt.dot(axis));high=maxf(high,pt.dot(axis))
 var width=maxf(.1,(high-low)*.5);var offset=(high+low)*.5
 var ridge=base+maxf(.8 if info.get("enriched_heights",{}).has(idx) else 2.65,b[2]);var rise=minf(width*.48,maxf(.1,ridge-base-2.55))
 if bounds.get_area()>540:rise=.16
 if observed.get("shape","")=="flat":rise=0.
 elif observed.get("shape","")=="gabled":
  rise=clampf(float(observed.get("roof_height",width*.45)),0.,maxf(0.,ridge-base-2.55))
 if not measured.is_empty():
  ridge=base+float(measured.eave_m)
  rise=0.
 var eave=ridge-rise;var id=absi(hash(Vector3i(info.id[0],info.id[1],idx)))
 var inventory=info.get("enriched_uses",{}).get(idx,{})
 var reported=inventory.get("stories")
 var floors=1
 if reported is int or reported is float:
  floors=clampi(int(reported),1,mini(3,maxi(1,floori((eave-base)/2.15))))
 var storey_height=(eave-base)/floors
 var finishes=preload("res://ObservedBuildings.gd")
 var wall=finishes.material(self,observed,"wall","siding"+str(id%4));var pts=[]
 for ring in b[0]:
  for i in ring.size():
   var a=point(ring[i]);var d=point(ring[(i+1)%ring.size()]);pts.append(a)
   var length=a.distance_to(d)
   if length<.04:continue
   var line=(d-a)/length;var out=line.orthogonal()
   if ((a+d)*.5-center).dot(out)<0:out=-out
   var normal=Vector3(out.x,0,out.y);var bottom=minf(base,minf(height_at(a),height_at(d)))-.35
   var parts=[a];var da=a.dot(axis)-offset;var dd=d.dot(axis)-offset
   if da*dd<0:parts.append(a.lerp(d,da/(da-dd)))
   parts.append(d)
   for k in range(1,parts.size()):
    var l=parts[k-1];var r=parts[k];var lh=roof_height(l,axis,offset,width,eave,rise);var rh=roof_height(r,axis,offset,width,eave,rise)
    quad(wall,vert(l,bottom),vert(r,bottom),vert(r,rh),vert(l,lh),normal,Vector2(r.distance_to(l),(lh+rh)*.5-bottom))
    collision_faces.append_array(PackedVector3Array([vert(l,bottom),vert(r,rh),vert(r,bottom),vert(l,bottom),vert(l,lh),vert(r,rh)]))
   if length<3.1:continue
   var count=clampi(roundi(length/3.9),1,8)
   for floor_index in floors:
    for k in count:
     var c=a.lerp(d,(k+.5)/count)+out*.022
     var lo=base+.91+floor_index*storey_height;var hi=minf(lo+1.25,eave-.18)
     if hi-lo<.7:continue
     var wing=line*.57
     quad("house_trim",vert(c-wing,lo),vert(c+wing,lo),vert(c+wing,hi),vert(c-wing,hi),normal)
     c+=out*.016;wing=line*.49
     quad("house_glass",vert(c-wing,lo+.065),vert(c+wing,lo+.065),vert(c+wing,hi-.065),vert(c-wing,hi-.065),normal)
     c+=out*.013;var mid=(lo+hi)*.5
     quad("house_trim",vert(c-wing,mid-.019),vert(c+wing,mid-.019),vert(c+wing,mid+.019),vert(c-wing,mid+.019),normal)
 var roof_key=finishes.material(self,observed,"roof",roof_base_key(b))
 if not measured.is_empty():
  LIDAR_ROOF.draw(self,measured,base,roof_key,wall)
  return
 var roof=tool(roof_key)
 for j in range(0,b[1].size(),3):
  var face=[pts[int(b[1][j])],pts[int(b[1][j+1])],pts[int(b[1][j+2])]]
  for sign_side in [-1.,1.]:
   var panel=roof_half(face,axis,offset,sign_side)
   for i in range(1,panel.size()-1):
    var a=panel[0];var d=panel[i];var f=panel[i+1]
    var va=vert(a,roof_height(a,axis,offset,width,eave,rise));var vd=vert(d,roof_height(d,axis,offset,width,eave,rise));var vf=vert(f,roof_height(f,axis,offset,width,eave,rise))
    var n=(vd-va).cross(vf-va).normalized()
    if n.y<0:n=-n
    tri(roof,va,vd,vf,a/side,d/side,f/side,n)

func survey_paint():
 var measured_zones: Array[PackedVector2Array] = []
 var filename = "res://data/markings-%d-%d.json" % [info.id[0],info.id[1]]
 if FileAccess.file_exists(filename):
  var overlay = JSON.parse_string(FileAccess.get_file_as_string(filename))
  if overlay is Dictionary:
   for ring in overlay.get("extent",[]):
    var zone = PackedVector2Array()
    for coordinate in ring:zone.append(point(coordinate))
    if zone.size()>=3:measured_zones.append(zone)
   for line in overlay.get("lines",[]):
    var ink = "yellowpaint" if line.ink=="yellow" else "whitepaint"
    for j in range(1,line.uv.size()):
     var first = point(line.uv[j-1]);var last = point(line.uv[j])
     var edge = (last-first).normalized().orthogonal()*float(line.width_m)*.5
     var count = maxi(1,ceili(first.distance_to(last)/.75))
     for k in count:
      var p = first.lerp(last,float(k)/count);var q = first.lerp(last,float(k+1)/count)
      # A nearby registered street photograph supplies its own paint.
      if capture.road_confidence((p+q)*.5/side)>.55:continue
      quad(ink,vert(p-edge,height_at(p-edge)+.022),vert(p+edge,height_at(p+edge)+.022),vert(q+edge,height_at(q+edge)+.022),vert(q-edge,height_at(q-edge)+.022),Vector3.UP)
 for seg in segments:
  if seg.size()>6 and seg[6]=="unpaved":continue
  var divided=seg[5] in ["motorway","trunk","primary"] and seg[4]!=0
  if not divided:continue
  var a=seg[0];var b=seg[1];var delta=b-a;var length=delta.length()
  var dir=delta/length;var cross_dir=dir.orthogonal()
  var offsets=[-.145,.145,-seg[3]*.5+.15,seg[3]*.5-.15]
  if divided:offsets=[0.,-seg[3]*.5+.14,seg[3]*.5-.14]
  for offset in offsets:
   var steps=maxi(1,ceili(length/3.))
   for i in steps:
    var start=a+delta*(float(i)/steps)+cross_dir*offset
    var end=a+delta*(float(i+1)/steps)+cross_dir*offset
    if divided and offset==0.:
     var global_pos=start+Vector2(info.id[0]*side,info.id[1]*side)
     if fposmod(global_pos.dot(dir),12.)>4.:continue
    var in_intersection=false
    for light in info.get("street_objects",{}).get("signals",[]):
     if ((start+end)*.5).distance_to(point(light))<9.:in_intersection=true;break
    if in_intersection:continue
    var left=cross_dir*.06
    var white=absf(offset)>.2
    if divided:white=offset<=0. if seg[4]==1 else offset>=0.
    if measured_zones.is_empty():
     quad("whitepaint" if white else "yellowpaint",vert(start-left,height_at(start-left)+.027),vert(start+left,height_at(start+left)+.027),vert(end+left,height_at(end+left)+.027),vert(end-left,height_at(end-left)+.027),Vector3.UP)
     continue
    var remaining = [PackedVector2Array([start,end])]
    for zone in measured_zones:
     var outside = []
     for piece in remaining:outside.append_array(Geometry2D.clip_polyline_with_polygon(piece,zone))
     remaining=outside
    for piece in remaining:
     for j in range(1,piece.size()):
      var p:Vector2=piece[j-1];var q:Vector2=piece[j]
      if p.distance_squared_to(q)<.000001:continue
      quad("whitepaint" if white else "yellowpaint",vert(p-left,height_at(p-left)+.027),vert(p+left,height_at(p+left)+.027),vert(q+left,height_at(q+left)+.027),vert(q-left,height_at(q-left)+.027),Vector3.UP)

static func read_aerial(directory:String,name:String)->Image:
 if name.is_empty() or name.get_file()!=name:return null
 var file=FileAccess.open(directory.path_join(name),FileAccess.READ)
 if not file or file.get_length()==0 or file.get_length()>4194304:return null
 var bytes=file.get_buffer(file.get_length())
 var im=Image.new()
 var err=im.load_jpg_from_buffer(bytes) if name.ends_with(".jpg") else im.load_png_from_buffer(bytes)
 return im if err==OK and not im.is_empty() else null

func prepare_pixels(image):
 if road_prior==null:return
 var width=image.get_width()/4;var height=image.get_height()/4
 var rect=Rect2i((int(info.id[0])%4)*width,(int(info.id[1])%4)*height,width,height)
 aerial_pixels=image.get_region(rect)
 aerial_pixels.clear_mipmaps();aerial_pixels.convert(Image.FORMAT_RGB8)
 photo_sample=aerial_pixels.duplicate();photo_sample.resize(96,96,Image.INTERPOLATE_BILINEAR)
 if aerial_pixels.get_width()!=256 or aerial_pixels.get_height()!=256:aerial_pixels.resize(256,256,Image.INTERPOLATE_BILINEAR)
 shrub_aerial=aerial_pixels
func accept_edges(image):
 learned_edges=image
 streaming_ground.set_shader_parameter("road_edges",ImageTexture.create_from_image(image))
 streaming_ground.set_shader_parameter("has_edges",true)
 road_prior=null;aerial_pixels=null
 if crowns_waiting_for_edges.is_empty():return false
 # The first pass used a conservative road-width buffer while the aerial mask loaded.
 var rows=crowns_waiting_for_edges
 crowns_waiting_for_edges=[]
 replace_crowns(rows)
 return true
func update_roof_photo(texture):
 if streamed_roof!=null:
  streamed_roof.set_shader_parameter("ortho",texture)
  streamed_roof.set_shader_parameter("has_image",true)

func roof_base_key(b):
 # Tall walls can displace the apparent roof in an orthophoto.
 if materials.has("streamed_roof") and float(b[2])<=16.:return "streamed_roof"
 return "field_roof" if materials.has("field_roof") else "roof"

func apply_photo(texture,image=null):
 update_roof_photo(texture)
 if image:prepare_pixels(image)
 if streaming_ground==null:return
 streaming_ground.set_shader_parameter("ortho",texture)
 streaming_ground.set_shader_parameter("quadrant",Vector2(int(info.id[0])%4,int(info.id[1])%4))
 streaming_ground.set_shader_parameter("imagery",true)
 info["aerial"]=info.get("aerial_key","")
