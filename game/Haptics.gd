extends RefCounted
## Grip you can feel: Xbox pad rumble from each tyre's slip relative to its
## peak, an ABS pulse, kerbs and broken road, and a faint engine buzz. Read-only:
## it reads Powertrain state after the force substeps and adds no force, so the
## physics and every calibration band are untouched.
##
## Normalised slip per tyre (1 = the end of the tyre's full-grip plateau):
##   s = sqrt((kappa/kappa*)² + (alpha/alpha*)²)
##   kappa*  Powertrain.peak_slip[i], where the slip-ratio curve stops rising
##   alpha*  plateau_angle(): the slip angle where Powertrain's lateral law
##           tanh(alpha·k)·(1 − falloff·smoothstep(from, to, alpha)) first
##           reaches PLATEAU of its maximum (10.6° on the 1999 roadster, whose
##           force peaks near 14° and stays within 1 % of that from 11.5° to
##           17.7°). checks/HapticsCheck.gd measures the front tyres' actual
##           lateral force against alpha/alpha*, so a new tyre law that moves
##           the peak fails there rather than silently shifting every threshold.
## Why slip and not the friction-circle use (budget/fxmax in Powertrain): the
## lateral law saturates at its ceiling and falls past ~14°, so a front tyre
## sliding at 25° uses less of its circle than one at the peak; a use-based cue
## would fade as understeer grows. Slip keeps rising into a slide, as tyre
## scrub and squeal do.
## Motors (Godot: weak = the right, high-frequency motor; strong = the left,
## low-frequency one):
##   weak    front tyres (understeer scrub, a locking front), broken road and
##           gravel or verge texture, and the engine under load (subtle)
##   strong  rear tyres and the driven wheels' longitudinal slip (oversteer,
##           wheelspin), an 8 Hz ABS pulse, kerbs and potholes (RoadNoise's
##           roughness, the same signal as the audio rumble)
## Coasting straight on a smooth road every term is zero. motors() is a pure
## function so checks/HapticsCheck.gd can test it headless; send() is the only
## part that touches Input.
##
## Impulse triggers (the motors in LT and RT) need a native add-on: Godot 4.7
## exposes only the two body motors (Input.start_joy_vibration). See
## docs/controls.md, "Rumble", for the path.
const PLATEAU := .98
# Rumble ramps: from the onset (92 % of the roadster's lateral grip in use) to
# just past the plateau (s 1.15, 99 %), then on into a slide. slip_feel is 0.44
# at s = 1.
const S_ONSET := .75
const S_LIMIT := 1.15
const S_SLIDE := 2.5
const LIMIT_SHARE := .65
const ABS_HZ := 8.
const ABS_ON := .6
const ABS_SLIP_SHARE := .3
const ABS_RELEASED := .95       # Powertrain.abs_release below this: the modulator is acting
# Road: on a windowed Kansas US-highway drive RoadNoise.roughness ran p50 0.01,
# p90 0.44, p99 0.76 (joints and patches). Joints give a light texture on the
# weak motor; only the larger hits (above about p90: the heavier joints,
# kerbs, potholes) reach the strong one. In the windowed --road-test drive
# (Kansas, 226 trace samples above 20 m/s) the road alone, with every tyre
# below the onset, put the strong motor above 0.05 in 11 % of them and the weak
# above 0.1 in 19 %: a jointed highway is felt, not silent. To be confirmed on
# a real pad.
const KERB_GAIN := .8
const KERB_FROM := .45
const TEXTURE_GAIN := .2
const TEXTURE_FROM := .15
const TEXTURE_TO := .7
const LOOSE_GAIN := .25
const ENGINE_GAIN := .06
# Input traffic: resend when a motor moves this much, refresh a running motor
# this often, and let every command lapse on its own if the game stalls.
const SEND_STEP := .03
const REFRESH_S := .15
const COMMAND_S := .3

var lateral_peak := .18
var slip := [0.,0.,0.,0.]       # normalised slip per tyre, last physics tick
var load_ratio := [0.,0.,0.,0.] # tyre load over the car's reference corner load
var slide := [0.,0.,0.,0.]      # contact-patch sliding speed (m/s)
var levels := Vector2.ZERO      # weak, strong before the player's strength
var abs_active := false
var pulse_time := 0.
var sent := Vector2.ZERO
var since_sent := 0.
var pads_running := []

func _init(spec = null) -> void:
 if spec != null:lateral_peak = plateau_angle(spec)

## Slip angle (rad) where the lateral law first reaches PLATEAU of its maximum.
static func plateau_angle(spec) -> float:
 var best := 0.
 var force := []
 for n in 1001:
  var alpha := n*.001
  var f: float = tanh(alpha*spec.slip_stiffness)*(1.-spec.slip_falloff*smoothstep(spec.slip_falloff_from,spec.slip_falloff_to,alpha))
  force.append(f);best = maxf(best,f)
 for n in force.size():
  if float(force[n]) >= PLATEAU*best:return maxf(n*.001,.001)
 return 1.

static func normalized_slip(kappa: float, peak_kappa: float, alpha: float, peak_alpha: float) -> float:
 return Vector2(kappa/maxf(peak_kappa,.001),alpha/maxf(peak_alpha,.001)).length()

