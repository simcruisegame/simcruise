extends SceneTree
# godot --headless --path game --script res://checks/LightingCheck.gd
# Headless measurements for the light, sky and shadow pass (SurveySky.gdshader,
# SurveyDaylight.gd, the Drive.gd environment block):
#  1. art/day.hdr: horizontal sky irradiance as the Mobile renderer sees it.
#     Its radiance map and reflection probe are 10-bit UNORM at half scale, so
#     sky radiance clips at 2.0 (SurveyDaylight.MOBILE_RADIANCE_CLIP) and the
#     photographed solar disc never reaches the lighting on this renderer.
#  2. The shadow/sun luminance ratio of the clear-sky constants under the
#     reference sun (SurveyDaylight.reference_sun, photographs: 0.18-0.32),
#     the legacy constants for contrast, the Mobile 0-2 colour range at a
#     high sun, and overcast.
#  3. The real-clock solar position at a fixed public observer: agreement
#     with the dated solar formula, the 7 degree floor at night on the sunset
#     (evening) or sunrise (after solar midnight) bearing, June transit
#     altitude, and the reference sun under a photographed sky.
#  4. The Godot 4.7 properties and project settings the environment code sets.
# Nothing here renders. To see the result, photograph the bundled offline
# start street (no network needed) with these user arguments after "--",
# where LAT LON HEADING FOV frame a sunlit stretch of it:
#   --reference LAT LON HEADING FOV /tmp/light-after.jpg              (new lighting)
#   --reference LAT LON HEADING FOV /tmp/light-before.jpg --legacy-sky --legacy-shadows
#   --reference LAT LON HEADING FOV /tmp/light-agx.jpg --tonemap-agx  (AgX side by side)
#   add --solar-epoch 1624309200 to every run so all three share one sun,
#   and --profile-trees (render-cost.json, F3 for live frame times) to cost
#   the 180 m blended shadows against --legacy-shadows and the 16x
#   anisotropy plus debanding against --legacy-filters.
const DAYLIGHT=preload("res://SurveyDaylight.gd")
const OBSERVER=Vector2(38.8899,-77.0091) # US Capitol, Washington DC (latitude, longitude)
const AFTERNOON_UNIX=1624309200          # 2021-06-21T21:00:00Z, the June solstice afternoon
var failures=[]

func expect(condition:bool,label:String)->void:
 if not condition:failures.append(label)

static func shadow_ratio(sky_ambient:float,env:Environment,sun:DirectionalLight3D)->float:
 # Luminance of a horizontal white surface in shadow over the same surface in
 # the sun: sky ambient plus the leak through shadow_opacity, over everything.
 var ambient=sky_ambient*env.ambient_light_energy
 var direct=sun.light_energy*maxf(sun.basis.z.y,0.)*DAYLIGHT.luminance(sun.light_color)
 return (ambient+(1.-sun.shadow_opacity)*direct)/(ambient+direct)

static func filmic(x:float,white:float)->float:
 # Godot's tonemap_filmic (Hable curve, exposure bias 2 folded into A and B).
 var a=0.22*4.;var b=0.30*2.;var c=0.10;var d=0.20;var e=0.01;var f=0.30
 var cx=((x*(a*x+c*b)+d*e)/(x*(a*x+b)+d*f))-e/f
 var cw=((white*(a*white+c*b)+d*e)/(white*(a*white+b)+d*f))-e/f
 return cx/cw

static func srgb8(linear:float)->int:
 return roundi(255.*Color(linear,linear,linear).linear_to_srgb().r)

static func has_property(klass:String,name:String)->bool:
 for info in ClassDB.class_get_property_list(klass):
  if info.name==name:return true
 return false

