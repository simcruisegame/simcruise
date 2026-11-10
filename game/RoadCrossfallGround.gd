extends RefCounted
## A transverse pavement profile from two airborne strips, installed only at tile construction.
## The same indexed triangles supply tire contact, map heights, and the visible surface.
const PARTS=6
const BIN_COUNT=64
const MAX_FINE=460
const MAX_RUNS=8
# Sections are surveyed for the reference width of the road's class, which the
# road signature also signs (SurveyOutcropGround.gd).
const SIGNATURE=preload("res://SurveyOutcropGround.gd")
var side:float
var runs=[]
var bins:Dictionary={}
var vertices:PackedVector3Array
var faces:PackedInt32Array
var original:PackedVector3Array
var old_faces:PackedInt32Array
var source_bins={}
var output_normals:PackedVector3Array
var output_uv:PackedVector2Array
var point_ids={}
var segment_grid={}
var active=false

static func for_tile(tile):
 var info:Dictionary=tile.info
 var id=info.get("id",[])
 var cache:String=info.get("_source_cache","")
 if cache.is_empty() or id.size()!=2 or tile.grid_steps not in [64,128] or info.get("field",false) or info.get("survey_ground_nodes",0)>0 or not info.get("roadbed",[]).is_empty():return null
 var stem=cache.path_join("road-crossfall-v1-%d-%d"%[int(id[0]),int(id[1])])
 if not FileAccess.file_exists(stem+".ready"):return null
 var sig=SIGNATURE.road_signature(info.roads).hex_encode()
 var now=Time.get_unix_time_from_system()*1000.
 var choices=[]
 for x in 4:
  for y in 4:
   var file=FileAccess.open(stem+"-%d-%d.json"%[x,y],FileAccess.READ)
   if not file or file.get_length()>64000:continue
   var v=JSON.parse_string(file.get_as_text())
   if not v is Dictionary or v.get("version",0)!=1 or v.get("roadSignature","")!=sig or v.get("expires",0)<=now:continue
   if v.get("id",[])!=[float(id[0]),float(id[1])] or v.get("patch",[])!=[float(x),float(y)] or not v.get("runs") is Array or v.runs.size()>12:continue
   var project=v.get("project",{})
   if not project is Dictionary or not project.get("name","").begins_with("USGS_LPC_"):continue
   for run in v.runs:
    if run is Array and run.size()>=3 and run.size()<=128:choices.append({"rows":run,"project":project})
 if choices.is_empty():return null
 choices.sort_custom(func(a,b):
  if str(a.project.end)!=str(b.project.end):return str(a.project.end)>str(b.project.end)
  return a.rows.size()>b.rows.size())
 var out=new();out.side=tile.side
 # All participating rows on a tile must share one source epoch and work unit.
 var best=choices[0].project
 for entry in choices:
  if entry.project!=best:continue
  var run=out.read_run(entry.rows,info.roads,entry.project)
  if not run.is_empty():out.runs.append(run)
 return null if out.runs.is_empty() else out

func read_run(rows:Array,roads:Array,project:Dictionary)->Dictionary:
 var pts=PackedVector2Array();var tangents=PackedVector2Array();var slopes=PackedVector3Array()
 var previous=[]
 for row in rows:
  if not row is Array or row.size()!=15:return {}
  for j in [0,2,3,4,5,6,7,8,9,10,11,12,13,14]:
   if typeof(row[j]) not in [TYPE_FLOAT,TYPE_INT] or not is_finite(row[j]):return {}
  var road=int(row[0]);var width=float(row[7])
  if road<0 or road>=roads.size() or not str(row[1]).is_valid_int() or width<2.25 or width>5. or absf(width-minf(5.,SIGNATURE.reference_width(roads[road])*.5-.8))>.02:return {}
  if row[11]<=0 or row[12]<=0 or row[11]==row[12] or minf(row[13],row[14])<18:return {}
  var p=Vector2(row[3],row[4])*side;var tangent=Vector2(row[5],row[6])
  if minf(minf(p.x,p.y),minf(side-p.x,side-p.y))<16.5 or absf(tangent.length()-1.)>.002:return {}
  if maxf(absf(row[9]),absf(row[10]))>.08 or absf(row[8])>.19:return {}
  if not previous.is_empty():
   if row[0]!=previous[0] or row[1]!=previous[1] or row[2]-previous[2]!=6 or row[11]!=previous[11] or row[12]!=previous[12]:return {}
   var step=p-pts[-1]
   if absf(step.length()-6.)>.15 or step.normalized().dot(tangent)<.99:return {}
  pts.append(p);tangents.append(tangent.normalized());slopes.append(Vector3(row[8],row[9],row[10]));previous=row
 var box=Rect2(pts[0],Vector2.ZERO)
 for p in pts:box=box.expand(p)
 return {"pts":pts,"tangents":tangents,"slopes":slopes,"width":float(rows[0][7]),"box":box.grow(float(rows[0][7])+6.01),"way":str(rows[0][1]),"ids":[int(rows[0][11]),int(rows[0][12])],"project":project.name,"end":str(project.end)}

