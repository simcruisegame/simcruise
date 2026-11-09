shader_type spatial;
render_mode cull_disabled;

uniform sampler2D foliage : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
// Light passing through a thin leaf is yellow-green. The Mobile renderer's
// default light pass adds BACKLIGHT where the sun is behind the surface
// (Godot 4.7 scene_forward_lights_inc.glsl: diffuse += light_color *
// (1/PI - brdf_NL) * backlight * attenuation; scene_forward_mobile.glsl passes
// it under LIGHT_BACKLIGHT_USED), so no custom light() function is needed.
uniform vec3 transmission = vec3(0.58, 0.72, 0.20);
uniform float transmission_strength : hint_range(0.0, 1.0) = 0.45;
// Sky light rarely reaches the inner crown; occlusion grows toward the trunk.
// Mobile applies AO to ambient light and AO_LIGHT_AFFECT to direct light.
uniform float inner_occlusion : hint_range(0.0, 1.0) = 0.55;
// Crown extents in mesh units; Grove.gd fits them per material from the mesh.
uniform vec3 crown_centre = vec3(0.0, 0.60, 0.0);
uniform vec3 crown_radii = vec3(0.58, 0.44, 0.58);
// The cluster photographs average 0.077 linear luma; real summer leaves
// reflect about twice that, so the lit shell gains while occlusion keeps the
// interior dark enough to read as a volume rather than a flat blob.
uniform float albedo_gain = 1.35;
// Leaves closer to the eye than fade_end dither away so a crown overhead
// shows the branches and sky through it instead of a solid lid.
uniform float fade_start = 2.0;
uniform float fade_end = 6.0;

varying vec3 crown_normal;
varying float pigment;
varying float depth_inside;
varying float eye_metres;

void vertex() {
    crown_normal = (VIEW_MATRIX * vec4(MODEL_NORMAL_MATRIX * NORMAL, 0.0)).xyz;
    pigment = INSTANCE_CUSTOM.r;
    vec3 local = (VERTEX - crown_centre) / max(crown_radii, vec3(0.05));
    depth_inside = 1.0 - clamp(length(local), 0.0, 1.0);
    eye_metres = length((MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz - CAMERA_POSITION_WORLD);
}

void fragment() {
    vec4 leaf = texture(foliage, UV);
    NORMAL = normalize(crown_normal);
    // Two per-tree hashes: a warm-to-cool hue turn and a brightness spread.
    float hue_turn = fract(pigment * 7.31 + 0.17);
    vec3 tint = mix(vec3(0.78, 0.86, 0.66), vec3(1.08, 1.00, 0.72), hue_turn) * mix(0.84, 1.10, pigment);
    float occlusion = 1.0 - inner_occlusion * smoothstep(0.0, 0.85, depth_inside);
    ALBEDO = leaf.rgb * COLOR.rgb * tint * albedo_gain * mix(1.0, occlusion, 0.35);
    AO = occlusion;
    AO_LIGHT_AFFECT = 0.30;
    float leaf_light = clamp(dot(leaf.rgb, vec3(0.2126, 0.7152, 0.0722)) / 0.08, 0.3, 1.6);
    BACKLIGHT = transmission * (transmission_strength * leaf_light * occlusion * COLOR.r);
    ALPHA = leaf.a;
    ALPHA_SCISSOR_THRESHOLD = .28;
    ROUGHNESS = .88;
    SPECULAR = .14;
    // Orthographic passes are the directional shadow maps: keep them dense.
    if (PROJECTION_MATRIX[3][3] < 0.5) {
        float fade = smoothstep(fade_start, fade_end, eye_metres);
        float dither = fract(52.9829189 * fract(dot(FRAGCOORD.xy, vec2(0.06711056, 0.00583715))));
        if (fade < dither) {
            discard;
        }
    }
}
