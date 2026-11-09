extends RefCounted
## Share established boundary heights when network availability changes the
## elevation product mid-drive. Only the incoming tile's worker writes terrain.
const DIRECTIONS = [Vector2i.LEFT, Vector2i.RIGHT, Vector2i.UP, Vector2i.DOWN]
const CORNERS = [Vector2i(-1,-1), Vector2i(1,-1), Vector2i(-1,1), Vector2i(1,1)]

static func edge(data:Dictionary, direction:Vector2i)->PackedFloat64Array:
 var n=int(data.get("grid",32))
 var h=data.h
 var values=PackedFloat64Array()
 for k in n+1:
  var x=k if direction.x==0 else (0 if direction.x<0 else n)
  var y=k if direction.y==0 else (0 if direction.y<0 else n)
  values.append(h[y*(n+1)+x])
 return values

static func collect(tiles:Dictionary, id:Vector2i)->Dictionary:
 var edges=[]
 for direction in DIRECTIONS:
  var tile=tiles.get(id+direction)
  edges.append(edge(tile.info,-direction) if tile else PackedFloat64Array())
 var corners=[]
 for direction in CORNERS:
  var tile=tiles.get(id+direction)
  if tile:
   var n=int(tile.info.get("grid",32))
   var x=0 if direction.x>0 else n
   var y=0 if direction.y>0 else n
   corners.append(tile.info.h[y*(n+1)+x])
  else:corners.append(null)
 return {"edges":edges,"corners":corners}

static func sample(line:PackedFloat64Array,t:float)->float:
 var pos=clampf(t,0.,1.)*(line.size()-1)
 var i=mini(floori(pos),line.size()-2)
 return lerpf(line[i],line[i+1],pos-i)

static func apply(data:Dictionary, anchors:Dictionary, side:float):
 if anchors.is_empty() or not data.get("live_canopy",false):return
 var n=int(data.get("grid",32))
 if n<2 or data.h.size()!=(n+1)*(n+1):return
 # Left, right, north and south edges share these corner indices.
 var ends=[[0,2],[1,3],[0,1],[2,3]]
 var deltas=[]
 var corners=[null,null,null,null]
 var maximum=0.
 for k in 4:
  var established:PackedFloat64Array=anchors.edges[k]
  var diff=PackedFloat64Array()
  if not established.is_empty():
   var incoming=edge(data,DIRECTIONS[k])
   for i in n+1:
    var v=sample(established,float(i)/n)-incoming[i]
    diff.append(v);maximum=maxf(maximum,absf(v))
   corners[ends[k][0]]=diff[0];corners[ends[k][1]]=diff[n]
  deltas.append(diff)
 for k in 4:
  if corners[k]==null:
   var i=(n if k%2 else 0)+(n+1)*(n if k>=2 else 0)
   corners[k]=0. if anchors.corners[k]==null else float(anchors.corners[k])-float(data.h[i])
   maximum=maxf(maximum,absf(corners[k]))
 if maximum<.001:return
 var reach=clampf(maximum*12.,side*.18,side)
 var fade=PackedFloat64Array()
 for i in n+1:fade.append(1.-smoothstep(0.,reach,side*i/n))
 for k in 4:
  var diff:PackedFloat64Array=deltas[k]
  if diff.is_empty():
   for i in n+1:diff.append(corners[ends[k][0]]*fade[i]+corners[ends[k][1]]*fade[n-i])
   deltas[k]=diff
 for j in n+1:
  var top=fade[j];var bottom=fade[n-j]
  for i in n+1:
   var left=fade[i];var right=fade[n-i]
   # Subtract the four duplicated corner contributions (Coons blend).
   var shift=left*deltas[0][j]+right*deltas[1][j]+top*deltas[2][i]+bottom*deltas[3][i]
   shift-=top*(left*corners[0]+right*corners[1])+bottom*(left*corners[2]+right*corners[3])
   data.h[j*(n+1)+i]+=shift
 data["terrain_edge_adjustment_m"]=maximum
