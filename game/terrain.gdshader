shader_type spatial;
render_mode cull_disabled;

uniform sampler2D surface_map : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D grit : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D ortho : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform vec2 quadrant;
uniform bool imagery = false;
uniform float meters = 480.0;
uniform sampler2D road_edges : filter_linear, repeat_disable;
uniform bool has_edges = false;
uniform bool has_section=false;
uniform sampler2D section_map : filter_linear,repeat_disable;
uniform vec4 section_box;
uniform sampler2D low_cover_map : filter_linear_mipmap_anisotropic, repeat_disable;
uniform bool has_low_cover = false;
uniform bool has_soil = false;
uniform sampler2D mineral_detail : filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec2 mineral_origin;
uniform sampler2D bare_mask : filter_linear,repeat_disable;

#include "res://GroundFinish.gdshaderinc"

uniform vec2 photo_strength = vec2(0.0);
uniform vec2 photo_point_0;
uniform vec2 photo_point_1;
uniform vec2 photo_point_2;
uniform vec3 photo_pavement;
uniform vec3 photo_green;

float segment_distance(vec2 p, vec2 a, vec2 b) {
 vec2 v=b-a;
 return length(p-a-v*clamp(dot(p-a,v)/max(dot(v,v),0.0000001),0.,1.));
}

#include "res://AerialPavement.gdshaderinc"

vec3 observed_tone(vec3 base, vec3 observed, float reference, float weight) {
 float light = dot(observed, LUMA);
 vec3 tint = clamp(observed/max(light,0.01),vec3(0.80),vec3(1.20));
 float exposure = clamp(light/reference,0.76,1.28);
 return base * mix(vec3(1.),tint*exposure,weight);
}