func bin_key(p:Vector2)->Vector2i:
 return Vector2i((p*(BIN_COUNT/side)).floor()).clamp(Vector2i.ZERO,Vector2i.ONE*(BIN_COUNT-1))

func height_in_triangles(p:Vector2,points:PackedVector3Array,idx:PackedInt32Array,index:Dictionary,fallback:float)->float:
 for face in index.get(bin_key(p),[]):
  var a=points[idx[face]];var b=points[idx[face+1]];var c=points[idx[face+2]]
  var offset=p-Vector2(a.x,a.z);var ab=Vector2(b.x-a.x,b.z-a.z);var ac=Vector2(c.x-a.x,c.z-a.z)
  var det=ab.cross(ac)
  if absf(det)<.000001:continue
  var u=offset.cross(ac)/det;var v=ab.cross(offset)/det
  if u>=-.00004 and v>=-.00004 and u+v<=1.00004:return a.y+u*(b.y-a.y)+v*(c.y-a.y)
 return fallback

func index_faces(points:PackedVector3Array,idx:PackedInt32Array)->Dictionary:
 var grid={}
 for i in range(0,idx.size(),3):
  var a=points[idx[i]];var b=points[idx[i+1]];var c=points[idx[i+2]]
  # Original and refined faces are subdivisions within one 64-grid cell.
  var key=bin_key(Vector2(a.x+b.x+c.x,a.z+b.z+c.z)/3.)
  if not grid.has(key):grid[key]=[]
  grid[key].append(i)
 return grid

func base(p:Vector2)->float:
 return height_in_triangles(p,original,old_faces,source_bins,NAN)

func admissible(run)->bool:
 var pts=run.pts;var dirs=run.tangents;var sl=run.slopes
 var w:float=run.width
 for i in pts.size():
  var a:Vector2=pts[i];var t:Vector2=dirs[i];var normal=Vector2(-t.y,t.x)
  var h=base(a)
  for sign_u in [-1.,1.]:
   var v=base(a+normal*(sign_u*w));var slope:float=sl[i].y if sign_u<0 else sl[i].z
   if not is_finite(v+h) or absf(v-h-sign_u*w*slope)>.46:return false
 var end=float(pts.size()-1)*6.
 var s=-15.
 while s<=end+15.:
  var sc=clampf(s,0.,end);var i=mini(floori(sc/6.),pts.size()-2);var f=sc/6.-i
  var t:Vector2=dirs[i].lerp(dirs[i+1],f).normalized()
  var center:Vector2=pts[i].lerp(pts[i+1],f)+t*(s-sc)
  var across=Vector2(-t.y,t.x);var grade=lerpf(sl[i].x,sl[i+1].x,f)
  for lateral in [-1.6,0.,1.6]:
   var q=center+across*lateral
   var slope=(base(q+t)-base(q-t))*.5
   if not is_finite(slope) or absf(slope)>.16 or absf(slope-grade)>.075:return false
  s+=2.5
 return true

