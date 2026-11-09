shader_type spatial;
render_mode cull_disabled;
uniform sampler2D bark : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
varying vec3 metres;
void vertex(){
 metres=VERTEX*vec3(length(MODEL_MATRIX[0].xyz),length(MODEL_MATRIX[1].xyz),length(MODEL_MATRIX[2].xyz));
}
void fragment(){
 vec3 photo_x=texture(bark,metres.zy/vec2(.64,1.4)).rgb;
 vec3 photo_z=texture(bark,metres.xy/vec2(.64,1.4)).rgb;
 float blend=abs(metres.x)/(abs(metres.x)+abs(metres.z)+.001);
 ALBEDO=mix(photo_z,photo_x,blend)*.68;
 ROUGHNESS=.98;SPECULAR=.16;
}