## Rumble strength for a normalised slip: 0 below S_ONSET, LIMIT_SHARE at the
## plateau, 1 deep in a slide. Never decreases as slip grows.
static func slip_feel(s: float) -> float:
 return LIMIT_SHARE*smoothstep(S_ONSET,S_LIMIT,s)+(1.-LIMIT_SHARE)*smoothstep(S_LIMIT,S_SLIDE,s)

## The two motor levels (0..1, before the player's strength).
##   front_s   largest normalised slip of the front tyres
##   rear_s    largest of the rear tyres' slip and the driven wheels' longitudinal slip
##   pulse     ABS pulse level 0..1 (abs_pulse())
##   roughness RoadNoise.roughness 0..1
##   loose     0 paved … 1 gravel, verge or field (WorldTile.wheel_grip classes)
##   speed     m/s
##   engine    throttle × combustion load 0..1 (0 for an electric car)
##   abs_on    ABS is acting: the rear tyres, which it holds at their peak,
##             drop to ABS_SLIP_SHARE so the pulse stands out from them
static func motors(front_s: float, rear_s: float, pulse: float, roughness: float, loose: float, speed: float, engine: float, abs_on := false) -> Vector2:
 var weak := slip_feel(front_s)
 weak += TEXTURE_GAIN*smoothstep(TEXTURE_FROM,TEXTURE_TO,roughness)
 weak += LOOSE_GAIN*clampf(loose,0.,1.)*smoothstep(.5,6.,absf(speed))
 weak += ENGINE_GAIN*clampf(engine,0.,1.)
 var strong := slip_feel(rear_s)*(ABS_SLIP_SHARE if abs_on else 1.)+pulse
 strong += KERB_GAIN*smoothstep(KERB_FROM,1.,roughness)
 return Vector2(clampf(weak,0.,1.),clampf(strong,0.,1.))

## An 8 Hz square pulse while ABS acts: the pump and valves cycling, felt
## through the pedal in a real car. Synthesised at a fixed rate rather than
## relayed from the modulator, which the motors' spin-up would smear.
static func abs_pulse(active: bool, t: float) -> float:
 return ABS_ON if active and fmod(t*ABS_HZ,1.) < .5 else 0.

## Read the tyres after Powertrain.drive() and compute this tick's levels.
func sample(car, roughness: float, dt: float) -> Vector2:
 var front_s := 0.;var rear_s := 0.
 abs_active = false
 for i in 4:
  var row: Array = car.contact_rows[i] if i < car.contact_rows.size() else []
  if row.is_empty() or float(car.spring_load[i]) <= 0.:
   slip[i] = 0.;load_ratio[i] = 0.;slide[i] = 0.
   continue
  var kappa: float = car.slip_ratio[i]
  slip[i] = normalized_slip(kappa,car.peak_slip[i],car.rolling_slip[i],lateral_peak)
  load_ratio[i] = float(car.spring_load[i])/maxf(car.spec.reference_corner_load_n,1.)
  slide[i] = Vector2(float(car.omega[i])*car.R-float(row[5]),float(row[6])).length()
  if i < 2:front_s = maxf(front_s,slip[i])
  else:rear_s = maxf(rear_s,slip[i])
  if car.spec.driven(i):rear_s = maxf(rear_s,absf(kappa)/maxf(car.peak_slip[i],.001))
  if car.service_brake and float(car.abs_release[i]) < ABS_RELEASED:abs_active = true
 pulse_time = pulse_time+dt if abs_active else 0.
 var loose := 0.
 for g in car.surface_grip:loose += 1.-smoothstep(.70,.92,float(g))
 loose *= .25
 var speed: float = car.linear_velocity.length()
 var engine := 0.
 if not car.electric():engine = clampf(car.sound_throttle,0.,1.)*clampf(car.sound_combustion,0.,1.)
 levels = motors(front_s,rear_s,abs_pulse(abs_active,pulse_time),roughness,loose,speed,engine,abs_active)
 return levels

## Zero every level (paused, loading, closing): the audio layer reads slip too.
func quiet() -> void:
 for i in 4:slip[i] = 0.;slide[i] = 0.
 levels = Vector2.ZERO
 abs_active = false;pulse_time = 0.
 stop()

## Drive the pads' body motors at levels × strength (0..1).
func send(strength: float, dt: float) -> void:
 var want := levels*clampf(strength,0.,1.)
 since_sent += dt
 if want.x <= .001 and want.y <= .001:
  stop();return
 var pads := []
 for device in Input.get_connected_joypads():
  if Input.has_joy_vibration(device):pads.append(device)
 if pads.is_empty():return
 var changed := absf(want.x-sent.x) > SEND_STEP or absf(want.y-sent.y) > SEND_STEP or pads != pads_running
 if not changed and since_sent < REFRESH_S:return
 for device in pads:Input.start_joy_vibration(device,want.x,want.y,COMMAND_S)
 sent = want;since_sent = 0.;pads_running = pads

func stop() -> void:
 if pads_running.is_empty():return
 for device in pads_running:Input.stop_joy_vibration(device)
 pads_running = [];sent = Vector2.ZERO;since_sent = 0.
