shader_type spatial;
uniform vec4 surface_color : source_color = vec4(0.6,0.55,0.49,1.0);
uniform sampler2D facade : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform bool use_facade = false;
uniform bool roof = false;
uniform int kind = 0;
uniform float roughness = 0.9;
varying vec2 metric;
void vertex() {
 vec2 along = vec2(-NORMAL.z,NORMAL.x);
 metric = roof ? VERTEX.xz : vec2(dot(VERTEX.xz,along),VERTEX.y);
}
float joint(float pos,float period,float gap){
 float distance_m=abs(fract(pos/period+0.5)-0.5)*period;
 float spread=max(fwidth(pos),0.001);
 return (1.0-smoothstep(0.0,spread+gap,distance_m))*(1.0-smoothstep(gap*2.0,period*0.40,spread));
}
void fragment(){
 vec3 base=surface_color.rgb;
 float mortar=0.0;
 if(kind==1 || kind==6){
  float row=kind==1 ? 0.075 : 0.17;
  float span=kind==1 ? 0.215 : 0.30;
  float offset=mod(floor(metric.y/row),2.0)*span*0.5;
  float line=max(joint(metric.x+offset,span,0.006),joint(metric.y,row,0.005));
  mortar=line;
  base=mix(base,kind==1 ? mix(base,vec3(0.30,0.27,0.23),0.46) : base*0.59,line);
 }else if(kind==2){
  base*=1.0-0.25*joint(metric.y,0.153,0.009);
 }else if(kind==3){
  base*=1.0-0.20*joint(metric.x,0.42,0.01);
 }
 if(use_facade){
  // Retain glazing, frames and floor breaks from the ordinary detail atlas.
  vec3 detail=texture(facade,UV).rgb;
  float luma=dot(detail,vec3(0.2126,0.7152,0.0722));
  vec3 tint=clamp(detail/max(0.035,luma),vec3(0.76),vec3(1.29));
  base*=tint*clamp(luma/0.30,0.24,1.5);
 }
 ALBEDO=base;
 METALLIC=kind==3 ? 0.35 : 0.0;
 ROUGHNESS=min(1.0,roughness+mortar*0.06);
 SPECULAR=kind==4 ? 0.48 : 0.27;
}
