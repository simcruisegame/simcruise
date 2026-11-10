extends Node
## --quality-bench OUTPUT_DIRECTORY: after the start place streams, holds each
## graphics preset for a few seconds (resolution adaptation off), records frame
## time, GPU time, draw calls and the ambient traffic and bird counts, saves a
## chase-view JPEG per preset and writes quality-bench.json. Run windowed:
##   godot --path game -- --bundled-start --quality=high --quality-bench /abs/out
## --place=lon,lat,bearing then jumps to a streamed place and holds the car
## where it lands; add --without-traffic and --solar-epoch UNIX for A/B runs
## whose frames match (GPU min / p90 / max and pipeline compiles are reported).
const HOLD := 6.
var game
var out := ""
var queue := ["low","medium","high","ultra"]
var current := ""
var clock := 0.
var rows := []
var report := {}
var place = null    # Vector3(lon, lat, bearing) from --place=

func _ready():
 game = get_parent()
 var args := OS.get_cmdline_user_args()
 out = args[args.find("--quality-bench")+1]
 DirAccess.make_dir_recursive_absolute(out)
 game.live_weather = false
 if "--no-vsync" in args:DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
 if "--mute" in args and game.sound_enabled:game.toggle_sound.call_deferred()
 for a in args:
  if a.begins_with("--only="):queue = Array(a.trim_prefix("--only=").split(",", false))
  # --place=lon,lat,bearing measures a streamed place (e.g. road ribbons).
  if a.begins_with("--place="):
   var p := a.trim_prefix("--place=").split(",")
   place = Vector3(float(p[0]), float(p[1]), float(p[2]))
 RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
 report["adapter"] = RenderingServer.get_video_adapter_name()
 report["api"] = RenderingServer.get_current_rendering_driver_name() + " " + RenderingServer.get_video_adapter_api_version()
 report["method"] = RenderingServer.get_current_rendering_method()
 report["engine_max_fps"] = Engine.max_fps
 report["low_processor"] = OS.low_processor_usage_mode
 report["physics_ticks"] = Engine.physics_ticks_per_second
 report["max_physics_steps"] = Engine.max_physics_steps_per_frame
 report["time_scale"] = Engine.time_scale
 report["auto_level"] = game.graphics.detect(RenderingServer.get_video_adapter_name(), RenderingServer.get_video_adapter_type())

func _process(dt):
 if not game.ready_drive or game.tiles.size() < 4 or game.job or not game.jump.is_empty():return
 if place != null:
  # Jump only once the start place has loaded, so the start cannot override it.
  game.choose_place(place.x, place.y, "Quality bench", "", place.z);place = null;clock = 0.;hold_pending = true
  return
 if hold_pending:
  # Keep the car where it was placed, so every run of a place frames the same view.
  hold_pending = false;held = game.vehicle.global_transform
  # --without-traffic hides the ambient cars, which otherwise stop behind the
  # held car and can block the chase camera (A/B of a ground shader).
  if "--without-traffic" in OS.get_cmdline_user_args():game.ambient_life.visible = false
 if game.hud.root.visible:game.hud.root.visible = false
 game.view = 0
 clock += dt
 if current == "":
  if clock < 8.:return               # let tiles, trees and traffic settle
  next();return
 if clock > 1.5:
  var v := get_viewport().get_viewport_rid()
  rows.append([dt*1000., RenderingServer.viewport_get_measured_render_time_gpu(v), Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME), RenderingServer.viewport_get_measured_render_time_cpu(v), Performance.get_monitor(Performance.TIME_PROCESS)*1000., Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS)*1000.])
 if clock < HOLD:return
 var img := get_viewport().get_texture().get_image()
 img.save_jpg(out.path_join("quality-%s.jpg" % current), .88)
 var cars := 0
 for car in game.ambient_life.cars:
  if car.seg >= 0:cars += 1
 var nearest := []
 for car in game.ambient_life.cars:
  if car.seg >= 0:nearest.append(snappedf(Vector2(car.pos.x, car.pos.z).distance_to(game.pos), .1))
 nearest.sort()
 report[current] = {"frame_ms": median(0), "gpu_ms": median(1), "draw_calls": median(2), "render_cpu_ms": median(3), "process_ms": median(4), "ambient_ms": game.ambient_life.cost_us / 1000., "drive_process_ms": drive_ms, "physics_ms": median(5), "scale": game.graphics.scale, "moving_cars": cars, "car_distances_m": nearest.slice(0, 6), "graph_segments": game.ambient_life.segs.size(), "fps": Engine.get_frames_per_second(), "vsync": DisplayServer.window_get_vsync_mode(), "gpu_ms_min": quantile(1, 0.), "gpu_ms_p90": quantile(1, .9), "gpu_ms_max": quantile(1, 1.), "frame_ms_max": quantile(0, 1.), "pipelines_compiled_at_draw": Performance.get_monitor(Performance.PIPELINE_COMPILATIONS_DRAW), "pipelines_compiled_ahead": Performance.get_monitor(Performance.PIPELINE_COMPILATIONS_SURFACE) + Performance.get_monitor(Performance.PIPELINE_COMPILATIONS_MESH)}
 next()

