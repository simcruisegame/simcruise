shader_type spatial;
render_mode cull_back;
uniform sampler2D stone_a : source_color,filter_linear_mipmap_anisotropic,repeat_disable;
uniform sampler2D stone_b : source_color,filter_linear_mipmap_anisotropic,repeat_disable;
uniform sampler2D grain : filter_linear_mipmap_anisotropic,repeat_enable;
varying vec3 n0;
varying vec3 point;
void vertex(){
 n0=normalize(MODEL_NORMAL_MATRIX*NORMAL);
 point=VERTEX;
}
void fragment(){
 vec3 n=normalize(n0);vec3 w=pow(abs(n),vec3(2.));w/=max(dot(w,vec3(1.)),.0001);
 float small=dot(w,vec3(texture(grain,point.yz/vec2(3.5,4.2)).r,texture(grain,point.xz/vec2(4.3,2.8)).r,texture(grain,point.xy/vec2(3.9,3.4)).r));
 float mottling=dot(w,vec3(texture(grain,point.yz/vec2(9.3,8.1)).r,texture(grain,point.xz/vec2(9.3,6.2)).r,texture(grain,point.xy/vec2(9.2,7.4)).r));
 vec3 fallback=vec3(.24,.218,.19)*exp((small-.502)*1.28+(mottling-.502)*.65);
 vec4 a=texture(stone_a,UV);vec4 b=texture(stone_b,UV2);
 float va=smoothstep(.15,.82,a.a)*COLOR.r;float vb=smoothstep(.15,.82,b.a)*COLOR.g*(1.-.72*va);
 float invert=.78+.52*max(dot(n,vec3(.162,.898,.409)),0.);
 vec3 measured=(a.rgb*va+b.rgb*vb)/max(va+vb,.0001);
 ALBEDO=mix(fallback,clamp(measured/invert,vec3(.02),vec3(.86)),clamp(va+vb,0.,1.));
 ROUGHNESS=.96;
}