func _initialize():
 var daylight=DAYLIGHT.new()
 # 1. Horizontal irradiance of the panorama's upper hemisphere.
 var image=Image.load_from_file("res://art/day.hdr")
 expect(image!=null and not image.is_empty(),"day.hdr loads")
 var w=image.get_width();var h=image.get_height()
 var sun_u=614.5/1024.;var sun_v=119.5/512.
 var sun_el=PI/2-PI*sun_v
 var toward=Vector3(cos(sun_el)*sin(TAU*sun_u),sin(sun_el),-cos(sun_el)*cos(TAU*sun_u))
 var dw0=(TAU/w)*(PI/h)
 var total=0.;var disc=0.;var capped=0.;var disc_clipped=0.;var brightest_cloud=0.
 for y in h/2:
  var v=(y+.5)/h;var el=PI/2-PI*v
  var weight=dw0*cos(el)*sin(el) # solid angle times the cosine for an upward surface
  var el_cos=cos(el);var el_sin=sin(el)
  for x in w:
   var c=image.get_pixel(x,y)
   var lum=.2126*c.r+.7152*c.g+.0722*c.b
   if lum<=0.:continue
   var az=TAU*(x+.5)/w
   var toward_dot=Vector3(el_cos*sin(az),el_sin,-el_cos*cos(az)).dot(toward)
   total+=lum*weight
   if toward_dot>cos(deg_to_rad(2.)):disc+=lum*weight
   elif toward_dot<cos(deg_to_rad(4.)) and lum>brightest_cloud:brightest_cloud=lum # beyond the glare halo
   capped+=minf(lum,DAYLIGHT.MOBILE_RADIANCE_CLIP)*weight
   if toward_dot>cos(deg_to_rad(2.)):disc_clipped+=minf(lum,DAYLIGHT.MOBILE_RADIANCE_CLIP)*weight
 var ambient_uncapped=total/PI;var ambient_capped=capped/PI
 print("PANORAMA upward ambient: HDR file %.4f (2-degree disc %.1f%% of it); at the Mobile clip %.1f: %.4f, of which the disc %.4f (%.1f%%); brightest_lum_beyond_4deg=%.2f"%[ambient_uncapped,100.*disc/maxf(total,1e-9),DAYLIGHT.MOBILE_RADIANCE_CLIP,ambient_capped,disc_clipped/PI,100.*disc_clipped/maxf(capped,1e-9),brightest_cloud])
 expect(absf(ambient_capped-DAYLIGHT.SKY_AMBIENT)<.03*DAYLIGHT.SKY_AMBIENT,"SKY_AMBIENT matches the panorama at the Mobile clip")
 expect(disc_clipped<.02*capped,"at the Mobile clip the solar disc adds under 2% of the sky irradiance (no second sun)")
 expect(capped>=.85*(total-disc),"the clip keeps at least 85% of the sky's own (disc-free) irradiance")
 # 2. Shadow/sun ratios of the model.
 var env=Environment.new();var sun=DirectionalLight3D.new()
 daylight.configure_environment(env);daylight.configure_sun(sun)
 var reference=DAYLIGHT.reference_sun()
 sun.basis=Basis.looking_at(-reference,Vector3.UP)
 daylight.apply_conditions(env,sun,DAYLIGHT.CLEAR_CLOUD,16093.)
 var ratio=shadow_ratio(ambient_capped,env,sun)
 var sunlit=ambient_capped*env.ambient_light_energy+sun.light_energy*reference.y*DAYLIGHT.luminance(sun.light_color)
 print("CLEAR reference sun elevation=%.1f deg ambient_energy=%.3f sun_energy=%.3f opacity=%.3f exposure=%.3f white=%.3f fog_density=%.6f aerial=%.2f sunlit_white=%.3f shadow/sun=%.3f"%[rad_to_deg(asin(reference.y)),env.ambient_light_energy,sun.light_energy,sun.shadow_opacity,env.tonemap_exposure,env.tonemap_white,env.fog_density,env.fog_aerial_perspective,sunlit,ratio])
 var e=env.tonemap_exposure;var white=env.tonemap_white
 print("CLEAR Filmic preview sRGB: white sun/shade %d/%d, concrete .40 %d/%d, grey .18 %d/%d, asphalt .10 %d/%d"%[srgb8(filmic(sunlit*e,white)),srgb8(filmic(sunlit*ratio*e,white)),srgb8(filmic(.4*sunlit*e,white)),srgb8(filmic(.4*sunlit*ratio*e,white)),srgb8(filmic(.18*sunlit*e,white)),srgb8(filmic(.18*sunlit*ratio*e,white)),srgb8(filmic(.1*sunlit*e,white)),srgb8(filmic(.1*sunlit*ratio*e,white))])
 expect(ratio>=.18 and ratio<=.32,"clear-sky shadow/sun ratio inside the photographed 0.18-0.32")
 expect(is_equal_approx(env.tonemap_white,2.*env.tonemap_exposure),"Filmic white point is the Mobile 0-2 range after exposure")
 expect(absf(env.fog_density-3.912/16093.)<1e-6,"Koschmieder fog density at 10 statute miles")
 expect(env.fog_aerial_perspective==1. and env.fog_sky_affect==0.,"fog colour comes from the sky")
 var legacy=DAYLIGHT.new();legacy.legacy_sky=true
 var legacy_env=Environment.new();var legacy_sun=DirectionalLight3D.new();legacy_sun.basis=sun.basis
 legacy.configure_environment(legacy_env);legacy.apply_conditions(legacy_env,legacy_sun,DAYLIGHT.CLEAR_CLOUD,16093.)
 var legacy_ratio=shadow_ratio(ambient_capped,legacy_env,legacy_sun)
 print("LEGACY same sun, same clipped sky: ambient_energy=%.3f sun_energy=%.3f exposure=%.2f white=%.2f shadow/sun=%.3f (new %.3f)"%[legacy_env.ambient_light_energy,legacy_sun.light_energy,legacy_env.tonemap_exposure,legacy_env.tonemap_white,legacy_ratio,ratio])
 expect(legacy_ratio>ratio,"the legacy constants were flatter (higher shadow/sun) than the new model")
 var high=Vector3(0.,sin(deg_to_rad(85.)),-cos(deg_to_rad(85.)))
 sun.basis=Basis.looking_at(-high,Vector3.UP)
 daylight.apply_conditions(env,sun,DAYLIGHT.CLEAR_CLOUD,16093.)
 var high_sunlit=ambient_capped*env.ambient_light_energy+sun.light_energy*high.y*DAYLIGHT.luminance(sun.light_color)
 print("HIGH SUN 85 deg sunlit_white=%.3f exposure=%.3f shadow/sun=%.3f"%[high_sunlit,env.tonemap_exposure,shadow_ratio(ambient_capped,env,sun)])
 expect(high_sunlit<2.,"sunlit white stays below the Mobile 2.0 clip at a high sun")
 var low=DAYLIGHT.sun_now(OBSERVER,AFTERNOON_UNIX+3*3600) # 00:00 UTC: held at the 7 deg floor
 sun.basis=Basis.looking_at(-low,Vector3.UP)
 daylight.apply_conditions(env,sun,DAYLIGHT.CLEAR_CLOUD,16093.)
 print("LOW SUN %.1f deg exposure=%.3f shadow/sun=%.3f"%[rad_to_deg(asin(low.y)),env.tonemap_exposure,shadow_ratio(ambient_capped,env,sun)])
 sun.basis=Basis.looking_at(-reference,Vector3.UP)
 daylight.apply_conditions(env,sun,.9,8000.,true)
 var overcast_ratio=shadow_ratio(DAYLIGHT.CLOUD_SKY_AMBIENT,env,sun)
 print("OVERCAST cloud=0.9 ambient_energy=%.3f sun_energy=%.3f opacity=%.3f exposure=%.3f fog_density=%.6f shadow/sun=%.3f"%[env.ambient_light_energy,sun.light_energy,sun.shadow_opacity,env.tonemap_exposure,env.fog_density,overcast_ratio])
 expect(overcast_ratio>.7,"overcast shadows are soft")
 # 3. The clock.
 # The real clock (sun_now -> sun_at_epoch) and the dated formula solar_for_day at
 # the same UTC instant, day 172 of 2021, must be one sun.
 var clock_reference=DAYLIGHT.sun_now(OBSERVER,AFTERNOON_UNIX)
 var formula=DAYLIGHT.solar_for_day(deg_to_rad(OBSERVER.x),OBSERVER.y,21.,172,365.)
 var preset_azimuth=fposmod(rad_to_deg(atan2(reference.x,-reference.z)),360.);var preset_elevation=rad_to_deg(asin(reference.y))
 print("CLOCK at 2021-06-21 21:00Z over the US Capitol: elevation %.2f azimuth %.1f, formula %.4f deg apart; reference preset elevation %.2f azimuth %.1f"%[rad_to_deg(asin(clock_reference.y)),fposmod(rad_to_deg(atan2(clock_reference.x,-clock_reference.z)),360.),rad_to_deg(clock_reference.angle_to(formula)),preset_elevation,preset_azimuth])
 expect(rad_to_deg(clock_reference.angle_to(formula))<.01,"clock sun matches the dated solar formula at the same instant")
 expect(absf(preset_azimuth-275.9)<.001 and absf(preset_elevation-32.4)<.001,"reference preset is azimuth 275.9, elevation 32.4")
 expect(DAYLIGHT.sun_direction(OBSERVER,DAYLIGHT.REFERENCE_REGION).is_equal_approx(reference),"the reference region gives the reference sun anywhere")
 var midnight=DAYLIGHT.sun_now(OBSERVER,AFTERNOON_UNIX+8*3600) # 2021-06-22T05:00:00Z
 expect(absf(rad_to_deg(asin(midnight.y))-DAYLIGHT.MIN_SUN_ELEVATION_DEG)<.01,"night holds the 7 degree floor")
 var midnight_az=fposmod(rad_to_deg(atan2(midnight.x,-midnight.z)),360.)
 var predawn=DAYLIGHT.sun_now(OBSERVER,AFTERNOON_UNIX+12*3600) # 2021-06-22T09:00:00Z, after solar midnight
 var predawn_az=fposmod(rad_to_deg(atan2(predawn.x,-predawn.z)),360.)
 print("CLOCK night hold: 05:00Z azimuth %.1f (sunset side), 09:00Z azimuth %.1f elevation %.2f (sunrise side)"%[midnight_az,predawn_az,rad_to_deg(asin(predawn.y))])
 expect(midnight_az>240. and midnight_az<320.,"the evening holds the June sunset bearing, not due north")
 expect(predawn_az>40. and predawn_az<120. and absf(rad_to_deg(asin(predawn.y))-DAYLIGHT.MIN_SUN_ELEVATION_DEG)<.01,"after solar midnight the light waits on the sunrise bearing")
 var transit=DAYLIGHT.sun_now(OBSERVER,AFTERNOON_UNIX-3*3600-45*60) # 17:15:00Z
 print("CLOCK US Capitol 17:15Z elevation %.1f, 05:00Z elevation %.1f"%[rad_to_deg(asin(transit.y)),rad_to_deg(asin(midnight.y))])
 expect(rad_to_deg(asin(transit.y))>74.,"June transit altitude over the US Capitol above 74 degrees")
 var los_angeles=DAYLIGHT.sun_now(Vector2(34.05,-118.24),AFTERNOON_UNIX) # 14:00 PDT
 expect(rad_to_deg(asin(los_angeles.y))>55.,"Los Angeles 14:00 PDT sun above 55 degrees")
 var material=daylight.sky_material(load("res://art/day.hdr"))
 expect(float(material.get_shader_parameter("sun_radiance_cap"))==DAYLIGHT.SUN_RADIANCE_CAP and float(material.get_shader_parameter("cubemap_horizon"))==1.,"sky material carries the sun cap and the shared horizon band")
 var legacy_material=legacy.sky_material(load("res://art/day.hdr"))
 expect(float(legacy_material.get_shader_parameter("sun_radiance_cap"))>1e8 and float(legacy_material.get_shader_parameter("cubemap_horizon"))==0.,"--legacy-sky material restores the uncapped panorama and the old cubemap")
 daylight.clock_override=AFTERNOON_UNIX
 daylight.update(OBSERVER,"",sun,material,0,0.,1.)
 expect(rad_to_deg(sun.basis.z.angle_to(reference))<.5,"update() under a photographed sky follows the reference sun")
 expect(daylight.capture_weight(OBSERVER,true)==0.,"no photographed sky ships: capture weight 0 everywhere")
 daylight.capture_center=OBSERVER # exercise the fade as if one were bundled here
 expect(daylight.capture_weight(OBSERVER,true)==1. and daylight.capture_weight(OBSERVER,false)==0. and daylight.capture_weight(OBSERVER+Vector2(.01,0.),true)==0.,"capture weight: full at the photograph, none with live weather or 1.1 km away")
 daylight.capture_center=Vector2.INF
 var later=AFTERNOON_UNIX+86400*120
 daylight.update(OBSERVER,"",sun,material,later,1.,0.)
 expect(daylight.applied_epoch==later and daylight.applied_weight==1.,"an archived photo epoch still takes over the sun")
 expect(rad_to_deg(sun.basis.z.angle_to(DAYLIGHT.sun_at_epoch(OBSERVER,later)))<.01,"...and the light points at that epoch's sun")
 # 4. Godot 4.7 names and project settings.
 for name in ["fog_aerial_perspective","fog_sky_affect","fog_sun_scatter","fog_density","tonemap_mode","tonemap_exposure","tonemap_white","ambient_light_energy"]:
  expect(has_property("Environment",name),"Environment."+name)
 for name in ["directional_shadow_max_distance","directional_shadow_blend_splits","directional_shadow_fade_start","shadow_blur","shadow_opacity","shadow_bias"]:
  expect(has_property("DirectionalLight3D",name),"DirectionalLight3D."+name)
 expect(Environment.TONE_MAPPER_AGX==4 and Environment.TONE_MAPPER_FILMIC==2,"tonemapper enum values")
 expect(has_property("Viewport","use_debanding") and has_property("Viewport","anisotropic_filtering_level") and Viewport.ANISOTROPY_4X==2 and Viewport.ANISOTROPY_16X==4,"Viewport runtime toggles behind --legacy-filters")
 expect(sun.directional_shadow_max_distance==180. and sun.directional_shadow_blend_splits and sun.shadow_blur==1.5,"shadows reach 180 m with blended splits and blur 1.5")
 var settings={"rendering/anti_aliasing/quality/use_debanding":true,"rendering/textures/default_filters/anisotropic_filtering_level":4,"rendering/lights_and_shadows/directional_shadow/size":2048}
 for key in settings:
  var value=ProjectSettings.get_setting_with_override(key)
  print("SETTING %s=%s"%[key,str(value)])
  expect(value==settings[key],key)
 print("SETTING rendering/lights_and_shadows/directional_shadow/soft_shadow_filter_quality=%s (effective on this machine; 0 hard, 1 very low, 2 soft low, 3 medium, 4 high, 5 ultra)"%str(ProjectSettings.get_setting_with_override("rendering/lights_and_shadows/directional_shadow/soft_shadow_filter_quality")))
 sun.free();legacy_sun.free()
 if failures.is_empty():print("LIGHTING PASS")
 else:print("LIGHTING FAIL ",failures)
 quit(0 if failures.is_empty() else 1)
