extends RefCounted
# Archive lookup is triggered by a located, UTC-dated still. One low-priority
# worker shares the helper's on-disk station/time cache across nearby frames.
var worker:Thread
var records:Dictionary={}
var completed_requests:=0
var pending:=""
var next_request_ms:=0
var enabled:=true
var last:Dictionary={}

func lookup(helper:String,cache:String,location:Vector2,epoch:int,elevation:float) -> Dictionary:
 var now:=Time.get_ticks_msec()
 if worker and not worker.is_alive():
  var result=worker.wait_to_finish()
  worker=null
  completed_requests+=int(result.get("requests",0))
  if records.size()>=48:records.erase(records.keys()[0])
  records[pending]={"data":result,"expires":now+(15*60000 if result.get("status")=="unavailable" else 86400000)}
 if not enabled or epoch<=0:return {}
 var key:="%d,%d,%d,%d"%[roundi(location.x*100),roundi(location.y*100),epoch,roundi(elevation/50.)]
 var entry:Dictionary=records.get(key,{})
 if not entry.is_empty() and int(entry.expires)>now:
  last=entry.data
  return last
 last={}
 if not worker and now>=next_request_ms:
  next_request_ms=now+1500
  pending=key
  worker=Thread.new()
  worker.start(fetch.bind(helper,cache,location,epoch,elevation),Thread.PRIORITY_LOW)
 return {}

func fetch(helper:String,cache:String,location:Vector2,epoch:int,elevation:float) -> Dictionary:
 var lines=[]
 var code=OS.execute(helper,["archive-weather",cache,str(location.x),str(location.y),str(epoch),str(elevation)],lines,true)
 if code==0 and not lines.is_empty():
  var result=JSON.parse_string(str(lines[0]))
  if result is Dictionary:return result
 return {"status":"unavailable"}

func shutdown():
 if worker:
  worker.wait_to_finish()
  worker=null
