extends SceneTree
## Headless: the traffic driving model and director rules, no map needed.
##  * IDM: a car of every persona approaching a stopped car from every road-class
##    speed, at 60 Hz and at a hitching 10 Hz frame rate, only seeing the stopped
##    car from the in-game horizon (max(70 m, 5 s)), comes to rest behind it with
##    a gap near its standstill gap, never closer than 1 m, never touching.
##  * A platoon of mixed personas behind a stop-and-go leader (hard stops at
##    6 m/s²) never closes any gap below 1 m.
##  * Signals: the two approaches of a junction are never green together, and
##    every approach gets green within one cycle.
##  * The rule-based place character and persona mixes are sane.
##  * The street graph (a two-tile fixture): minor streets are driven, roads join
##    across a tile seam, and a car keeps its segment through a tile unload
##    (or drives on along a ghost of it while in view) and through a rebase.
##   godot --headless --path game --script res://checks/TrafficCheck.gd
var failures := []
const D = preload("res://TrafficDirector.gd")
const AmbientLife = preload("res://AmbientLife.gd")
const LENGTH := 4.6

func expect(ok: bool, what: String) -> void:
 if not ok:failures.append(what)

class FakeTile:
 var segments := []
 var info := {}
 var side := 480.

class FakeGame:
 var tiles := {}
 var origin := Vector2i.ZERO
 var side := 480.
 var pos := Vector2.ZERO
 func tile_offset(id: Vector2i) -> Vector2:return Vector2(id - origin) * side
 func tile_id(p: Vector2) -> Vector2i:return origin + Vector2i((p / side).floor())

## The street graph on two tiles in the helper's own format: a tertiary road
## running east across the seam, a minor street north from a junction on it
## and a minor dead end south. Checks that minor streets are driven, that the
## road joins across the seam, and that a car keeps its segment (same key, same
## place) through an unload of the other tile and through a rebase.
func graph_fixture() -> void:
 # Every road class the helper draws is driven except driveways and tracks.
 var widths := FileAccess.get_file_as_string(ProjectSettings.globalize_path("res://").path_join("../helper/road-widths.mjs"))
 var block := RegEx.create_from_string("referenceWidths=\\{([^}]*)\\}").search(widths)
 expect(block != null, "referenceWidths not found in helper/road-widths.mjs")
 if block:
  for m in RegEx.create_from_string("(\\w+):").search_all(block.get_string(1)):
   var c := m.get_string(1)
   if c in ["service", "track"]:continue
   expect(c in AmbientLife.DRIVABLE, "helper road class %s is not drivable" % c)
 var life = AmbientLife.new()
 var g := FakeGame.new()
 life.game = g
 var a := FakeTile.new()
 a.segments = [
  [Vector2(0, 240), Vector2(200, 240), "Main", 10., 0, "tertiary"],
  [Vector2(200, 240), Vector2(480, 240), "Main", 10., 0, "tertiary"],
  [Vector2(200, 240), Vector2(200, 0), "Elm", 8., 0, "minor"],
  [Vector2(200, 240), Vector2(200, 400), "Oak", 8., 0, "minor"],
  [Vector2(300, 240), Vector2(300, 300), "lot", 4.6, 0, "service"],
 ]
 var b := FakeTile.new()
 b.segments = [[Vector2(0, 240), Vector2(300, 240), "Main", 10., 0, "tertiary"]]
 g.tiles = {Vector2i(0, 0):a, Vector2i(1, 0):b}
 life.rebuild_graph()
 expect(life.segs.size() == 5, "graph has %d segments, expected 5 (service is not driven)" % life.segs.size())
 var main0: int = life.key_index[Vector3i(0, 0, 0)]
 var opts: Array = life.options(main0, 1)
 var classes := []
 for o in opts:classes.append(life.segs[o[0]][4])
 expect("minor" in classes and "tertiary" in classes, "a tertiary road meeting minor streets offers %s" % [classes])
 var main1: int = life.key_index[Vector3i(0, 0, 1)]
 var across: Array = life.options(main1, 1)
 expect(across.size() == 1 and life.seg_keys[across[0][0]] == Vector3i(1, 0, 0), "the road does not join across the tile seam")
 expect(AmbientLife.density("primary") > 10. * AmbientLife.density("minor"), "an arterial does not carry far more cars per km than a side street")
 # A car on the far tile with a route back across the seam, then the near tile unloads.
 var on_b: int = life.key_index[Vector3i(1, 0, 0)]
 var car := {"seg":on_b, "dir":-1, "s":50., "route":[[main1, -1], [main0, -1]], "pos":Vector3(680, 0, 240), "held":false, "gone":""}
 var lone := {"seg":main0, "dir":1, "s":20., "route":[], "pos":Vector3(20, 0, 240), "held":false, "gone":""}
 life.cars = [car, lone]
 var before: Array = life.segs[car.seg].duplicate()
 g.tiles.erase(Vector2i(0, 0))
 life.rebuild_graph()
 expect(car.seg >= 0 and life.seg_keys[car.seg] == Vector3i(1, 0, 0) and life.segs[car.seg][0] == before[0], "a car lost its segment when another tile unloaded")
 expect(car.route.is_empty(), "a route through an unloaded tile was kept")
 expect(lone.seg < 0 and lone.gone == "tile_unloaded", "an unseen car on an unloaded tile was not retired")
 # In view, a car on an unloading tile drives on along a ghost of its segment.
 g.tiles[Vector2i(0, 0)] = a
 life.rebuild_graph()
 lone.seg = life.key_index[Vector3i(0, 0, 0)];lone.gone = ""
 life.views = [[Vector3.ZERO, [], 1e9]]          # everything is in view
 g.tiles.erase(Vector2i(0, 0))
 life.rebuild_graph()
 expect(lone.seg >= life.real_count and life.segs[lone.seg][0] == Vector2(0, 240), "a car in view on an unloading tile did not keep a ghost segment")
 expect(life.options(lone.seg, -1).is_empty(), "a ghost segment joins the graph")
 # Off the loaded tiles there is no ground to sample: the car keeps its own height.
 expect(is_equal_approx(life.height(lone, Vector2(20, 240), [Vector2(20, 240), Vector2.RIGHT, 200., 0.], 12.5), 12.5), "a car on a ghost segment does not keep its own height")
 life.views = []
 # Rebase: the origin moves one tile east; the car keeps its segment, shifted.
 g.tiles[Vector2i(0, 0)] = a
 life.rebuild_graph()
 g.origin += Vector2i(1, 0)
 life.rebuild_graph()
 expect(car.seg >= 0 and life.seg_keys[car.seg] == Vector3i(1, 0, 0) and life.segs[car.seg][0] == before[0] - Vector2(480, 0), "a rebase moved a car off its segment")
 print("TRAFFIC graph %d segments, junction offers %s, seam joins, unload/ghost/rebase keep cars, retired %s" % [life.segs.size(), classes, life.retired])
 life.director.free()
 life.free()

