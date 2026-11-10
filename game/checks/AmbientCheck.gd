extends SceneTree
## Headless: the pure parts of the night-shift features, no map needed.
##  * GraphicsQuality.detect picks sensible tiers for known adapters.
##  * AmbientLife.normalise puts an arbitrary downloaded car (wrong unit, wrong
##    axis, floating) on the ground at the stated length.
##  * RoadRoughness: RMS profile height rises with IRI, a motorway is smoother
##    than a residential street, and nothing exceeds a few centimetres (the 1.0
##    roadmap's "bumpy enough to launch the car" must not come back).
##   godot --headless --path game --script res://checks/AmbientCheck.gd
var failures := []

func expect(ok: bool, what: String) -> void:
 if not ok:failures.append(what)

func _initialize() -> void:
 var gq = load("res://GraphicsQuality.gd")
 for row in [["NVIDIA GeForce RTX 2070 Super with Max-Q Design",2,"high"],["NVIDIA GeForce RTX 4070",2,"ultra"],["Intel(R) UHD Graphics",1,"low"],["llvmpipe (LLVM 17.0.6, 256 bits)",4,"low"],["Apple M2",1,"medium"],["AMD Radeon RX 580",2,"medium"]]:
  var got: String = gq.detect(row[0], row[1])
  print("AMBIENT tier %-52s -> %s" % [row[0], got])
  expect(got == row[2], "tier for %s: %s, expected %s" % [row[0], got, row[2]])

 var life = load("res://AmbientLife.gd")
 # A car modelled in centimetres along X, floating 30 cm up.
 var box := AABB(Vector3(-230, 30, -90), Vector3(460, 145, 180))
 var fit: Transform3D = life.normalise(box, 4.7, false)
 var placed: AABB = fit * box
 print("AMBIENT normalise size=%s position=%s" % [placed.size, placed.position])
 expect(absf(placed.size.z - 4.7) < .01, "normalised length %.3f" % placed.size.z)
 expect(placed.size.x < placed.size.z, "long axis not along Z")
 expect(absf(placed.position.y) < .001, "car not on the ground: %.3f" % placed.position.y)
 expect(absf(placed.position.x + placed.size.x * .5) < .001 and absf(placed.position.z + placed.size.z * .5) < .001, "car not centred")

 var rough = load("res://RoadRoughness.gd").new()
 var rms := {}
 var peak := 0.
 for iri in [1.0, 2.6, 6.5]:
  var sum := 0.;var n := 0
  for i in 4000:
   var p := Vector2(i * .37 + 1000., 5000. + i * .11)
   var h: float = rough.height(p, iri);sum += h * h;n += 1;peak = maxf(peak, absf(h))
  rms[iri] = sqrt(sum / n)
  print("AMBIENT roughness IRI %.1f m/km -> RMS %.1f mm" % [iri, rms[iri] * 1000.])
 expect(rms[1.0] < rms[2.6] and rms[2.6] < rms[6.5], "roughness does not rise with IRI")
 expect(rms[1.0] > .0008 and rms[1.0] < .006, "motorway RMS %.4f m out of band" % rms[1.0])
 expect(peak < .08, "profile peak %.3f m could launch a car" % peak)

 if failures.is_empty():
  print("AMBIENT PASS")
  quit(0)
 else:
  for f in failures:print("AMBIENT FAIL ", f)
  quit(1)
