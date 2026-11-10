extends Node
## --rail-view OUTPUT_DIRECTORY [--place=lon,lat,bearing]: loads a place, waits
## for it to stream, then frames the nearest guard-rail run (GuardRails.gd) or,
## where there is none, the car, from three cameras of its own (roadside, low
## roadside, oblique aerial) and saves rails-0.jpg, rails-1.jpg and
## aerial-oblique.jpg. Windowed only; pass --quality=ultra for judging looks.
var game
var out := ""
var clock := 0.
var place := Vector3(-83.9202491, 35.4667227, 315.)
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
 set_process(false)
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
  print("RAILVIEW no rails loaded; framing the car")
  best = game.vehicle.global_transform
 var along := -best.basis.z;along.y = 0.;along = along.normalized()
 var side := along.cross(Vector3.UP)
 # A camera of our own: the cockpit camera re-aims itself every frame.
 var cam := Camera3D.new();cam.fov = 50.;cam.far = 4000.;game.add_child(cam);cam.current = true
 var focus: Vector3 = best.origin
 var views := [[side * 7. - along * 9. + Vector3.UP * 2.2, "rails-0", Vector3.UP * .4],
  [-side * 6. - along * 14. + Vector3.UP * 1.3, "rails-1", Vector3.UP * .4],
  [-along * 28. + side * 10. + Vector3.UP * 22., "aerial-oblique", Vector3.ZERO]]
 for v in views:
  cam.global_position = focus + v[0]
  cam.look_at(focus + along * 6. + v[2], Vector3.UP)
  for i in 20:await RenderingServer.frame_post_draw
  get_viewport().get_texture().get_image().save_jpg(out.path_join(v[1] + ".jpg"), .9)
 var under=game.tile_id(game.pos)
 if game.tiles.has(under):
  var t=game.tiles[under]
  var keys=[]
  for k in t.materials:if str(k).contains("roof"):keys.append("%s:%s"%[k,t.materials[k].get_class()])
  print("RAILVIEW roof materials on tile ",under,": ",keys," lidar_roofs=",t.info.get("lidar_roofs",{}).size() if t.info.get("lidar_roofs") is Dictionary else t.info.get("lidar_roofs",[]).size() if t.info.get("lidar_roofs") is Array else "-")
 var houses:=Vector2i.ZERO
 for id in game.tiles:
  houses+=game.tiles[id].house_stats
  var layer=game.tiles[id].structure_layer
  if layer:houses+=layer.house_stats
 print("RAILVIEW surveyed houses %d, triangles %d (%.1f per house)" % [houses.x, houses.y, houses.y / maxf(1., houses.x)])
 # The surveyed house front nearest the car (HouseRecipe.gd), head-on and oblique.
 var door := Vector3.ZERO;var facing := Vector3.ZERO;var door_d := INF
 for id in game.tiles:
  for node in [game.tiles[id], game.tiles[id].structure_layer]:
   if node == null:continue
   for i in range(0, node.house_fronts.size(), 2):
    var p: Vector3 = node.global_transform * node.house_fronts[i]
    var d := p.distance_to(game.vehicle.global_position)
    if d < door_d:door_d = d;door = p;facing = node.global_transform.basis * node.house_fronts[i + 1]
 if is_finite(door_d):
  var across := facing.cross(Vector3.UP)
  for v in [[facing * 13. + across * 3. + Vector3.UP * 1.7, "house-front"], [facing * 11. - across * 11. + Vector3.UP * 7., "house-oblique"]]:
   cam.global_position = door + v[0]
   cam.look_at(door + Vector3.UP * 2.2, Vector3.UP)
   for i in 20:await RenderingServer.frame_post_draw
   get_viewport().get_texture().get_image().save_jpg(out.path_join(v[1] + ".jpg"), .9)
  print("RAILVIEW house front %.0f m from the car at %s" % [door_d, door])
 print("RAILVIEW saved, nearest rail %.0f m from the car" % best_d)
 get_tree().quit(0)
