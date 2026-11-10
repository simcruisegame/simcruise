shader_type spatial;
render_mode cull_disabled;

uniform sampler2D ortho : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform bool has_image = false;
uniform vec2 parent_quadrant;
uniform float tile_side = 480.;
uniform vec4 fallback : source_color = vec4(0.47,0.48,0.46,1.);
varying vec2 photo_uv;
varying vec3 local_pos;
varying vec3 local_normal;

float shingle_hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void vertex(){
 photo_uv=(VERTEX.xz/tile_side+parent_quadrant)*0.25;
 local_pos=VERTEX;
 local_normal=NORMAL;
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
 // Pitched roofs: 3-tab asphalt shingles over the observed colour. Courses
 // follow the slope (5 5/8 in exposure), tabs are 1/3 m with the cut-outs
 // offset every other course; each course's lower edge casts a shadow line.
 // The true facet slope from the geometry itself: some roof builders pass
 // an upward vertex normal for every face, so the vertex normal is not trusted.
 vec3 n=normalize(cross(dFdx(local_pos),dFdy(local_pos)));
 if(n.y<0.)n=-n;
 if(abs(n.y)<0.995){
  vec2 down=normalize(n.xz+vec2(1e-5));
  vec2 across=vec2(-down.y,down.x);
  float along_slope=dot(local_pos.xz,down)/max(abs(n.y),0.2);
  float course=along_slope/0.143;
  float row=floor(course);
  float tab=dot(local_pos.xz,across)/0.333+mod(row,2.0)*0.5;
  float px=max(fwidth(course),fwidth(tab));
  float detail=1.0-smoothstep(0.3,0.8,px);
  float f=fract(course);
  float shadow=mix(0.62,1.0,smoothstep(0.0,0.12,1.0-f));
  float slot=1.0-0.35*(1.0-smoothstep(0.0,0.035,abs(fract(tab)-0.5)-0.0))*step(0.55,1.0-f);
  float granule=0.9+0.2*shingle_hash(vec2(floor(tab),row));
  // Far away the courses blur into their average shade (~0.9), not the bare photo.
  color*=mix(0.9,shadow*slot*granule,detail);
 }
 ALBEDO=color;
 ROUGHNESS=0.94;
}
