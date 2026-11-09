shader_type spatial;
render_mode cull_back;
uniform float eave;
varying vec3 local;
void vertex(){local=VERTEX;}
void fragment(){
 float z=(eave-local.y-1.63147)/3.98443;
 float grad=clamp(fwidth(z),.001,.15);
 float f=fract(z);
 float glass=smoothstep(0.,grad,f)*(1.-smoothstep(0.611-grad,0.611+grad,f))*smoothstep(-grad,grad,z);
 // Mullion size is subordinate and approximate. Do not replicate a photographed reflection.
 float dist_along=UV.x;
 float mullion=1.-smoothstep(.012,.027,abs(fract(dist_along/1.36+.5)-.5));
 vec3 spandrel=vec3(.43,.378,.36);
 vec3 pane=vec3(.027,.044,.058);
 ALBEDO=mix(spandrel,mix(pane,vec3(.065),mullion*.65),glass);
 ROUGHNESS=mix(.86,.22,glass);
 SPECULAR=mix(.25,.55,glass);
 METALLIC=0.;
}