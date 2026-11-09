extends SceneTree
# A small, network-independent regression for the evidence gate and dash suppression boundary.

class Tile:
 extends RefCounted
 var side=553.
 var info={}
 func point(p)->Vector2:return Vector2(p[0],p[1])*side

var failures=PackedStringArray()
func check(condition:bool,message:String):
 if not condition:failures.append(message)

func _initialize():
 var script=preload("res://PaintEvidence.gd")
 var t=Tile.new()
 var raw=JSON.parse_string(FileAccess.get_file_as_string("res://data/observed-paint-18122-28047.json"))
 var road=["trunk",1,raw.road,raw.anchors,10.4,"paved"]
 t.info={"id":[18122,28047],"roads":[road]}
 var measured=script.for_tile(t)
 check(measured!=null,"matching named carriageway should enable")
 if measured!=null:
  var center=t.point(raw.anchors[1])
  var direction=(t.point(raw.anchors[-1])-t.point(raw.anchors[0])).normalized()
  check(measured.remaining(center-direction,center+direction).is_empty(),"a surveyed dash must displace the guessed dash")
  var outside=center+Vector2(-direction.y,direction.x)*8.
  var retained=measured.remaining(outside-direction,outside+direction)
  check(retained.size()==1 and retained[0][0].distance_to(outside-direction)<.002,"neighboring road must retain its stripe")
  check(measured.footprints.size()==129,"two arrow polygons and 127 terrain-following paint pieces expected")
 road[2]="Other road"
 check(script.for_tile(t)==null,"changed road name must fall back")
 road[2]=raw.road;road[1]=-1
 check(script.for_tile(t)==null,"wrong carriageway direction must fall back")
 road[1]=1;road[5]="unpaved"
 check(script.for_tile(t)==null,"unpaved surface must fall back")
 road[5]="paved";road[3]=[[.10,.10],[.25,.10]]
 check(script.for_tile(t)==null,"realigned road must fall back")
 t.info.id=[19818,24238]
 check(script.for_tile(t)==null,"Boston control must have no paint asset")
 if failures.is_empty():print("OBSERVED PAINT PASS");quit(0)
 else:
  for failure in failures:push_error(failure)
  quit(1)
