extends SceneTree
const C=preload("res://SignalControls.gd")
const W=preload("res://WorldTile.gd")
var failures=[]
func check(ok:bool,message:String):
 if not ok:failures.append(message)
func _initialize():
 var tile=W.new();tile.side=200.
 var a=Vector2(20,100);var b=Vector2(180,100)
 var mid=Vector2(100,100)
 tile.segments=[[a,b,"Market",12.,1,"primary"],[Vector2(100,40),mid,"Oak",8.,0,"minor"],[mid,Vector2(100,160),"Oak",8.,0,"minor"]]
 var sources=[{"uv":[.502,.502],"osm":"n1@2"}]
 var matches=C.crossings(tile,sources)
 check(matches.size()==1,"reconstruct a single real, traversing junction")
 if matches.size()==1:
  check(matches[0].arms.size()==4,"through road creates two opposite arms")
  check(matches[0].arms.filter(func(arm):return arm.incoming).size()==3,"one-way travel restricts signal orientation")
 check(C.match_records(200.,sources+sources,matches,[]).size()==1,"same crossing drawn once")
 check(C.match_records(200.,sources,matches+matches,[]).is_empty(),"ambiguous junction withheld")
 check(C.match_records(200.,sources,matches,[[.5,.5]]).is_empty(),"previously inventoried fixtures respected")
 check(C.crossings(tile,[{"uv":[.55,.5]}]).is_empty(),"offset crossing rejected")
 check(C.crossings(tile,[null,{"uv":["bad",.4]},{"uv":[NAN,.5]}]).is_empty(),"invalid observations rejected")
 tile.segments.pop_back();tile.segments[0][2]="Oak"
 check(C.crossings(tile,sources).is_empty(),"single named street insufficient")
 tile.free()
 if not failures.is_empty():push_error(", ".join(failures));quit(1)
 else:print("SIGNAL CONTROL PASS · junction, one-way, offset, duplicate, existing and malformed-source gates");quit(0)
