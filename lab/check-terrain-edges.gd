extends SceneTree
const S = preload("res://TerrainEdges.gd")
class Neighbor:
 var info:Dictionary
 func _init(data):info=data

func grid(id:Vector2i,offset:float,n:int):
 var h=[]
 for y in n+1:
  for x in n+1:
   h.append(30.+float(id.x)+float(x)/n+3.*(float(id.y)+float(y)/n)+offset)
 return {"grid":n,"live_canopy":true,"h":h}
func check(value:bool,label:String):
 if not value:
  push_error(label);failed=true
var failed=false
func _initialize():
 var tiles={}
 # Previously attached northern, western and diagonal tiles have a different
 # vertical datum. The new one must meet all three without altering neighbors.
 for id in [Vector2i.LEFT,Vector2i.UP,Vector2i(-1,-1),Vector2i(1,1)]:
  tiles[id]=Neighbor.new(grid(id,14.,32 if id.x<0 else 64))
 var northern=tiles[Vector2i.UP].info.h.duplicate()
 var d=grid(Vector2i.ZERO,0.,64)
 var copy=d.duplicate(true)
 S.apply(d,S.collect(tiles,Vector2i.ZERO),410.)
 for k in 65:
  check(absf(d.h[k]-(copy.h[k]+14.))<.000001,"north edge")
  check(absf(d.h[k*65]-(copy.h[k*65]+14.))<.000001,"west edge")
 check(absf(d.h[-1]-copy.h[-1]-14.)<.000001,"diagonal corner")
 check(tiles[Vector2i.UP].info.h==northern,"resident terrain mutated")
 check(absf(d.h[32*65+32]-copy.h[32*65+32])<.000001,"interior moved unnecessarily")
 var identical=grid(Vector2i.ZERO,14.,64)
 var prior=identical.h.duplicate()
 S.apply(identical,S.collect(tiles,Vector2i.ZERO),410.)
 check(identical.h==prior,"equal products changed")
 print("TERRAIN EDGES ","FAILED" if failed else "PASSED"," · two shared sides, diagonal, mixed resolution, stable resident")
 quit(int(failed))
