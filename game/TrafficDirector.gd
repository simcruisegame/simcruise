extends Node
## Traffic director: who is behind each wheel of the ambient traffic, and what
## they mean to do next. AmbientLife.gd moves the cars every frame with the
## Intelligent Driver Model (idm() below); this node works at a quarter of the
## cars every quarter second, so each car is reconsidered about once a second:
##
##  * a persona per car (commuter, delivery van, cautious elder, hurried driver,
##    tourist) with its own IDM desired-speed factor, time headway, standstill
##    gap and comfortable acceleration and braking, varied per driver;
##  * a route a few segments ahead, with the turn chosen at each junction by the
##    persona (commuters and hurried drivers keep to the bigger road and go
##    straight, delivery vans prefer residential streets, tourists wander), and
##    the speed to slow to before a sharp turn;
##  * stops at mapped traffic signals (SignalControls records and OSM signal
##    nodes), from a fixed-time two-phase plan per junction, with the yellow
##    dilemma decided by whether the driver can stop at a comfortable rate;
##  * the occasional pull-over of a delivery van on a residential street.
##
## The persona mix comes from the place: road classes and mapped POIs near the
## driver give a traffic character by rule (classify()). If a local Ollama
## server answers, it is asked once per new area (z14 tile, about 2.4 km) for the
## same character as a one-word JSON answer, strictly asynchronously with a 2 s
## timeout; without it, or when it is late, the rule-based character stands.
##
## Nothing here runs in headless or QA runs (traffic count zero).

const ORDER := ["commuter","delivery","elder","hurried","tourist"]
# IDM parameters (Treiber, Hennecke & Helbing 2000; Treiber & Kesting, "Traffic
# Flow Dynamics", 2013, table 11.2: city traffic T 1.0-1.5 s, s0 2 m, a 1.0 m/s²,
# b 1.5 m/s²), spread by persona. speed scales the road-class speed.
# straight/major/wander weight the turn choice; lateral is the comfortable
# cornering acceleration (m/s²).
const PERSONAS := {
 "commuter":{"speed":1.0, "T":1.3, "s0":2.0, "a":1.4, "b":2.0, "straight":1.0, "major":.8, "wander":.4, "lateral":2.6},
 "delivery":{"speed":.92, "T":1.6, "s0":2.5, "a":.9, "b":1.7, "straight":.5, "major":-.7, "wander":.9, "lateral":2.0},
 "elder":{"speed":.86, "T":2.1, "s0":3.0, "a":.8, "b":1.4, "straight":1.4, "major":-.3, "wander":.3, "lateral":1.8},
 "hurried":{"speed":1.12, "T":.9, "s0":1.5, "a":2.0, "b":2.8, "straight":.8, "major":1.2, "wander":.3, "lateral":3.4},
 "tourist":{"speed":.88, "T":1.8, "s0":2.5, "a":1.0, "b":1.6, "straight":.2, "major":.2, "wander":1.6, "lateral":2.2},
}
# Persona weights (in ORDER) for each traffic character.
const CHARACTERS := {
 "residential":[.50,.12,.18,.10,.10],
 "downtown":[.40,.22,.08,.18,.12],
 "commercial_strip":[.46,.20,.12,.14,.08],
 "industrial":[.42,.38,.04,.14,.02],
 "tourist":[.22,.10,.14,.09,.45],
 "retirement":[.25,.10,.50,.05,.10],
 "campus":[.45,.10,.05,.25,.15],
 "rural":[.52,.10,.20,.13,.05],
}
const RANK := {"motorway":6,"trunk":5,"primary":4,"secondary":3,"tertiary":2,"unclassified":1,"residential":0,"living_street":-1,
 "motorway_link":3,"trunk_link":3,"primary_link":2,"secondary_link":2,"tertiary_link":1}
# OpenMapTiles poi classes, grouped.
const POI_GROUPS := {
 "food":["restaurant","fast_food","cafe","bar","beer","ice_cream","pub","bakery"],
 "retail":["shop","grocery","clothing_store","department_store","alcohol_shop","hardware","furniture","jewelry","mobile_phone","books","florist","supermarket","electronics","shoes","gift","toys"],
 "tourism":["lodging","museum","attraction","art_gallery","zoo","campsite","theatre","monument","castle","information","viewpoint","aquarium","theme_park","stadium","golf"],
 "education":["college","school","kindergarten","university","library"],
 "care":["hospital","doctors","pharmacy","dentist","clinic","social_facility","nursing_home"],
 "auto":["fuel","car","car_repair","parking","bus","railway"],
}
const DELIVERY_ROADS := ["residential","unclassified","tertiary","living_street","secondary"]

