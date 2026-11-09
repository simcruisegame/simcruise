extends RefCounted
# The mask identifies a forest stand, not the species of any individual tree.
var grid := 0
var bits := PackedByteArray()
var years := []
var quadrant := Vector2i.ZERO

static func for_tile(id: Vector2i):
 var region := Vector2i(id.x >> 2,id.y >> 2)
 var path := "res://data/forest-form-%d-%d.json" % [region.x,region.y]
 if not FileAccess.file_exists(path):return null
 return from_document(id,JSON.parse_string(FileAccess.get_file_as_string(path)))

static func from_document(id: Vector2i,doc):
 var region := Vector2i(id.x >> 2,id.y >> 2)
 if not doc is Dictionary or int(doc.get("version",0)) != 1:return null
 var tile = doc.get("tile",[])
 if not tile is Array or tile.size()!=2 or int(tile[0])!=region.x or int(tile[1])!=region.y:return null
 var size := int(doc.get("grid",0))
 if size<12 or size>128:return null
 var encoded := str(doc.get("bits",""))
 if encoded.length()!=ceili(ceili(size*size/8.)/3.)*4:return null
 var decoded := Marshalls.base64_to_raw(encoded)
 if decoded.size() != ceili(size*size/8.):return null
 var result=load("res://ForestForm.gd").new()
 result.years=[doc.get("evtYear"),doc.get("nlcdYear")]
 result.grid=size
 result.bits=decoded
 result.quadrant=Vector2i(id.x % 4,id.y % 4)
 return result

func conifer(uv: Vector2) -> bool:
 var cell := Vector2i(((Vector2(quadrant)+uv)*(.25*grid)).floor())
 if cell.x<0 or cell.x>=grid or cell.y<0 or cell.y>=grid:return false
 var bit := cell.y*grid+cell.x
 return (bits[bit >> 3] & (1 << (bit % 8))) != 0
