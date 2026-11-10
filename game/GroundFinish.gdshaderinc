// Close-range surfaces use photographed aggregate, grass and forest litter.
// The repair pattern is illustrative and independent of measured road geometry.
#include "res://Stochastic.gdshaderinc"
uniform sampler2D asphalt_wear : hint_default_black, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D grass_detail : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D litter : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D recent_image : source_color, filter_linear, repeat_disable;
// Procedural aggregate normals generated once per run by GroundFinish.gd.
uniform sampler2D asphalt_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform bool has_asphalt_normal = false;
uniform bool use_recent = false;
uniform vec2 recent_quadrant;
uniform vec2 detail_origin;
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

vec3 season_tint(vec3 base, vec2 uv, float amount) {
 if (!use_recent) { return base; }
 vec4 recent = texture(recent_image, (uv + recent_quadrant) * 0.25);
 vec3 tint = clamp(recent.rgb / max(0.035, dot(recent.rgb, LUMA)), vec3(0.55), vec3(1.65));
 vec3 seasonal = mix(base * tint, clamp(dot(base, LUMA), 0.045, 0.48) * tint, 0.58);
 return mix(base, seasonal, amount * recent.a);
}

vec3 weathering(vec2 metres) {
 vec3 first = stochastic(asphalt_wear, metres / 24.0, false).rgb;
 vec3 other = texture(asphalt_wear, vec2(-metres.y, metres.x) / 32.0 + vec2(0.26, 0.39)).rgb;
 // Two independent periods prevent a recognizable run of identical repairs.
 return vec3(max(first.r * 0.9, other.r * 0.58), max(first.g, other.g * 0.65),
             first.b * 0.62 + other.b * 0.38);
}

vec3 paved_surface(vec3 aerial, float fine, float broad, vec3 wear) {
 // The photograph supplies metre-scale variations in the actual pavement.
 // Compress its broad exposure range so painted lines and highlights in the
 // aerial image do not become bright blobs beneath the separate lane meshes.
 float pavement_light = clamp(0.022 + dot(aerial, LUMA) * 0.091, 0.027, 0.106);
 vec3 base = vec3(pavement_light) * vec3(1.02, 1.00, 0.96);
 float aggregate = 1.0 + (fine - 0.224) * 1.95 + (broad - 0.224) * 1.50;
 float age = 0.79 + wear.b * 0.48;
 return base * clamp(aggregate, 0.65, 1.60) * age * (1.0 - wear.r * 0.42) * (1.0 - wear.g * 0.58);
}

// ---- Lateral position across a carriageway -------------------------------
// Neither ground shader knows the road centreline, but both have a mipmapped
// coverage mask of the pavement. A mip level whose filter half-width is r
// metres turns a straight edge into a ramp: coverage c = (r + e) / (2 r) for
// a point e metres inside the edge, so e = r (2 c - 1) within [-r, r].
// (Trilinear mip filtering is a tent rather than a box, which compresses the
// estimate toward the edge by roughly a quarter; wheel_paths() allows for it.)
float coverage_lod(float mask_texels, float tile_metres, float half_width_m) {
 float texel_m = tile_metres / max(mask_texels, 1.0);
 return log2(max(2.0 * half_width_m / texel_m, 1.0));
}

float edge_metres(float coverage, float half_width_m) {
 return half_width_m * (2.0 * clamp(coverage, 0.0, 1.0) - 1.0);
}

// Asphalt fraction of a filtered semantic street-map sample. Godot averaged the
// 8-bit sRGB mipmaps, so return to that scale before reading the green channel,
// which separates mapped asphalt (#484a49) from land (#429a31) and sidewalks.
float mapped_asphalt(vec3 blurred_paint) {
 float g = pow(max(blurred_paint.g, 0.0), 1.0 / 2.2);
 return clamp((0.604 - g) / (0.604 - 0.290), 0.0, 1.0);
}

// Two soft tyre tracks per lane, measured from the pavement edge. The outer
// track of the kerbside lane is the clearest; the inner one merges toward the
// centre of a two-lane road because the coverage estimate saturates there.
float wheel_paths(float edge_m) {
 float outer = edge_m - 0.95;
 float inner = edge_m - 2.05;
 return clamp(exp(-outer * outer * 7.0) + 0.55 * exp(-inner * inner * 3.5), 0.0, 1.0);
}

// Old binder wears off the wheel paths and exposes lighter aggregate; fresh
// asphalt instead darkens slightly under tyre rubber. Amplitudes stay under
// ten percent so lane paint and the calibrated pavement level are preserved.
vec3 worn_asphalt(vec3 paved, float paths, float age) {
 float lighten = mix(-0.055, 0.095, age);
 float between = 1.0 - 0.035 * age * (1.0 - paths);
 return paved * (1.0 + paths * lighten) * between;
}

// Fresh asphalt is matte; polished wheel paths and the smoother broad patches
// of old pavement lower the roughness toward a sheen at grazing sun angles.
float asphalt_roughness(float fine, float broad, vec3 wear, float paths) {
 float age = wear.b;
 float base = 0.89 + (fine - 0.224) * 0.25 + (broad - 0.224) * 0.30 - wear.r * 0.095 - wear.g * 0.13;
 return clamp(base - paths * mix(0.05, 0.16, age) - age * 0.03, 0.64, 0.97);
}

