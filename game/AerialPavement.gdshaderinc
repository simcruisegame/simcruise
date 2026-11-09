// Use the broad photographed tone only on plausible, locally even paved road.
// Aerial colors include exposure and baked light, so they only constrain a
// bounded diffuse estimate. High-frequency aggregate and wear stay procedural.
vec3 aerial_pavement(vec3 base, vec3 pixel, vec2 uv) {
 if (!imagery || !has_edges) { return base; }
 // Street observations are the better cue from driving height wherever their
 // measured sequence is close enough. Start using aerial tone at its boundary.
 float beyond_photo = 1.0;
 if (photo_strength.x > 0.0) {
  float distance_m = meters * min(segment_distance(uv, photo_point_0, photo_point_1),
                                  segment_distance(uv, photo_point_1, photo_point_2));
  beyond_photo = smoothstep(65.0, 95.0, distance_m);
  if (beyond_photo <= 0.0) { return base; }
 }
 vec2 edge = texture(road_edges, uv).rg;
 float mapped = smoothstep(0.48, 0.78, edge.r) * smoothstep(0.21, 0.57, edge.g);
 if (mapped <= 0.0) { return base; }

 vec3 neighborhood = textureLod(ortho, (uv + quadrant) * 0.25, 1.0).rgb;
 float local_light = dot(pixel, LUMA);
 float broad_light = dot(neighborhood, LUMA);
 float chroma = max(max(pixel.r,pixel.g),pixel.b) - min(min(pixel.r,pixel.g),pixel.b);
 float broad_chroma = max(max(neighborhood.r,neighborhood.g),neighborhood.b) - min(min(neighborhood.r,neighborhood.g),neighborhood.b);
 float neutral = 1.0 - smoothstep(0.19,0.44,max(chroma/max(local_light,0.035),broad_chroma/max(broad_light,0.035)));
 float even_light = 1.0 - smoothstep(0.16,0.41,abs(local_light-broad_light)/max(broad_light,0.055));
 float usable = smoothstep(0.090,0.16,min(local_light,broad_light)) * (1.0-smoothstep(0.46,0.63,max(local_light,broad_light)));
 float confidence = mapped * neutral * even_light * usable * beyond_photo;
 float diffuse = clamp(0.012 + broad_light*0.58,0.047,0.225);
 float old_light = clamp(0.022 + local_light*0.091,0.027,0.106);
 return base*mix(1.0,clamp(diffuse/old_light,1.0,3.70),confidence*0.88);
}

// Pavement age read from the orthophoto where it is usable: bright, neutral
// asphalt has lost its binder and reads lighter and rougher; dark asphalt is
// recent. Returns the wear-texture age when the photograph cannot be trusted,
// so the procedural variation and this measured cue blend into one estimate.
float aerial_asphalt_age(float procedural_age, vec3 pixel, vec2 uv) {
 if (!imagery || !has_edges) { return procedural_age; }
 vec2 edge = texture(road_edges, uv).rg;
 float mapped = smoothstep(0.48, 0.78, edge.r) * smoothstep(0.21, 0.57, edge.g);
 if (mapped <= 0.0) { return procedural_age; }
 vec3 neighborhood = textureLod(ortho, (uv + quadrant) * 0.25, 2.0).rgb;
 float broad_light = dot(neighborhood, LUMA);
 float broad_chroma = max(max(neighborhood.r,neighborhood.g),neighborhood.b) - min(min(neighborhood.r,neighborhood.g),neighborhood.b);
 float neutral = 1.0 - smoothstep(0.16, 0.40, broad_chroma / max(broad_light, 0.035));
 float usable = smoothstep(0.06, 0.12, broad_light) * (1.0 - smoothstep(0.50, 0.66, broad_light));
 float observed = smoothstep(0.10, 0.34, broad_light);
 return mix(procedural_age, observed, mapped * neutral * usable * 0.75);
}
