# 6. Straight, clean road edges: the ribbon owns the carriageway; edges fitted in road coordinates; smoothed centreline; per-class shoulders; grip from geometry (road-edge-authority)
category world · impact 5 · 7 d · depends: none

## Rationale
This is the owner's complaint: edges are not straight and dirt blends onto the road. Today a 7-weight per-pixel logistic classifier on 1.9 m/px aerials decides where the asphalt is. For any road narrower than about 8 m it overrides the mapped pavement, so tree shadow turns asphalt into dirt and the edge wobbles by ±1-2 m. The same classifier sets tyre grip, so the car can get a 0.65 mu patch on plain asphalt. The principle that fixes it: geometry decides, and imagery only tints.

## Code claims (proposer)
terrain.gdshader:162-167 sets road = smoothstep(0.33,0.67,edges.r) whenever edges.g > 0.03, replacing the semantic rule at :155-161. RoadEdges.gd:33-35 forces asphalt only where the prior is above 0.94 (the w-6 m stroke), and :43 is the score formula. WorldTile.gd:1158-1163: 256 px over 480 m (1.875 m/texel). helper/road-grade.mjs:219-223 and :323 export raw chord station points, with only the tangent smoothed (:253-256). RoadSurface.gd:30-31,77-87 build a 1.2 m shoulder shaded as ground. LaneMarkings.gd:202 lets edge lines sit up to about 1 m outside the asphalt. WorldTile.gd:633-637 and :773-774 compute grip from nearest-texel classifier values (0.65). Drive.gd:672 feeds that grip to Powertrain.gd:345.

## Design (proposer)
1) RoadSurface.mesh() writes UV2 = (signed lateral m, half-width) at each of the 5 section points. The terrain shader's ribbon path forces road = 1 for |lat| <= half - 0.15. The classifier may only add pavement in the shoulder band, and is clamped to 0 on terrain beyond the ribbon. aerial_pavement() keeps the tint, so the tone match is preserved.
2) Smooth the helper centreline: a Gaussian local-linear fit of X(s), Y(s) with sigma 8-10 m, deviation capped at 1 m, junction endpoints pinned. Bump the road_grade cache version. street_image, RoadEdges.geometric_prior, LaneMarkings and GuardRails all read the smoothed RoadSurface.lines.
3) Optional width from the photo:
- Per 2 m station and side, a 1D logistic-step fit of the classifier profile along the normal.
- Tukey robust smoothing plus a Gaussian along the station (sigma 12 m), clamped to [half-0.3, half+class shoulder].
- Apply a per-tile imagery shift first, estimated by cross-correlating prior and classifier.
4) Per-class shoulder table (AASHTO): motorway 3.0 m paved outside, primary 2.0 paved, secondary 1.2 paved, tertiary 0.6 paved + 0.6 gravel, minor 0.6 gravel.
- It drives the ribbon width and the shading zones: asphalt, a lighter paved shoulder, then FieldTerrain's chip gravel, with fwidth anti-aliased 3-5 cm transitions.
- LaneMarkings room = the paved shoulder, so edge lines are always on asphalt.
5) Grip: if a finite ribbon height is found, |lat| <= half gives 1.0, paved shoulder 0.97, gravel 0.70. Bilinear sampling per contact. The learned mask applies only off the ribbon. Vegetation placement (Rangeland.gd:170, Grove.gd:150, Roadside.gd:69) gets its own query.
6) Verge halo: within 3 m outside the carriageway, sample the aerial 2-3 m further out along the normal.
7) Ribbon skirt: an extra section point at half+shoulder+2 m set to terrain height, with grid nodes under it lowered. Section indices 0 and 4 stay where GuardRails expects them.

## Gate
New checks/RoadEdgeCheck.gd on 20 seeded live US tiles (CPU copy of the shader decision), targets:
- carriageway_ground_frac = 0
- edge_wobble_rms < 0.05 m after subtracting a 20 m trend; edge_step_max < 0.1 m
- plan-view kinks over 2 deg per station = 0, except at junctions (RoadProfile extension)
- edge_line_on_ground_frac = 0
- carriageway_grip_below_1_frac = 0
- terrain poke-through above the ribbon edge = 0
Regression gates:
- SeamCheck, RoadProfile, LaneMarkingCheck, VegetationCheck, CrownArrivalOrder and RideCheck pass.
- A straight dry RoadDrive run logs 0 ABS/TC events.
- SplitGrip and TractionControlCheck are unchanged.
- CarTrack is untouched, since it runs on the check track.
Visual: Ultra A/B captures at 10 random places plus the dragon and capitol shots, with README screenshots refreshed.