// Normal mapping without precomputed tangents (Schueler, "Followup: Normal
// Mapping Without Precomputed Tangents"): the cotangent frame comes from
// screen-space derivatives of the view position and the metre coordinate, so
// the refined bank, crossfall and outcrop ground meshes need no tangent array.
vec3 cotangent_perturb(vec3 view_pos, vec3 surface_normal, vec2 uv, vec3 tangent_normal) {
 vec3 dp1 = dFdx(view_pos);
 vec3 dp2 = dFdy(view_pos);
 vec2 duv1 = dFdx(uv);
 vec2 duv2 = dFdy(uv);
 vec3 dp2perp = cross(dp2, surface_normal);
 vec3 dp1perp = cross(surface_normal, dp1);
 vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
 vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
 float invmax = inversesqrt(max(max(dot(t, t), dot(b, b)), 0.000000001));
 return normalize(t * invmax * tangent_normal.x + b * invmax * tangent_normal.y + surface_normal * tangent_normal.z);
}

// Aggregate micro-normals at two scales; mip filtering flattens them with
// distance and strength removes the rest, so the far road keeps the old look.
vec3 asphalt_bumps(vec3 view_pos, vec3 surface_normal, vec2 metres, float strength) {
 vec3 coarse = stochastic(asphalt_normal, metres / 0.55, false).xyz * 2.0 - 1.0;
 vec3 fine = texture(asphalt_normal, vec2(-metres.y, metres.x) / 1.70 + vec2(0.31, 0.07)).xyz * 2.0 - 1.0;
 vec3 tangent_normal = normalize(vec3((coarse.xy * 0.75 + fine.xy * 0.45) * strength, 1.0));
 return cotangent_perturb(view_pos, surface_normal, metres, tangent_normal);
}

// Mown verge: finer, more even blades with the yellow-green of cut grass. The
// weight is 1 beside the pavement and 0 a few metres out.
vec3 mown_turf(vec3 ground, vec3 fine_grass, float aerial_value, float weight, float near_view) {
 float local_light = clamp(0.70 + aerial_value * 1.15, 0.66, 1.22);
 vec3 lawn = fine_grass * vec3(1.00, 0.97, 0.66) * local_light;
 // Cut grass shows little of the tall-grass shadowing that darkens the
 // photograph, so blend toward its mean before mixing.
 lawn = mix(lawn, vec3(dot(lawn, LUMA)) * vec3(0.88, 1.0, 0.62), 0.35);
 return mix(ground, lawn, weight * near_view * 0.80);
}

// Field grass further out grows in clumps: the fine aggregate luminance
// stands in for shaded gaps between tussocks.
float clump_gaps(float fine) {
 return smoothstep(0.16, 0.27, fine);
}

vec3 ground_surface(vec3 aerial, vec3 grass, vec3 leaves, float turf_mask, float tree_mask,
                    float winter_mix, float fine, float broad, float near_view) {
 float value = dot(aerial, LUMA);
 // Preserve the local aerial color, moderating its baked direct light when
 // the real-time sun illuminates this close-range material a second time.
 float diffuse_value = 0.025 + value * 0.53;
 vec3 diffuse = aerial * mix(1.0, diffuse_value / max(value, 0.015), near_view * 0.88);
 float grit_gain = clamp(1.0 + (fine - 0.224) * 3.45 + (broad - 0.224) * 1.70, 0.60, 1.58);
 vec3 loose = diffuse * mix(1.0, grit_gain, near_view);
 vec3 turf_photo = grass * mix(vec3(0.56, 0.99, 0.62), vec3(0.88, 0.85, 0.85), winter_mix);
 float local_light = clamp(0.66 + value * 1.25, 0.63, 1.25);
 vec3 growing = mix(diffuse * clamp(dot(grass, LUMA) / 0.121, 0.55, 1.65),
                    turf_photo * local_light, near_view * 0.81);
 vec3 floor_color = leaves * mix(vec3(0.72, 0.96, 0.61), vec3(1.06, 0.98, 0.89), winter_mix);
 vec3 surface = mix(loose, growing, turf_mask);
 return mix(surface, floor_color * local_light, tree_mask * mix(0.38, 0.83, near_view));
}

vec3 relief_normal(vec3 view_position, vec3 surface_normal, float relief_m) {
 // Convert the physical relief into a screen-space gradient on the terrain
 // itself. Mip filtering of the source photographs removes distant glitter.
 vec3 px = dFdx(view_position);
 vec3 py = dFdy(view_position);
 vec3 bx = cross(py, surface_normal);
 vec3 by = cross(surface_normal, px);
 float determinant = dot(px, bx);
 vec3 gradient = (bx * dFdx(relief_m) + by * dFdy(relief_m)) * sign(determinant);
 gradient /= max(abs(determinant), 0.000001);
 return normalize(surface_normal - gradient / max(1.0, length(gradient) / 0.23));
}