func delta(p:Vector2)->float:
 var best=INF;var selected=null;var station=0.;var u=0.;var at=Vector2.ZERO;var n=Vector2.ZERO;var slope=0.
 for seg in segment_grid.get(Vector2i((p/12.).floor()),[]):
  var run=seg[0];var i:int=seg[1];var a:Vector2=run.pts[i];var b:Vector2=run.pts[i+1];var t:Vector2=seg[2];var length:float=seg[3]
  var s=(p-a).dot(t)
  if (s<0. and i==0) or (s>length and i==run.pts.size()-2):continue
  var f=clampf(s/length,0.,1.);var c=a.lerp(b,f);var sep=p-c
  var d=sep.length_squared()
  if d>=best or d>(run.width+6.)*(run.width+6.):continue
  best=d;at=c;n=Vector2(-t.y,t.x);u=sep.dot(n);slope=lerpf(run.slopes[i].y if u<0. else run.slopes[i].z,run.slopes[i+1].y if u<0. else run.slopes[i+1].z,f)
  selected=run;station=6.*(i+f)
 if selected==null:return 0.
 var extent=6.*(selected.pts.size()-1)
 var taper=smoothstep(0.,minf(7.,.3*extent),minf(station,extent-station))*smoothstep(0.,6.,selected.width+6.-absf(u))
 if taper<=0.:return 0.
 var edge=clampf(u,-selected.width,selected.width)
 var raw=edge*slope-base(at+n*edge)+base(at)
 return clampf(raw,-.46,.46)*taper if is_finite(raw) else 0.

func edge_key(a:int,b:int)->Vector2i:return Vector2i(mini(a,b),maxi(a,b))

func vertex(p:Vector3,normal:Vector3)->int:
 var xz=Vector2(p.x,p.z)
 var key=Vector2i((xz*((BIN_COUNT*2.*PARTS)/side)).round())
 if point_ids.has(key):return point_ids[key]
 var j=vertices.size();point_ids[key]=j
 p.y+=delta(xz)
 var step=.45
 var gradient=Vector2(delta(xz+Vector2.RIGHT*step)-delta(xz-Vector2.RIGHT*step),delta(xz+Vector2.DOWN*step)-delta(xz-Vector2.DOWN*step))/(2.*step)
 vertices.append(p);output_normals.append(Vector3(normal.x-normal.y*gradient.x,normal.y,normal.z-normal.y*gradient.y).normalized());output_uv.append(xz/side)
 return j

func interior(ids:Array,norm:PackedVector3Array)->void:
 var lattice=[]
 for i in PARTS+1:
  var row=[]
  for j in PARTS+1-i:
   var f=float(i)/PARTS;var g=float(j)/PARTS
   var p=original[ids[0]]*(1.-f-g)+original[ids[1]]*f+original[ids[2]]*g
   var n=norm[ids[0]]*(1.-f-g)+norm[ids[1]]*f+norm[ids[2]]*g
   row.append(vertex(p,n.normalized()))
  lattice.append(row)
 for i in PARTS:
  for j in PARTS-i:
   faces.append(lattice[i][j]);faces.append(lattice[i+1][j]);faces.append(lattice[i][j+1])
   if j<PARTS-i-1:faces.append(lattice[i+1][j]);faces.append(lattice[i+1][j+1]);faces.append(lattice[i][j+1])

func edge_transition(ids:Array,norm:PackedVector3Array,split_edges:Dictionary)->void:
 var ring=[]
 for i in 3:
  var a:int=ids[i];var b:int=ids[(i+1)%3]
  var count=PARTS if split_edges.has(edge_key(a,b)) else 1
  for j in count:
   var f=float(j)/count
   ring.append(vertex(original[a].lerp(original[b],f),norm[a].lerp(norm[b],f).normalized()))
 var center=vertex((original[ids[0]]+original[ids[1]]+original[ids[2]])/3.,(norm[ids[0]]+norm[ids[1]]+norm[ids[2]]).normalized())
 for i in ring.size():
  faces.append(ring[i]);faces.append(ring[(i+1)%ring.size()]);faces.append(center)

func fine_triangles(run)->Dictionary:
 var found={}
 for i in run.pts.size()-1:
  var a:Vector2=run.pts[i];var b:Vector2=run.pts[i+1];var margin=Vector2.ONE*(run.width+6.)
  var lo=bin_key(a.min(b)-margin);var hi=bin_key(a.max(b)+margin)
  for y in range(lo.y,hi.y+1):
   for x in range(lo.x,hi.x+1):
    for j in source_bins.get(Vector2i(x,y),[]):
     var v0=original[old_faces[j]];var v1=original[old_faces[j+1]];var v2=original[old_faces[j+2]]
     var c=Vector2(v0.x+v1.x+v2.x,v0.z+v1.z+v2.z)/3.
     if c.distance_to(Geometry2D.get_closest_point_to_segment(c,a,b))<=run.width+6.+.87*side/BIN_COUNT:found[j]=true
 return found

