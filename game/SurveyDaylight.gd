extends RefCounted
# KartaView 3690649/2059 shows 2021-06-13 17:35:39 in Amelia, Virginia.
# Its camera timezone is unknown. The preset assumes Eastern Daylight Time.
# The other locations use the same LOCAL MEAN SOLAR afternoon, approximately
# 16:24. Thus Alaska and Hawaii stay in daylight without civil-timezone guesses.
# Ordinary play now follows the real clock (sun_now); the preset remains the
# Chula photograph's own sun, and the whole scene under --legacy-sky.
const CHULA_LONGITUDE := -77.907503
const REFERENCE_UTC_HOUR := 21.0 + 35.0/60.0 + 39.0/3600.0
const LOCAL_MEAN_HOUR := REFERENCE_UTC_HOUR + CHULA_LONGITUDE/15.0

const CAPTURE_CENTER := Vector2(37.369103, CHULA_LONGITUDE)

# Lighting model shared by the start-up environment and every weather update.
# Units are Godot's: a white Lambertian surface lit head-on by a
# DirectionalLight3D of energy 1, or under a uniform sky of radiance 1 with
# ambient energy 1, renders as linear 1.0. checks/LightingCheck.gd measures
# SKY_AMBIENT from art/day.hdr and holds the clear-sky shadow/sun ratio to the
# 0.18-0.32 photographs show.
# On the Mobile renderer the sky radiance map and the reflection-probe atlas are
# 10-bit UNORM written at half scale, so the lighting never sees sky radiance
# above MOBILE_RADIANCE_CLIP: the photographed solar disc was already clipped
# there and never acted as a second sun. SUN_RADIANCE_CAP only matters on an
# HDR path (Forward+ or use_hdr_2d) and is kept as insurance for that case.
const MOBILE_RADIANCE_CLIP := 2.0    # what the Mobile sky/probe path can represent
const SUN_RADIANCE_CAP := 8.0        # SurveySky.gdshader luminance ceiling, every pass
const SKY_AMBIENT := 0.43            # horizontal white surface under day.hdr clipped at 2.0
const CLOUD_SKY_AMBIENT := 0.38      # the same estimate for the procedural overcast sky
const SUN_ENERGY := 1.84             # clear direct energy; on the ground it is SUN_ENERGY*sin(elevation)
const CLEAR_CLOUD := 0.12            # DaylightWeather's default cover counts as clear
const MIN_SUN_ELEVATION_DEG := 7.0   # held until a night mode exists
const FILMIC_SUNLIT_WHITE := 0.865   # exposure*sunlit white: 0.80 linear on screen, grey at sRGB 131
const HAZE_COLOR := Color(0.826, 0.895, 0.985) # SurveySky's mean horizon air, sRGB encoded
const CLOCK_STEP := 30               # seconds between solar updates; each one refilters the sky radiance
static var registered_capture_fade := false
var last_capture_fade := -1.0
var weather=preload("res://DaylightWeather.gd").new()
var previous_sky_params:=Vector4(-1.,-1.,-1.,-1.)
var previous_broken := -1.0
var legacy_sky := false      # --legacy-sky: uncapped panorama sun, old energies, fixed June afternoon
var legacy_shadows := false  # --legacy-shadows: 85 m shadows without split blending
var agx := false             # --tonemap-agx: AgX instead of Filmic for side-by-side captures
var clock_override := 0      # --solar-epoch <unix>: pin the clock for repeatable captures

func _init() -> void:
 var args := OS.get_cmdline_user_args()
 legacy_sky = "--legacy-sky" in args
 legacy_shadows = "--legacy-shadows" in args
 agx = "--tonemap-agx" in args
 var at := args.find("--solar-epoch")
 if at >= 0 and at+1 < args.size():
  clock_override = maxi(0, str(args[at+1]).to_int())

