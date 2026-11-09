extends SceneTree
func _initialize():
 var crown=preload("res://Rangeland.gd").new()
 var shape=crown.plant_mesh()
 var surface=shape.surface_get_arrays(0)
 var vertices:PackedVector3Array=surface[Mesh.ARRAY_VERTEX]
 var path=OS.get_cmdline_user_args()[0]
 var doc=FileAccess.open(path,FileAccess.WRITE)
 doc.store_buffer(vertices.to_byte_array())
 print("crown triangles=",vertices.size()/3," output=",path)
 crown.free()
 quit()
