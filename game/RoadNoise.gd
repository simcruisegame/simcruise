extends Node
## Procedural tyre, wind and off-pavement rumble for the cockpit mix. Rendered
## with AudioStreamGenerator at 22.05 kHz like InlineFourSound; no sample files.
##
## Bus tree, created here in code (Godot 4.7 AudioServer):
##   Master ← Cabin ← Engine / Road / Wind
## A bus may only send to a bus with a lower index, so Cabin is added first.
## Cabin carries one AudioEffectLowPassFilter that is enabled in the driver view:
## with the soft top up, exterior sources lose treble above CABIN_CUTOFF_HZ.
##
## Level plan (peak sample × player trim × bus gain, every layer at once):
##   engine  InlineFourSound clamps at 0.94; trim −6 dB, bus −6 dB → 0.236
##   road    tanh limited below 0.90 by construction (the tyre scrub and
##           squeal are mixed in before that limiter); bus −8 dB    → 0.358
##   wind    tanh limited below 0.85 by construction; bus −30 dB   → 0.027
##   worst-case sum 0.62 < 1.0, so Master cannot clip even if all three layers
##   peak on the same sample. Rolling at 30 m/s on asphalt the mix sits near
##   −20 dBFS RMS. checks/RoadNoiseCheck.gd measures each layer headless.
## Each generator keeps about 60 ms queued (AudioFeed.gd), not its full buffer.
const RATE := 22050.
const ENGINE_TRIM_DB := -6.
# Wind is 30 dB down (it was −10 dB), at the owner's request on 10 November
# 2026: it was too prominent. It is now only a faint hiss at highway speed.
const BUS_DB := {"Cabin":0.,"Engine":-6.,"Road":-8.,"Wind":-30.}
const CABIN_CUTOFF_HZ := 2400.
# Surface classes from WorldTile.wheel_grip: 1.0 paved, .76 gravel or mapped
# unpaved, .65 verge or field, .72 while a tile is still loading.
const PAVED_GRIP := .92
const GRAVEL_GRIP := .70
# Layer gains, set from checks/RoadNoiseCheck.gd so that on asphalt at 30 m/s
# the tyre layer sits near −16 dBFS RMS before its bus (an NB at 65 mph is a
# road-and-wind car at cruise) and reaches the engine's full-load level by 45 m/s.
const HISS_GAIN := .72
const CRUNCH_GAIN := .55
const RUMBLE_GAIN := 9.
const WIND_GAIN := 2.7
# Tyre layer (render_tyres), set from checks/RoadNoiseCheck.gd: every tyre at
# s = 1.1 and its static load sits near −17 dBFS RMS before the bus, about the
# asphalt hiss at 30 m/s; a squeal at s 1.5 near −15 dBFS, a slide at s 2 −13.
const SCRUB_FROM_S := .6
const SQUEAL_FULL_S := 1.4
const SKID_FULL_S := 2.5
const SQUEAL_BASE_HZ := 700.
const SQUEAL_HZ_PER_MPS := 25.
const SCRUB_GAIN := 1.
const SQUEAL_GAIN := 2.
const SKID_GAIN := .3
# Squeal needs a sealed surface: none on gravel (.76) or verge, full on pavement.
const TYRE_LOOSE_GRIP := .80
const TYRE_PAVED_GRIP := .95

var game
var road_player: AudioStreamPlayer
var wind_player: AudioStreamPlayer
var road_playback: AudioStreamGeneratorPlayback
var wind_playback: AudioStreamGeneratorPlayback
var running := false
var playback_allowed := true
var closed := false
var cabin_known := false
var cabin_state := false
var travel := [0.,0.,0.,0.]
var travel_known := false
var travel_slow := 0.
var roughness := 0.
# Render state. Smoothed levels avoid steps between 17 ms buffers.
var random_state: int = 7919
var hiss := 0.
var crunch := 0.
var rumble := 0.
var hiss_cutoff := 900.
var band_l := 0.
var band_l_low := 0.
var band_r := 0.
var band_r_low := 0.
var crunch_env := 0.
var crunch_low := 0.
var rumble_a := 0.
var rumble_b := 0.
var wind := 0.
var wind_cutoff := 200.
var wind_l := 0.
var wind_r := 0.
var gust_l := 0.
var gust_r := 0.
# Tyre layer: its own noise source, so the hiss, crunch and rumble sequence
# is unchanged by whether a tyre is slipping.
var tyre_random: int = 104729
var scrub_hz := 500.
var squeal_hz := 800.
var scrub_level := [0.,0.]
var squeal_level := [0.,0.]
var skid_level := [0.,0.]
var scrub_low := [0.,0.]
var scrub_band := [0.,0.]
var squeal_low := [0.,0.]
var squeal_band := [0.,0.]
var skid_low := [0.,0.]
var road_feed = preload("res://AudioFeed.gd").new()
var wind_feed = preload("res://AudioFeed.gd").new()