var drive_ms := {}
var hold_pending := false
var held = null     # Transform3D the car is held at after --place= (runs after Drive.gd's physics step)
func _physics_process(_dt):
 if held != null:
  game.vehicle.global_transform = held;game.vehicle.linear_velocity = Vector3.ZERO;game.vehicle.angular_velocity = Vector3.ZERO
 # Coarse attribution: time the Drive._process sub-steps once per second.
 if current == "" or Engine.get_physics_frames() % 60 != 0:return
 for step in ["stream","stream_photo","stream_canopy","stream_recent","stream_surfaces"]:
  var t := Time.get_ticks_usec();game.call(step);drive_ms[step] = (Time.get_ticks_usec()-t)/1000.
 for pair in [["street_detail",game.street_detail],["building_stream",game.building_stream],["spectral_ground",game.spectral_ground],["forest_stream",game.forest_stream],["road_banks",game.road_banks],["regional_terrain",game.regional_terrain],["rangeland",game.rangeland]]:
  var t := Time.get_ticks_usec();pair[1].tick(game);drive_ms[pair[0]] = (Time.get_ticks_usec()-t)/1000.
 for pair in [["grove",game.grove],["roadside",game.roadside]]:
  var t := Time.get_ticks_usec();pair[1].refresh(game);drive_ms[pair[0]] = (Time.get_ticks_usec()-t)/1000.
 var t2 := Time.get_ticks_usec();game.update_parked();drive_ms["update_parked"] = (Time.get_ticks_usec()-t2)/1000.
 var t3 := Time.get_ticks_usec();game.nearest_world();drive_ms["nearest_world"] = (Time.get_ticks_usec()-t3)/1000.

func median(col: int) -> float:
 var vals := []
 for r in rows:vals.append(r[col])
 vals.sort()
 return vals[vals.size()/2] if vals.size() else -1.

## Value at fraction q (0 = smallest, 1 = largest) of a measured column.
func quantile(col: int, q: float) -> float:
 var vals := []
 for r in rows:vals.append(r[col])
 vals.sort()
 return vals[mini(int(q * vals.size()), vals.size() - 1)] if vals.size() else -1.

func overview():
 # A drone's view straight down the street, 60 m up behind the car: proves the
 # traffic and birds render, which a chase camera at the curb may not show.
 var dir := Vector3(-sin(game.yaw), 0, -cos(game.yaw))
 var at: Vector3 = game.vehicle.global_position
 game.camera.global_position = at - dir * 55. + Vector3.UP * 38.
 game.camera.look_at(at + dir * 45., Vector3.UP)

func next():
 rows.clear();clock = 0.
 if queue.is_empty() and current != "overview":
  current = "overview";game.graphics.apply("high");game.graphics.settle = 1e9
  set_process(false)
  for i in 90:
   await RenderingServer.frame_pre_draw   # after Drive.gd placed its camera
   overview()
  get_viewport().get_texture().get_image().save_jpg(out.path_join("quality-overview.jpg"), .9)
  queue.clear()
 if queue.is_empty():
  var f := FileAccess.open(out.path_join("quality-bench.json"), FileAccess.WRITE)
  f.store_string(JSON.stringify(report, "  "));f.close()
  game.request_game_close(0);return
 current = queue.pop_front()
 game.graphics.apply(current)
 game.graphics.settle = 1e9          # hold the preset's own render scale while measuring
