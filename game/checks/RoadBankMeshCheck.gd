extends SceneTree
## godot --headless --path game --script res://checks/RoadBankMeshCheck.gd
const BANKS=preload("res://RoadBankMesh.gd")

func _initialize():
 var base=[];base.resize(65*65)
 for j in 65:
  for i in 65:base[j*65+i]=.1*i-.03*j+.2*((i+j)%2)
 var index=61*129+71
 var tile={"grid":64,"h":base,"road_banks":{"detailGrid":128,"datum":"NAVD88","deltas":[index,870]}}
 var certified=tile.duplicate(true);certified.survey_ground_nodes=5
 BANKS.apply(certified)
 if certified.grid!=64 or certified.h!=base:quit(2);return
 var missing=tile.duplicate(true);missing.road_banks.deltas=[0,1000]
 BANKS.apply(missing)
 if missing.grid!=64:quit(3);return
 BANKS.apply(tile)
 if tile.grid!=128 or not tile.get("bank_detail_active",false):quit(4);return
 var k=30*65+35
 if absf(tile.h[index]-(base[k+1]+base[k+65])*.5-.87)>.0000001:quit(5);return
 if absf(tile.h[4*129+4]-base[2*65+2])>.0000001:quit(6);return
 var m=BANKS.make(tile.h,128.,StandardMaterial3D.new());var vertices=m.get_faces();var area=0.
 if vertices.size()<=8192*3 or vertices.size()>=32768*3:quit(7);return
 for i in range(0,vertices.size(),3):
  var normal=(vertices[i+1]-vertices[i]).cross(vertices[i+2]-vertices[i])
  if normal.y>=0.:quit(8);return
  area-=normal.y*.5
 var collider=m.create_trimesh_shape()
 if absf(area-128.*128.)>.0001 or collider.get_faces().size()!=vertices.size():quit(9);return
 print("Survey takes precedence; the adaptive road-bank mesh covers the tile and shares every face with the collider.")
 quit()
