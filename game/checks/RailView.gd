extends Node
## --rail-view OUTPUT_DIRECTORY [--place=lon,lat,bearing]: loads a place, waits
## for it to stream, then frames the nearest guard-rail run (GuardRails.gd) and
## the road ribbon from a roadside camera and saves rails-N.jpg. Windowed only.
var game
var out := ""
var clock := 0.
var place := Vector3(-83.9202491, 35.4667227, 315.)
var shots := 0
func _ready():
 game = get_parent()
 var args := OS.get_cmdline_user_args()
 out = args[args.find("--rail-view") + 1]
 DirAccess.make_dir_recursive_absolute(out)
 for a in args:
  if a.begins_with("--place="):
   var p := a.trim_prefix("--place=").split(",")
   place = Vector3(float(p[0]), float(p[1]), float(p[2]))
 game.live_weather = false
 game.choose_place.call_deferred(place.x, place.y, "Rail view", "", place.z)
func _process(dt):
 if not game.ready_drive or game.tiles.size() < 6 or game.job:return
 clock += dt
 if clock < 10.:return
 game.vehicle.freeze = true
 game.hud.root.visible = false
 # Nearest post to the car among loaded tiles.
 var best := Transform3D();var best_d := INF
 for id in game.tiles:
  var posts = game.tiles[id].get_node_or_null("GuardRailPosts")
  if posts == null:continue
  for i in range(0, posts.multimesh.instance_count, 7):
   var t: Transform3D = posts.global_transform * posts.multimesh.get_instance_transform(i)
   var d := t.origin.distance_to(game.vehicle.global_position)
   if d < best_d:best_d = d;best = t
 if not is_finite(best_d):
  print("RAILVIEW no rails loaded; driver view only")
  best = game.vehicle.global_transform
 set_process(false)
 var along := -best.basis.z;along.y = 0.;along = along.normalized()
 var side := along.cross(Vector3.UP)
 for v in [[side * 7. - along * 9. + Vector3.UP * 2.2, "rails-0"], [-side * 6. - along * 14. + Vector3.UP * 1.3, "rails-1"]]:
  for i in 30:
   await RenderingServer.frame_pre_draw
   game.camera.global_position = best.origin + v[0]
   game.camera.look_at(best.origin + along * 6. + Vector3.UP * .4, Vector3.UP)
  await RenderingServer.frame_post_draw
  get_viewport().get_texture().get_image().save_jpg(out.path_join(v[1] + ".jpg"), .9)
 print("RAILVIEW saved, nearest rail %.0f m from the car" % best_d)
 get_tree().quit(0)