## Follower starts at x 0 with speed v, a stopped car's rear bumper sits at
## `at`. Returns [min net gap, final net gap, final speed, peak braking].
func approach(p: Dictionary, v: float, v0: float, at: float, dt: float) -> Array:
 var x := 0.
 var min_gap := INF
 var peak := 0.
 for i in int((at / maxf(v, 1.) + 60.) / dt):
  var front := x + LENGTH * .5
  var net := at - front
  var horizon := maxf(70., v * 5.)
  var gap := net if net + LENGTH < horizon else INF   # seen centre to centre, as in game
  var acc: float = D.idm(v, v0, gap, v, p)
  peak = minf(peak, acc)
  var m: Vector2 = D.advance(v, acc, dt)
  x += m.x;v = m.y
  min_gap = minf(min_gap, at - (x + LENGTH * .5))
 return [min_gap, at - (x + LENGTH * .5), v, peak]

func _initialize() -> void:
 var r := RandomNumberGenerator.new()
 r.seed = 7
 var worst := INF
 for persona in D.ORDER:
  for base in [5., 10., 13., 17., 25., 29.]:
   for dt in [1. / 60., .1]:
    var p: Dictionary = D.driver(persona, r)
    var v0: float = base * float(p.speed)
    var got := approach(p, v0, v0, 600., dt)
    worst = minf(worst, got[0])
    var label := "%s v0 %.1f m/s dt %.3f" % [persona, v0, dt]
    expect(got[0] >= 1., "%s: closest gap %.2f m" % [label, got[0]])
    expect(got[2] < .05, "%s: still moving at %.2f m/s" % [label, got[2]])
    expect(got[1] >= p.s0 * .8 and got[1] <= p.s0 + 1.5, "%s: rest gap %.2f m (s0 %.2f)" % [label, got[1], p.s0])
    expect(got[3] > -D.MAX_BRAKE - .001, "%s: braking %.2f" % [label, got[3]])
    if dt < .05 and base in [13., 29.]:
     print("TRAFFIC stop %-36s rest gap %.2f m (s0 %.2f) closest %.2f m peak brake %.2f m/s²" % [label, got[1], p.s0, got[0], -got[3]])
 print("TRAFFIC approach worst closest gap %.2f m" % worst)

 # Platoon behind a stop-and-go leader.
 var cars := []
 var x := 0.
 for i in 9:
  var p := D.driver(D.ORDER[i % D.ORDER.size()], r)
  cars.append({"p":p, "x":x, "v":13.})
  x -= LENGTH + p.s0 + 13. * p.T
 var dt := 1. / 30.
 var t := 0.
 var min_gap := INF
 var crashes := 0
 while t < 400.:
  t += dt
  # Leader: cruise 13 m/s, every 40 s brake at 6 m/s² to a stop, wait 8 s.
  var lead: Dictionary = cars[0]
  var phase := fmod(t, 40.)
  var acc0 := -6. if phase > 20. and phase < 28. else D.idm(lead.v, 13., INF, 0., lead.p)
  var m0: Vector2 = D.advance(lead.v, acc0, dt)
  var moves := [m0]
  for i in range(1, cars.size()):
   var c: Dictionary = cars[i]
   var ahead: Dictionary = cars[i - 1]
   var gap: float = ahead.x - c.x - LENGTH
   moves.append(D.advance(c.v, D.idm(c.v, 13. * c.p.speed, gap, c.v - ahead.v, c.p), dt))
  for i in cars.size():
   cars[i].x += moves[i].x;cars[i].v = moves[i].y
  for i in range(1, cars.size()):
   var g: float = cars[i - 1].x - cars[i].x - LENGTH
   min_gap = minf(min_gap, g)
   if g <= 0.:crashes += 1
 print("TRAFFIC platoon 9 cars 400 s closest gap %.2f m, contacts %d" % [min_gap, crashes])
 expect(crashes == 0 and min_gap >= 1., "platoon closest gap %.2f m, %d contacts" % [min_gap, crashes])

 # Signals.
 var sig := {"at":Vector2.ZERO, "axis":Vector2.RIGHT, "cycle":75., "offset":13., "split":.55}
 var both := 0
 var first_green := [INF, INF]
 for i in 1500:
  var tt := i * .1
  var a: Array = D.signal_phase(sig, Vector2.RIGHT, tt)
  var b: Array = D.signal_phase(sig, Vector2.UP, tt)
  if a[0] == "green" and b[0] == "green":both += 1
  if a[0] == "green":first_green[0] = minf(first_green[0], tt)
  if b[0] == "green":first_green[1] = minf(first_green[1], tt)
  for pair in [[a, Vector2.RIGHT], [b, Vector2.UP]]:
   if pair[0][0] != "green":
    expect(D.signal_phase(sig, pair[1], tt + pair[0][1] + .05)[0] == "green", "not green after the stated wait at %.1f s" % tt)
 expect(both == 0, "both approaches green %d times" % both)
 expect(first_green[0] < 75. and first_green[1] < 75., "an approach waited more than a cycle")
 print("TRAFFIC signal conflicts %d, first green %.1f s / %.1f s" % [both, first_green[0], first_green[1]])

 # Place character.
 var cases := [
  [{"residential":5000., "tertiary":800.}, {"school":1, "restaurant":2}, "residential"],
  [{"primary":1200., "secondary":900., "residential":800.}, {"restaurant":20, "fast_food":12, "shop":14, "cafe":6, "bank":3}, "downtown"],
  [{"primary":1500., "secondary":800., "unclassified":600.}, {"fuel":2, "car":2, "car_repair":1}, "industrial"],
  [{"unclassified":3000., "tertiary":1500.}, {}, "rural"],
  [{"residential":2000., "secondary":900.}, {"museum":3, "lodging":4, "attraction":3, "restaurant":3}, "tourist"],
  [{"secondary":1500., "residential":1500.}, {"college":4, "school":1, "cafe":3, "library":1}, "campus"],
  [{"primary":2500., "residential":1000.}, {"fast_food":5, "fuel":3, "shop":4}, "commercial_strip"],
 ]
 for c in cases:
  var got := D.classify(c[0], c[1])
  print("TRAFFIC character %-16s expected %s" % [got, c[2]])
  expect(got == c[2], "character %s, expected %s" % [got, c[2]])
 for name in D.CHARACTERS:
  var w: Array = D.CHARACTERS[name]
  var total := 0.
  for x2 in w:total += x2
  expect(w.size() == D.ORDER.size() and absf(total - 1.) < .011, "mix %s sums to %.2f" % [name, total])
 # Turn speeds: a right-angle urban corner at 4-6 m/s, a gentle bend much faster.
 var corner := D.turn_speed(0., 2.6)
 expect(corner > 4. and corner < 6., "right-angle turn speed %.1f m/s" % corner)
 expect(D.turn_speed(.8, 2.6) > 9., "gentle bend too slow")

 graph_fixture()

 if failures.is_empty():
  print("TRAFFIC PASS")
  quit(0)
 else:
  for f in failures:print("TRAFFIC FAIL ", f)
  quit(1)
