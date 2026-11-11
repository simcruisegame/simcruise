extends SceneTree
## Headless: renders one second of each procedural sound layer across the speed
## and surface range and reports peak, RMS and render time. Fails on a
## non-finite sample, a peak at or above .95 (no headroom before the bus
## trims), a tyre or wind level that does not rise with speed, or a rev
## limiter that does not stutter. Prints once at the end, then quits.
##   godot --headless --path game --script res://checks/RoadNoiseCheck.gd
const FRAMES := 22050
var failures := []

# Render FRAMES samples in the 367/368-frame chunks the game pushes each frame.
func measure(renderer: Callable, frames := FRAMES) -> Dictionary:
 var peak := 0.;var energy := 0.;var bad := 0;var done := 0;var chunk := 0
 var start := Time.get_ticks_usec()
 while done < frames:
  var n := mini(368 if chunk%2 == 0 else 367,frames-done);chunk += 1
  var samples: PackedVector2Array = renderer.call(n)
  if samples.size() != n:bad += 1
  for s in samples:
   if not (is_finite(s.x) and is_finite(s.y)):bad += 1;continue
   peak = maxf(peak,maxf(absf(s.x),absf(s.y)));energy += s.x*s.x+s.y*s.y
  done += n
 return {"peak":peak,"rms":sqrt(energy/(2.*frames)),"bad":bad,"ms":(Time.get_ticks_usec()-start)/1000.}

# Ratio of the loudest to the quietest 10 ms window: a stuttering limiter is
# strongly modulated, steady running is not.
func modulation(renderer: Callable, frames := 11025) -> float:
 var window := 220;var loudest := 0.;var quietest := INF;var i := 0
 var samples: PackedVector2Array = renderer.call(frames)
 while i+window <= samples.size():
  var energy := 0.
  for j in window:energy += samples[i+j].x*samples[i+j].x
  var level := sqrt(energy/window);loudest = maxf(loudest,level);quietest = minf(quietest,level);i += window
 return loudest/maxf(quietest,1e-6)

func report(name: String, stats: Dictionary) -> Dictionary:
 print("ROADNOISE %-28s peak=%.3f rms=%.3f (%.1f dBFS) render_ms=%.1f"%[name,stats.peak,stats.rms,20.*log(maxf(stats.rms,1e-9))/log(10.),stats.ms])
 if stats.bad > 0:failures.append(name+": non-finite samples or short buffer")
 if stats.peak >= .95:failures.append(name+": peak %.3f leaves no headroom"%stats.peak)
 return stats

func db(rms: float) -> float:
 return 20.*log(maxf(rms,1e-9))/log(10.)

# Four tyres at the same normalised slip s, static load and sliding speed.
func tyres(s: float, slide: float, load := 1.) -> PackedFloat32Array:
 var out := PackedFloat32Array()
 for i in 4:out.append_array([s,load,slide])
 return out

# The tyre layer alone, silent stretches as zeros.
func tyre_render(noise, n: int, packed: PackedFloat32Array, paved := 1.) -> PackedVector2Array:
 var out: PackedVector2Array = noise.render_tyres(n,packed,paved)
 if out.size() == n:return out
 out = PackedVector2Array();out.resize(n)
 return out

# Strongest frequency (Hz, 25 Hz steps from 100 to 4000) of the left channel, by Goertzel.
func spectral_peak(samples: PackedVector2Array) -> float:
 var best := 0.;var best_hz := 0.
 var hz := 100.
 while hz <= 4000.:
  var w := TAU*hz/22050.;var c := 2.*cos(w);var s1 := 0.;var s2 := 0.
  for x in samples:
   var s0: float = x.x+c*s1-s2;s2 = s1;s1 = s0
  var power := s1*s1+s2*s2-c*s1*s2
  if power > best:best = power;best_hz = hz
  hz += 25.
 return best_hz