static func setup_buses() -> void:
 if AudioServer.get_bus_index("Cabin") != -1:return
 for name in ["Cabin","Engine","Road","Wind"]:
  var index := AudioServer.bus_count
  AudioServer.add_bus(index)
  AudioServer.set_bus_name(index,name)
  AudioServer.set_bus_send(index,"Master" if name == "Cabin" else "Cabin")
  AudioServer.set_bus_volume_db(index,BUS_DB[name])
 var lowpass := AudioEffectLowPassFilter.new()
 lowpass.cutoff_hz = CABIN_CUTOFF_HZ
 lowpass.resonance = .5
 AudioServer.add_bus_effect(AudioServer.get_bus_index("Cabin"),lowpass)
 AudioServer.set_bus_effect_enabled(AudioServer.get_bus_index("Cabin"),0,false)

static func set_cabin(inside: bool) -> void:
 var cabin := AudioServer.get_bus_index("Cabin")
 if cabin != -1 and AudioServer.get_bus_effect_count(cabin) > 0:
  AudioServer.set_bus_effect_enabled(cabin,0,inside)

func _ready() -> void:
 setup_buses()
 road_player = make_player("Road")
 wind_player = make_player("Wind")
 # Headless checks keep the mixer idle (see Drive.toggle_sound).
 playback_allowed = DisplayServer.get_name() != "headless"

func make_player(bus: String) -> AudioStreamPlayer:
 var player := AudioStreamPlayer.new()
 var stream := AudioStreamGenerator.new()
 stream.mix_rate = RATE
 stream.buffer_length = .15
 player.stream = stream
 player.bus = bus
 add_child(player)
 return player

func _process(dt: float) -> void:
 if game == null:return
 if game.closing and not closed:
  # Stop every generator as soon as the close begins, so the audio thread can
  # release the playbacks during the close window instead of leaking at exit.
  closed = true
  game.engine.stop()
 var want: bool = playback_allowed and game.sound_enabled and game.ready_drive and not game.closing
 if want != running:
  running = want
  if running:
   road_player.play();road_playback = road_player.get_stream_playback();road_feed.reset()
   wind_player.play();wind_playback = wind_player.get_stream_playback();wind_feed.reset()
  else:
   road_player.stop();wind_player.stop();road_playback = null;wind_playback = null
 var inside: bool = game.view == 1
 if not cabin_known or inside != cabin_state:
  cabin_known = true;cabin_state = inside;set_cabin(inside)
 # Roughness also drives the pad rumble (Haptics.gd), so it runs with sound off.
 var car = game.vehicle
 if game.ready_drive and not game.closing:update_roughness(car,maxf(dt,1./240.))
 if not running:return
 var speed: float = absf(game.velocity)
 var grip := 0.
 for g in car.surface_grip:grip += g
 grip *= .25
 var contact := float(car.wheel_contact)/4.
 if road_playback:
  var frames: int = road_feed.frames_to_push(road_playback)
  if frames > 0:road_playback.push_buffer(render_road(frames,speed,grip,roughness,contact,tyre_inputs()))
 if wind_playback:
  var frames: int = wind_feed.frames_to_push(wind_playback)
  if frames > 0:wind_playback.push_buffer(render_wind(frames,speed))

