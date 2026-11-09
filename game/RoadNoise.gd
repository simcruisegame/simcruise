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
##   road    tanh limited below 0.90 by construction; bus −8 dB    → 0.358
##   wind    tanh limited below 0.85 by construction; bus −10 dB   → 0.269
##   worst-case sum 0.86 < 1.0, so Master cannot clip even if all three layers
##   peak on the same sample. Rolling at 30 m/s on asphalt the mix sits near
##   −20 dBFS RMS. checks/RoadNoiseCheck.gd measures each layer headless.
const RATE := 22050.
const ENGINE_TRIM_DB := -6.
const BUS_DB := {"Cabin":0.,"Engine":-6.,"Road":-8.,"Wind":-10.}
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
   road_player.play();road_playback = road_player.get_stream_playback()
   wind_player.play();wind_playback = wind_player.get_stream_playback()
  else:
   road_player.stop();wind_player.stop();road_playback = null;wind_playback = null
 var inside: bool = game.view == 1
 if not cabin_known or inside != cabin_state:
  cabin_known = true;cabin_state = inside;set_cabin(inside)
 if not running:return
 var car = game.vehicle
 var speed: float = absf(game.velocity)
 update_roughness(car,maxf(dt,1./240.))
 var grip := 0.
 for g in car.surface_grip:grip += g
 grip *= .25
 var contact := float(car.wheel_contact)/4.
 if road_playback:
  var frames: int = road_playback.get_frames_available()
  if frames > 0:road_playback.push_buffer(render_road(frames,speed,grip,roughness,contact))
 if wind_playback:
  var frames: int = wind_playback.get_frames_available()
  if frames > 0:wind_playback.push_buffer(render_wind(frames,speed))

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
##   bump   0..1 roughness, contact the fraction of wheels touching the ground.
func render_road(frames: int, speed: float, grip: float, bump: float, contact := 1.) -> PackedVector2Array:
 var samples := PackedVector2Array();samples.resize(frames)
 if frames < 1:return samples
 speed = absf(speed)
 var paved := smoothstep(GRAVEL_GRIP,PAVED_GRIP,grip)
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
  samples[i] = Vector2(.9*tanh(l/.9),.9*tanh(r/.9))
 return samples

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
