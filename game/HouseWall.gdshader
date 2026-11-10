shader_type spatial;
render_mode cull_disabled;
// Exterior walls of houses (WorldTile.survey_building), in world space so no
// wall needs UVs and every footprint shape works anywhere.
//   style 0  lap siding: 6.5 in (0.165 m) boards. Each board's lower edge laps
//            over the one below, which casts a fine shadow line, and its face
//            tilts outward, so light catches each board slightly differently.
//   style 1  brick: US modular brick, 0.203 x 0.067 m courses (with 10 mm
//            mortar) in running bond, per-brick tone variation.
// Fine detail fades out once a board or brick course is below a pixel, so
// distant houses keep their average colour without shimmer. Colours come from
// a palette of common US exterior paints and brick (Drive.gd sets one per key).

uniform vec4 paint : source_color = vec4(0.79, 0.77, 0.72, 1.0);
uniform int style = 0;
uniform float board_m = 0.165;

varying vec3 world;
varying vec3 world_normal;

float h11(float x) { return fract(sin(x * 127.1) * 43758.5453); }
float h21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void vertex() {
	world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	world_normal = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

void fragment() {
	vec3 colour = paint.rgb;
	float rough = 0.78;
	vec2 slope = vec2(0.0);
	if (style == 0) {
		float yb = world.y / board_m;
		float px = fwidth(yb);
		float detail = 1.0 - smoothstep(0.12, 0.4, px);
		float f = fract(yb);
		// Shadow under each lap and a lighter top edge.
		float shadow = mix(0.70, 1.0, smoothstep(0.0, 0.14, f));
		float tone = 1.0 + (h11(floor(yb)) - 0.5) * 0.05;
		colour *= mix(0.94, shadow * tone, detail);   // far: the laps' average shade
		// Board face tilts out at the bottom: the normal leans down.
		slope = vec2(0.0, -0.22 * detail);
		rough = 0.72;
	} else {
		vec3 n = normalize(world_normal);
		vec3 along = normalize(cross(vec3(0.0, 1.0, 0.0), n) + vec3(1e-5));
		float u = dot(world, along) / 0.213;
		float v = world.y / 0.0767;
		float row = floor(v);
		u += mod(row, 2.0) * 0.5;
		vec2 cell = vec2(floor(u), row);
		vec2 f = vec2(fract(u), fract(v));
		float px = max(fwidth(u), fwidth(v));
		float detail = 1.0 - smoothstep(0.18, 0.5, px);
		float mortar = 1.0 - smoothstep(0.0, 0.06, min(min(f.x, 1.0 - f.x) * 0.213 / 0.0767, min(f.y, 1.0 - f.y)) - 0.04);
		vec3 brick = colour * (0.85 + 0.3 * h21(cell));
		vec3 joint = vec3(0.62, 0.60, 0.56);
		vec3 detailed = mix(brick, joint, mortar);
		vec3 average = mix(colour, joint, 0.18);
		colour = mix(average, detailed, detail);
		rough = 0.9;
	}
	ALBEDO = colour;
	ROUGHNESS = rough;
	SPECULAR = 0.35;
	NORMAL_MAP = vec3(0.5 + slope * 0.5, 1.0);
}