# Per-tyre slip, load and sliding speed from the last physics tick (Haptics.gd),
# packed for render_road: [s, load, slide] for FL, FR, RL, RR.
func tyre_inputs() -> PackedFloat32Array:
 var feel = game.haptics if "haptics" in game else null
 if feel == null:return PackedFloat32Array()
 var out := PackedFloat32Array();out.resize(12)
 for i in 4:
  out[i*3] = feel.slip[i];out[i*3+1] = feel.load_ratio[i];out[i*3+2] = feel.slide[i]
 return out

# Suspension travel rate from the Powertrain contact lengths, high-passed so
# steady roll or brake dive does not read as roughness. 0 smooth … 1 rough.
func update_roughness(car, dt: float) -> void:
 var rate := 0.
 for i in 4:
  var y: float = car.connected_wheels[i].position.y
  if travel_known:rate += minf(1.5,absf(y-travel[i])/dt)
  travel[i] = y
 travel_known = true
 rate *= .25
 travel_slow += (rate-travel_slow)*(1.-exp(-dt/.5))
 roughness += (clampf((rate-travel_slow)/.35,0.,1.)-roughness)*(1.-exp(-dt/.1))

## Tyre hiss, gravel crunch and low rumble as one stereo "Road" layer.
##   speed  m/s, grip the mean WorldTile.wheel_grip of the four contacts,
##   bump   0..1 roughness, contact the fraction of wheels touching the ground,
##   tyres  optional per-tyre [s, load, slide] × 4 (tyre_inputs): the scrub and
##          squeal of render_tyres, mixed in before the limiter. Without it the
##          output is sample-for-sample what it was before the tyre layer.
func render_road(frames: int, speed: float, grip: float, bump: float, contact := 1., tyres := PackedFloat32Array()) -> PackedVector2Array:
 var samples := PackedVector2Array();samples.resize(frames)
 if frames < 1:return samples
 speed = absf(speed)
 var paved := smoothstep(GRAVEL_GRIP,PAVED_GRIP,grip)
 var tyre := render_tyres(frames,tyres,smoothstep(TYRE_LOOSE_GRIP,TYRE_PAVED_GRIP,grip)*contact) if tyres.size() >= 12 else PackedVector2Array()
 var has_tyre := tyre.size() == frames
 var loose := 1.-paved
 var offroad := 1.-smoothstep(.62,GRAVEL_GRIP,grip)
 var pace := clampf(speed/35.,0.,1.3)
 # Pass-by tyre noise rises about 30 dB per decade of speed: amplitude ∝ v^1.5.
 var hiss_target := (.04+.96*pow(pace,1.5))*(1.-.45*loose)*contact
 var crunch_target := smoothstep(.5,8.,speed)*(.35+.65*clampf(speed/22.,0.,1.))*loose*contact
 var rumble_target := clampf(.28*offroad*smoothstep(.5,6.,speed)+1.2*bump,0.,1.)*contact
 var cutoff_target := (850.+9.*speed)*(1.-.35*loose)
 hiss_cutoff += (cutoff_target-hiss_cutoff)*.3
 var f := 2.*sin(PI*hiss_cutoff/RATE)
 var smoothing := 1.-exp(-1./(RATE*.04))
 var crunch_decay := exp(-1./(RATE*.0055))
 var event_threshold := int((25.+4.5*speed)/RATE*2147483647.)
 for i in frames:
  hiss += (hiss_target-hiss)*smoothing
  crunch += (crunch_target-crunch)*smoothing
  rumble += (rumble_target-rumble)*smoothing
  random_state = (random_state*1664525+1013904223)&0x7fffffff
  var wl := float(random_state)/1073741824.-1.
  random_state = (random_state*1664525+1013904223)&0x7fffffff
  var wr := float(random_state)/1073741824.-1.
  # Independent left/right white noise through a state-variable band-pass (Q ≈ 1.1).
  band_l_low += f*band_l;band_l += f*(wl-band_l_low-.9*band_l)
  band_r_low += f*band_r;band_r += f*(wr-band_r_low-.9*band_r)
  # Gravel: Poisson bursts of low-passed noise, 25 + 4.5·v stones per second.
  random_state = (random_state*1664525+1013904223)&0x7fffffff
  if random_state < event_threshold:crunch_env = maxf(crunch_env,.35+.65*float(random_state)/float(maxi(event_threshold,1)))
  crunch_env *= crunch_decay
  random_state = (random_state*1664525+1013904223)&0x7fffffff
  var grain := (float(random_state)/1073741824.-1.)*crunch_env
  crunch_low += .33*(grain-crunch_low)
  # Rumble: white noise through two one-pole low-passes at 60 Hz.
  random_state = (random_state*1664525+1013904223)&0x7fffffff
  rumble_a += .0171*((float(random_state)/1073741824.-1.)-rumble_a)
  rumble_b += .0171*(rumble_a-rumble_b)
  var shared := crunch_low*crunch*CRUNCH_GAIN+rumble_b*rumble*RUMBLE_GAIN
  var l := band_l*hiss*HISS_GAIN+shared
  var r := band_r*hiss*HISS_GAIN+shared
  if has_tyre:l += tyre[i].x;r += tyre[i].y
  samples[i] = Vector2(.9*tanh(l/.9),.9*tanh(r/.9))
 return samples