func sky_material(panorama: Texture2D) -> ShaderMaterial:
 # A global uniform drives only the visible background. Updating this buffer
 # never dirties the sky radiance cubemap as a per-material uniform would.
 if not registered_capture_fade:
  RenderingServer.global_shader_parameter_add("chula_survey_sky_fade",RenderingServer.GLOBAL_VAR_TYPE_FLOAT,0.)
  RenderingServer.global_shader_parameter_add("road_sky_high",RenderingServer.GLOBAL_VAR_TYPE_VEC4,Vector4.ZERO)
  RenderingServer.global_shader_parameter_add("road_sky_low",RenderingServer.GLOBAL_VAR_TYPE_VEC4,Vector4.ZERO)
  RenderingServer.global_shader_parameter_add("observed_day_sky",RenderingServer.GLOBAL_VAR_TYPE_VEC4,Vector4.ZERO)
  registered_capture_fade = true
 var material := ShaderMaterial.new()
 material.shader = load("res://SurveySky.gdshader")
 material.set_shader_parameter("panorama",panorama)
 material.set_shader_parameter("sun_radiance_cap",1e9 if legacy_sky else SUN_RADIANCE_CAP)
 material.set_shader_parameter("cubemap_horizon",0. if legacy_sky else 1.)
 for entry in [["clear_field","day-clear.png"],["scattering","day-scattering.png"],["cloud_rank","day-cloud-rank.png"]]:
  var field:=Image.new()
  if field.load_png_from_buffer(FileAccess.get_file_as_bytes("res://data/"+entry[1]))==OK:
   material.set_shader_parameter(entry[0],ImageTexture.create_from_image(field))
 var image := Image.new()
 if image.load_png_from_buffer(FileAccess.get_file_as_bytes("res://data/capture-chula-sky.png")) == OK:
  image.generate_mipmaps()
  material.set_shader_parameter("survey_sky",ImageTexture.create_from_image(image))
 return material

func update_appearance(material:ShaderMaterial) -> void:
 var weight:float=weather.sky_weight
 var controls:=Vector4(weight if weather.sky_kind==weather.CLEAR else 0.,weight if weather.sky_kind==weather.CIRRUS else 0.,weather.veil,weather.extent)
 if not controls.is_equal_approx(previous_sky_params):
  previous_sky_params=controls
  RenderingServer.global_shader_parameter_set("observed_day_sky",controls)
 # The broken-sky transition also changes the ambient cubemap. Bound the
 # updates while crossing the 205 m feather. Live storms keep their old Sky.
 var broken:=snappedf(weight,0.025) if weather.sky_kind==weather.BROKEN else 0.
 if not is_equal_approx(broken,previous_broken):
  previous_broken=broken
  material.set_shader_parameter("archived_broken",broken)

func capture_weight(location: Vector2, use_reference: bool) -> float:
 # How much of the photographed Chula sky (capture-chula-sky.png) is shown:
 # full within 155 m of the junction, gone at 350 m, never with live weather.
 if not use_reference or absf(location.x-CAPTURE_CENTER.x) >= .004 or absf(location.y-CAPTURE_CENTER.y) >= .005:
  return 0.0
 var metres := (location-CAPTURE_CENTER)*Vector2(111000.0,88443.0)
 return 1.0-smoothstep(155.0,350.0,metres.length())

func update_capture_blend(location: Vector2, use_reference: bool) -> void:
 var fade := capture_weight(location, use_reference)
 if fade != last_capture_fade:
  last_capture_fade = fade
  RenderingServer.global_shader_parameter_set("chula_survey_sky_fade",fade)

var previous_location := Vector2.INF
var previous_region := "uninitialized"

static func sun_direction(location: Vector2, region: String) -> Vector3:
 var latitude := deg_to_rad(location.x)
 var longitude := location.y
 var utc_hour := REFERENCE_UTC_HOUR if region == "Chula" else LOCAL_MEAN_HOUR - longitude/15.0
 # June 13 is day 164 in 2021. February 15 represents the undated leaf-off
 # Raven dataset. Southern hemisphere destinations use local summer (Dec 13).
 var day := 46 if region == "Raven" else (347 if latitude < 0.0 else 164)
 # NOAA Global Monitoring Laboratory, General Solar Position Calculations.
 # https://gml.noaa.gov/grad/solcalc/solareqns.PDF
 return solar_for_day(latitude,longitude,utc_hour,day,365.)

static func sun_at_epoch(location:Vector2,stamp:int) -> Vector3:
 var utc=Time.get_datetime_dict_from_unix_time(stamp)
 var jan=int(Time.get_unix_time_from_datetime_dict({"year":utc.year,"month":1,"day":1}))
 var next_jan=int(Time.get_unix_time_from_datetime_dict({"year":utc.year+1,"month":1,"day":1}))
 var day=floori(float(stamp-jan)/86400.)+1
 return solar_for_day(deg_to_rad(location.x),location.y,utc.hour+utc.minute/60.+utc.second/3600.,day,(next_jan-jan)/86400.)

