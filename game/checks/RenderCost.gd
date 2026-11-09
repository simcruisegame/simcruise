extends Node
# Optional, repeatable comparison of an identical real scene with/without its vegetation draw.
var game
var elapsed=0.
var phase=-1
var previous=0
var warm=0.
var datasets=[[],[]]
var active=0
func _ready():game=get_parent();RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(),true)
func _process(dt):
 if not game.ready_drive or game.job or game.tiles.size()<4:return
 var v=get_viewport().get_viewport_rid();var us=Time.get_ticks_usec()
 if phase<0:
  warm+=dt
  if warm<2:return
  phase=0;previous=us;game.hud.message("");return
 var stamp=(us-previous)/1000.;previous=us;elapsed+=dt
 if elapsed>.55:
  datasets[active].append([stamp,RenderingServer.viewport_get_measured_render_time_cpu(v),RenderingServer.viewport_get_measured_render_time_gpu(v),Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME),Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME)])
 if elapsed<2.6:return
 elapsed=0;phase+=1
 if phase<4:
  active=1 if phase in [1,2] else 0;game.grove.visible=active==0;return
 var output={};var names=["frame_ms","render_cpu_ms","render_gpu_ms","draw_calls","primitives"]
 for i in 2:
  var block={"frames":datasets[i].size()}
  for j in 5:
   var vals=[]
   for row in datasets[i]:vals.append(row[j])
   vals.sort();block[names[j]]={"median":vals[int(vals.size()*.5)],"p95":vals[int(vals.size()*.95)]}
  output["trees_on" if i==0 else "trees_off"]=block
 output["tree_instances"]=[game.grove.rings[0].visible_instance_count,game.grove.rings[1].visible_instance_count,game.grove.rings[2].visible_instance_count]
 var dir=OS.get_environment("DRIVE_QA");if dir=="":dir=ProjectSettings.globalize_path("user://road-test")
 DirAccess.make_dir_recursive_absolute(dir);var f=FileAccess.open(dir+"/render-cost.json",FileAccess.WRITE);f.store_string(JSON.stringify(output,"  "));f.close();get_tree().quit()
