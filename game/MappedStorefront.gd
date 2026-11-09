extends RefCounted
## A main-road address is insufficient to establish a shop. Accept an explicit
## retail footprint or a named customer-facing POI inside a modest footprint.
const RETAIL=["retail","shop","supermarket","kiosk"]
const POI=["shop","grocery","bakery","pharmacy","restaurant","fast_food","cafe","clothing","books","florist","hairdresser","optician","department_store"]

static func accepts(building:Array,pois:Array,outline:PackedVector2Array,bounds:Rect2,side:float)->bool:
 if building.size()>7 and (building[6] in RETAIL or building[7] in RETAIL):return true
 if outline.size()<3 or bounds.get_area()>1800. or float(building[3])>1.:return false
 for place in pois:
  if not place is Array or place.size()<3 or not place[2] in POI or str(place[1]).strip_edges().is_empty():continue
  var xy=place[0]
  if not xy is Array or xy.size()!=2:continue
  var point=Vector2(xy[0],xy[1])*side
  if not bounds.has_point(point) or not Geometry2D.is_point_in_polygon(point,outline):continue
  var courtyard=false
  for i in range(1,building[0].size()):
   var hole=PackedVector2Array()
   for uv in building[0][i]:hole.append(Vector2(uv[0],uv[1])*side)
   if Geometry2D.is_point_in_polygon(point,hole):
    courtyard=true
    break
  if not courtyard:return true
 return false
