extends RefCounted

# Dated street photography and municipal lidar for the pre-renovation exterior
# at 601 West Fifth. The present exterior was substantially rebuilt in 2023.
const TILE := Vector2i(5480,18579)
const FIRST_DATE := 20070720
const LAST_DATE := 20150522
const PITCH_M := 3.98443
const RING := [[58887,77246],[64551,77246],[64551,76074],[67480,76074],[67480,77148],[69434,77148],[69434,81641],[68164,81641],[63281,86426],[63281,89551],[58984,89551]]
const SHADER := preload("res://HistoricRibbon.gdshader")

static func _date_number(parts:Dictionary) -> int:
 return int(parts.year)*10000+int(parts.month)*100+int(parts.day)

static func _anchorage_date(epoch:int) -> int:
 # Post-2007 US daylight time. At 02:00 local the March transition is
 # 11:00 UTC, and the November transition is 10:00 UTC.
 var year:int=Time.get_datetime_dict_from_unix_time(epoch).year
 var march:=int(Time.get_unix_time_from_datetime_dict({"year":year,"month":3,"day":1,"hour":11,"minute":0,"second":0}))
 var november:=int(Time.get_unix_time_from_datetime_dict({"year":year,"month":11,"day":1,"hour":10,"minute":0,"second":0}))
 var start:=march+(7+posmod(-int(Time.get_datetime_dict_from_unix_time(march).weekday),7))*86400
 var finish:=november+posmod(-int(Time.get_datetime_dict_from_unix_time(november).weekday),7)*86400
 return _date_number(Time.get_datetime_dict_from_unix_time(epoch-(8 if epoch>=start and epoch<finish else 9)*3600))

static func enabled(args:PackedStringArray) -> bool:
 var i:=args.find("--source-era")
 if i<0 or args.count("--source-era")!=1 or i+1>=args.size():return false
 var source:=args[i+1]
 if source.length()!=10 or source[4]!="-" or source[7]!="-":return false
 var year:=source.substr(0,4).to_int();var month:=source.substr(5,2).to_int();var day:=source.substr(8,2).to_int()
 var number:=year*10000+month*100+day
 if number<FIRST_DATE or number>LAST_DATE or month<1 or month>12:return false
 if source!="%04d-%02d-%02d" % [year,month,day]:return false
 var days: Array[int]=[31,29 if year%4==0 else 28,31,30,31,30,31,31,30,31,30,31]
 if day<1 or day>days[month-1]:return false
 var j:=args.find("--reference-epoch")
 if j<0:return true
 if args.count("--reference-epoch")!=1 or j+1>=args.size() or not args[j+1].is_valid_int():return false
 var epoch:=args[j+1].to_int()
 return epoch>1104537600 and epoch<1483228800 and _anchorage_date(epoch)==number

static func matches(id,building,profile:Dictionary) -> bool:
 if int(id[0])!=TILE.x or int(id[1])!=TILE.y or absf(float(profile.get("eave_m",-1.))-39.2)>.02:return false
 if building[0].size()!=1 or building[0][0].size()!=RING.size() or absf(float(building[3]))>.1:return false
 var ring=building[0][0]
 for i in RING.size():
  if Vector2(ring[i][0],ring[i][1]).distance_to(Vector2(RING[i][0],RING[i][1])*.00001)>.000015:return false
 return true

static func draw(tile,edge:int,a:Vector2,b:Vector2,normal:Vector3,lo:float,hi:float,top:float,base_key,repeats:Vector2) -> bool:
 if edge<5 or edge>9:return false
 var cut:=top-8.*PITCH_M
 if cut<=lo or cut>=hi:return false
 var key:="anchorage_601_historic_cladding"
 if not tile.materials.has(key):
  var mat:=ShaderMaterial.new()
  mat.shader=SHADER
  mat.set_shader_parameter("eave",top)
  tile.materials[key]=mat
 # Clip the existing lower wall at the unobserved podium, retaining its exact
 # UVs. The ribbon section shares vertices with that wall without an overlay.
 var original=tile.tool(base_key)
 var uv_above:=repeats.y*(hi-cut)/(hi-lo)
 var l=tile.vert(a,lo);var r=tile.vert(b,lo);var tl=tile.vert(a,cut);var tr=tile.vert(b,cut)
 tile.tri(original,l,r,tr,repeats,Vector2(0,repeats.y),Vector2(0,uv_above),normal)
 tile.tri(original,l,tr,tl,repeats,Vector2(0,uv_above),Vector2(repeats.x,uv_above),normal)
 tile.quad(key,tl,tr,tile.vert(b,hi),tile.vert(a,hi),normal,Vector2(a.distance_to(b),hi-cut))
 return true