## Tyre scrub and squeal (RoadNoise.render_tyres) from the normalised slip of
## Haptics.gd: silent below the scrub onset, at least −22 dBFS just past the
## plateau, a spectral peak in the 600-1600 Hz squeal band while sliding,
## louder as slip grows, silent off pavement, and render_road unchanged
## sample for sample when no tyre slips.
func tyre_layer() -> void:
 var noise = preload("res://RoadNoise.gd").new()
 var levels := []
 for s in [.5,.8,1.,1.1,1.5,2.]:
  var stats := measure(func(n):return tyre_render(noise,n,tyres(s,3.+6.*maxf(s-1.,0.))))
  report("tyres s %.1f"%s,stats);levels.append(stats.rms)
 if db(levels[0]) >= -40.:failures.append("tyre layer audible at s 0.5 (%.1f dBFS)"%db(levels[0]))
 if db(levels[3]) < -22.:failures.append("tyre layer only %.1f dBFS at s 1.1 (want ≥ −22)"%db(levels[3]))
 for i in range(1,levels.size()):
  if levels[i] < levels[i-1]*.97:failures.append("tyre layer quieter at a larger slip (%.1f → %.1f dBFS)"%[db(levels[i-1]),db(levels[i])])
 var sliding := tyre_render(noise,8192,tyres(1.5,8.))
 sliding = tyre_render(noise,8192,tyres(1.5,8.))
 var peak_hz := spectral_peak(sliding)
 print("ROADNOISE tyre squeal spectral peak %.0f Hz at s 1.5, 8 m/s sliding"%peak_hz)
 if peak_hz < 600. or peak_hz > 1600.:failures.append("squeal peak %.0f Hz outside 600-1600 Hz"%peak_hz)
 var gravel = preload("res://RoadNoise.gd").new()
 var loose := measure(func(n):return gravel.render_road(n,20.,.76,.2,1.,tyres(2.,8.)))
 var plain = preload("res://RoadNoise.gd").new()
 var bare := measure(func(n):return plain.render_road(n,20.,.76,.2))
 if absf(loose.rms-bare.rms) > 1e-6:failures.append("tyre layer sounds on gravel (%.4f vs %.4f rms)"%[loose.rms,bare.rms])
 # No slip: the road layer is sample for sample what it was before.
 var with_tyres = preload("res://RoadNoise.gd").new();var without = preload("res://RoadNoise.gd").new()
 var same := true
 for chunk in 12:
  var a: PackedVector2Array = with_tyres.render_road(368,30.,1.,.1,1.,tyres(.3,1.))
  var b: PackedVector2Array = without.render_road(368,30.,1.,.1)
  if a != b:same = false
 if not same:failures.append("render_road changed with the tyres below the scrub onset")
 report("road asphalt 45 m/s tyres s 3",measure(func(n):return noise.render_road(n,45.,1.,1.,1.,tyres(3.,20.,2.))))
 for node in [noise,gravel,plain,with_tyres,without]:node.free()

func _initialize():
 var noise = preload("res://RoadNoise.gd").new()
 var asphalt := []
 for speed in [0.,10.,20.,30.,45.]:
  asphalt.append(report("road asphalt %2d m/s"%int(speed),measure(func(n):return noise.render_road(n,speed,1.,0.))).rms)
 report("road gravel 20 m/s",measure(func(n):return noise.render_road(n,20.,.76,.2)))
 report("road verge 20 m/s rough",measure(func(n):return noise.render_road(n,20.,.65,.6)))
 report("road verge 45 m/s bump 1",measure(func(n):return noise.render_road(n,45.,.65,1.)))
 report("road airborne 30 m/s",measure(func(n):return noise.render_road(n,30.,1.,0.,0.)))
 tyre_layer()
 var gusts := []
 for speed in [0.,15.,30.,45.]:
  gusts.append(report("wind %2d m/s"%int(speed),measure(func(n):return noise.render_wind(n,speed))).rms)
 for i in range(1,asphalt.size()):
  if asphalt[i] <= asphalt[i-1]:failures.append("asphalt level does not rise with speed")
 for i in range(1,gusts.size()):
  if gusts[i] <= gusts[i-1]:failures.append("wind level does not rise with speed")
 var voice = preload("res://InlineFourSound.gd").new()
 report("engine idle cabin",measure(func(n):return voice.render(n,850.,.12,0.,false,true,0.)))
 report("engine 4500 full exterior",measure(func(n):return voice.render(n,4500.,.9,.8,false,false,1.)))
 report("engine 4500 full cabin",measure(func(n):return voice.render(n,4500.,.9,.8,false,true,1.)))
 report("engine limiter 7050",measure(func(n):return voice.render(n,7050.,.05,.9,false,false,1.)))
 var steady_voice = preload("res://InlineFourSound.gd").new()
 steady_voice.render(2205,6800.,.9,.8,false,false,1.)
 var steady := modulation(func(n):return steady_voice.render(n,6800.,.9,.8,false,false,1.))
 var limiter_voice = preload("res://InlineFourSound.gd").new()
 limiter_voice.render(2205,7050.,.05,.9,false,false,1.)
 var stutter := modulation(func(n):return limiter_voice.render(n,7050.,.05,.9,false,false,1.))
 print("ROADNOISE limiter modulation steady=%.2f stutter=%.2f (10 ms window loud/quiet ratio)"%[steady,stutter])
 if stutter < 2.0 or stutter < steady*1.5:failures.append("rev limiter does not stutter (ratio %.2f vs steady %.2f)"%[stutter,steady])
 var popper = preload("res://InlineFourSound.gd").new()
 popper.render(2205,4200.,.9,.8,false,false,1.)
 var pops := report("engine overrun 4200 exterior",measure(func(n):return popper.render(n,4200.,0.,-.8,false,false,0.)))
 print("ROADNOISE overrun crest factor %.2f"%(pops.peak/maxf(pops.rms,1e-6)))
 noise.free()
 if failures.is_empty():print("ROADNOISE PASSED")
 else:
  for f in failures:print("ROADNOISE FAILED ",f)
 quit(0 if failures.is_empty() else 1)