static func sun_now(location: Vector2, unix: int) -> Vector3:
 # The real sun at this Unix time (UTC). There is no night mode yet, so while the
 # sun is below MIN_SUN_ELEVATION_DEG the light holds the moment it last stood at
 # that height (the sunset bearing through the evening) or will next stand there
 # (the sunrise bearing after solar midnight). Holding the current bearing would
 # light the scene from the north at midnight. Polar nights, where the sun never
 # reaches the floor within a day, keep the current bearing at the floor.
 var direction := sun_at_epoch(location, unix)
 var floor_y := sin(deg_to_rad(MIN_SUN_ELEVATION_DEG))
 if direction.y >= floor_y:
  return direction
 var rising := sun_at_epoch(location, unix + 600).y > direction.y
 var step := 600 if rising else -600
 var below := unix
 for i in 144: # up to 24 hours in 10-minute steps
  var probe := below + step
  if sun_at_epoch(location, probe).y >= floor_y:
   var above := probe
   for j in 10: # about 0.6 s resolution
    var mid := floori((below + above) * 0.5)
    if sun_at_epoch(location, mid).y >= floor_y:
     above = mid
    else:
     below = mid
   return hold_at_floor(sun_at_epoch(location, above), floor_y)
  below = probe
 return hold_at_floor(direction, floor_y)

static func hold_at_floor(direction: Vector3, floor_y: float) -> Vector3:
 var bearing := Vector2(direction.x, direction.z)
 bearing = bearing.normalized() if bearing.length_squared() > 1e-9 else Vector2(0.0, 1.0)
 bearing *= sqrt(1.0-floor_y*floor_y)
 return Vector3(bearing.x, floor_y, bearing.y)

func clock_now() -> int:
 # Quantised so the sun, and with it the sky radiance cubemap, moves once per
 # CLOCK_STEP rather than every frame. Time.get_unix_time_from_system is UTC.
 if clock_override > 0:
  return clock_override
 return int(Time.get_unix_time_from_system()/CLOCK_STEP)*CLOCK_STEP

static func solar_for_day(latitude:float,longitude:float,utc_hour:float,day:int,days:float) -> Vector3:
 var gamma := TAU/days * (day - 1.0 + (utc_hour - 12.0)/24.0)
 var c1 := cos(gamma)
 var s1 := sin(gamma)
 var c2 := cos(2.0*gamma)
 var s2 := sin(2.0*gamma)
 var minutes := 229.18 * (.000075 + .001868*c1 - .032077*s1 - .014615*c2 - .040849*s2)
 var declination := .006918 - .399912*c1 + .070257*s1 - .006758*c2 + .000907*s2 - .002697*cos(3.0*gamma) + .00148*sin(3.0*gamma)
 var hour_angle := deg_to_rad((utc_hour*60.0 + minutes + 4.0*longitude)/4.0 - 180.0)
 var cd := cos(declination)
 var sd := sin(declination)
 var east := -cd * sin(hour_angle)
 var north := cos(latitude)*sd - sin(latitude)*cd*cos(hour_angle)
 var up := sin(latitude)*sd + cos(latitude)*cd*cos(hour_angle)
 # In the Web Mercator scene east is +X, north is -Z and up is +Y.
 return Vector3(east, up, -north).normalized()

var applied_epoch:=0
var applied_weight:=0.0
var previous_epoch=-1
var previous_weight=-1.
var previous_clock := -1
var previous_reference := -1.0
func update(location: Vector2, region: String, light: DirectionalLight3D, sky_material: ShaderMaterial, epoch:int=0,weight:float=0.,reference_sky:float=0.) -> void:
 var clock := clock_now()
 if epoch==previous_epoch and absf(weight-previous_weight)<.025 and region == previous_region and location.distance_squared_to(previous_location) < .0001 and clock==previous_clock and absf(reference_sky-previous_reference)<.025:
  return
 previous_epoch=epoch;previous_weight=weight
 previous_location = location
 previous_region = region
 previous_clock = clock
 previous_reference = reference_sky
 var direction := sun_direction(location, region) if legacy_sky else sun_now(location, clock)
 if not legacy_sky and reference_sky > 0.:
  # capture-chula-sky.png is direction-mapped at its own shutter time, so the
  # sun follows the photograph exactly as far as that sky is on screen.
  direction = direction.slerp(sun_direction(location, "Chula"), reference_sky).normalized()
 applied_epoch=0
 applied_weight=0.
 if epoch>0 and weight>0.:
  var measured=sun_at_epoch(location,epoch)
  if measured.y>sin(deg_to_rad(MIN_SUN_ELEVATION_DEG)):
   direction=direction.slerp(measured,weight).normalized()
   applied_epoch=epoch
   applied_weight=weight
 # DirectionalLight3D emits along local -Z; local +Z must point at the sun.
 light.basis = Basis.looking_at(-direction, Vector3.UP)
 var azimuth := atan2(direction.x, -direction.z)
 sky_material.set_shader_parameter("solar_bearing_turns", azimuth/TAU)
 sky_material.set_shader_parameter("solar_v", .5 - asin(direction.y)/PI)

