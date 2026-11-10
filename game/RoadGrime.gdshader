shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_back;
// Road grime drawn over any car (GeometryInstance3D.material_overlay), so the
// 1999 roadster, generated cars and downloaded models all get it with no
// per-model work. CarWear.gd sets dirt (0 showroom .. 1 after a gravel road)
// and wet, and the car's inverse transform so every part, wheels included,
// shares one body frame (x right, y up from the ground, -z forward).
//   * film: grows from the sills up; mostly below the beltline
//   * spray: a fan behind and above each wheel, the way tyres throw it
//   * brake dust: darker grey on the wheels themselves
//   * rain: wet grime darkens and runs into vertical streaks

uniform float dirt = 0.15;
uniform float wet = 0.0;
uniform mat4 car_inverse = mat4(1.0);
uniform vec4 wheel_fl = vec4(-0.75, 0.3, -1.1, 0.31);   // xyz centre, w radius (body frame)
uniform vec4 wheel_fr = vec4(0.75, 0.3, -1.1, 0.31);
uniform vec4 wheel_rl = vec4(-0.75, 0.3, 1.1, 0.31);
uniform vec4 wheel_rr = vec4(0.75, 0.3, 1.1, 0.31);
uniform vec4 road_dust : source_color = vec4(0.47, 0.44, 0.40, 1.0);
uniform vec4 brake_dust : source_color = vec4(0.16, 0.14, 0.12, 1.0);

varying vec3 body;

float hash13(vec3 p) {
	p = fract(p * 0.1031);
	p += dot(p, p.zyx + 31.32);
	return fract((p.x + p.y) * p.z);
}
float noise(vec3 p) {
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(hash13(i), hash13(i + vec3(1, 0, 0)), f.x), mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y),
		mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x), mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float fbm(vec3 p) {
	return noise(p) * 0.55 + noise(p * 2.7) * 0.3 + noise(p * 7.3) * 0.15;
}
float spray(vec4 w, vec3 p) {
	// Behind the wheel (+z) and up the panel, fading with distance.
	vec3 d = p - w.xyz;
	float behind = smoothstep(-0.15, 0.35, d.z);
	float reach = 1.0 - smoothstep(w.w * 0.9, w.w * 3.2, length(vec2(d.y * 1.3, d.z)));
	float side = 1.0 - smoothstep(0.25, 0.6, abs(d.x));
	return behind * reach * side;
}
float on_wheel(vec4 w, vec3 p) {
	vec3 d = p - w.xyz;
	return (1.0 - smoothstep(w.w * 0.95, w.w * 1.05, length(d.yz))) * (1.0 - smoothstep(0.12, 0.2, abs(d.x)));
}

void vertex() {
	body = (car_inverse * (MODEL_MATRIX * vec4(VERTEX, 1.0))).xyz;
}

void fragment() {
	float grain = fbm(body * vec3(9.0, 14.0, 9.0));
	// Sill film: strongest at the bottom edge, gone by ~0.8 m.
	float film = 1.0 - smoothstep(0.10, 0.55, body.y + (grain - 0.5) * 0.2);
	float spray_m = max(max(spray(wheel_fl, body), spray(wheel_fr, body)), max(spray(wheel_rl, body), spray(wheel_rr, body)));
	float wheel_m = max(max(on_wheel(wheel_fl, body), on_wheel(wheel_fr, body)), max(on_wheel(wheel_rl, body), on_wheel(wheel_rr, body)));
	float mask = clamp(film * 0.6 + spray_m * 0.8, 0.0, 1.0) * (0.35 + 1.1 * grain * grain);
	// Rain: grime runs into vertical streaks and darkens.
	float streak = noise(vec3(body.x * 38.0, body.y * 2.0, body.z * 38.0));
	mask *= mix(1.0, 0.45 + 0.9 * streak, wet);
	float amount = clamp(mask * dirt * 0.75, 0.0, 0.6);
	float brake = wheel_m * clamp(dirt * 1.8 + 0.15, 0.0, 0.9) * (0.6 + 0.4 * grain);
	vec3 colour = mix(road_dust.rgb * (0.85 + 0.3 * grain), brake_dust.rgb, step(0.01, brake) * 0.85);
	colour *= mix(1.0, 0.55, wet);
	ALBEDO = colour;
	ROUGHNESS = mix(0.92, 0.35, wet);
	METALLIC = 0.0;
	SPECULAR = 0.3;
	ALPHA = max(amount, brake);
}
