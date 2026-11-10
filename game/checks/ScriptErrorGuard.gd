extends Logger
## Counts the script errors a check provokes, so the check itself can fail on
## them. A GDScript runtime error ("SCRIPT ERROR: Invalid access ...") only
## abandons the function it happened in; a check that goes on and prints its
## own PASS would otherwise hide it outside scripts/check.sh, which greps logs.
##   var guard = preload("res://checks/ScriptErrorGuard.gd").install()
##   ... ; if guard.count() > 0: fail
## Counts script errors and errors raised from res:// scripts (push_error).

var errors := 0
var first := ""
var lock := Mutex.new()

static func install():
 var guard = new()
 OS.add_logger(guard)
 return guard

func _log_error(function: String, file: String, line: int, code: String, rationale: String, _editor_notify: bool, error_type: int, _script_backtrace: Array[ScriptBacktrace]) -> void:
 if error_type == ERROR_TYPE_WARNING:return
 if error_type != ERROR_TYPE_SCRIPT and not file.begins_with("res://"):return
 lock.lock()
 errors += 1
 if first == "":first = "%s (%s:%d %s)" % [rationale if rationale != "" else code, file, line, function]
 lock.unlock()

func _log_message(_message: String, _error: bool) -> void:
 pass

func count() -> int:
 lock.lock()
 var n := errors
 lock.unlock()
 return n