const MAX_BRAKE := 8.          # m/s², hard emergency stop on dry asphalt
const DELTA := 4.              # IDM acceleration exponent
const CAR_LENGTH := 4.6
const TICK := .25              # a quarter of the cars per tick: each car once a second
const LOOK := 110.             # metres of route planned ahead
const STOP_LINE := 3.          # stop bar this far before the cross road's near edge (a crosswalk)
const YELLOW := 4.
const ALL_RED := 2.
const LLM_TIMEOUT := 2.
const LLM_WARM_TIMEOUT := 60.
# Small local models, tried in this order; all are under permissive licences
# (Qwen3: Apache-2.0; Phi-4-mini: MIT; Granite 4: Apache-2.0). Nothing is
# bundled: the player's own Ollama install supplies the weights.
const LLM_MODELS := ["qwen3:1.7b","qwen3:4b","phi4-mini:latest","granite4:tiny-h"]

var life
var rng := RandomNumberGenerator.new()
var now := 0.
var clock := 0.
var cursor := 0
var mix: Array = CHARACTERS.residential.duplicate()
var character := "residential"
var character_source := "rules"
var area := Vector2i(-99999, -99999)
var signals: Array = []        # {at:Vector2, axis:Vector2, cycle, offset, split}
var seg_signal := {}           # segment * 2 + arriving end -> signal index
var log_rows: Array = []       # [area, character, source, ms] for QA and captures

# Off by default: a local model costs gigabytes of RAM and CPU time while
# driving. --traffic-llm opts in; the rule-based place character is the default.
var llm_enabled := false
var llm_state := "off"         # off, probing, warming, ready, absent
var llm_host := ""
var llm_model := ""
var llm_timeout := LLM_TIMEOUT
var llm_http: HTTPRequest
var llm_asked := {}
var llm_pending := Vector2i(-99999, -99999)
var llm_area := Vector2i.ZERO
var llm_started := 0
var llm_stats := {}

func _ready() -> void:
 rng.seed = 334
 for arg in OS.get_cmdline_user_args():
  if arg == "--without-traffic-llm":llm_enabled = false
  elif arg == "--traffic-llm":llm_enabled = true
  elif arg.begins_with("--traffic-llm-model="):llm_model = arg.get_slice("=", 1)
  elif arg.begins_with("--traffic-llm-timeout="):llm_timeout = maxf(.5, float(arg.get_slice("=", 1)))

# ------------------------------------------------------------- driving model ----

## Intelligent Driver Model acceleration (m/s²) for speed v, desired speed v0,
## net gap to the leader (bumper to bumper, INF for a free road) and approach
## rate dv = v - v_leader. Above the desired speed (entering a slower road) the
## driver eases off at the comfortable rate instead of the model's steep
## (v/v0)^4 term. Clamped to MAX_BRAKE.
static func idm(v: float, v0: float, gap: float, dv: float, p: Dictionary) -> float:
 var a: float = p.a
 var b: float = p.b
 var free := maxf(a * (1. - pow(v / maxf(v0, .1), DELTA)), -b)
 if gap > 1e5:return clampf(free, -MAX_BRAKE, a)
 if gap <= .05:return -MAX_BRAKE
 var s_star: float = p.s0 + maxf(0., v * p.T + v * dv / (2. * sqrt(a * b)))
 return clampf(free - a * pow(s_star / gap, 2.), -MAX_BRAKE, a)

## Ballistic update (Treiber & Kanagaraj 2015): returns (distance, new speed)
## and never lets the speed go negative inside a step, so a long frame cannot
## roll a stopped car backwards or through its leader.
static func advance(v: float, acc: float, dt: float) -> Vector2:
 var nv := v + acc * dt
 if nv < 0.:return Vector2(v * v / (2. * -acc) if acc < 0. else 0., 0.)
 return Vector2((v + nv) * .5 * dt, nv)

## One driver: the persona's parameters, each varied by up to ±10 %.
static func driver(persona: String, r: RandomNumberGenerator) -> Dictionary:
 var p: Dictionary = PERSONAS[persona]
 var d := {"persona":persona}
 for key in ["T","s0","a","b","lateral"]:d[key] = float(p[key]) * r.randf_range(.9, 1.1)
 d["speed"] = float(p.speed) * r.randf_range(.94, 1.06)
 return d