void fragment() {
 vec3 paint = texture(surface_map, UV).rgb;
 // Only mapped foot/cycle paths use magenta in the semantic surface map.
 // Remove its chroma before ordinary shading, including antialiased/mipmap edges.
 float path_signal = max(0., min(paint.r, paint.b) - paint.g);
 float path_cover = smoothstep(0.0, 0.80, path_signal);
 paint.rb -= vec2(path_signal);
 // Cyan marks an explicitly unpaved mapped motor road. The threshold clears
 // the existing water hue; remove the label before ordinary albedo shading.
 float loose_signal=max(0.0,min(paint.g,paint.b)-paint.r);
 float loose_cover=smoothstep(0.24,0.80,loose_signal);
 paint.gb-=vec2(loose_signal*loose_cover);
 vec2 metres = UV * meters + detail_origin;
 float fine = dot(stochastic(grit, metres / 2.0, true).rgb, LUMA);
 float broad = dot(texture(grit, vec2(metres.y, -metres.x) / 12.0).rgb, LUMA);
 vec3 grass = stochastic(grass_detail, metres / 2.0, true).rgb;
 vec3 lawn_blades = texture(grass_detail, vec2(metres.y, -metres.x) / 0.8).rgb;
 vec3 leaves = stochastic(litter, vec2(-metres.y, metres.x) / 3.0, true).rgb;
 float near_view = 1.0 - smoothstep(22.0, 125.0, length(VERTEX));
 vec3 wear = weathering(metres);
 // The red channel places invented rectangular repairs. Its mean over the
 // 96 m pattern repeat preserves the calibrated diffuse level and roughness.
 // Green and blue retain fine crack seals and gradual age variation.
 wear.r = 0.0393;
 vec3 aerial = imagery ? texture(ortho, (UV + quadrant) * 0.25).rgb : vec3(0.143, 0.168, 0.081);
 float value = dot(aerial, LUMA);
 float aerial_green = smoothstep(0.05, 0.30, (aerial.g - 0.50 * (aerial.r + aerial.b)) / max(0.022, value));
 float green = aerial_green;
 // Only dark, green airborne samples receive a near-field forest-litter layer.
 float trees = green * (1.0 - smoothstep(0.039, 0.095, value));
 // Positive red/NIR contrast can add turf on low-canopy land. Mapped surfaces
 // and motor road edges retain priority in the blending below.
 if (has_low_cover) {green=max(green,texture(low_cover_map,UV).r);}
 vec3 ground = ground_surface(aerial, grass, leaves, green, trees, 0.0, fine, broad, near_view);
 ground = season_tint(ground, UV, green * 0.70);
 float mineral_weight=0.;
 vec3 mineral=vec3(.5,.5,.96);
 if(imagery && use_recent && has_soil && green<.8) {
  vec2 st=(UV+recent_quadrant)*.25;
  vec4 model=texture(bare_mask,st);
  float confidence=smoothstep(.72,.97,model.a)*(1.-smoothstep(.12,.75,green));
  if(confidence>0.) {
   // Linear reflectance with a 0.5 full scale. Alpha distinguishes direct
   // measurements, locally supported red-channel estimates and missing data.
   vec3 soil=model.rgb*.5;
   vec3 broad_aerial=textureLod(ortho,(UV+quadrant)*.25,3.).rgb;
   vec3 local_ratio=clamp(aerial/max(broad_aerial,vec3(.018)),vec3(.48),vec3(1.65));
   mineral_weight=confidence*near_view;
   mineral=stochastic(mineral_detail,(UV*meters+mineral_origin)/2.53,true).rgb;
   float grit_gain=mix(1.,clamp(mineral.r*2.,.45,1.75),near_view);
   ground=mix(ground,soil*local_ratio*grit_gain,confidence);
  }
 }
 // Steep ground: an aerial photograph is projected straight down, so on a
 // slope above ~30° each pixel smears down the hill. Blend toward rock and
 // soil sampled triplanar in world space (it cannot stretch), keeping the
 // photo's broad local hue, with faint horizontal strata from world height
 // the way road cuts and outcrops read. No extra data: slope comes from the
 // geometry's own normal.
 vec3 world_n = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
 vec3 world_p = (INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
 float steep = smoothstep(0.15, 0.36, 1.0 - world_n.y);
 if (steep > 0.0) {
  vec3 tri_w = pow(abs(world_n), vec3(4.0)); tri_w /= max(tri_w.x + tri_w.y + tri_w.z, 0.0001);
  float rock_detail = tri_w.x * dot(texture(grit, world_p.zy / 1.7).rgb, LUMA)
   + tri_w.y * dot(texture(grit, world_p.xz / 1.7).rgb, LUMA)
   + tri_w.z * dot(texture(grit, world_p.xy / 1.7).rgb, LUMA);
  float rock_broad = tri_w.x * dot(texture(grit, world_p.zy / 9.0).rgb, LUMA)
   + tri_w.y * dot(texture(grit, world_p.xz / 9.0).rgb, LUMA)
   + tri_w.z * dot(texture(grit, world_p.xy / 9.0).rgb, LUMA);
  vec3 hue = imagery ? textureLod(ortho, (UV + quadrant) * 0.25, 4.0).rgb : vec3(0.16, 0.15, 0.12);
  float hue_light = dot(hue, LUMA);
  // Exposed rock and soil: the local hue, half desaturated, kept in a plausible albedo range.
  // Weathered rock and soil sit near 0.08-0.14 albedo; keep the photo's hue but
  // not its forest shadow, which makes the canopy-dark photo too black.
  vec3 earth = mix(vec3(hue_light), hue, 0.6) * clamp(0.075 / max(hue_light, 0.02), 0.6, 1.25);
  float bands = sin(world_p.y * 4.1 + rock_broad * 11.0) * 0.5 + sin(world_p.y * 9.7 + rock_detail * 5.0) * 0.25;
  float strata = clamp(0.5 + 0.5 * bands, 0.0, 1.0);
  float grain = clamp((rock_detail - 0.22) * 3.2, -0.6, 0.6);
  vec3 rock = earth * (0.95 + grain) * (0.72 + 0.48 * strata) * (0.85 + 0.35 * rock_broad);
  // Vegetation clings where the photo is green and the slope is moderate.
  float clinging = green * (1.0 - smoothstep(0.30, 0.55, 1.0 - world_n.y));
  ground = mix(ground, rock, steep * (1.0 - 0.65 * clinging));
 }
 vec3 paved = paved_surface(imagery ? aerial : paint, fine, broad, wear);
 vec3 unadjusted_paved = paved;
 paved = aerial_pavement(unadjusted_paved, aerial, UV);
 // Lateral position across the mapped carriageway from the box-filtered
 // semantic map: 3 m half-width for tyre tracks, 8 m for the mown verge.
 float mask_texels = float(textureSize(surface_map, 0).x);
 float lane_edge = edge_metres(mapped_asphalt(textureLod(surface_map, UV, coverage_lod(mask_texels, meters, 3.0)).rgb), 3.0);
 float verge_edge = edge_metres(mapped_asphalt(textureLod(surface_map, UV, coverage_lod(mask_texels, meters, 8.0)).rgb), 8.0);
 float paths = wheel_paths(lane_edge);
 float age = aerial_asphalt_age(wear.b, aerial, UV);
 // On a road ribbon the lane frame is known (GroundFinish lane_wear): wheel
 // paths and the oil strip sit at real lane offsets, and crack seals and
 // patches follow the street's condition.
 bool ribbon = UV2.x > LANE_FLAG * 0.5;
 float seal = 0.0;
 float lane_tone = 1.0;
 if (ribbon) {
  float lateral = UV2.x - LANE_FLAG;
  float along = UV2.y;
  float oil; float patch_tone;
  lane_wear(lateral, along, COLOR.r * 8.0, COLOR.b * 16.0, COLOR.g,
            fwidth(lateral), fwidth(along), paths, oil, seal, patch_tone);
  // Wear varies along the lane: broad grit for the paths, lane-aligned
  // streaks for drips (stretched along the direction of travel).
  float streak = dot(texture(grit, vec2(along / 6.0, lateral / 0.7)).rgb, LUMA);
  paths *= clamp(0.80 + (broad - 0.224) * 3.0, 0.55, 1.20) * mix(0.75, 1.0, COLOR.g);
  float drip = oil * mix(0.09, 0.24, COLOR.g) * clamp(0.55 + (streak - 0.224) * 5.0, 0.15, 1.45);
  // Tyres scrub the binder off the wheel paths of an older road (lighter,
  // exposed aggregate); worn_asphalt() below adds the age-dependent part.
  lane_tone = (1.0 - drip) * (1.0 + patch_tone) * (1.0 + paths * mix(0.03, 0.11, COLOR.g));
  // The texture's own sealed cracks follow the condition too.
  wear.g *= mix(0.35, 1.2, COLOR.g);
 }
 paved = worn_asphalt(paved, paths, age);
 float mown = smoothstep(-6.0, -1.5, verge_edge) * green * (1.0 - trees);
 ground = mown_turf(ground, lawn_blades, value, mown, near_view);
 if (max(photo_strength.x,photo_strength.y)>0.) {
  float dist=meters*min(segment_distance(UV,photo_point_0,photo_point_1),segment_distance(UV,photo_point_1,photo_point_2));
  float influence=1.0-smoothstep(30.0,85.0,dist);
  // Three co-located road observations establish the broad pavement tone.
  // Retain local aggregate, aerial variation and real-time shadows. Bound the
  // recovered diffuse level because the archive cameras have unknown exposure.
  float road_light=dot(photo_pavement,LUMA);
  vec3 dry_tone=clamp(photo_pavement/max(road_light,.015),vec3(.87),vec3(1.13));
  float diffuse_light=clamp(road_light*.72,.041,.32);
  vec3 observed_paved=dry_tone*diffuse_light*clamp(dot(unadjusted_paved,LUMA)/.050,.64,1.70);
  paved=mix(paved,observed_paved,influence*photo_strength.x*.88);
  ground=observed_tone(ground,photo_green,0.12,influence*photo_strength.y*green*(1.-trees)*0.5);
 }
 // Lane wear after the observed tone, which would otherwise replace it.
 // Rubberised sealant: near black when fresh, greying as it ages.
 paved *= lane_tone;
 paved = mix(paved, paved * mix(0.42, 0.68, age), seal);
 bool land = paint.g > paint.r * 1.3 && paint.g > paint.b * 1.3;
 // Identify the #484a49 vector centerline surface without affecting water,
 // unpaved tracks, painted markings, curbs or sidewalks.
 float spread = max(paint.r, max(paint.g, paint.b)) - min(paint.r, min(paint.g, paint.b));
 float road = (1.0 - smoothstep(0.012, 0.033, spread)) * (1.0 - smoothstep(0.10, 0.175, paint.g));
 vec3 other = paint * clamp(1.0 + (fine - 0.224) * 1.3, 0.84, 1.24);
 vec3 color = land ? ground : mix(other, paved, road);
 if (has_edges && imagery) {
  vec2 edges = texture(road_edges, UV).rg;
  if (edges.g > 0.03 && max(paint.r, paint.b) < 0.58) {
   road = smoothstep(0.33, 0.67, edges.r);
   color = mix(ground, paved, road);
  }
 }
 // Preserve mapped paving at the edge of the learned motor-road shoulder.
 float path_light = imagery ? clamp(0.19 + value * 0.37, 0.19, 0.43) : 0.27;
 vec3 path_paving = vec3(1.02, 1.00, 0.95) * path_light * clamp(1.0 + (fine - 0.224)*0.8, 0.83, 1.20);
 color = mix(color, path_paving, path_cover);
 // Preserve local colour and broad variation in the overhead observation.
 // A diffuse fallback is needed while imagery is missing or covers vegetation.
 vec3 dry_track=vec3(0.35,0.30,0.20);
 float visible_surface=imagery ? 1.0-aerial_green : 0.0;
 // Allow for baked direct light before the game lights the surface again.
 vec3 loose_color=mix(dry_track,clamp(aerial*0.72,vec3(0.035),vec3(0.50)),visible_surface);
 color=mix(color,loose_color*clamp(1.0+(fine-0.224)*0.75+(broad-0.224)*0.25,0.86,1.18),loose_cover);
 road *= (1.0 - path_cover) * (1.0 - loose_cover);
 if (has_section) {
  vec2 position_in_section=(UV-section_box.xy)/section_box.zw;
  if (all(greaterThanEqual(position_in_section,vec2(0.))) && all(lessThan(position_in_section,vec2(1.)))) {
   vec2 survey=texture(section_map,position_in_section).rg;
   if(survey.g>.01){
    road=mix(road,survey.r/max(survey.g,.001),survey.g);
    color=mix(color,mix(ground,paved,clamp(survey.r/max(survey.g,.001),0.,1.)),survey.g);
   }
  }
 }
 mineral_weight*=float(land)*(1.-road)*(1.-loose_cover)*(1.-path_cover);
 ALBEDO = color;
 ROUGHNESS = mix(mix(0.96, 0.93, mown), asphalt_roughness(fine, broad, vec3(wear.r, wear.g, age), paths) - seal * 0.06, road);
 ROUGHNESS=mix(ROUGHNESS,mineral.b,mineral_weight);
 SPECULAR = mix(0.19, 0.30, road);
 vec3 shading_normal = NORMAL;
 if (has_asphalt_normal && road > 0.02 && near_view > 0.0) {
  // Polished wheel paths carry less aggregate relief than the lane centre.
  shading_normal = normalize(mix(NORMAL, asphalt_bumps(VERTEX, NORMAL, metres, near_view * (1.0 - 0.35 * paths)), road));
 }
 float land_relief = fine * mix(0.009, 0.006, mown) + green * dot(grass, LUMA) * 0.016 + trees * dot(leaves, LUMA) * 0.024;
 land_relief=mix(land_relief,(mineral.g-.5)*.018,mineral_weight);
 NORMAL = relief_normal(VERTEX, shading_normal, mix(land_relief, fine * 0.004 - wear.g * 0.0015, road) * near_view);
}
