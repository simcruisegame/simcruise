shader_type spatial;

uniform sampler2D photo : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D grain : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D pavement : filter_linear_mipmap_anisotropic, repeat_disable;
uniform bool winter = false;
uniform float meters;

#include "res://GroundFinish.gdshaderinc"

uniform bool has_capture_road = false;
uniform sampler2D capture_road : filter_linear_mipmap_anisotropic, repeat_disable;
uniform vec3 captured_turf : source_color = vec3(0.25, 0.35, 0.15);
uniform vec2 capture_origin;
uniform vec2 capture_axis;
uniform vec2 capture_extent = vec2(1.0);
// Width of the mown band beside the pavement, in metres from the asphalt edge.
uniform float verge_mown_m = 2.5;
uniform float verge_rough_m = 8.0;

void fragment() {
 vec4 cover = texture(pavement, UV);
 vec2 metres = UV * meters + detail_origin;
 vec3 aerial = texture(photo, UV).rgb;
 vec3 grass = stochastic(grass_detail, metres / 2.0, true).rgb;
 vec3 lawn_blades = texture(grass_detail, vec2(metres.y, -metres.x) / 0.8).rgb;
 vec3 leaves = stochastic(litter, vec2(-metres.y, metres.x) / 3.0, true).rgb;
 float fine = dot(stochastic(grain, metres / 2.0, true).rgb, LUMA);
 float broad = dot(texture(grain, vec2(metres.y, -metres.x) / 12.0).rgb, LUMA);
 vec3 wear = weathering(metres);
 float near_view = 1.0 - smoothstep(22.0, 125.0, length(VERTEX));
 float road = max(cover.r, smoothstep(0.86, 0.96, cover.a));
 float gravel = smoothstep(0.56, 0.69, cover.a) * (1.0 - smoothstep(0.81, 0.93, cover.a));
 vec3 ground = ground_surface(aerial, grass, leaves, cover.g, cover.b, float(winter), fine, broad, near_view);
 ground = season_tint(ground, UV, max(cover.g, cover.b) * 0.7 * (1.0 - gravel));

 // Lateral position across the mapped pavement, from box-filtered coverage:
 // a 3 m half-width for the tyre tracks and 8 m for the mown verge band.
 float mask_texels = float(textureSize(pavement, 0).x);
 float lane_edge = edge_metres(textureLod(pavement, UV, coverage_lod(mask_texels, meters, 3.0)).r, 3.0);
 float verge_edge = edge_metres(textureLod(pavement, UV, coverage_lod(mask_texels, meters, 8.0)).r, 8.0);
 float paths = wheel_paths(lane_edge);
 float mown = smoothstep(-verge_rough_m, -verge_mown_m, verge_edge) * (1.0 - road) * (1.0 - gravel) * (1.0 - cover.b);
 // Beside the road the turf is cut; further out it grows in tussocks with
 // shaded gaps, which also carries into the relief below.
 float turf = cover.g;
 float gaps = clump_gaps(fine) * (1.0 - mown) * turf * near_view;
 ground *= mix(1.0, 0.84, gaps) * mix(1.0, 1.06, (1.0 - clump_gaps(fine)) * (1.0 - mown) * turf * near_view);
 ground = mown_turf(ground, lawn_blades, dot(aerial, LUMA), mown * turf, near_view);

 // A broad, cool-gray strip adjoining a mapped road is often the shoulder.
 // Restrict that inference to the neighborhood of surveyed pavement and keep
 // mapped turf, canopy and separately classified county driveways authoritative.
 float proximity = smoothstep(0.045, 0.30, textureLod(pavement, UV, 3.5).r);
 float neutral = smoothstep(-0.012, 0.035, min(aerial.b, aerial.g) - aerial.r * 0.97);
 float shoulder = proximity * neutral * (1.0 - cover.g) * (1.0 - cover.b) * (1.0 - gravel);
 vec3 chip = vec3(0.117, 0.108, 0.090) * clamp(0.47 + fine * 2.6 + broad * 0.53, 0.62, 1.54);
 vec3 paved = worn_asphalt(paved_surface(aerial, fine, broad, wear), paths, wear.b);
 ground = mix(ground, mix(chip, paved, 0.72), shoulder * near_view * 0.86);
 ground = mix(ground, chip * clamp(0.79 + dot(aerial, LUMA) * 1.1, 0.76, 1.29), gravel);
 ALBEDO = mix(ground, paved, road);
 if (has_capture_road) {
  float grass_luma = dot(grass, LUMA);
  vec3 turf_color = captured_turf * clamp(0.47 + grass_luma * 3.25 + broad * 0.52, 0.52, 1.45);
  float lawn = cover.g * (1.0-road) * (1.0-gravel) * (1.0-cover.b);
  ALBEDO = mix(ALBEDO, turf_color, lawn * 0.80);
  vec2 delta = UV - capture_origin;
  vec2 coord = vec2(dot(delta, capture_axis), dot(delta, vec2(-capture_axis.y, capture_axis.x))) / capture_extent;
  vec4 sample_road = texture(capture_road, coord);
  float inside = step(0.0, min(coord.x, coord.y)) * step(max(coord.x, coord.y), 1.0);
  // Red is exposure-normalized measured pavement detail and green is paint.
  float confidence = inside * sample_road.a;
  float observed_width = inside * sample_road.b * (1.0 - cover.g) * (1.0 - cover.b);
  vec3 capture_base = worn_asphalt(paved_surface(aerial, fine, broad, vec3(wear.r * .22, wear.g * .46, wear.b)), paths, wear.b);
  ALBEDO = mix(ALBEDO, capture_base, observed_width * 0.97);
  vec3 repaired = capture_base * clamp(0.48 + sample_road.r * 1.04, 0.67, 1.31);
  vec3 measured = mix(repaired, vec3(0.54, 0.53, 0.48), sample_road.g * 0.94);
  ALBEDO = mix(ALBEDO, measured, confidence);

 }
 ROUGHNESS = mix(mix(0.96, 0.93, mown), asphalt_roughness(fine, broad, wear, paths), road);
 SPECULAR = mix(0.19, 0.30, road);
 vec3 shading_normal = NORMAL;
 if (has_asphalt_normal && road > 0.02 && near_view > 0.0) {
  // Polished wheel paths carry less aggregate relief than the lane centre.
  shading_normal = normalize(mix(NORMAL, asphalt_bumps(VERTEX, NORMAL, metres, near_view * (1.0 - 0.35 * paths)), road));
 }
 float ground_relief = fine * mix(0.024, 0.008, mown) + cover.g * dot(grass, LUMA) * 0.017 + cover.b * dot(leaves, LUMA) * 0.024;
 float road_relief = fine * 0.004 - wear.g * 0.0015;
 NORMAL = relief_normal(VERTEX, shading_normal, mix(ground_relief, road_relief, road) * near_view);
}