static func luminance(color: Color) -> float:
 # Light colours are sRGB encoded; the renderer lights the scene with their linear values.
 var c := color.srgb_to_linear()
 return .2126*c.r + .7152*c.g + .0722*c.b

func configure_environment(env: Environment) -> void:
 # Everything about the Environment that does not change with the weather.
 env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
 env.fog_enabled = true
 if legacy_sky:
  env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
  env.tonemap_exposure = .82
  env.ambient_light_energy = .58
  env.fog_density = .0008
  env.fog_light_color = Color("#bcc4bc")
  env.fog_sky_affect = .09
  return
 env.tonemap_mode = Environment.TONE_MAPPER_AGX if agx else Environment.TONE_MAPPER_FILMIC
 # One haze: fog takes its colour from the sky radiance toward each fragment
 # (Environment.fog_aerial_perspective), so distance fades into the horizon
 # the eye sees; the sky already is that colour and is left alone. A little
 # of the sun scatters forward through the haze.
 env.fog_aerial_perspective = 1.0
 env.fog_sky_affect = 0.0
 env.fog_sun_scatter = .08
 env.fog_light_color = HAZE_COLOR # also the far-forest haze read by Grove.gd and SurveyForest.gd

func configure_sun(sun: DirectionalLight3D) -> void:
 sun.shadow_enabled = true
 sun.shadow_bias = .045
 if legacy_shadows:
  sun.directional_shadow_max_distance = 85.
  return
 # Four PSSM splits (0.1/0.2/0.5 of the range) now reach 180 m: roughly
 # 18/36/90/180 m of depth from the 2048 px map, a few centimetres per texel
 # at the car and ~18 cm at the far trees. Blended splits hide the resolution
 # steps and the blur widens the Soft Low PCF kernel the project already uses
 # (directional PCSS, light_angular_distance, is Forward+ only in Godot 4.7).
 sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
 sun.directional_shadow_max_distance = 180.
 sun.directional_shadow_blend_splits = true
 sun.directional_shadow_fade_start = .85
 sun.shadow_blur = 1.5

func apply_conditions(env: Environment, sun: DirectionalLight3D, cloud: float, visibility_m: float, overcast_sky: bool=false) -> void:
 # The weather-dependent half of the model. cloud is DaylightWeather's cover
 # (0.12 by default, 0.90 for leaf-off winter, a METAR fraction when live);
 # visibility_m is the meteorological visibility; overcast_sky says whether
 # Drive.gd is showing the procedural cloud sky instead of day.hdr.
 sun.light_color = Color("#f9eed8").lerp(Color("#ebf2fa"), cloud)
 if legacy_sky:
  env.ambient_light_energy = lerpf(.52,.87,cloud)
  env.tonemap_exposure = .84
  env.fog_density = clampf(2.4/maxf(visibility_m,80.),.00022,.035)
  sun.shadow_opacity = lerpf(.94,.24,cloud)
  sun.light_energy = lerpf(1.46,.44,cloud)
  return
 var overcast := clampf((cloud-CLEAR_CLOUD)/(1.0-CLEAR_CLOUD), 0.0, 1.0)
 env.ambient_light_energy = lerpf(.58, .95, overcast)
 sun.light_energy = SUN_ENERGY*lerpf(1.0, .22, overcast)
 # Shadows are filled by the sky, not by leaked sun; the 4 % hides shadow-map steps.
 sun.shadow_opacity = lerpf(.96, .30, overcast)
 # Koschmieder: 2 % of contrast remains at the meteorological visibility.
 env.fog_density = clampf(3.912/maxf(visibility_m, 80.), .00015, .035)
 # The camera exposes for the sunlit ground, within bounds, like a
 # photographer would. On the Mobile renderer linear colour lives in a 0-2
 # range, so Filmic's white point is that range after exposure. AgX on Mobile
 # always uses a white of 2.0 (Environment.tonemap_agx_white docs), so it is
 # given exposure 1.0 to meet it.
 var sky_ambient := CLOUD_SKY_AMBIENT if overcast_sky else SKY_AMBIENT
 var sunlit := sky_ambient*env.ambient_light_energy + sun.light_energy*maxf(sun.basis.z.y, 0.0)*luminance(sun.light_color)
 if agx:
  env.tonemap_exposure = 1.0
 else:
  env.tonemap_exposure = clampf(FILMIC_SUNLIT_WHITE/maxf(sunlit, .05), .45, 1.3)
 env.tonemap_white = 2.0*env.tonemap_exposure