## Files
game/RoadSurface.gd
game/terrain.gdshader
game/GroundFinish.gdshaderinc
game/RoadEdges.gd
game/WorldTile.gd
game/LaneMarkings.gd
game/GuardRails.gd
helper/road-grade.mjs
game/checks/RoadEdgeCheck.gd
game/checks/RoadProfile.gd
README.md
docs/screenshots

## Skeptic verdict
claims_hold true · already_done false · keep true

### Wrong claims
- helper/road-grade.mjs:253-256 does not smooth the tangent. Those lines store raw 2 m chord headings (H from rows i+/-1). The smoothing happens later, in profileAt() at :289, using gaussianLinear with SIGMA=16 (:75). The underlying point stands: positions are raw chord interpolation (:213-222) and only the tangent is smoothed. That mismatch is itself a source of non-straight edges, because section points c + n*L pair a raw centre that has a corner at each OSM vertex with a normal smoothed over 16 m.
- 'For any road narrower than about 8 m it overrides the mapped pavement' understates the problem. RoadEdges.classify forces 255 only where prior > .94, which is the gray-249 stroke of width w-6. For every road width, the band out to w+7 is left to the classifier, and for w <= 6 the classifier decides the whole road. In terrain.gdshader:162-166 the override also applies only where max(paint.r, paint.b) < 0.58, so painted markings are exempt.
- RoadSurface's 1.2 m shoulder is not specifically 'shaded as ground'. The ribbon uses the same terrain material and UVs (RoadSurface.mesh :179-190), so the shoulder shows whatever the surface_map paint and the classifier say at that texel. Usually that is ground, because street_image paints asphalt at width w.
- The other code claims check out. shader :162-167; RoadEdges :33-35 and :43; prepare_pixels resizing to 256 px over 480 m; RoadSurface :30-31 and :77-87; LaneMarkings :202 (room defaults to SHOULDER_M 1.2, so the edge line centre can sit about 0.98 m outside the paved half); surface_grip :633-637 returning .65 from the nearest texel via sample_surface :773-774; Drive.gd:672 calling wheel_grip; Powertrain :345 multiplying mu by surface_grip; vegetation at Rangeland:170, Grove:150 and Roadside:69 using surface_grip thresholds.

### Design problems
- Scope: the relayed user request asks about the AI engine for traffic, extras and detail generation. That engine is the Ollama traffic-character hook in game/TrafficDirector.gd:15-30 and the plan in docs/research/detail-engine.md, which also covers AI tiers 1 and 2. This proposal does not address either. It is a valid world-quality item, but it does not answer the user's question and should not be presented as the reply to it.
- Step 2, the centreline smoothing, would split the world into two geometries. street_image() (WorldTile:380/:508: asphalt, curbs, crosswalks, junction paint), geometric_prior (:394), traffic `segments`, road_grid (used by wheel_grip/surface_grip), RoadLayout's roadSignature and BuildingStream all read raw info.roads. Only road_surface stations would move, by up to 1 m. The claim that those consumers would 'read the smoothed RoadSurface.lines' does not fit the build order: RoadSurface.for_tile runs at :858, after street_image and geometric_prior, and traffic is not mentioned at all. Traffic cars and painted curbs would end up 1 m off the ribbon.
- Step 2 has two more problems. Pinning only junction endpoints is not enough, because chains cross other roads mid-chain, so every node shared with another way must be pinned. And a Gaussian local-linear position fit with sigma 8-10 m cuts corners by about sigma^2/(2R): on a 20 m hairpin that is about 1.6-2.5 m. The 1 m cap would engage, and its onset creates a curvature discontinuity, which is exactly what the 'kinks over 2 deg = 0' gate would flag on the Tail of the Dragon.
- Step 1: 'clamped to 0 on terrain beyond the ribbon' cannot be done as described. The terrain ArrayMesh has no lateral coordinate and UV2 reads 0 there, which matches lat = 0 on the ribbon centre. The ribbon needs an explicit flag (UV2.y = half > 0, or a separate material instance). Off the ribbon, the classifier's band reaches half+3.5 m (the w+7 prior stroke). That is wider than the proposed minor-road skirt (half+0.6+2 m), so a second, wobbly classifier edge would still show outside the ribbon.
- Step 4 applies AASHTO rural shoulders to every road of a class. Urban curbed streets have no shoulder, and street_image draws curbs and sidewalks at the mapped width. Widening the ribbon by 0.6-3 m in town would cover curbs and sidewalks and move the collider onto them. Shoulders must depend on town/curb status (LaneMarkings already has `town`), and on whether the OSM width already includes shoulders.
- Step 3 puts imagery back into geometry, through a per-station logistic width fit and a per-tile cross-correlation shift on 1.9 m/px aerials. That contradicts the stated principle that geometry decides and imagery only tints. It adds the most risk for the least certain gain, so it should be deferred.
- Step 7: RoadSurface.build, join(), height() and the quads iterate every section point. A skirt section point would therefore become collider surface and part of height_at. With the rule 'finite ribbon height means grip 1.0', it would also give grip 1.0 on the embankment unless a lateral test is added. Adding points at both ends also shifts indices: GuardRails uses sections[i][4] and sections[i][side], and LaneMarkings uses [e]. A separate render-only skirt mesh is safer.
- Step 5: wheel_grip also feeds RoadRoughness.iri (Drive.gd:677) and so road noise and ride. RoadNoiseCheck, SustainedSurfaceCheck and RideCheck therefore belong in the gates. The proposed gravel 0.70 does not match the existing loose-surface constant 0.76 used in wheel_grip and surface_grip. Keep the measured_section and capture road_confidence precedence.
- Gate: a CPU copy of the shader decision is duplicated logic that will drift from the shader. Put the decision in one GDScript function that the gate tests and that also writes the mask or uniforms the shader reads. Twenty 'live' US tiles need a cached, offline tile set so the check stays deterministic and headless. TrafficCheck should be added to the gates if the centreline moves. The claim that CarTrack and LiftOff are unaffected is plausible, since they run on the check track and not on WorldTile.