static func pick(weights: Array, r: RandomNumberGenerator) -> String:
 var total := 0.
 for w in weights:total += w
 var x := r.randf() * total
 for i in weights.size():
  x -= weights[i]
  if x <= 0.:return ORDER[i]
 return ORDER[0]

## Speed (m/s) a driver slows to for a turn whose headings meet at cosine dot:
## a corner radius from the angle (about 8 m for a right angle at an urban
## corner) and the driver's comfortable lateral acceleration.
static func turn_speed(dot: float, lateral: float) -> float:
 var radius := 8. / maxf(.04, 1. - dot)
 return sqrt(lateral * radius)

## Two-phase fixed-time signal: [state, seconds until this approach is green].
## The main axis (sig.axis) gets sig.split of the green time.
static func signal_phase(sig: Dictionary, heading: Vector2, t: float) -> Array:
 var cycle: float = sig.cycle
 var green := cycle - 2. * (YELLOW + ALL_RED)
 var ga: float = green * float(sig.split)
 var gb: float = green - ga
 var x := fposmod(t + float(sig.offset), cycle)
 var b0 := ga + YELLOW + ALL_RED
 if absf(heading.dot(sig.axis)) >= .7071:
  if x < ga:return ["green", 0.]
  return ["yellow" if x < ga + YELLOW else "red", cycle - x]
 if x >= b0 and x < b0 + gb:return ["green", 0.]
 return ["yellow" if x >= b0 + gb and x < b0 + gb + YELLOW else "red", fposmod(b0 - x, cycle)]

## Traffic character of a place from road length by class (m) and POI counts
## by OpenMapTiles class. Deterministic, so the same place always gets the same mix.
static func classify(roads: Dictionary, pois: Dictionary) -> String:
 var total := 0.
 for k in roads:total += float(roads[k])
 var share := func(classes: Array) -> float:
  var s := 0.
  for c in classes:s += float(roads.get(c, 0.))
  return s / maxf(total, 1.)
 var groups := {}
 var n := 0
 for g in POI_GROUPS:groups[g] = 0
 for c in pois:
  n += int(pois[c])
  for g in POI_GROUPS:
   if c in POI_GROUPS[g]:groups[g] += int(pois[c])
 var res: float = share.call(["residential","living_street"])
 var major: float = share.call(["primary","secondary","trunk","primary_link","secondary_link","trunk_link"])
 var nf := maxf(float(n), 1.)
 if n < 4 and res < .35:return "rural"
 if groups.tourism >= 4 and groups.tourism / nf >= .25:return "tourist"
 if groups.education >= 3 and groups.education / nf >= .2:return "campus"
 if groups.care >= 4 and groups.care / nf >= .25:return "retirement"
 if n >= 40 and (groups.food + groups.retail) / nf >= .45:return "downtown"
 if groups.food + groups.retail + groups.auto >= 8 and major >= .25:return "commercial_strip"
 if groups.auto >= 3 and n < 12 and major >= .3:return "industrial"
 return "residential"

# ------------------------------------------------------------- cars -----------

## A new car: persona from the area's mix, its driver parameters and the
## desired speed on this road class.
func assign(car: Dictionary, class_speed: float) -> void:
 car.drv = driver(pick(mix, rng), rng)
 car.cruise = class_speed * float(car.drv.speed)
 car.route = [];car.turn_at = INF;car.turn_v = 99.;car.stop_at = INF;car.stop_until = 0.
 car.park_at = INF;car.parked = false;car.park_until = 0.;car.dwell = 0.;car.lat = 0.;car.shift = 0.

## Score a junction option for this driver: straightness, road rank, chance.
func score(car: Dictionary, option: Array) -> float:
 var p: Dictionary = PERSONAS[car.drv.persona]
 var rank: float = RANK.get(life.segs[option[0]][4], 0)
 return option[2] * float(p.straight) + rank * .15 * float(p.major) + rng.randf() * float(p.wander)

func choose(car: Dictionary, options: Array) -> Array:
 if options.is_empty():return []
 if options.size() == 1:return options[0]
 var best: Array = options[0]
 var best_score := -INF
 for o in options:
  var s := score(car, o)
  if s > best_score:best_score = s;best = o
 return best

