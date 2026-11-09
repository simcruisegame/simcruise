extends SceneTree
## godot --headless --path game --script res://checks/CrossfallMeshCheck.gd -- TILE.json CACHE expected_runs [--banks]
## Optional --banks adds a controlled bank node inside the tile to exercise the adaptive source mesh.
const CROSS=preload("res://RoadCrossfallGround.gd")
const BANK=preload("res://RoadBankMesh.gd")

func _initialize():
 var args=OS.get_cmdline_user_args()
 if args.size()<3:quit(2);return
 var data=JSON.parse_string(FileAccess.get_file_as_string(args[0]));data["_source_cache"]=args[1]
 var expect=int(args[2])
 var tile=preload("res://WorldTile.gd").new();tile.info=data;tile.grid_steps=int(data.grid)
 tile.side=40075016.68557849/65536./cosh(PI*(1.-2.*(float(data.id[1])+.5)/65536.))
 var material=StandardMaterial3D.new()
 var arrays
 if "--banks" in args:
  data.road_banks={"detailGrid":128,"datum":"NAVD88","deltas":[45*129+45,320]}
  BANK.apply(data);tile.grid_steps=int(data.grid);arrays=BANK.make(data.h,tile.side,material).surface_get_arrays(0)
 else:
  var plane=PlaneMesh.new();plane.size=Vector2.ONE*tile.side;plane.subdivide_width=63;plane.subdivide_depth=63
  arrays=plane.get_mesh_arrays();var v=arrays[Mesh.ARRAY_VERTEX]
  for i in v.size():
   var p=Vector2(v[i].x,v[i].z)+Vector2.ONE*tile.side*.5
   v[i]=Vector3(p.x,tile.base_height(p),p.y)
  arrays[Mesh.ARRAY_VERTEX]=v
 var t=Time.get_ticks_usec();var surface=CROSS.for_tile(tile)
 var corrected=surface.refine(tile,arrays) if surface else arrays
 var elapsed=(Time.get_ticks_usec()-t)/1000.
 var info=tile.info.get("surveyed_crossfall",{})
 var actual=int(info.get("runs",0))
 if (expect==0 and actual!=0) or (expect>0 and actual<expect):
  print("Unexpected accepted runs ",actual," expected ",expect);tile.free();quit(3);return
 var faces:PackedInt32Array=corrected[Mesh.ARRAY_INDEX]
 var v:PackedVector3Array=corrected[Mesh.ARRAY_VERTEX]
 var edges={};var area=0.;var min_triangle=INF;var wrong=0
 for i in range(0,faces.size(),3):
  var a=v[faces[i]];var b=v[faces[i+1]];var c=v[faces[i+2]]
  var twice=-(b-a).cross(c-a).y
  if twice<=0.:wrong+=1
  min_triangle=minf(min_triangle,twice*.5);area+=twice*.5
  for e in 3:
   var f=faces[i+e];var g=faces[i+(e+1)%3];var key=Vector2i(mini(f,g),maxi(f,g))
   edges[key]=int(edges.get(key,0))+1
 var defects=0
 for edge in edges:
  if edges[edge]==2:continue
  var a=v[edge.x];var b=v[edge.y]
  if edges[edge]!=1 or not (maxf(absf(a.x),absf(b.x))<.001 or maxf(absf(a.z),absf(b.z))<.001 or maxf(absf(tile.side-a.x),absf(tile.side-b.x))<.001 or maxf(absf(tile.side-a.z),absf(tile.side-b.z))<.001):defects+=1
 var mesh=ArrayMesh.new();mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,corrected)
 var target=args.find("--mesh-out")
 if target>=0 and target+1<args.size():
  var f=FileAccess.open(args[target+1],FileAccess.WRITE)
  f.store_32(v.size());f.store_32(faces.size());f.store_buffer(v.to_byte_array());f.store_buffer(faces.to_byte_array());f.close()
 var contacts=mesh.create_trimesh_shape().get_faces()
 var result={"meshBuildMs":elapsed,"id":data.id,"runs":actual,"banks":tile.grid_steps==128,"faces":faces.size()/3,"colliderFaces":contacts.size()/3,"interiorOrNonmanifoldEdges":defects,"minimumTriangleM2":min_triangle,"inverted":wrong,"areaErrorM2":absf(tile.side*tile.side-area),"stats":info}
 print(JSON.stringify(result))
 tile.free()
 quit(4 if defects or wrong or contacts.size()!=faces.size() or result.areaErrorM2>.1 else 0)
