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
##   godot --headless --path game --script res://checks/TrafficCheck.gd
var failures := []
const D = preload("res://TrafficDirector.gd")
const LENGTH := 4.6

func expect(ok: bool, what: String) -> void:
 if not ok:failures.append(what)

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

 if failures.is_empty():
  print("TRAFFIC PASS")
  quit(0)
 else:
  for f in failures:print("TRAFFIC FAIL ", f)
  quit(1)
