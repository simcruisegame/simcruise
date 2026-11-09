extends RefCounted
# A city LiDAR polygon and its tree-inventory trunk constrain the summer mesh.
# The radial samples are east, then clockwise toward south, in ground metres.
const MAX_MODELS := 12
const MAX_ACTIVE := 8
var templates:Dictionary = {}
var instances := {}
var requests := {}
var active := {}
var selected := {}
var selection_changed := false
var last_epoch := -1
var epoch_year := 0
var serial := 0
var worker:Thread
var pending_key := ""
var pending_materials := []
var pending_rgb := 0.
var pending_slot := 0

func set_epoch(epoch:int)->bool:
 if last_epoch==epoch:return false
 last_epoch=epoch
 var year=int(Time.get_datetime_dict_from_unix_time(epoch).year) if epoch>0 else int(Time.get_date_dict_from_system(true).year)
 var changed=year!=epoch_year
 epoch_year=year
 return changed

func dated(profile:Dictionary)->bool:
 return profile.get("source")=="cambridge-2024" and int(profile.get("year",0))==2024 and absi(epoch_year-2024)<=3

func begin_frame():
 serial+=1
 active.clear()
 requests.clear()

func offer(mesh:Mesh,row:Array,slot:int,transform:Transform3D,pigment:float,distance_sq:float)->bool:
 var profile:Dictionary=row[6]
 # This edition has a documented independent layer, metric units and named source.
 if not dated(profile):return false
 if not profile.get("radii") is Array or profile.radii.size()!=16 or not profile.get("height") is float and not profile.get("height") is int:return false
 var key="%s/%s/%d"%[profile.source,str(profile.get("objectId","")),slot]
 var input={"key":key,"mesh":mesh,"profile":profile,"angle":row[2],"width":row[3],"height":row[1],"slot":slot,"pigment":pigment,"distance":distance_sq,"transform":transform}
 requests[key]=input
 if not instances.has(key) or not selected.has(key):return false
 var node=instances[key].node
 instances[key].last=serial
 node.transform=transform
 node.show()
 active[key]=true
 return true

func end_frame():
 for key in instances:
  if not active.has(key):instances[key].node.hide()
 var candidates=requests.values()
 candidates.sort_custom(func(a,b):return a.distance<b.distance if a.distance!=b.distance else a.key<b.key)
 candidates=candidates.slice(0,MAX_ACTIVE)
 var nearby={}
 for task in candidates:nearby[task.key]=true
 if nearby!=selected:selected=nearby;selection_changed=true
 if worker!=null:return
 for task in candidates:
  if instances.has(task.key):continue
  if templates.is_empty():
   var doc=JSON.parse_string(FileAccess.get_file_as_string("res://data/municipal-template-radii.json"))
   if not doc is Dictionary:return
   templates=doc
  var kind=["oak","leaf","pine"][floori(task.slot/3.)]
  var template:Array=templates.get("%s-%d"%[kind,int(task.slot)%3],[])
  if template.size()!=72:continue
  var radii:PackedFloat32Array=PackedFloat32Array(task.profile.radii)
  if radii.size()!=16:continue
  var valid=true
  for radius in radii:
   if not is_finite(radius) or radius<1.0 or radius>22.01:valid=false;break
  var h:float=task.profile.height
  if not valid or not is_finite(h) or h<4. or h>55. or task.width<1.:continue
  var sections=[];pending_materials=[]
  for i in task.mesh.get_surface_count():
   sections.append(task.mesh.surface_get_arrays(i));pending_materials.append(task.mesh.surface_get_material(i))
  pending_key=task.key;pending_rgb=task.pigment;pending_slot=task.slot
  worker=Thread.new()
  var ok=worker.start(deform.bind(sections,radii,PackedFloat32Array(template),task.width,h/task.height,task.angle),Thread.PRIORITY_LOW)
  if ok!=OK:worker=null;pending_key="";pending_materials.clear()
  return

static func deform(sections:Array,radii:PackedFloat32Array,template:PackedFloat32Array,width:float,height_ratio:float,angle:float)->Array:
 for arrays in sections:
  var vertices:PackedVector3Array=arrays[Mesh.ARRAY_VERTEX]
  for i in vertices.size():
   var v=vertices[i]
   var azimuth=atan2(v.z,v.x)
   var local_sector=fposmod(azimuth,TAU)*72./TAU
   var k=floori(local_sector)
   var nominal=width*lerpf(template[k],template[(k+1)%72],local_sector-k)
   # In Godot a positive rotation around UP rotates +X toward -Z.
   var sector=fposmod(azimuth-angle,TAU)*16./TAU
   var j=floori(sector)
   var radius=lerpf(radii[j],radii[(j+1)%16],sector-j)
   var gain=clampf(radius/maxf(.15,nominal),.38,2.60)
   gain=lerpf(1.,gain,smoothstep(.22,.52,v.y)*smoothstep(.025,.22,Vector2(v.x,v.z).length()))
   vertices[i]=Vector3(v.x*gain,v.y*height_ratio,v.z*gain)
  arrays[Mesh.ARRAY_VERTEX]=vertices
 return sections

func poll(grove:Node3D)->bool:
 if worker==null or worker.is_alive():
  var changed=selection_changed;selection_changed=false
  return changed
 var sections=worker.wait_to_finish();worker=null
 if not selected.has(pending_key):pending_materials.clear();return true
 while instances.size()>=MAX_MODELS:
  var oldest="";var first=9223372036854775807
  for key in instances:
   if not active.has(key) and instances[key].last<first:oldest=key;first=instances[key].last
  if oldest=="":return false
  instances[oldest].node.queue_free();instances.erase(oldest)
 var surface=ArrayMesh.new()
 for i in sections.size():
  surface.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,sections[i]);surface.surface_set_material(i,pending_materials[i])
 pending_materials.clear()
 var batch=MultiMesh.new();batch.transform_format=MultiMesh.TRANSFORM_3D;batch.use_custom_data=true;batch.mesh=surface;batch.instance_count=1
 batch.set_instance_transform(0,Transform3D.IDENTITY);batch.set_instance_custom_data(0,Color(pending_rgb,0,0,0))
 var node=MultiMeshInstance3D.new();node.multimesh=batch
 if pending_slot%3==2:node.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
 node.hide();grove.add_child(node);instances[pending_key]={"node":node,"last":serial}
 return true

func shutdown():
 if worker!=null:worker.wait_to_finish();worker=null
