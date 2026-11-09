extends SceneTree

const SURVEY=preload("res://MeasuredSection.gd")


func _initialize():
 var args=OS.get_cmdline_user_args()
 if args.size()!=2:printerr("Pass game-map.json geometry-proof.json");quit(1);return
 var tile=preload("res://WorldTile.gd").new()
 tile.side=451.51
 tile.info=JSON.parse_string(FileAccess.get_file_as_string(args[0]))
 var survey=SURVEY.for_tile(tile)
 if survey==null:printerr("no matched westbound section");tile.free();quit(1);return
 tile.measured_section=survey
 var proof=JSON.parse_string(FileAccess.get_file_as_string(args[1]))
 var n=0
 for probe in proof.probes:
  for key in ["inner","shoulder","outside"]:
   var sample=survey.surface(tile.point(probe[key]),tile.side)
   if not is_equal_approx(sample,(-1. if key=="outside" else 1.)):printerr("unexpected sample ",probe.s," ",key," ",sample);tile.free();quit(2);return
   if key!="outside" and tile.wheel_grip(tile.point(probe[key]))!=1.:printerr("wrong wheel surface");tile.free();quit(3);return
   n+=1
 for probe in proof.medianProbes:
  if not is_equal_approx(tile.wheel_grip(tile.point(probe.uv)),.65):printerr("median ground mismatch");tile.free();quit(4);return
  n+=1
 var tested=0
 for road in tile.info.roads:
  if road[2]==survey.road_name and int(road[1])==1:
   road[3].reverse()
   tested+=1
 var reversed=SURVEY.for_tile(tile)
 if reversed!=null:printerr("accepted wrong-direction counterpart");tile.free();quit(5);return
 print("SURVEY CHECK matched tile; lane/shoulder/abstention/median samples=",n," paint=",survey.marks.size()," reversed rejection segments=",tested," median checks=",proof.medianProbes.size())
 tile.free();quit(0)
