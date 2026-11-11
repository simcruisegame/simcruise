extends Node
## Windowed companion to any drive (--feel-stats, for example with --road-test):
## records what the pad rumble and the low-latency audio feeds did and prints
## one summary when the game closes. Headless runs have no mixer, so the audio
## part needs a window and an audio device.
##   godot --path game -- --road-test --quality=ultra --feel-stats
## Reports per feed (engine, road, wind) the queue target it settled on, the
## times the mixer starved while the game was feeding it, and the latency the
## target means; then percentiles of RoadNoise roughness and both motor levels
## while driving, and the share of driving time each motor ran.
var game
var roughness := []
var weak := []
var strong := []
var front_slip := []
var rear_slip := []
var trace := OS.has_environment("FEEL_TRACE")
var driving_ticks := 0

func _ready():
 game = get_parent()

func _physics_process(_dt):
 if game == null or not game.ready_drive or game.closing or game.haptics == null:return
 if absf(game.velocity) < 1.:return
 driving_ticks += 1
 roughness.append(game.road_noise.roughness)
 weak.append(game.haptics.levels.x)
 strong.append(game.haptics.levels.y)
 var h = game.haptics;var car = game.vehicle
 front_slip.append(maxf(h.slip[0],h.slip[1]));rear_slip.append(maxf(h.slip[2],h.slip[3]))
 if trace and driving_ticks%30==0:print("FEELTRACE v=%.1f s=[%.2f %.2f %.2f %.2f] kappa=[%.3f %.3f %.3f %.3f] alpha=[%.3f %.3f %.3f %.3f] rough=%.2f weak=%.2f strong=%.2f abs=%s"%[game.velocity,h.slip[0],h.slip[1],h.slip[2],h.slip[3],car.slip_ratio[0],car.slip_ratio[1],car.slip_ratio[2],car.slip_ratio[3],car.rolling_slip[0],car.rolling_slip[1],car.rolling_slip[2],car.rolling_slip[3],game.road_noise.roughness,h.levels.x,h.levels.y,str(h.abs_active)])

static func percentile(values: Array, p: float) -> float:
 if values.is_empty():return 0.
 var sorted := values.duplicate();sorted.sort()
 return float(sorted[clampi(int(p*(sorted.size()-1)),0,sorted.size()-1)])

static func share(values: Array, above: float) -> float:
 if values.is_empty():return 0.
 var n := 0
 for v in values:if float(v) > above:n += 1
 return float(n)/values.size()

func _exit_tree():
 if game == null:return
 var feeds := {"engine":game.engine_feed,"road":game.road_noise.road_feed,"wind":game.road_noise.wind_feed}
 for name in feeds:
  var f = feeds[name]
  print("FEEL audio %-6s target %.0f ms, %d starved pushes, %d skipped frames, buffer %d frames, frame gaps at starvation (ms) %s"%[name,f.target_s*1000.,f.skip_events,f.skips,f.capacity,str(f.starved_gaps)])
 var seconds := driving_ticks/float(Engine.physics_ticks_per_second)
 print("FEEL drove %.0f s above 1 m/s"%seconds)
 for pair in [["roughness",roughness],["front s",front_slip],["rear s",rear_slip],["weak",weak],["strong",strong]]:
  var v: Array = pair[1]
  print("FEEL %-9s p50 %.3f p90 %.3f p99 %.3f max %.3f, on (> .05) %.0f %% of the time"%[pair[0],percentile(v,.5),percentile(v,.9),percentile(v,.99),percentile(v,1.),100.*share(v,.05)])