## Plan ahead: choose the route through the next junctions, and find the first
## sharp turn and the first signal stop on it. Distances are measured from the
## start of the car's current segment.
func plan(car: Dictionary) -> void:
 var k: int = car.seg
 var d: int = car.dir
 var cum: float = life.seg_length(k)
 var route := []
 var turn_at := INF
 var turn_v := 99.
 var stop_at := INF
 var stop_until := 0.
 var signal_seen := false
 var heading: Vector2 = life.seg_heading(k, d)
 var guard := 0
 while guard < 8:
  guard += 1
  # A signal at the end we are driving towards.
  if not signal_seen:
   var si: int = seg_signal.get(k * 2 + (1 if d > 0 else 0), -1)
   if si >= 0:
    var line := cum - float(signals[si].get("half", 3.5)) - STOP_LINE
    var to_line: float = line - car.s
    if to_line > .5:
     var ph := signal_phase(signals[si], heading, now)
     # Yellow: stop if it can be done at the comfortable rate, else go on.
     # Red: stop unless even firm braking (half the emergency rate) would not
     # make the line, i.e. the driver was already committed on the yellow.
     var stop_need: float = car.speed * car.speed / (2. * to_line)
     if (ph[0] == "yellow" and stop_need <= float(car.drv.b)) or (ph[0] == "red" and stop_need <= MAX_BRAKE * .5):
      stop_at = line;stop_until = now + float(ph[1])
    signal_seen = true                    # only the first signal ahead counts
  if cum - car.s > LOOK:break
  var opts: Array = life.options(k, d)
  if opts.is_empty():break
  # Keep the choices already made (a driver does not change their mind every
  # second); choose only beyond them.
  var o: Array = []
  if route.size() < car.route.size():
   var kept: Array = car.route[route.size()]
   for x in opts:
    if x[0] == kept[0] and x[1] == kept[1]:o = x
  if o.is_empty():o = choose(car, opts)
  route.append([o[0], o[1]])
  var out: Vector2 = life.seg_heading(o[0], o[1])
  var dot := heading.dot(out)
  if turn_at == INF and dot < .85:
   turn_at = cum;turn_v = turn_speed(dot, float(car.drv.lateral))
  heading = out;k = o[0];d = o[1]
  cum += life.seg_length(k)
 car.route = route
 car.turn_at = turn_at;car.turn_v = turn_v
 car.stop_at = stop_at
 car.stop_until = stop_until

func errand(car: Dictionary) -> void:
 if car.drv.persona != "delivery" or car.parked or car.park_at != INF:return
 if rng.randf() > 1. / 40.:return               # about one stop per 40 s of driving
 var s: Array = life.segs[car.seg]
 if not s[4] in DELIVERY_ROADS or s[3] != 0:return
 var room: float = life.seg_length(car.seg) - car.s
 if room < 40. or car.stop_at < car.s + room:return
 car.park_at = car.s + rng.randf_range(15., room - 15.)
 car.dwell = rng.randf_range(20., 60.)      # dwell, started on arrival

## Called by AmbientLife every frame with the traffic step.
func step(dt: float) -> void:
 now += dt
 clock -= dt
 if clock > 0.:return
 clock = TICK
 var cars: Array = life.cars
 if cars.is_empty():return
 var n := ceili(cars.size() / 4.)
 for i in n:
  cursor = (cursor + 1) % cars.size()
  var car: Dictionary = cars[cursor]
  if car.seg < 0 or not car.has("drv") or car.parked:continue
  plan(car)
  errand(car)
 if cursor < n:look_around()

# ------------------------------------------------------------- signals ---------

