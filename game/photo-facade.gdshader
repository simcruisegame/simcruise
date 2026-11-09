shader_type spatial;
render_mode cull_disabled;
uniform sampler2D observed : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D masonry : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec2 face_size;
void fragment(){
 vec4 wall = texture(observed,UV);
 ALBEDO = mix(texture(masonry,UV*face_size/2.0).rgb,wall.rgb,wall.a);
 ROUGHNESS = .89;
 SPECULAR = .21;
}
