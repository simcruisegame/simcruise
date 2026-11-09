extends SceneTree
# Run with Godot --headless --path game --script ../lab/check_building_records.gd
const STREAM = preload("res://BuildingStream.gd")

func _initialize():
 var buildings=[[[[]],[],5.,0.,.4,.6]]
 var valid={"baseIndex":0.,"center":[.4,.6],"style":{"shape":"gabled","roof_height":2.5,"across":true}}
 var ok=STREAM.matched_index(valid,buildings)==0 and STREAM.valid_finish(valid.style)
 for value in [null,"0",-1.0,.1,INF]:
  var invalid=valid.duplicate()
  invalid.baseIndex=value
  ok=ok and STREAM.matched_index(invalid,buildings)<0
 for center in [null,[".4",.6],[NAN,.6],[.4,.599],[.4]]:
  var invalid=valid.duplicate()
  invalid.center=center
  ok=ok and STREAM.matched_index(invalid,buildings)<0
 for invalid in [null,[],{},{"wall_color":false},{"across":"false"},{"roof_height":NAN}]:
  ok=ok and not STREAM.valid_finish(invalid)
 ok=ok and STREAM.matched_index(null,buildings)<0 and STREAM.rows({"finishes":null},"finishes").is_empty()
 print("Building record validation passed" if ok else "Building record validation failed")
 quit(0 if ok else 1)