func refine(tile,arrays:Array)->Array:
 original=arrays[Mesh.ARRAY_VERTEX];old_faces=arrays[Mesh.ARRAY_INDEX];var norm:PackedVector3Array=arrays[Mesh.ARRAY_NORMAL]
 source_bins=index_faces(original,old_faces)
 var fine={};var split_edges={};var accepted=[]
 for run in runs:
  if accepted.size()>=MAX_RUNS:break
  var overlaps=false
  for prior in accepted:
   if not prior.box.intersects(run.box):continue
   for p in run.pts:
    for i in prior.pts.size()-1:
     if p.distance_to(Geometry2D.get_closest_point_to_segment(p,prior.pts[i],prior.pts[i+1]))<run.width+prior.width+12.:overlaps=true;break
    if overlaps:break
   if overlaps:break
  if overlaps or not admissible(run):continue
  var new_faces=fine_triangles(run)
  if new_faces.is_empty() or fine.size()+new_faces.size()>MAX_FINE:continue
  fine.merge(new_faces);accepted.append(run)
  for j in run.pts.size()-1:
   var a:Vector2=run.pts[j];var b:Vector2=run.pts[j+1];var margin=Vector2.ONE*(run.width+6.1);var lo=Vector2i(((a.min(b)-margin)/12.).floor());var hi=Vector2i(((a.max(b)+margin)/12.).floor())
   var seg=[run,j,(b-a).normalized(),a.distance_to(b)]
   for yy in range(lo.y,hi.y+1):
    for xx in range(lo.x,hi.x+1):
     var key=Vector2i(xx,yy)
     if not segment_grid.has(key):segment_grid[key]=[]
     segment_grid[key].append(seg)
 runs=accepted
 if runs.is_empty():
  source_bins.clear();original=PackedVector3Array();old_faces=PackedInt32Array()
  return arrays
 for i in fine:
  for j in 3:split_edges[edge_key(old_faces[i+j],old_faces[i+(j+1)%3])]=true
 vertices=original.duplicate();output_normals=norm.duplicate();output_uv=arrays[Mesh.ARRAY_TEX_UV].duplicate()
 # A reused original corner on the corridor must have exactly the same displaced elevation.
 for j in vertices.size():
  var p=vertices[j];var xz=Vector2(p.x,p.z);var key=Vector2i((xz*((BIN_COUNT*2.*PARTS)/side)).round())
  point_ids[key]=j
  if segment_grid.has(Vector2i((xz/12.).floor())):
   p.y+=delta(xz);vertices[j]=p
   var dx=(delta(xz+Vector2(.45,0))-delta(xz-Vector2(.45,0)))/.9;var dz=(delta(xz+Vector2(0,.45))-delta(xz-Vector2(0,.45)))/.9
   output_normals[j]=Vector3(norm[j].x-norm[j].y*dx,norm[j].y,norm[j].z-norm[j].y*dz).normalized()
 for i in range(0,old_faces.size(),3):
  var ids=[old_faces[i],old_faces[i+1],old_faces[i+2]]
  if fine.has(i):interior(ids,norm)
  elif split_edges.has(edge_key(ids[0],ids[1])) or split_edges.has(edge_key(ids[1],ids[2])) or split_edges.has(edge_key(ids[2],ids[0])):edge_transition(ids,norm,split_edges)
  else:faces.append(ids[0]);faces.append(ids[1]);faces.append(ids[2])
 bins=index_faces(vertices,faces)
 active=true
 var result=[];result.resize(Mesh.ARRAY_MAX)
 result[Mesh.ARRAY_VERTEX]=vertices;result[Mesh.ARRAY_NORMAL]=output_normals
 result[Mesh.ARRAY_TEX_UV]=output_uv;result[Mesh.ARRAY_INDEX]=faces
 var ways=[];var strips=[]
 for r in runs:
  if not ways.has(r.way):ways.append(r.way)
  if not strips.has(r.ids):strips.append(r.ids)
 tile.info["surveyed_crossfall"]={"runs":runs.size(),"ways":ways,"sourceIds":strips,"project":runs[0].project,"surveyEnd":runs[0].end,"extraTriangles":(faces.size()-old_faces.size())/3}
 point_ids.clear();source_bins.clear();segment_grid.clear();original=PackedVector3Array();old_faces=PackedInt32Array()
 return result

func elevation(p:Vector2,fallback:float)->float:
 if not active:return fallback
 return height_in_triangles(p,vertices,faces,bins,fallback)
