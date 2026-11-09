shader_type spatial;
render_mode cull_disabled, world_vertex_coords;

// Procedural blade-clump card drawn by Rangeland.gd; alpha is the silhouette.
uniform sampler2D clump : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
// Tip displacement as a fraction of plant height during a full gust.
uniform float sway : hint_range(0.0, 0.3) = 0.06;
uniform vec2 wind = vec2(0.8, 0.5);
// Plants dither out across this ring so the 96 m placement radius never pops.
uniform float fade_start = 78.0;
uniform float fade_end = 94.0;
uniform vec3 transmission = vec3(0.42, 0.55, 0.14);

varying float eye_metres;

void vertex() {
    // UV.y runs from 0 at the tip to 1 at the root: tips bend, roots stay put.
    float bend = 1.0 - UV.y;
    float height_m = length(MODEL_MATRIX[1].xyz);
    float phase = INSTANCE_CUSTOM.r * 6.2831853 + dot(VERTEX.xz, vec2(0.21, 0.17));
    float gust = sin(TIME * 1.7 + phase) * 0.6 + sin(TIME * 2.9 + phase * 1.7) * 0.4;
    VERTEX.xz += wind * (gust * sway * height_m * bend * bend);
    eye_metres = length(VERTEX - CAMERA_POSITION_WORLD);
}

void fragment() {
    vec4 card = texture(clump, UV);
    // COLOR carries the per-plant tint taken from the aerial pixel.
    ALBEDO = card.rgb * COLOR.rgb;
    ALPHA = card.a;
    ALPHA_SCISSOR_THRESHOLD = 0.42;
    // Cards light like the ground they stand on: lean the normal toward world up.
    vec3 up_view = normalize((VIEW_MATRIX * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
    NORMAL = normalize(mix(NORMAL, up_view, 0.65));
    ROUGHNESS = 0.92;
    SPECULAR = 0.08;
    BACKLIGHT = transmission * (0.35 * card.a);
    // Orthographic passes are the directional shadow maps: keep them intact.
    if (PROJECTION_MATRIX[3][3] < 0.5) {
        float fade = 1.0 - smoothstep(fade_start, fade_end, eye_metres);
        float dither = fract(52.9829189 * fract(dot(FRAGCOORD.xy, vec2(0.06711056, 0.00583715))));
        if (fade < dither) {
            discard;
        }
    }
}
