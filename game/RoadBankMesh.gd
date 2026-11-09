extends RefCounted
## Refine survey-supported road banks while preserving coarse road triangles
## and the heights used by vegetation, buildings, and the shared collider.
const N:=128
const W:=129
const COUNT:=W*W

static func apply(data:Dictionary)->void:
 var record=data.get("road_banks",{})
 if record.get("detailGrid",0)!=N or record.get("datum","")!="NAVD88" or data.get("grid",0)!=64:return
 var changes=record.get("deltas",[])
 # A site-specific surveyed grade and an explicitly authored roadbed have priority.
 if data.get("survey_ground_nodes",0)>0 or not data.get("roadbed",[]).is_empty() or data.get("field",false):
  record.erase("deltas")
  return
 var coarse=data.get("h",[])
 if coarse.size()!=65*65 or changes.is_empty() or changes.size()%2!=0 or changes.size()>2*COUNT:return
 var last=-1
 for i in range(0,changes.size(),2):
  var index=int(changes[i])
  if index<=last or index>=COUNT or index%W in [0,N] or (index/W) in [0,N] or absi(int(changes[i+1]))>6000:return
  last=index
 var fine=PackedFloat64Array()
 fine.resize(COUNT)
 for j in W:
  for i in W:
   var k=(j>>1)*65+(i>>1)
   # Under Godot PlaneMesh the center lies on the NE/SW diagonal.
   if i%2 and j%2:fine[j*W+i]=(coarse[k+1]+coarse[k+65])*.5
   elif i%2:fine[j*W+i]=(coarse[k]+coarse[k+1])*.5
   elif j%2:fine[j*W+i]=(coarse[k]+coarse[k+65])*.5
   else:fine[j*W+i]=coarse[k]
 var surveyed=PackedInt32Array()
 for i in range(0,changes.size(),2):
  fine[int(changes[i])]+=float(changes[i+1])*.001
  surveyed.append(int(changes[i]))
 record.erase("deltas")
 data.h=fine
 data.grid=N
 data["bank_detail_active"]=true
 # Measured bank posts keep their height through the road-corridor smoothing.
 data["bank_detail_nodes"]=surveyed

static func mark(splits:PackedByteArray,h:PackedFloat64Array,a:int,b:int,c:int)->void:
 var ab=(a+b)>>1;var bc=(b+c)>>1;var ca=(c+a)>>1
 if absf(h[ab]-(h[a]+h[b])*.5)>.0001 or absf(h[bc]-(h[b]+h[c])*.5)>.0001 or absf(h[ca]-(h[c]+h[a])*.5)>.0001:
  splits[ab]=1;splits[bc]=1;splits[ca]=1

static func faces(indices:PackedInt32Array,splits:PackedByteArray,a:int,b:int,c:int)->void:
 var ab=(a+b)>>1;var bc=(b+c)>>1;var ca=(c+a)>>1
 var count=splits[ab]+splits[bc]+splits[ca]
 if count==0:
  indices.append_array(PackedInt32Array([a,b,c]))
 elif count==3:
  indices.append_array(PackedInt32Array([a,ab,ca,ab,b,bc,ca,bc,c,ab,bc,ca]))
 else:
  # Rotate to a common ordering. A triangle beside a refined neighbor uses
  # exactly the same midpoint, including when its own three corners are planar.
  if (count==1 and splits[bc]) or (count==2 and not splits[ab]):
   var first=a;a=b;b=c;c=first
  elif (count==1 and splits[ca]) or (count==2 and not splits[bc]):
   var first=a;a=c;c=b;b=first
  ab=(a+b)>>1;bc=(b+c)>>1
  if count==1:indices.append_array(PackedInt32Array([a,ab,c,ab,b,c]))
  else:indices.append_array(PackedInt32Array([ab,b,bc,a,ab,bc,a,bc,c]))

static func make(h:PackedFloat64Array,side:float,material:Material)->ArrayMesh:
 var splits=PackedByteArray();splits.resize(COUNT)
 for j in range(0,N,2):
  for i in range(0,N,2):
   var a=j*W+i;var b=a+2;var c=a+2*W;var d=c+2
   mark(splits,h,a,b,c);mark(splits,h,d,c,b)
 var indices=PackedInt32Array()
 for j in range(0,N,2):
  for i in range(0,N,2):
   var a=j*W+i;var b=a+2;var c=a+2*W;var d=c+2
   faces(indices,splits,a,b,c);faces(indices,splits,d,c,b)
 var positions=PackedVector3Array();var normals=PackedVector3Array();var uvs=PackedVector2Array()
 positions.resize(COUNT);normals.resize(COUNT);uvs.resize(COUNT)
 var step=side/N
 for j in W:
  for i in W:
   var k=j*W+i
   var uv=Vector2(i,j)/N
   var dx=h[k-1 if i>0 else k]-h[k+1 if i<N else k]
   var dz=h[k-W if j>0 else k]-h[k+W if j<N else k]
   uvs[k]=uv;positions[k]=Vector3(uv.x*side,h[k],uv.y*side)
   normals[k]=Vector3(dx,2*step,dz).normalized()
 var arrays=[];arrays.resize(Mesh.ARRAY_MAX)
 arrays[Mesh.ARRAY_VERTEX]=positions;arrays[Mesh.ARRAY_NORMAL]=normals
 arrays[Mesh.ARRAY_TEX_UV]=uvs;arrays[Mesh.ARRAY_INDEX]=indices
 var mesh=ArrayMesh.new()
 mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,arrays)
 mesh.surface_set_material(0,material)
 return mesh
