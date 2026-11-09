extends RefCounted
## Four power strokes per 720 crank degrees. Separate induction and exhaust
## character change with delivered combustion torque and driveline overrun.
## Acoustic envelopes are approximations, not BP-4W microphone measurements.
##
## Cabin pickup (driver view) keeps the fundamental and a 120 Hz body boom and
## rolls the exhaust off above ~780 Hz; the exterior pickup is brighter
## (~2.6 kHz) with more exhaust presence. Above LIMITER_RPM with the pedal down
## the fuel cut stutters at LIMITER_HZ, and closing the throttle above
## POP_RPM gives a decaying chance of mild exhaust pops. Output is clamped to
## ±.94 so the Engine bus trim in RoadNoise.gd can budget for the peak.
const RATE := 22050.
const SIZE := 1024
const MASK := SIZE-1
const LIMITER_RPM := 6950.
const LIMITER_HZ := 14.
const POP_RPM := 3000.

var exhaust_light := PackedFloat32Array()
var exhaust_load := PackedFloat32Array()
var intake := PackedFloat32Array()
var pulse_window := PackedFloat32Array()
var phase := 0.
var firing_hz := 0.
var charge := 0.
var pull := 0.
var overrun := 0.
var gain := 0.
var random_state: int = 38791
var noise_low := 0.
var noise_slow := 0.
var output_low := 0.
var boom := 0.
var pulse_gain := 1.
var throttle := 0.
var fired := false
var limiter_phase := 0.
var cut_gain := 1.
var overrun_age := 0.
var pop_env := 0.
var pop_low := 0.

func _init():
 # Each smooth, band-limited cycle is one evenly spaced exhaust event.
 # A short pressure pulse retains upper orders under load. Gentle running
 # and cabin pickup emphasize the fundamental; turbulent air is separate.
 var light := [.82,.14,.085,.067,.045,.037,.028,.020,.014,.012,.007,.005]
 var load := [.64,.31,.19,.13,.10,.072,.073,.054,.055,.046,.035,.031,.025,.019,.012,.007]
 for i in SIZE+1:
  var theta := TAU*float(i)/SIZE
  var quiet := 0.;var loud := 0.;var air := 0.
  for n in load.size():
   loud += load[n]*sin(theta*(n+1)-.12*n)
   if n<light.size():quiet+=light[n]*sin(theta*(n+1)-.15*n)
  for n in 7:air+=sin((theta-.78)*(n+1))*.21/(1.+.36*n*n)
  exhaust_light.append(quiet);exhaust_load.append(loud);intake.append(air)
  pulse_window.append(.28+.72*pow(.5+.5*cos(theta-.65),2))

func render(frames:int,rpm:float,combustion:float,shaft_load:float,stalled:bool,cabin:bool,pedal:float)->PackedVector2Array:
 var samples:=PackedVector2Array();samples.resize(frames)
 if frames<1 or (stalled and gain<.00003):return samples
 var target_hz:=clampf(rpm,0.,7200.)/30.
 # RPM follows physics continuously, including rev limiter, shifts and stalls.
 if not fired:firing_hz=target_hz;fired=true
 var hz_step:=(target_hz-firing_hz)/float(frames)
 var target_charge:=clampf(combustion,0.,1.) if not stalled else 0.
 var target_throttle:=clampf(pedal,0.,1.)
 var target_pull:=clampf(shaft_load,0.,1.)
 var target_overrun:=clampf(-shaft_load,0.,1.)*smoothstep(1150.,2300.,rpm)*(1.-target_charge)
 var target_gain:=0. if stalled else 1.
 var smoothing:=1.-exp(-1./(RATE*.025))
 var filter:=.20 if cabin else .52
 var boom_level:=.45 if cabin else 0.
 var exhaust_presence:=1. if cabin else 1.12
 # Powertrain cuts torque above 7040 rpm; the pedal is still down, so the
 # sound alternates between fuel cut and fire at LIMITER_HZ.
 var limiting:=rpm>LIMITER_RPM and target_throttle>.35 and not stalled
 var cut_smoothing:=1.-exp(-1./(RATE*.0025))
 # Overrun pops: likely right after the throttle closes, fading over ~1.2 s.
 if target_overrun>.25 and rpm>POP_RPM:overrun_age+=float(frames)/RATE
 else:overrun_age=0.
 var pop_chance:=0.
 if overrun_age>0.:pop_chance=.10*target_overrun*smoothstep(POP_RPM,4200.,rpm)*exp(-overrun_age/1.2)
 var pop_level:=.30 if cabin else .55
 var pop_decay:=exp(-1./(RATE*.012))
 for i in frames:
  throttle+=(target_throttle-throttle)*smoothing
  charge+=(target_charge-charge)*smoothing
  pull+=(target_pull-pull)*smoothing
  overrun+=(target_overrun-overrun)*smoothing
  gain+=(target_gain-gain)*smoothing
  firing_hz+=hz_step
  phase+=firing_hz*SIZE/RATE
  if phase>=SIZE:
   phase-=SIZE
   # Small zero-mean combustion-cycle variation, strongest under light load.
   pulse_gain=1.+(float(random_state)/1073741824.-1.)*(.035-.018*charge)
   if pop_chance>0. and float(random_state)/2147483647.<pop_chance:pop_env=pop_level*(.6+.4*float(random_state&1023)/1023.)
  var cut_target:=1.
  if limiting:
   limiter_phase+=LIMITER_HZ/RATE
   if limiter_phase>=1.:limiter_phase-=1.
   if limiter_phase<.42:cut_target=.12
  cut_gain+=(cut_target-cut_gain)*cut_smoothing
  var k:=int(phase)&MASK;var t:=phase-floorf(phase)
  var quiet:=lerpf(exhaust_light[k],exhaust_light[k+1],t)
  var loaded:=lerpf(exhaust_load[k],exhaust_load[k+1],t)
  var air:=lerpf(intake[k],intake[k+1],t)
  random_state=(random_state*1664525+1013904223)&0x7fffffff
  var white:=float(random_state)/1073741824.-1.
  noise_low+=.32*(white-noise_low);noise_slow+=.04*(noise_low-noise_slow)
  var breath:=(noise_low-noise_slow)*pulse_window[k]
  var speed:=clampf(firing_hz/215.,0.,1.)
  var pulse:=lerpf(quiet,loaded,charge)*pulse_gain*cut_gain
  var exhaust_level:=(.17+.34*charge+.14*pull+.08*speed-.045*overrun)*exhaust_presence
  var inlet_level:=(.02+.42*throttle*charge)*(.50 if cabin else 1.)*cut_gain
  var airflow:=(.036+.13*charge+.08*overrun)*(.55+speed)+.05*(1.-cut_gain)
  pop_env*=pop_decay
  pop_low+=.35*(white*pop_env-pop_low)
  var exhaust:=pulse*exhaust_level
  boom+=.034*(exhaust-boom)
  var pressure:=exhaust+boom*boom_level+air*inlet_level+breath*airflow+pop_low
  output_low+=filter*(pressure-output_low)
  var v:=clampf(output_low*gain,-.94,.94)
  samples[i]=Vector2(v,v)
 return samples
