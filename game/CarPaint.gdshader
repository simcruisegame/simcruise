shader_type spatial;
render_mode cull_disabled;
// Metallic car paint for the 1999 roadster (CarFinish.gd). A base coat with
// mica flakes under a clearcoat with factory "orange peel": the two things that
// separate photographed paint from a perfect CG mirror. Flakes are cells in the
// body's own coordinates (they stay on the panel as it moves); each cell has
// its own tilt, so only some catch the sun at a given angle, which is the
// sparkle. Orange peel is a millimetre-scale ripple in the clearcoat normal
// that makes long reflections wobble slightly. Both fade out once a cell is
// smaller than a pixel, so the paint never shimmers at a distance.

uniform vec4 base_color : source_color = vec4(0.059, 0.231, 0.165, 1.0);
uniform float base_metallic = 0.42;
uniform float base_roughness = 0.36;
uniform float flake_cells_per_m = 1400.0;   // ~0.7 mm mica
uniform float flake_fraction = 0.35;
uniform float flake_tilt = 0.45;
uniform float peel_cells_per_m = 260.0;     // ~4 mm orange-peel wavelength
uniform float peel_strength = 0.06;

varying vec3 body;

float hash13(vec3 p) {
	p = fract(p * 0.1031);
	p += dot(p, p.zyx + 31.32);
	return fract((p.x + p.y) * p.z);
}
vec3 hash33(vec3 p) {
	p = fract(p * vec3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yxz + 33.33);
	return fract((p.xxy + p.yxx) * p.zyx);
}
float value_noise(vec3 p) {
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float n000 = hash13(i), n100 = hash13(i + vec3(1, 0, 0)), n010 = hash13(i + vec3(0, 1, 0)), n110 = hash13(i + vec3(1, 1, 0));
	float n001 = hash13(i + vec3(0, 0, 1)), n101 = hash13(i + vec3(1, 0, 1)), n011 = hash13(i + vec3(0, 1, 1)), n111 = hash13(i + vec3(1, 1, 1));
	return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y), mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

void vertex() {
	body = VERTEX;
}

void fragment() {
	ALBEDO = base_color.rgb;
	METALLIC = base_metallic;
	ROUGHNESS = base_roughness;
	SPECULAR = 0.5;
	CLEARCOAT = 1.0;
	CLEARCOAT_ROUGHNESS = 0.07;

	// Mica flakes: visible only while a cell covers more than ~a pixel.
	vec3 fp = body * flake_cells_per_m;
	float flake_px = length(fwidth(fp));
	float flake_vis = 1.0 - smoothstep(0.6, 1.6, flake_px);
	vec3 cell = floor(fp);
	float pick = step(1.0 - flake_fraction, hash13(cell + 17.0));
	vec3 tilt = (hash33(cell) * 2.0 - 1.0) * flake_tilt;
	vec3 flake_n = normalize(NORMAL + (VIEW_MATRIX * vec4(tilt, 0.0)).xyz);
	// A flake facing the half-vector reads as a bright metallic glint.
	vec3 l = normalize((VIEW_MATRIX * vec4(0.3, 0.9, 0.2, 0.0)).xyz);
	float facing = pow(max(dot(flake_n, normalize(l + VIEW)), 0.0), 48.0);
	float glint = pick * flake_vis * facing;
	ALBEDO = mix(ALBEDO, ALBEDO * 1.9 + vec3(0.04), glint);
	METALLIC = mix(METALLIC, 0.95, pick * flake_vis * 0.6);
	ROUGHNESS = mix(ROUGHNESS, 0.18, pick * flake_vis * 0.5);

	// Orange peel: a gentle ripple, faded with distance.
	vec3 pp = body * peel_cells_per_m;
	float peel_px = length(fwidth(pp));
	float peel_vis = 1.0 - smoothstep(0.35, 1.0, peel_px);
	float e = 0.35;
	float n0 = value_noise(pp);
	float nx = value_noise(pp + vec3(e, 0.0, 0.0));
	float ny = value_noise(pp + vec3(0.0, e, 0.0));
	vec2 slope = vec2(nx - n0, ny - n0) / e;
	NORMAL_MAP = vec3(0.5 + slope * peel_strength * peel_vis, 1.0);
	NORMAL_MAP_DEPTH = 1.0;
}
