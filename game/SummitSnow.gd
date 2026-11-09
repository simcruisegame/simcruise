extends RefCounted
# One small classification window at a time. Only clear satellite pixels can
# replace the broad snow observation. This never modifies the driving surface.
const EMPTY:=Vector2i(-1,-1)
var worker:Thread
var requested:=EMPTY
var requested_date:=""
var attached_region:=EMPTY
var attached_date:=""
var next_request:=0
var layers:Dictionary={}
var attached:Array=[]
var last_result:Dictionary={}

func read_patch(helper:String,directory:String,cell:Vector2i,date:String)->Dictionary:
 var lines=[]
 if OS.execute(helper,["summit-snow",directory,str(cell.x),str(cell.y),date],lines,true)!=0 or lines.is_empty():return {}
 var data=JSON.parse_string(lines[0])
 if not data is Dictionary or data.get("x")!=cell.x or data.get("y")!=cell.y or data.get("date")!=date:return {}
 if data.get("status")=="available":
  var name:="summit-snow-v2-%d-%d-%s.png"%[cell.x,cell.y,date]
  if data.get("file")!=name:return {}
  var file:=FileAccess.open(directory.path_join(name),FileAccess.READ)
  if not file or file.get_length()>400000:return {}
  var im:=Image.new()
  if im.load_png_from_buffer(file.get_buffer(file.get_length()))!=OK or im.get_size()!=Vector2i(384,384):return {}
  im.convert(Image.FORMAT_RGBA8)
  im.generate_mipmaps()
  data["image"]=im
 return data

func intersects_snow(mask:Image,rect:Vector4)->bool:
 var size:Vector2i=mask.get_size()
 var lo=Vector2i(floori(rect.x*size.x),floori(rect.y*size.y)).max(Vector2i.ZERO)
 var hi=Vector2i(ceili((rect.x+rect.z)*size.x),ceili((rect.y+rect.w)*size.y)).min(size)
 for y in range(lo.y,hi.y,2):
  for x in range(lo.x,hi.x,2):
   if mask.get_pixel(x,y).r>=.40:return true
 return false

func tick(game,terrain):
 var ridge=terrain.ridge
 var snow=terrain.snow
 var region:Vector2i=terrain.displayed
 var date:String=snow.checked_date
 if region!=attached_region or date!=attached_date:
  layers.clear()
  attached.clear()
  terrain.finish.set_shader_parameter("has_fine_0",false)
  terrain.finish.set_shader_parameter("has_fine_1",false)
  next_request=0
  attached_region=region
  attached_date=date
 var now:=Time.get_ticks_msec()
 if worker and not worker.is_alive():
  var response:Dictionary=worker.wait_to_finish()
  worker=null
  game.calls+=int(response.get("requests",0))
  if requested_date==date and layers.has(requested):
   var record:Dictionary=layers[requested]
   if response.has("image"):record["texture"]=ImageTexture.create_from_image(response.image)
   elif response.get("status")!="unobserved":record["retry"]=now+300000
   response.erase("image")
   last_result=response
 var cells:Array=ridge.cells if snow.mask and snow.checked_region==region and (game.recent_mode or not snow.date_override.is_empty()) else []
 var active=[]
 for cell:Dictionary in cells:
  var key:Vector2i=cell.id
  if not layers.has(key):
   layers[key]={"allowed":intersects_snow(snow.mask,cell.rect),"started":false}
  var entry:Dictionary=layers[key]
  active.append(entry.get("texture"))
  if not entry.allowed or entry.has("texture") or worker:continue
  if entry.started and now<int(entry.get("retry",9223372036854775807)):continue
  if now<next_request and not FileAccess.file_exists(game.cache.path_join("summit-snow-v2-%d-%d-%s.json"%[key.x,key.y,date])):continue
  requested=key
  requested_date=date
  entry["started"]=true
  next_request=now+45000
  worker=Thread.new()
  if worker.start(read_patch.bind(game.helper,game.cache,key,date),Thread.PRIORITY_LOW)!=OK:
   worker=null
   entry["retry"]=now+10000
 if layers.size()>12:
  for key in layers.keys():
   if key!=requested and not cells.any(func(c):return c.id==key):layers.erase(key)
 if active!=attached:
  attached=active
  for i in 2:
   var enabled:bool=i<active.size() and active[i]!=null
   terrain.finish.set_shader_parameter("has_fine_"+str(i),enabled)
   if enabled:terrain.finish.set_shader_parameter("fine_snow_"+str(i),active[i])

func shutdown():
 if worker:
  worker.wait_to_finish()
  worker=null