## Signals in world space from the loaded tiles, matched to the segment ends
## that arrive at them. Called by AmbientLife after each graph rebuild.
func rebuild_signals() -> void:
 signals.clear();seg_signal.clear()
 var game = life.game
 var cells := {}
 for id in game.tiles:
  var tile = game.tiles[id]
  var o: Vector2 = game.tile_offset(id)
  var points := []
  var controls = tile.info.get("signal_controls", {})
  if controls is Dictionary:
   for p in controls.get("points", []):
    if p is Vector2:points.append(p)
  var objects = tile.info.get("street_objects", {})
  if objects is Dictionary:
   for uv in objects.get("signals", []):
    if uv is Array and uv.size() == 2:points.append(Vector2(uv[0], uv[1]) * float(tile.side))
  for p in points:
   var at: Vector2 = p + o
   var key := Vector2i((at / 12.).floor())
   var dup := false
   for dx in [-1, 0, 1]:
    for dy in [-1, 0, 1]:
     for j in cells.get(key + Vector2i(dx, dy), []):
      if signals[j].at.distance_to(at) < 12.:dup = true
   if dup:continue
   var world := Vector2(game.origin) * float(game.side) + at
   var h: int = hash(Vector2i(world.round()))
   cells[key] = cells.get(key, []) + [signals.size()]
   signals.append({"at":at, "axis":Vector2.RIGHT, "rank":-9, "half":3.5, "cycle":60. + float(absi(h) % 41), "offset":float(absi(h >> 8) % 97), "split":.55})
 if signals.is_empty():return
 for k in life.segs.size():
  var s: Array = life.segs[k]
  for e in 2:
   var p: Vector2 = s[e]
   var key := Vector2i((p / 12.).floor())
   for dx in [-1, 0, 1]:
    for dy in [-1, 0, 1]:
     for j in cells.get(key + Vector2i(dx, dy), []):
      var sig: Dictionary = signals[j]
      if sig.at.distance_to(p) > 9.:continue
      seg_signal[k * 2 + e] = j
      # The stop bar sits behind the widest road through the junction.
      sig.half = maxf(float(sig.half), float(s[2]) * .5)
      var r: int = RANK.get(s[4], 0)
      if r > sig.rank:sig.rank = r;sig.axis = (s[1] - s[0]).normalized()

# ------------------------------------------------------------- the place -------

func look_around() -> void:
 var game = life.game
 var id: Vector2i = game.tile_id(game.pos)
 var here := Vector2i(floori(id.x / 4.), floori(id.y / 4.))
 if here == area:return
 area = here
 var stats := place_stats()
 character = classify(stats.roads, stats.pois)
 character_source = "rules"
 mix = CHARACTERS[character].duplicate()
 log_rows.append([area, character, "rules", 0])
 print("TRAFFIC area %s character %s (rules) roads %d m pois %d" % [area, character, int(stats.total), int(stats.n)])
 if not llm_enabled or life.traffic_target <= 0 or llm_asked.has(area):return
 llm_pending = area;llm_stats = stats
 if llm_state == "off":probe()
 elif llm_state == "ready":ask()

func place_stats() -> Dictionary:
 var game = life.game
 var me: Vector2 = game.pos
 var roads := {}
 var total := 0.
 for s in life.segs:
  var mid: Vector2 = (s[0] + s[1]) * .5
  if mid.distance_to(me) > 800.:continue
  var l: float = s[0].distance_to(s[1])
  roads[s[4]] = roads.get(s[4], 0.) + l;total += l
 var pois := {}
 var n := 0
 for id in game.tiles:
  var tile = game.tiles[id]
  var o: Vector2 = game.tile_offset(id)
  for place in tile.info.get("pois", []):
   if not place is Array or place.size() < 3 or not place[0] is Array or place[0].size() != 2:continue
   var at: Vector2 = Vector2(place[0][0], place[0][1]) * float(tile.side) + o
   if at.distance_to(me) > 800.:continue
   var c := str(place[2])
   pois[c] = pois.get(c, 0) + 1;n += 1
 return {"roads":roads, "pois":pois, "total":total, "n":n}

# ------------------------------------------------------------- local LLM -------

func host_url() -> String:
 var h := OS.get_environment("OLLAMA_HOST").strip_edges()
 if h.is_empty():h = "127.0.0.1:11434"
 var scheme := "http://"
 if h.begins_with("http://"):h = h.substr(7)
 elif h.begins_with("https://"):h = h.substr(8);scheme = "https://"
 h = h.trim_suffix("/")
 # OLLAMA_HOST is often the server's bind address (0.0.0.0): talk to loopback.
 if h.begins_with("0.0.0.0"):h = "127.0.0.1" + h.substr(7)
 if h.begins_with(":"):h = "127.0.0.1" + h
 if not ":" in h:h += ":11434"
 return scheme + h

func http(timeout: float) -> HTTPRequest:
 if llm_http == null:
  llm_http = HTTPRequest.new()
  llm_http.use_threads = true                  # never resolve or connect on the main thread
  add_child(llm_http)
  llm_http.request_completed.connect(_on_llm)
 llm_http.timeout = timeout
 return llm_http