## Tyre scrub and squeal from each tyre's normalised slip s (Haptics.gd: 1 = the
## end of the full-grip plateau), load over the reference corner load and
## contact-patch sliding speed (m/s). Before the bus and the road limiter.
##   scrub   band-passed noise (Q 1.4, 480 Hz + 12 Hz per m/s of sliding),
##           amplitude smoothstep(.6, 1, s) × load: the hum of a tyre near its limit
##   squeal  past the plateau the scrub crossfades (s 1 → 1.4) into a resonant
##           noise-excited tone, Q 8 at 700 Hz + 25 Hz per m/s of sliding
##           (600-1600 Hz, the band where measured tyre squeal sits)
##   skid    deep in a slide or locked (s 1.4 → 2.5) a broadband high-passed hiss
## Each side's tyres are panned to their channel with half their level in the
## other. A tyre with no sliding speed is silent; paved scales the layer, so
## gravel, verge and an airborne car make none. Returns an empty array while
## the layer is silent, so render_road adds nothing.
func render_tyres(frames: int, tyres: PackedFloat32Array, paved: float) -> PackedVector2Array:
 var side_scrub := [0.,0.];var side_squeal := [0.,0.];var side_skid := [0.,0.]
 var hz_sum := 0.;var hz_weight := 0.;var slide_sum := 0.;var slide_weight := 0.
 for w in 4:
  var s: float = tyres[w*3];var load: float = clampf(tyres[w*3+1],0.,2.);var slide: float = tyres[w*3+2]
  var a := smoothstep(SCRUB_FROM_S,1.,s)*load*smoothstep(.3,2.,slide)*paved
  if a <= 0.:continue
  var tone := smoothstep(1.,SQUEAL_FULL_S,s)
  var skid := smoothstep(SQUEAL_FULL_S,SKID_FULL_S,s)
  side_scrub[w%2] += a*(1.-.6*tone)*.5;side_squeal[w%2] += a*tone*.5;side_skid[w%2] += a*skid*.5
  hz_sum += a*tone*(SQUEAL_BASE_HZ+SQUEAL_HZ_PER_MPS*slide);hz_weight += a*tone
  slide_sum += a*slide;slide_weight += a
 var target_scrub := [(side_scrub[0]+.5*side_scrub[1])/1.5,(side_scrub[1]+.5*side_scrub[0])/1.5]
 var target_squeal := [(side_squeal[0]+.5*side_squeal[1])/1.5,(side_squeal[1]+.5*side_squeal[0])/1.5]
 var target_skid := [(side_skid[0]+.5*side_skid[1])/1.5,(side_skid[1]+.5*side_skid[0])/1.5]
 var loudest := 0.
 for c in 2:loudest = maxf(loudest,maxf(maxf(target_scrub[c],target_squeal[c]),target_skid[c]))
 if loudest <= 0. and maxf(maxf(scrub_level[0],scrub_level[1]),maxf(maxf(squeal_level[0],squeal_level[1]),maxf(skid_level[0],skid_level[1]))) < 1e-4:
  return PackedVector2Array()
 if hz_weight > 0.:squeal_hz += (clampf(hz_sum/hz_weight,600.,1600.)-squeal_hz)*.3
 if slide_weight > 0.:scrub_hz += (480.+12.*clampf(slide_sum/slide_weight,0.,40.)-scrub_hz)*.3
 var fs := 2.*sin(PI*scrub_hz/RATE);var qs := 1./1.4
 var fq := 2.*sin(PI*squeal_hz/RATE);var qq := 1./8.
 # A noise-excited band-pass passes energy ∝ its bandwidth (∝ frequency at fixed Q).
 var squeal_norm := sqrt(1000./squeal_hz)
 var hp := exp(-TAU*1000./RATE)
 var smoothing := 1.-exp(-1./(RATE*.015))
 var gain_squeal := squeal_norm*SQUEAL_GAIN
 var channels := [PackedFloat32Array(),PackedFloat32Array()]
 for c in 2:
  # Locals in the per-sample loop: GDScript member and Array access is slow.
  var buffer := PackedFloat32Array();buffer.resize(frames)
  var scrub: float = scrub_level[c];var squeal: float = squeal_level[c];var skid: float = skid_level[c]
  var scrub_to: float = target_scrub[c];var squeal_to: float = target_squeal[c];var skid_to: float = target_skid[c]
  var s_low: float = scrub_low[c];var s_band: float = scrub_band[c]
  var q_low: float = squeal_low[c];var q_band: float = squeal_band[c];var k_low: float = skid_low[c]
  var state := tyre_random
  for i in frames:
   scrub += (scrub_to-scrub)*smoothing
   squeal += (squeal_to-squeal)*smoothing
   skid += (skid_to-skid)*smoothing
   state = (state*1664525+1013904223)&0x7fffffff
   var n := float(state)/1073741824.-1.
   s_low += fs*s_band;s_band += fs*(qs*n-s_low-qs*s_band)
   q_low += fq*q_band;q_band += fq*(qq*n-q_low-qq*q_band)
   k_low = n+(k_low-n)*hp
   buffer[i] = s_band*scrub*SCRUB_GAIN+q_band*squeal*gain_squeal+(n-k_low)*skid*SKID_GAIN
  tyre_random = state
  scrub_level[c] = scrub;squeal_level[c] = squeal;skid_level[c] = skid
  scrub_low[c] = s_low;scrub_band[c] = s_band;squeal_low[c] = q_low;squeal_band[c] = q_band;skid_low[c] = k_low
  channels[c] = buffer
 var left: PackedFloat32Array = channels[0];var right: PackedFloat32Array = channels[1]
 var out := PackedVector2Array();out.resize(frames)
 for i in frames:out[i] = Vector2(left[i],right[i])
 return out

