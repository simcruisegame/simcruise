shader_type spatial;
render_mode cull_disabled;

uniform sampler2D field : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D range_map : filter_nearest, repeat_disable;
uniform sampler2D fallback_texture : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform bool has_fallback_texture = false;
uniform bool preserve_wall_value = false;
uniform vec4 tint : source_color;
uniform vec3 optical_center;
uniform vec3 camera_right;
uniform vec3 camera_up;
uniform vec3 camera_forward;
uniform vec2 image_size;
uniform vec2 principal;
uniform float focal_pixels;
varying vec3 local_position;
varying vec3 local_normal;

void vertex() {
 local_position = VERTEX;
 local_normal = NORMAL;
}

void fragment() {
 vec3 fallback = tint.rgb;
 if (has_fallback_texture) {
  vec3 original = texture(fallback_texture, UV).rgb;
  if (preserve_wall_value) {
   // Keep the existing unobserved wall pattern in the documented stone finish.
   // The observation never repeats a neighboring building bay onto this area.
   float value = dot(original, vec3(0.2126,0.7152,0.0722));
   fallback *= clamp(value / 0.25, 0.21, 1.48);
  } else {
   fallback *= original;
  }
 }
 vec3 delta = local_position - optical_center;
 float depth = dot(delta, camera_forward);
 vec2 sensor = vec2(dot(delta, camera_right), -dot(delta, camera_up));
 vec2 uv = (principal + focal_pixels * sensor / max(depth, 0.1)) / image_size;
 vec4 color = texture(field, uv);
 vec2 packed = floor(texture(range_map, uv).rg * 255.0 + 0.5);
 float surveyed_depth = dot(packed, vec2(256.0, 1.0)) * 0.01;
 float visible = 0.0;
 if (depth > 0.1 && surveyed_depth > 0.0 && all(greaterThan(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0)))) {
  visible = 1.0 - smoothstep(0.22, 0.55, abs(depth - surveyed_depth));
  visible *= smoothstep(0.26, 0.50, abs(dot(normalize(local_normal), normalize(delta))));
 }
 ALBEDO = mix(fallback, color.rgb, visible * color.a);
 ROUGHNESS = 0.89;
 SPECULAR = 0.20;
}
