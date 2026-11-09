shader_type sky;

uniform sampler2D panorama : source_color, filter_linear, repeat_enable;
global uniform float chula_survey_sky_fade;
global uniform vec4 road_sky_high;
global uniform vec4 road_sky_low;
uniform sampler2D survey_sky : source_color, filter_linear_mipmap, repeat_disable;
uniform float solar_bearing_turns = 0.0;
uniform float solar_v = 0.30;
// Pixel center of the brightest light in the supplied 1024 × 512 panorama.
const vec2 SOURCE_SUN = vec2(614.5/1024.0, 119.5/512.0);
// Luminance ceiling applied to the finished radiance in every pass. The
// photographed solar disc peaks near 53,600. On the Mobile renderer this
// project uses, the radiance map and the reflection probe are 10-bit UNORM
// written at half scale, so they already clip sky radiance at 2.0 and the disc
// never lit the scene; this ceiling only matters on an HDR path (Forward+ or
// use_hdr_2d), where an unclipped disc would add a second, unshadowed sun.
// At 8 the visible disc still draws pure white. checks/LightingCheck.gd
// measures the panorama at the Mobile clip. SurveyDaylight sets 1e9 for
// --legacy-sky captures.
uniform float sun_radiance_cap = 8.0;
// 1.0 shares the visible horizon band with the radiance cubemap (see sky()),
// 0.0 leaves the cubemap's photographed treeline as before (--legacy-sky).
uniform float cubemap_horizon = 1.0;

