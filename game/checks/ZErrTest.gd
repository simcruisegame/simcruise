extends SceneTree
# Scratch (not committed): does ScriptErrorWatch see a script error?
func _initialize():
 var watch=preload("res://checks/ScriptErrorWatch.gd").install()
 boom()
 print("WATCH count=%d first=%s"%[watch.count,watch.first])
 quit(0)
func boom():
 var d={}
 print(d.missing_key)
