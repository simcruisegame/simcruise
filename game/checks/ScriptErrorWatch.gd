extends Logger
## Counts script errors while a check runs, so the check itself can fail on
## them. A SCRIPT ERROR aborts only the function it happens in: a check that
## measures roads would print its table from whatever ran and exit 0 (run with
## --dir on field tiles, outside scripts/check.sh, nothing else looked at the
## log). install() at the start, then fail when count > 0 at the end.
var count:=0
var first:=""
var _mutex:=Mutex.new()

static func install()->Logger:
 var watch=new()
 OS.add_logger(watch)
 return watch

func _log_error(function:String,file:String,line:int,code:String,rationale:String,_editor_notify:bool,error_type:int,_script_backtraces:Array[ScriptBacktrace])->void:
 if error_type!=ERROR_TYPE_SCRIPT:return
 _mutex.lock()
 count+=1
 if first=="":first="%s (%s:%d %s)"%[rationale if rationale!="" else code,file,line,function]
 _mutex.unlock()

func _log_message(_message:String,_error:bool)->void:
 pass
