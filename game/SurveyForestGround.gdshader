shader_type spatial;
render_mode cull_disabled, fog_disabled;
uniform vec3 haze_color : source_color = vec3(.74,.77,.74);
uniform float extinction=.00014;
void fragment(){
 vec3 diffuse=pow(max(COLOR.rgb,vec3(.004)),vec3(2.2));
 ALBEDO=diffuse*.78;
 ROUGHNESS=mix(.96,.70,COLOR.a);
 SPECULAR=mix(.1,.18,COLOR.a);
 FOG=vec4(haze_color,1.-exp(-extinction*length(VERTEX)));
}
