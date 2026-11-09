extends RefCounted
# A stable look direction can request a more detailed orthophoto on each of
# the two most visible distant ridges. The normal world keeps drawing meanwhile.
const EMPTY:=Vector2i(-1,-1)
var worker:Thread
var requested:=EMPTY
var requested_sector:=-1
var checked:=EMPTY
var checked_sector:=-1
var next_check:=0
var held_since:=0
var observed_region:=EMPTY
var observed_sector:=-1
var last_result:Dictionary={}
var cells:Array=[]

func read_images(helper:String,cache:String,id:Vector2i,eye:Vector3,bearing:float)->Dictionary:
 var output=[]
 var args=["ridge",cache,str(id.x),str(id.y),str(eye.x),str(eye.y),str(eye.z),str(bearing)]
 if OS.execute(helper,args,output,true)!=0 or output.is_empty():return {}
 var data=JSON.parse_string(output[0])
 if not data is Dictionary or data.get("region")!=[float(id.x),float(id.y)] or not data.get("images") is Array or data.images.size()>2:return {}
 var layers=[]
 for entry in data.images:
  if not entry is Dictionary or not entry.get("cell") is Array or entry.cell.size()!=2:return {}
  var name="ridge-aerial-v1-11-%d-%d.jpg"%[int(entry.cell[0]),int(entry.cell[1])]
  if entry.get("name")!=name or not entry.get("uv") is Array or entry.uv.size()!=4:return {}
  var rect:Vector4=Vector4(entry.uv[0],entry.uv[1],entry.uv[2],entry.uv[3])
  if not rect.is_finite() or rect.z<=0. or rect.w<=0.:return {}
  var file:=FileAccess.open(cache.path_join(name),FileAccess.READ)
  if not file or file.get_length()>1400000:continue
  var im:=Image.new()
  if im.load_jpg_from_buffer(file.get_buffer(file.get_length()))!=OK or im.get_size()!=Vector2i(1536,1536):continue
  im.convert(Image.FORMAT_RGB8)
  im.generate_mipmaps()
  layers.append({"image":im,"rect":rect,"id":Vector2i(int(entry.cell[0]),int(entry.cell[1]))})
 data["layers"]=layers
 return data

func tick(game,terrain):
 var region:Vector2i=terrain.displayed
 var now:=Time.get_ticks_msec()
 if worker and not worker.is_alive():
  var data=worker.wait_to_finish()
  worker=null
  game.calls+=int(data.get("requests",0))
  if requested==region and data.has("layers"):
   last_result={"region":region,"sector":requested_sector,"expected":data.get("expected",0),"requests":data.get("requests",0),"images":data.images}
   cells.clear()
   for i in data.layers.size():
    cells.append({"id":data.layers[i].id,"rect":data.layers[i].rect})
    terrain.finish.set_shader_parameter("ridge_image_"+str(i),ImageTexture.create_from_image(data.layers[i].image))
    terrain.finish.set_shader_parameter("ridge_uv_"+str(i),data.layers[i].rect)
   terrain.finish.set_shader_parameter("ridge_count",data.layers.size())
   if data.get("expected")!=null and data.layers.size()==int(data.expected):
    checked=region
    checked_sector=requested_sector
   else:next_check=now+180000
 if region==EMPTY or worker or not game.ready_drive or not game.jump.is_empty():return
 var camera:Camera3D=game.get_viewport().get_camera_3d()
 if not camera:return
 var forward=-camera.global_basis.z
 var angle=fposmod(rad_to_deg(atan2(forward.x,-forward.z)),360.)
 var sector=roundi(angle/22.5)%16
 if region!=observed_region or sector!=observed_sector:
  if region!=observed_region:next_check=now
  observed_region=region
  observed_sector=sector
  held_since=now
 if (region==checked and sector==checked_sector) or now<next_check or now-held_since<1600:return
 var eye=camera.global_position-terrain.instance.global_position
 if maxf(absf(eye.x),absf(eye.z))>14000.:return
 requested=region
 requested_sector=sector
 next_check=now+25000
 worker=Thread.new()
 if worker.start(read_images.bind(game.helper,game.cache,region,eye,angle),Thread.PRIORITY_LOW)!=OK:worker=null

func shutdown():
 if worker:
  worker.wait_to_finish()
  worker=null