### Corrected design (USE THIS where it differs)
Three phases. Phase A delivers most of the visible win.

Phase A (about 2-3 days): ribbon authority and grip.
- Write ribbon UV2 = (signed lateral m, half-width), with half > 0 acting as the 'on ribbon' flag.
- In terrain.gdshader, when the ribbon flag is set:
  - road = 1 for |lat| <= half - 0.15, with an fwidth anti-aliased edge;
  - the classifier may only add pavement in the shoulder band;
  - aerial_pavement keeps the tint.
- On non-ribbon terrain, stop the classifier from adding asphalt: use the semantic rule only, since the ribbon covers the carriageway.
- Add RoadSurface.lateral(p) -> (lat, half) or NaN.
- wheel_grip returns 1.0 when |lat| <= half and has_mapped_unpaved does not apply, keeping the measured_section and capture precedence first.
- Off the ribbon, sample learned_edges bilinearly. Reuse 0.76 for loose surface instead of adding 0.70.
- Keep surface_grip()'s current semantics for vegetation, or rename it to a placement query, so Rangeland, Grove and Roadside are unchanged.
- LaneMarkings: put the edge-line centre at half - ew/2, with room = 0 for edge lines.
- Gates: carriageway_ground_frac = 0, carriageway_grip_below_1_frac = 0 and edge_line_on_ground_frac = 0 on a cached set of seeded tiles. Also LaneMarkingCheck, VegetationCheck, RideCheck, RoadNoiseCheck, SplitGrip, TractionControlCheck, and RoadDrive with 0 ABS/TC events.

Phase B (about 2 days): one smoothed centreline, applied at the source.
- In helper/road-grade.mjs, smooth each chain's positions with a curvature-preserving fit: a local-quadratic Gaussian, or a cubic smoothing spline with a vertex tolerance of about 0.5-1 m and a smooth taper rather than a hard cap.
- Pin every node shared with another way.
- Derive the tangent from the smoothed positions.
- Export the smoothed polylines both as road_surface and as the tile's roads, so street_image, geometric_prior, traffic segments, road_grid and RoadLayout all agree. RoadLayout's signature will change, so bump its version.
- Bump the road_grade cache version.
- Gates: RoadProfile kink extension, SeamCheck, TrafficCheck, CrownArrivalOrder.

Phase C (about 2 days): rural per-class shoulders.
- Apply only where not town/curbed and where the OSM width excludes shoulders.
- Shade them as asphalt, then a lighter paved shoulder, then chip gravel.
- Add a render-only skirt mesh, not new section points, so GuardRails indices 0/4, height() and the collider are untouched.
- Gate on GuardRails and CrossfallDrive.

Drop step 3 (photo-derived width and per-tile shift) for now.

Separately, the user's actual question about the AI engine should be answered from TrafficDirector.gd's Ollama hook and docs/research/detail-engine.md. This proposal does not cover it.
