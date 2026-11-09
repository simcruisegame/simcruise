extends RefCounted

## Apply sampled heights to the same grid used by terrain meshes and wheel contacts.
static func apply(data: Dictionary) -> int:
 var id: Array = data.get("id", [])
 if id.size() != 2 or data.get("field", false) or not data.get("roadbed", []).is_empty():
  return 0
 var path := "res://data/district-surface-%d-%d.json" % [id[0], id[1]]
 if not FileAccess.file_exists(path):
  return 0
 var document = JSON.parse_string(FileAccess.get_file_as_string(path))
 if not document is Dictionary or document.get("schema", 0) != 1 or document.get("tile", []) != id:
  return 0
 var grid := int(data.get("grid", 0))
 var heights: Array = data.get("h", [])
 if int(document.get("grid", 0)) != grid or heights.size() != (grid + 1) * (grid + 1):
  return 0
 var nodes: Array = document.get("nodes", [])
 if nodes.is_empty() or nodes.size() > 512:
  return 0
 # All original elevations must match before mutating the shared array.
 var last_index := -1
 for row in nodes:
  if not row is Array or row.size() != 3:
   return 0
  var index := int(row[0])
  if index <= last_index or index <= grid or index >= heights.size() - grid - 1:
   return 0
  if index % (grid + 1) in [0, grid]:
   return 0
  var original := float(row[1])
  var measured := float(row[2])
  if not is_finite(measured) or not is_finite(original):
   return 0
  if absf(float(heights[index]) - original) > .015 or absf(measured - original) > 9:
   return 0
  last_index = index
 for row in nodes:
  heights[int(row[0])] = row[2]
 data["survey_ground_nodes"] = nodes.size()
 return nodes.size()
