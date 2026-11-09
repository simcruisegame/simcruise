extends SceneTree
## godot --headless --path game --script res://checks/CrownArrivalOrder.gd

const TILE = preload("res://WorldTile.gd")
const EDGES = preload("res://RoadEdges.gd")

func fixture():
 var t=TILE.new()
 t.info={"h":[0,0,0,0],"grid":1}
 t.side=100.;t.grid_steps=1
 t.segments=[[Vector2(10,50),Vector2(90,50),"Test street",4.8,0,"minor"]]
 t.road_index()
 t.road_prior=Image.create_empty(32,32,false,Image.FORMAT_R8)
 t.streaming_ground=ShaderMaterial.new()
 t.streaming_ground.shader=preload("res://terrain.gdshader")
 return t

func _initialize():
 var rows=[[.50,.533,11.,10.,.52,"Linden",0,.50,.50,"street-A","matched","test-city"],[.40,.50,11.,10.]]
 var edges=Image.create_empty(32,32,false,Image.FORMAT_RG8)
 edges.fill(Color(1.,1.,0.))
 edges.set_pixel(16,17,Color(0.,.2,0.))
 var early=fixture();var late=fixture()
 early.replace_crowns(rows)
 var provisional=early.grove.size()
 var refreshed=early.accept_edges(edges)
 var no_rebuild=late.accept_edges(edges)
 late.replace_crowns(rows)
 var same=early.grove==late.grove and early.info.tree_inventories==late.info.tree_inventories
 var p:Vector3=early.grove[0][0] if early.grove.size()==1 else Vector3.ZERO
 var correct=early.grove.size()==1 and p.distance_to(Vector3(50.,0.,53.3))<.01
 var consumed=early.crowns_waiting_for_edges.is_empty() and late.crowns_waiting_for_edges.is_empty()
 early.free();late.free()
 if provisional!=0 or not refreshed or no_rebuild or not same or not correct or not consumed:
  push_error("Canopy depended on worker order or placed a trunk on a traveled lane.")
  quit(2);return
 print("Both worker orders place the surveyed verge tree and exclude the traffic-lane crown.")
 quit()