func probe() -> void:
 llm_host = host_url()
 llm_state = "probing"
 llm_started = Time.get_ticks_msec()
 if http(LLM_TIMEOUT).request(llm_host + "/api/tags") != OK:llm_state = "absent"

## Load the model into memory once, without waiting on it for any decision (an
## aborted load is cancelled by Ollama, so this one gets a longer timeout).
func warm() -> void:
 llm_state = "warming"
 llm_started = Time.get_ticks_msec()
 var body := JSON.stringify({"model":llm_model, "keep_alive":"30m", "options":{"num_gpu":0}})
 if http(LLM_WARM_TIMEOUT).request(llm_host + "/api/generate", ["Content-Type: application/json"], HTTPClient.METHOD_POST, body) != OK:llm_state = "absent"

func prompt(stats: Dictionary) -> String:
 var parts := []
 var total := maxf(float(stats.total), 1.)
 var classes: Array = stats.roads.keys()
 classes.sort_custom(func(x, y): return stats.roads[x] > stats.roads[y])
 for c in classes.slice(0, 6):parts.append("%d%% %s" % [roundi(100. * stats.roads[c] / total), c])
 var kinds: Array = stats.pois.keys()
 kinds.sort_custom(func(x, y): return stats.pois[x] > stats.pois[y])
 var places := []
 for c in kinds.slice(0, 10):places.append("%s x%d" % [c, stats.pois[c]])
 return "A place in the United States within 800 m. Roads by length: %s. Mapped places (%d): %s. Which traffic character fits best?" % [", ".join(parts), stats.n, ", ".join(places) if places.size() else "none"]

func ask() -> void:
 if llm_pending.x == -99999:return
 llm_area = llm_pending
 llm_pending = Vector2i(-99999, -99999)
 llm_asked[llm_area] = true
 llm_state = "asking"
 llm_started = Time.get_ticks_msec()
 var body := JSON.stringify({"model":llm_model, "system":"You classify places by their road traffic. Answer only JSON.", "prompt":prompt(llm_stats),
  "stream":false, "think":false, "keep_alive":"30m",
  "format":{"type":"object", "properties":{"character":{"type":"string", "enum":CHARACTERS.keys()}}, "required":["character"]},
  "options":{"num_gpu":0, "num_predict":20, "temperature":0}})
 if http(llm_timeout).request(llm_host + "/api/generate", ["Content-Type: application/json"], HTTPClient.METHOD_POST, body) != OK:llm_state = "ready"

func _on_llm(result: int, code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
 var ms := Time.get_ticks_msec() - llm_started
 var ok := result == HTTPRequest.RESULT_SUCCESS and code == 200
 var data = JSON.parse_string(body.get_string_from_utf8()) if ok else null
 match llm_state:
  "probing":
   if not data is Dictionary:
    llm_state = "absent";print("TRAFFIC llm absent at %s (%d ms, result %d)" % [llm_host, ms, result]);return
   var names := []
   for m in data.get("models", []):names.append(str(m.get("name", "")))
   if llm_model.is_empty():
    for m in LLM_MODELS:
     if m in names:llm_model = m;break
   if llm_model.is_empty() or not (llm_model in names or llm_model + ":latest" in names):
    llm_state = "absent";print("TRAFFIC llm: no small model installed at %s" % llm_host);return
   print("TRAFFIC llm %s at %s (%d ms)" % [llm_model, llm_host, ms])
   warm()
  "warming":
   llm_state = "ready" if ok else "absent"
   print("TRAFFIC llm warm %s in %d ms" % ["ok" if ok else "failed", ms])
   if ok:ask()
  "asking":
   llm_state = "ready"
   var answer = JSON.parse_string(str(data.get("response", ""))) if data is Dictionary else null
   var got := str(answer.get("character", "")) if answer is Dictionary else ""
   if CHARACTERS.has(got):
    log_rows.append([llm_area, got, "llm", ms])
    print("TRAFFIC area %s character %s (llm %s, %d ms)" % [llm_area, got, llm_model, ms])
    if llm_area == area:character = got;character_source = "llm";mix = CHARACTERS[got].duplicate()
   else:
    log_rows.append([llm_area, character, "llm-late", ms])
    print("TRAFFIC area %s llm gave no answer in %d ms (result %d); rules stand" % [llm_area, ms, result])
   ask()
