extends SceneTree

func _initialize():
 var tile=preload("res://WorldTile.gd").new()
 var photo=preload("res://PhotoFacades.gd").new()
 tile.info={"id":[19880,23726]};tile.side=437.
 var m=JSON.parse_string(FileAccess.get_file_as_string("res://data/photo-facades-19880-23726.json"))
 var row=[[m.ring],[],5.,0.,m.center[0],m.center[1]]
 var original=photo.find(tile,row)
 var shifted=row.duplicate(true)
 shifted[0][0][3][0]+=.004
 var reordered=row.duplicate(true)
 reordered[0][0].reverse()
 var raised=row.duplicate(true)
 raised[3]=3.
 var passes=not original.is_empty() and photo.find(tile,shifted).is_empty() and photo.find(tile,reordered).is_empty() and photo.find(tile,raised).is_empty() and photo.find(tile,row,{"eave_m":15}).is_empty() and not photo.find(tile,row,{"eave_m":9.49}).is_empty()
 var files=[str(m.masonry_file)]
 for face in m.faces:files.append(str(face.file))
 for file in files:
  var image=Image.new()
  var bytes=FileAccess.get_file_as_bytes("res://data/"+file)
  passes=passes and not bytes.is_empty() and image.load_png_from_buffer(bytes)==OK and image.get_width()>=48
 var other=preload("res://WorldTile.gd").new()
 other.info={"id":[19879,23726]};other.side=tile.side
 passes=passes and preload("res://PhotoFacades.gd").new().find(other,row).is_empty()
 var preset=preload("res://Hud.gd").davidson_hall()
 passes=passes and preset.street=="Church Street" and is_equal_approx(preset.bearing,341.23)
 print("Photo facade fingerprint, decoded assets and measured-height cases ",passes)
 tile.free();other.free();quit(0 if passes else 2)
