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

func _initialize():
 var noise = preload("res://RoadNoise.gd").new()
 var asphalt := []
 for speed in [0.,10.,20.,30.,45.]:
  asphalt.append(report("road asphalt %2d m/s"%int(speed),measure(func(n):return noise.render_road(n,speed,1.,0.))).rms)
 report("road gravel 20 m/s",measure(func(n):return noise.render_road(n,20.,.76,.2)))
 report("road verge 20 m/s rough",measure(func(n):return noise.render_road(n,20.,.65,.6)))
 report("road verge 45 m/s bump 1",measure(func(n):return noise.render_road(n,45.,.65,1.)))
 report("road airborne 30 m/s",measure(func(n):return noise.render_road(n,30.,1.,0.,0.)))
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
