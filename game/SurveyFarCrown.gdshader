shader_type spatial;
render_mode cull_disabled, fog_disabled;
uniform sampler2D foliage : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform float extinction = .00014;
uniform vec3 haze_color : source_color = vec3(.74,.77,.74);
varying vec3 measured_color;
varying vec3 crown_normal;
void vertex() {
 measured_color=INSTANCE_CUSTOM.rgb;
 crown_normal=(VIEW_MATRIX*vec4(MODEL_NORMAL_MATRIX*NORMAL,0.)).xyz;
}
void fragment(){
 vec4 needles=texture(foliage,UV);
 vec3 diffuse=pow(max(measured_color,vec3(.004)),vec3(2.2));
 float branch_light=clamp(dot(needles.rgb,vec3(.21,.72,.07))/.145,.28,1.5);
 ALBEDO=diffuse*mix(vec3(1.),COLOR.rgb,.38)*mix(1.,branch_light,.65);
 NORMAL=normalize(crown_normal);
 ALPHA=needles.a;
 ALPHA_SCISSOR_THRESHOLD=.28;
 ROUGHNESS=.98;
 SPECULAR=.08;
 FOG=vec4(haze_color,1.-exp(-extinction*length(VERTEX)));
}