## Wind over the body and soft top: level ∝ speed², cutoff rising with speed,
## two decorrelated channels with slow buffeting.
func render_wind(frames: int, speed: float) -> PackedVector2Array:
 var samples := PackedVector2Array();samples.resize(frames)
 if frames < 1:return samples
 speed = absf(speed)
 var level_target := clampf(speed*speed/1600.,0.,1.4)
 wind_cutoff += ((160.+7.*speed)-wind_cutoff)*.3
 var a := 1.-exp(-TAU*wind_cutoff/RATE)
 var slow := TAU*1.2/RATE
 var smoothing := 1.-exp(-1./(RATE*.04))
 for i in frames:
  wind += (level_target-wind)*smoothing
  random_state = (random_state*1664525+1013904223)&0x7fffffff
  var wl := float(random_state)/1073741824.-1.
  random_state = (random_state*1664525+1013904223)&0x7fffffff
  var wr := float(random_state)/1073741824.-1.
  gust_l += slow*(wl-gust_l);gust_r += slow*(wr-gust_r)
  wind_l += a*(wl-wind_l);wind_r += a*(wr-wind_r)
  var l := wind_l*wind*WIND_GAIN*(1.+clampf(gust_l*40.,-.35,.35))
  var r := wind_r*wind*WIND_GAIN*(1.+clampf(gust_r*40.,-.35,.35))
  samples[i] = Vector2(.85*tanh(l/.85),.85*tanh(r/.85))
 return samples
