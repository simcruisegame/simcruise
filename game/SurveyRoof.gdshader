shader_type spatial;
render_mode cull_disabled;

uniform sampler2D ortho : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform bool has_image = false;
uniform vec2 parent_quadrant;
uniform float tile_side = 480.;
uniform vec4 fallback : source_color = vec4(0.47,0.48,0.46,1.);
varying vec2 photo_uv;

void vertex(){
 photo_uv=(VERTEX.xz/tile_side+parent_quadrant)*0.25;
}

void fragment(){
 vec3 color=fallback.rgb;
 if(has_image && all(greaterThanEqual(photo_uv,vec2(0.))) && all(lessThanEqual(photo_uv,vec2(1.)))){
  vec3 observed=texture(ortho,photo_uv).rgb;
  float brightness=dot(observed,vec3(0.2126,0.7152,0.0722));
  // Dark cast shadows and green canopy pixels seldom describe the roof finish.
  float canopy=smoothstep(0.12,0.36,(observed.g-0.5*(observed.r+observed.b))/max(0.02,brightness));
  float weight=smoothstep(0.016,0.075,brightness)*(1.-canopy);
  color=mix(color,observed,weight*0.86);
 }
 ALBEDO=color;
 ROUGHNESS=0.94;
}