global uniform vec4 observed_day_sky; // measured-clear weight, high-cloud weight, veil, low/mid area
uniform sampler2D clear_field : filter_linear, repeat_enable;
uniform sampler2D scattering : filter_linear, repeat_disable;
uniform sampler2D cloud_rank : filter_linear, repeat_enable;
uniform float archived_broken = 0.0;
uniform vec4 broken_top : source_color = vec4(.509804,.580392,.650980,1.);
uniform vec4 broken_horizon : source_color = vec4(.788235,.803922,.815686,1.);
uniform vec4 broken_ground_horizon : source_color = vec4(.666667,.690196,.678431,1.);
uniform vec4 broken_ground_bottom : source_color = vec4(.407843,.443137,.407843,1.);
vec3 single_scattering(float azimuth, float elevation, float sun_elevation) {
 float slice=clamp((sun_elevation-3.)/5.,0.,16.);
 vec2 uv=vec2((.5+azimuth/180.*80.)/81.,(.5+clamp(elevation/90.,0.,1.)*47.+48.*floor(slice))/(48.*17.));
 vec3 a=texture(scattering,uv).rgb;
 vec3 b=texture(scattering,uv+vec2(0.,min(1.,16.-floor(slice))/17.)).rgb;
 return 4.*mix(a*a,b*b,fract(slice));
}
void sky() {
 float bearing_turns = atan(EYEDIR.x, -EYEDIR.z) / TAU;
 float u = SOURCE_SUN.x + bearing_turns - solar_bearing_turns;
 float v = SKY_COORDS.y;
 // Both poles and the horizon stay fixed as the disk changes elevation.
 if (v < solar_v) {
  v *= SOURCE_SUN.y / solar_v;
 } else if (v < 0.5) {
  v = mix(SOURCE_SUN.y, 0.5, (v - solar_v)/(0.5 - solar_v));
 }
 vec3 radiance = texture(panorama, vec2(u, v)).rgb;
 // The lighting panorama includes a photographed treeline. Its foreground
 // was never a geographic observation of the street beneath the sky.
 if (v > 0.445) {
  // A horizontally smooth low atmosphere avoids stretching the HDR cloud row.
  float forward = 0.5+0.5*cos(TAU*(bearing_turns-solar_bearing_turns));
  vec3 horizon_air=mix(vec3(.57,.74,1.0),vec3(.89,.89,.86),pow(forward,5.0));
  float band = smoothstep(.445,.492,v);
  // The radiance cubemap keeps the photographed ground for bounce light and
  // reflections but shares this horizon band, so Environment aerial
  // perspective (which samples the cubemap toward each distant fragment)
  // fades terrain into the colour the eye sees at the horizon: one haze.
  if (AT_CUBEMAP_PASS) {
   band *= cubemap_horizon*(1.0 - smoothstep(.50,.545,v));
  }
  radiance=mix(radiance,horizon_air,band);
 }
 if (!AT_CUBEMAP_PASS && EYEDIR.y>=0. && (observed_day_sky.x>0. || observed_day_sky.y>0.)) {
  float sun_elevation=(.5-solar_v)*PI;
  vec3 toward_sun=vec3(cos(sun_elevation)*sin(solar_bearing_turns*TAU),sin(sun_elevation),-cos(sun_elevation)*cos(solar_bearing_turns*TAU));
  // Keep the photographed solar disk in place for the background; its
  // radiance is limited by sun_radiance_cap at the end like everything else.
  float retain_disk=smoothstep(cos(radians(.6)),cos(radians(.25)),dot(EYEDIR,toward_sun));
  vec3 field;
  if (observed_day_sky.x>0.) {
   float source_v=133.5/512.;
   float cv=SKY_COORDS.y<solar_v?SKY_COORDS.y*source_v/solar_v:mix(source_v,.5,(SKY_COORDS.y-solar_v)/(.5-solar_v));
   vec3 encoded=texture(clear_field,vec2(u,clamp(cv*2.,.002,.998))).rgb;
   field=4.*encoded*encoded;
  } else {
   float relative=abs(fract(bearing_turns-solar_bearing_turns+.5)-.5)*360.;
   field=single_scattering(relative,(.5-SKY_COORDS.y)*180.,degrees(sun_elevation));
   field=mix(field,vec3(dot(field,vec3(.2126,.7152,.0722))+.13),observed_day_sky.z);
   float rank=texture(cloud_rank,vec2(u,clamp(v/.4375,.002,.998))).r;
   float extent=observed_day_sky.w;
   float low=mix(0.,smoothstep(1.-extent-.025,1.-extent+.025,rank),smoothstep(0.,.035,extent));
   field=mix(field,radiance,low*smoothstep(8.,16.,(.5-v)*180.));
  }
  radiance=mix(radiance,field,max(observed_day_sky.x,observed_day_sky.y)*(1.-retain_disk));
 }
 if (archived_broken>0.) {
  // The same procedural overcast gradient as the stock rain environment,
  // crossfaded into the visible field and IBL while leaving the archived site.
  vec3 cover=EYEDIR.y>=0.?mix(broken_top.rgb,broken_horizon.rgb,pow(1.-EYEDIR.y,4.)):
   mix(broken_ground_bottom.rgb,broken_ground_horizon.rgb,pow(1.+EYEDIR.y,30.));
  radiance=mix(radiance,cover,archived_broken);
 }
 // The dashcam is an LDR observation. Geometry lighting and mirror reflections
 // always retain the original HDR radiance, including the calibrated sun.
 if (!AT_CUBEMAP_PASS && road_sky_high.a>0. && EYEDIR.y>0.) {
  float bearing_delta=abs(fract(bearing_turns-road_sky_low.a+.5)-.5)*360.;
  float elevation=degrees(asin(clamp(EYEDIR.y,0.,1.)));
  float window=(1.-smoothstep(27.,45.,bearing_delta))*smoothstep(1.,8.,elevation)*(1.-smoothstep(40.,62.,elevation));
  vec3 clear=mix(road_sky_low.rgb,road_sky_high.rgb,smoothstep(10.,27.,elevation));
  // Photographic inputs already include display contrast; the game tonemaps
  // the sky again. Bring the photographed colors into the reference daylight
  // exposure before blending them with the HDR sky.
  radiance=mix(radiance,clear*.65,window*min(1.,road_sky_high.a*1.15));
 }
 if (!AT_CUBEMAP_PASS && chula_survey_sky_fade > 0.0) {
  vec2 observed_uv = vec2((bearing_turns*360.0-18.0)/173.0, (57.0-(.5-SKY_COORDS.y)*180.0)/59.0);
  if (all(greaterThanEqual(observed_uv,vec2(0.))) && all(lessThanEqual(observed_uv,vec2(1.)))) {
   vec4 observation = texture(survey_sky,observed_uv);
   radiance = mix(radiance,observation.rgb*1.02,observation.a*chula_survey_sky_fade);
  }
 }
 // One sun: the disc keeps its hue but never outshines sun_radiance_cap, in
 // the background, in the reflection probe and in the radiance cubemap.
 float luminance = dot(radiance, vec3(.2126,.7152,.0722));
 if (luminance > sun_radiance_cap) {
  radiance *= sun_radiance_cap/luminance;
 }
 COLOR = radiance;
}
