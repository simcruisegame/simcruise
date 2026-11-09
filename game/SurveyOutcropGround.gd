extends RefCounted
## Project-locked fine terrain for the two checked Needles Highway tiles.
## Weights decay to zero at map boundaries and throughout the road corridor.
const TILE_Y := 23870
const TILE_X := [13918, 13919]
const GRID := 64
const SUB := 6
var first:Vector2i
var count:Vector2i
var dimensions:Vector2i
var vertical_offset:float
var side:float
var bounds:Rect2
var heights:=PackedFloat32Array()
var changed:=PackedByteArray()
var weights:=PackedByteArray()

static func for_tile(tile):
 var id=tile.info.get("id",[])
 if id.size()!=2 or int(id[1])!=TILE_Y or int(id[0]) not in TILE_X:return null
 if tile.grid_steps not in [64,128] or tile.info.get("field",false):return null
 var instance=new()
 return instance if instance.read(tile,Vector2i(int(id[0]),int(id[1]))) else null

static func road_signature(roads:Array)->PackedByteArray:
 var rows:=PackedStringArray()
 for road in roads:
  var route:=PackedStringArray()
  for p in road[3]:route.append("%d/%d"%[roundi(float(p[0])*1000000.),roundi(float(p[1])*1000000.)])
  rows.append("%d|%s"%[roundi(float(road[4])*100.),",".join(route)])
 rows.sort()
 return "\n".join(rows).sha256_buffer()

func read(tile,id:Vector2i)->bool:
 var f=FileAccess.open("res://data/needles-ground-%d.meshdata"%id.x,FileAccess.READ)
 if not f or f.get_length()<88 or f.get_32()!=4:return false
 if Vector2i(f.get_32(),f.get_32())!=id or f.get_32()!=GRID or f.get_32()!=SUB:return false
 first=Vector2i(f.get_32(),f.get_32())
 count=Vector2i(f.get_32(),f.get_32())
 dimensions=Vector2i(f.get_32(),f.get_32())
 f.get_32()
 var datum=f.get_float()
 vertical_offset=f.get_float()
 if dimensions!=count*SUB+Vector2i.ONE or first.x<0 or first.y<0 or (first+count).x>GRID or (first+count).y>GRID:return false
 var cells=dimensions.x*dimensions.y
 if cells<=0 or cells>32768 or f.get_length()!=88+3*cells:return false
 if f.get_buffer(32)!=road_signature(tile.info.roads):return false
 var cm=f.get_buffer(cells*2)
 weights=f.get_buffer(cells)
 heights.resize(cells)
 changed.resize(count.x*count.y)
 side=tile.side
 bounds=Rect2(Vector2(first)*side/GRID,Vector2(count)*side/GRID)
 for y in dimensions.y:
  for x in dimensions.x:
   var i=y*dimensions.x+x
   var p=bounds.position+Vector2(x,y)*side/(GRID*SUB)
   heights[i]=lerpf(tile.base_height(p),datum+cm.decode_s16(i*2)*.01,weights[i]/255.)
   if weights[i]==0:continue
   for cy in [floori((y-1.)/SUB),floori(float(y)/SUB)]:
    for cx in [floori((x-1.)/SUB),floori(float(x)/SUB)]:
     if cx>=0 and cx<count.x and cy>=0 and cy<count.y:changed[cy*count.x+cx]=1
 return true

func active(cell:Vector2i)->bool:
 var q=cell-first
 return q.x>=0 and q.y>=0 and q.x<count.x and q.y<count.y and changed[q.y*count.x+q.x]!=0

func elevation(p:Vector2,original:float)->float:
 if not bounds.has_point(p):return original
 var uv=(p-bounds.position)*(GRID*SUB/side)
 var x=clampi(floori(uv.x),0,dimensions.x-2)
 var y=clampi(floori(uv.y),0,dimensions.y-2)
 var i=y*dimensions.x+x
 var down=i+dimensions.x
 if weights[i]==0 and weights[i+1]==0 and weights[down]==0 and weights[down+1]==0:return original
 var fx=uv.x-x
 var fy=uv.y-y
 if fx+fy<=1.:return heights[i]+fx*(heights[i+1]-heights[i])+fy*(heights[down]-heights[i])
 return heights[down+1]+(1.-fx)*(heights[down]-heights[down+1])+(1.-fy)*(heights[i+1]-heights[down+1])

func refine(tile,arrays:Array)->Array:
 var vertices:PackedVector3Array=arrays[Mesh.ARRAY_VERTEX]
 var normals:PackedVector3Array=arrays[Mesh.ARRAY_NORMAL]
 var uvs:PackedVector2Array=arrays[Mesh.ARRAY_TEX_UV]
 var old:PackedInt32Array=arrays[Mesh.ARRAY_INDEX]
 var indices:=PackedInt32Array()
 for i in range(0,old.size(),3):
  var mid=(vertices[old[i]]+vertices[old[i+1]]+vertices[old[i+2]])/3.
  if not active(Vector2i((Vector2(mid.x,mid.z)*GRID/side).floor())):
   indices.append(old[i]);indices.append(old[i+1]);indices.append(old[i+2])
 var start=vertices.size()
 var step=side/(GRID*SUB)
 for y in dimensions.y:
  for x in dimensions.x:
   var p=bounds.position+Vector2(x,y)*step
   var slope=Vector3(tile.height_at(p-Vector2(step,0))-tile.height_at(p+Vector2(step,0)),2*step,tile.height_at(p-Vector2(0,step))-tile.height_at(p+Vector2(0,step)))
   vertices.append(Vector3(p.x,heights[y*dimensions.x+x],p.y))
   normals.append(slope.normalized())
   uvs.append(p/side)
 for y in dimensions.y-1:
  for x in dimensions.x-1:
   if not active(first+Vector2i(x/SUB,y/SUB)):continue
   var a=start+y*dimensions.x+x
   var c=a+dimensions.x
   indices.append_array(PackedInt32Array([a,a+1,c,a+1,c+1,c]))
 var refined=[]
 refined.resize(Mesh.ARRAY_MAX)
 refined[Mesh.ARRAY_VERTEX]=vertices
 refined[Mesh.ARRAY_NORMAL]=normals
 refined[Mesh.ARRAY_TEX_UV]=uvs
 refined[Mesh.ARRAY_INDEX]=indices
 return refined
