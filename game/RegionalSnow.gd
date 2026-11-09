extends RefCounted
# Satellite snow-index observations are a display layer on the distant landscape.
var worker: Thread
var request_region:=Vector2i(-1,-1)
var attached_region:=Vector2i(-1,-1)
var checked_region:=Vector2i(-1,-1)
var checked_date:=""
var next_check:=0
var date_override:=""
var result:Dictionary={}
var mask:Image

func _init():
 var args=OS.get_cmdline_user_args()
 var idx=args.find("--snow-date")
 if "--reference" in args and idx>=0 and idx+1<args.size():date_override=args[idx+1]

func read_observation(helper:String,directory:String,region:Vector2i,date:String)->Dictionary:
 var lines=[]
 if OS.execute(helper,["snow",directory,str(region.x),str(region.y),date],lines,true)!=0 or lines.is_empty():return {}
 var data=JSON.parse_string(lines[0])
 if not data is Dictionary or data.get("region")!=[float(region.x),float(region.y)] or data.get("date")!=date:return {}
 if data.get("mask") is String:
  var img:=Image.new()
  if img.load_png_from_buffer(Marshalls.base64_to_raw(data.mask))!=OK or img.get_size()!=Vector2i(256,256):return {}
  data["image"]=img
 data.erase("mask")
 return data

func tick(game,region:Vector2i,material:ShaderMaterial):
 if attached_region!=region:
  material.set_shader_parameter("has_snow",false)
  mask=null
  checked_region=Vector2i(-1,-1)
  next_check=0
  attached_region=region
 var now=Time.get_ticks_msec()
 if worker and not worker.is_alive():
  var response=worker.wait_to_finish()
  worker=null
  game.calls+=int(response.get("requests",0))
  if request_region==region:
   result=response.duplicate()
   result.erase("image")
   if response.get("status")=="available":
    checked_region=region
    checked_date=str(response.get("date"))
    mask=response.get("image")
    material.set_shader_parameter("has_snow",false)
    if int(response.get("tiles",0))<int(response.get("expected",0)):
     next_check=now+300000
    if response.has("image") and (game.recent_mode or not date_override.is_empty()):
     material.set_shader_parameter("snow_index",ImageTexture.create_from_image(response.image))
     material.set_shader_parameter("has_snow",true)
   else:next_check=now+300000
 if not game.recent_mode and date_override.is_empty():
  if checked_region==region:
   material.set_shader_parameter("has_snow",false)
   checked_region=Vector2i(-1,-1)
   mask=null
  return
 if region.x<0 or worker or now<next_check or not game.ready_drive or not game.jump.is_empty():return
 next_check=now+2000
 var date=date_override
 if date.is_empty():date=Time.get_date_string_from_unix_time(int(Time.get_unix_time_from_system())-3*86400)
 if region==checked_region and date==checked_date and int(result.get("tiles",0))==int(result.get("expected",0)):return
 request_region=region
 worker=Thread.new()
 if worker.start(read_observation.bind(game.helper,game.cache,region,date),Thread.PRIORITY_LOW)!=OK:worker=null

func shutdown():
 if worker:
  worker.wait_to_finish()
  worker=null
