// Stochastic texturing: a tiling texture without visible repetition.
//
// A plain texture(t, metres / period) repeats every `period` metres, and the eye
// finds that grid at once on a large open surface: the main reason open ground
// and asphalt read as "old game". Here the plane is covered by a hexagonal grid
// of cells (the triangle-grid construction of Heitz & Neyret 2018, "High-
// performance by-example noise using a histogram-preserving blending operator",
// as simplified by Mikkelsen 2022, "Practical real-time hex-tiling", JCGT 11(2));
// written from the papers, no code copied. Each cell reads the texture at its
// own random offset (and, for textures with no direction, its own rotation). A
// point is blended from its three surrounding cells with sharpened barycentric
// weights, and the blend is re-centred on the texture mean and rescaled by the
// weights' norm, so contrast and colour statistics stay those of the source
// instead of washing out where cells overlap.
//
// Determinism: offsets hash the integer cell coordinates, so the same place
// always looks the same. Derivatives come from the unrotated coordinate and are
// rotated with it (textureGrad), so mip selection and anisotropy stay correct
// across cell borders.

vec2 stochastic_hash2(ivec2 p) {
	vec2 q = vec2(float(p.x), float(p.y));
	q = vec2(dot(q, vec2(127.1, 311.7)), dot(q, vec2(269.5, 183.3)));
	return fract(sin(q) * 43758.5453);
}

// Barycentric weights and vertex ids of the triangle (of the hex lattice's dual)
// containing st. Cell size is about one texture period.
void stochastic_grid(vec2 st, out vec3 w, out ivec2 v1, out ivec2 v2, out ivec2 v3) {
	st *= 3.4641016; // 2 * sqrt(3)
	vec2 skewed = vec2(st.x, -0.57735027 * st.x + 1.15470054 * st.y);
	ivec2 base = ivec2(floor(skewed));
	vec3 t = vec3(fract(skewed), 0.0);
	t.z = 1.0 - t.x - t.y;
	float s = step(0.0, -t.z);
	float s2 = 2.0 * s - 1.0;
	w = vec3(-t.z * s2, s - t.y * s2, s - t.x * s2);
	int si = int(s);
	v1 = base + ivec2(si, si);
	v2 = base + ivec2(si, 1 - si);
	v3 = base + ivec2(1 - si, si);
}

vec2 stochastic_uv(vec2 uv, ivec2 v, bool rotate, inout vec2 dx, inout vec2 dy) {
	vec2 h = stochastic_hash2(v);
	if (rotate) {
		float a = h.x * 6.2831853;
		mat2 r = mat2(vec2(cos(a), sin(a)), vec2(-sin(a), cos(a)));
		dx = r * dx; dy = r * dy;
		return r * uv + h * 7.31;
	}
	return uv + h * 7.31;
}

// The texture at uv (in periods) with no visible repeat. rotate=false for
// directional texture (normal maps, lane-aligned wear).
vec4 stochastic(sampler2D tex, vec2 uv, bool rotate) {
	vec3 w; ivec2 v1; ivec2 v2; ivec2 v3;
	stochastic_grid(uv, w, v1, v2, v3);
	vec2 dx0 = dFdx(uv), dy0 = dFdy(uv);
	vec2 dx1 = dx0, dy1 = dy0, dx2 = dx0, dy2 = dy0, dx3 = dx0, dy3 = dy0;
	vec2 u1 = stochastic_uv(uv, v1, rotate, dx1, dy1);
	vec2 u2 = stochastic_uv(uv, v2, rotate, dx2, dy2);
	vec2 u3 = stochastic_uv(uv, v3, rotate, dx3, dy3);
	vec4 c1 = textureGrad(tex, u1, dx1, dy1);
	vec4 c2 = textureGrad(tex, u2, dx2, dy2);
	vec4 c3 = textureGrad(tex, u3, dx3, dy3);
	// Sharpened weights keep most of each cell as one clean sample.
	vec3 k = w * w * w;
	k /= max(k.x + k.y + k.z, 1e-5);
	// Histogram-preserving blend around the texture mean (its smallest mip).
	vec4 mean = textureLod(tex, vec2(0.5), 12.0);
	vec4 mixed = c1 * k.x + c2 * k.y + c3 * k.z;
	float norm = inversesqrt(max(dot(k, k), 1e-5));
	return clamp(mean + (mixed - mean) * min(norm, 1.6), 0.0, 1.0);
}
