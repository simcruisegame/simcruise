shader_type spatial;
render_mode unshaded, cull_disabled, fog_disabled;
uniform sampler2D photo : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D middle_photo : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform bool has_middle = false;
uniform bool has_snow=false;
uniform int ridge_count=0;
uniform bool has_fine_0=false;
uniform bool has_fine_1=false;
uniform sampler2D fine_snow_0 : filter_linear_mipmap,repeat_disable;
uniform sampler2D fine_snow_1 : filter_linear_mipmap,repeat_disable;
uniform vec4 ridge_uv_0;
uniform vec4 ridge_uv_1;
uniform sampler2D ridge_image_0 : source_color,filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D ridge_image_1 : source_color,filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D snow_index:filter_linear,repeat_disable;
uniform sampler2D mapped_roads : filter_linear_mipmap_anisotropic, repeat_disable;
uniform bool has_roads = false;
uniform sampler2D sea_mask : filter_linear_mipmap,repeat_disable;
uniform bool has_ocean = false;
uniform sampler2DArray detail_colors : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform vec4 detail_uv[4];
uniform int detail_count = 0;
uniform bool has_photo = false;
uniform vec3 sun_from = vec3(-0.6,0.7,-0.2);
uniform float visibility_m = 55000.0;
varying vec3 ground_position;
void vertex() {
 UV = VERTEX.xz / 90000.0 + vec2(0.5);
 vec2 delta = (MODEL_MATRIX*vec4(VERTEX,1.)).xz - CAMERA_POSITION_WORLD.xz;
 float d = length(delta);
 // Earth curvature and a gradual underlap beneath the detailed driving tiles.
 VERTEX.y -= dot(delta,delta)/12742000.0 + mix(65.0,0.0,smoothstep(600.0,3000.0,d));
 ground_position = (MODEL_MATRIX*vec4(VERTEX,1.)).xyz;
}
vec3 refine_snow(vec3 coarse,vec2 st,sampler2D classification) {
 if(min(min(st.x,1.-st.x),min(st.y,1.-st.y))<=0.)return coarse;
 vec4 sample_value=texture(classification,st);
 float snow=clamp(sample_value.r/max(.001,sample_value.a),0.,1.);
 return vec3(mix(coarse.x,snow,sample_value.a),sample_value.g/max(.001,sample_value.b),sample_value.b);
}
void fragment() {
 float d = length(ground_position-CAMERA_POSITION_WORLD);
 if(d < 550.0) discard;
 vec3 n=normalize(cross(dFdx(ground_position),dFdy(ground_position)));
 n*=sign(n.y);
 vec3 color = has_photo?texture(photo,UV).rgb:vec3(.17,.16,.14);
 for(int layer=0;layer<4;layer++) {
  if(layer>=detail_count)break;
  vec2 st=(UV-detail_uv[layer].xy)/detail_uv[layer].zw;
  float edge=min(min(st.x,1.0-st.x),min(st.y,1.0-st.y));
  if(edge>0.0)color=mix(color,texture(detail_colors,vec3(st,float(layer))).rgb,smoothstep(0.0,0.035,edge));
 }
 if(ridge_count>0) {
  vec2 st=(UV-ridge_uv_0.xy)/ridge_uv_0.zw;
  float edge=min(min(st.x,1.-st.x),min(st.y,1.-st.y));
  if(edge>0.)color=mix(color,texture(ridge_image_0,st).rgb,smoothstep(0.,.01,edge));
 }
 if(ridge_count>1) {
  vec2 st=(UV-ridge_uv_1.xy)/ridge_uv_1.zw;
  float edge=min(min(st.x,1.-st.x),min(st.y,1.-st.y));
  if(edge>0.)color=mix(color,texture(ridge_image_1,st).rgb,smoothstep(0.,.01,edge));
 }
 if(has_middle){
  vec2 st=(UV-vec2(.5))*(90000./13000.)+vec2(.5);
  float edge=min(min(st.x,1.-st.x),min(st.y,1.-st.y));
  if(edge>0.)color=mix(color,texture(middle_photo,st).rgb,smoothstep(0.,.065,edge));
 }
 if(has_roads){
  vec2 st=(UV-vec2(.5))*(90000./13000.)+vec2(.5);
  if(all(greaterThan(st,vec2(0.)))&&all(lessThan(st,vec2(1.)))){
   float w=texture(mapped_roads,st).r;
   color=mix(color,vec3(.15,.145,.14),w*.93);
  }
 }
 // The orthophoto already contains some shading; preserve its broad ground tone.
 float shade = clamp(1.0+0.45*(dot(n,sun_from)-sun_from.y),0.65,1.22);
 if(has_snow){
  float index=texture(snow_index,UV).r;
  vec3 patch=vec3(smoothstep(.38,.76,index),0.,0.);
  if(ridge_count>0 && has_fine_0)patch=refine_snow(patch,(UV-ridge_uv_0.xy)/ridge_uv_0.zw,fine_snow_0);
  if(ridge_count>1 && has_fine_1)patch=refine_snow(patch,(UV-ridge_uv_1.xy)/ridge_uv_1.zw,fine_snow_1);
  // Coarse observations are area evidence. Preserve mountain facets, existing
  // orthophoto shading and visible shade variations within snowy terrain.
  float luminance=dot(color,vec3(.2126,.7152,.0722));
  float relief=clamp(.25+1.3*sqrt(luminance),.32,1.08);
  float diffuse=.26+.74*max(0.,dot(n,sun_from));
  vec3 lit_snow=vec3(.76,.83,.90)*relief*diffuse;
  vec3 measured_snow=vec3(.93,.97,1.0)*patch.y*relief;
  lit_snow=mix(lit_snow,measured_snow,patch.z);
  color=mix(color,lit_snow/max(shade,.01),patch.x*.95);
 }
 if(has_ocean){
  float ocean=smoothstep(.10,.90,texture(sea_mask,UV).r);
  color=mix(color,vec3(.043,.13,.18),ocean);shade=mix(shade,1.,ocean);
 }
 float haze = 1.0-exp(-d/visibility_m);
 ALBEDO = mix(color*shade,vec3(.36,.43,.52),haze);
}
