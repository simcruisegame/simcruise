# 13. Cars that look real down to the parts: one material-role table, shared paint/glass/tyre/disc/lens shaders, windscreen, interior grain, cockpit shadows, real mirrors (car-look-pipeline)
category visuals · impact 4 · 7 d · depends: reality-loop

## Rationale
The owner wants visible parts to be right: tyre and tread, wheels, interior materials. Material handling is name-matching scattered over three places. The flake glint uses a hard-coded light direction. Glass alpha-blends its own reflections away. The driver view has no windscreen. Interior plastics are white-noise albedo with no normal or roughness, and the wheel rim is drawn as seat cloth. Tyres have no tread blocks. Front lamps work only on the NB. Fixing these needs no extra GPU, only correct materials.

## Code claims (proposer)
CarFinish.gd:180-198, CarBody.gd:44-63 and ModelRig.gd:202-209 each do their own material mapping. CarPaint.gdshader:63 fixes the glint light at (0.3,0.9,0.2) and :66 boosts albedo. CarFinish.gd:167-170 sets glass alpha .34 with blend_mix. Cockpit.gd:158 hides layer 2, and build_cockpit.py has no windscreen pane. build_cockpit.py:381-384 and Cockpit.gd:89-101 apply albedo noise only. build_cockpit.py:320 makes the rim 'Leather', which Cockpit.gd:112-117 recolours to cloth r.95. Cockpit.gd:65-67 cast_shadow OFF. Cockpit.gd:119-148 is one 320x88 mirror camera at 6.7 Hz with cull_mask 1. SportWheel.gd:180-201 has a flat tread strip and one rubber roughness. ModelRig maps no front-lamp role.

## Design (proposer)
1) game/CarMaterialRoles.gd: about 14 roles (paint, glass, chrome, grained plastic, smooth plastic, rubber seal, tyre, rim, disc, caliper, lamp front lens, lamp rear lens, reflector, cloth, leather, gauge), with name patterns. Used by CarFinish, CarBody and ModelRig. Unknown names are logged by RigProbe.
2) Shaders in game/art/materials/car/:
- CarPaint: flakes modulate ROUGHNESS and METALLIC only, so the engine's sun makes the glint. Flop is ALBEDO *= mix(1, flop_dark, (1-NdotV)²). Applied to the Mustang and C8 paint via ModelRig, with base_color from the imported albedo.
- CarGlass: blend_premul_alpha, ALPHA = mix(tint_alpha 0.06, 1, Schlick(F0 .04)).
- Tyre: generic brand-free sidewall plus tread normal and ORM on the existing u-layout, worn tread 0.1 smoother, dust by height. Real brand lettering only in the owner's private model folder, never shipped.
- Disc: anisotropic radial streaks plus hat and vane rings. Cast-iron caliper with a bracket.
- Lens: interior-mapping fake-depth chambers.
3) Cockpit:
- A windscreen strip bounded by the header and A-pillar polylines, sharing grime and wet with CarWear.
- 512 px CC0 normal and roughness detail for pebble plastic, cloth, leather and rubber, triplanar as now.
- WheelLeather role at r .5 with a grip-wear mask.
- Seat bolsters.
- Shadows enabled on the wheel, bezel, header and door tops. Exterior stand-in cabin set to SHADOW_CASTING_SETTING_OFF.
- Live oil, temperature and fuel needles. Gauge emission on lamps.
4) Mirrors: one camera per mirror at the reflected eye point, 256x128, alternating frames, including layer 2, plus the right door mirror. Fall back to a single camera on Medium and Low.
5) Low-beam SpotLight3D with an SAE cut-off light_projector, for every kept car via the lamp_front role.

## Gate
reality-loop receipts:
- Glass-region luminance against the matched Commons crop.
- Glint pixel positions move with sun azimuth (0/90/180 deg).
- Dash-top luminance std rises with the mean within 5 %.
- The rim highlight exceeds the dash highlight.
- A rim shadow scanline drop appears on the dash at a 45 deg sun.
- A follower in the mirror updates at least every 2 frames.
- The road luminance profile shows a sharp cut-off row with --lamps.
Performance: frame time within +0.5 ms at Ultra on the bundled start, measured plugged in. Shader variants precompiled.
The TrafficLineup check passes. Physics is untouched (MOUNTS and R unchanged), so CarTrack is bit-identical.

## Files
game/CarMaterialRoles.gd
game/CarFinish.gd
game/CarBody.gd
game/ModelRig.gd
game/CarPaint.gdshader
game/art/materials/car
game/SportWheel.gd
game/Cockpit.gd
authoring/build_cockpit.py
game/checks/CarTurntable.gd
lab/miata/appearance/README.md
docs/cars.md

## Skeptic verdict
claims_hold true · already_done false · keep false

### Wrong claims
- SportWheel.gd:180-201 'flat tread strip' is overstated. tyre_profile() already models four circumferential grooves (x = +-.060, +-.023, 4.0 mm deep). Only the UVs of the tread sample a flat strip (v = .02), and tread and sidewall share one rubber material (roughness .88). Lateral blocks and sipes and a separate tread roughness are missing. The grooves are not.
- 'Material handling is scattered over three places' is half true. ModelRig.role_material (lines 202-209) and CarBody.gd (lines 44-63) already form a two-stage role pipeline: ModelRig renames materials to 'lamp_rear' and 'glass', and CarBody applies those names. CarFinish.gd:180-198 is a separate mapping for the 1999 roadster only, and it holds per-car finish decisions (the Sport-package 'Leather' becomes cloth, Wheelhouse becomes liner). Those decisions are not generic roles.
- 'Unknown names are logged by RigProbe': no RigProbe exists in game/ (grep finds nothing). ModelRig has note() and log_lines, and CarBody pushes a warning on a failed fit.
- 'Front lamps work only on the NB' is correct, but the NB already has two SpotLight3D low beams (CarFinish.gd:564-592: 26 deg cone, 2 deg down, no projector, no shadow). The new part is the SAE cut-off projector and the lamp_front role for the other cars, not the spot lights.
- The design says 'Apply CarPaint to the Mustang via ModelRig with base_color from the imported albedo', but the Mustang's glTF materials are named surfaceShader9SG to 12SG, so no name pattern finds its paint or glass. Its body colour comes from an atlas baseColor texture (surfaceShader9SG/10SG), and CarPaint has only a uniform base_color with no texture input. The C8 does have semantic names (Body_Color with KHR clearcoat, Windshield, Tire_Treads, Tire_Sidewall, Brake_Disc, Headlights).

### Design problems
- This does not answer the user's request. The user asked about 'the AI engine we discussed to assist with traffic, extras, generation of detail'. This proposal is a hand-authored car material and shader pipeline with no AI, traffic, extras or generated detail in it.
- The flake redesign removes the sparkle. If flakes modulate only ROUGHNESS and METALLIC, every flake shares the panel's shading normal. The sun highlight then just gets speckled metallic patches, without the per-flake angle-dependent glint that the current tilt produces. The 'glint moves with sun azimuth' gate would pass trivially while the sparkle is gone. The per-flake tilt has to go into the shading normal (combined with the orange-peel NORMAL_MAP, faded by flake_vis), so that the engine's lights, headlamps included, produce the glint. Also check whether Godot's clearcoat lobe uses the perturbed normal or the vertex normal. If it uses the vertex normal, orange peel on the clearcoat does nothing today either.
- The mirror plan contradicts its own gate. Three mirrors (centre, left, right) rendered on alternating frames means each mirror updates only every 3rd frame, which fails 'a follower updates at least every 2 frames'. The cost is also badly underestimated. Going from one render at 6.7 Hz to one full-world SubViewport render every frame, with city draw calls and directional shadow cascades re-rendered per viewport (positional_shadow_atlas_size = 0 does not remove directional shadows), is unlikely to fit in +0.5 ms at Ultra.
- Reflected-eye mirror cameras need clipping at the mirror plane (frustum offset plus near distance at the mirror). Otherwise the windscreen header, wipers and hood between the reflected eye and the mirror block the view. Including layer 2 from inside also shows CULL_DISABLED exterior panels from behind. The cabin view probably needs layer 3 (the cockpit) instead.
- Cockpit shadows will be doubled. The exterior car on layer 2 is hidden only from the camera's cull mask. The directional light still renders its shadows, and those shadows (windscreen frame, door tops) fall on the cockpit today. Enabling header and door-top shadows in the cockpit gives doubled, offset shadows unless the matching exterior parts switch to shadow-off only while the cockpit is visible. A permanent SHADOW_CASTING_SETTING_OFF would remove self-shadowing in the chase and turntable views.
- The windscreen 'sharing grime and wet with CarWear' needs explicit wiring. CarWear.is_interior() deliberately skips everything under game.cockpit, and RoadGrime is applied as a material_overlay on exterior meshes.
- Cockpit work (windscreen, rim, bolsters, live needles, mirrors) applies only to the 1999 roadster. Drive.gd:724 disables Cockpit for every generic_car, and those cars use spec.driver_eye with their own model's glass. 'Live temperature and fuel needles' would need coolant and fuel models that do not exist, which is scope creep. An rpm-driven oil-pressure needle is the only one with an existing input.
- A single role table that also holds finish decisions would regress CarFinish. Roles should be generic, with colour, roughness and package choices kept per car (CarSpec JSON or CarFinish). Models with meaningless names, like the Mustang, need a per-model role map in the car file, not name patterns.
- Effort is optimistic. 7 days covers about 5 new shaders, a cockpit rebuild in build_cockpit.py, 3 mirror cameras, projector low beams for 6 cars, CarTurntable gates and docs. 'parallel_safe' is doubtful, because the work touches the 1239-line CarFinish.gd, ModelRig, CarBody and Cockpit, which other car and lamp work also edits.
- The physics claim holds: these are visual-only changes and do not touch Powertrain, CarSuspension, MOUNTS or R, so CarTrack and LiftOff stay bit-identical. The per-object light limit of the Mobile renderer is not a problem for two player low beams, but it would be if 'every kept car' later means traffic cars too.

### Corrected design (USE THIS where it differs)
If this proposal is kept as separate visuals work (it does not address the AI engine the user asked about), split it.

A) Roles:
- Add game/CarMaterialRoles.gd as a name-to-role helper that ModelRig.role_material calls. It extends today's lamp_rear and glass with lamp_front, tyre_tread, tyre_sidewall, disc, chrome and paint.
- Add an optional per-car "material_roles" map in the CarSpec JSON for models with meaningless names (the Mustang's surfaceShaderNSG).
- Unknown names go to ModelRig.note().
- Keep CarFinish's NB-specific finish decisions where they are.

B) Paint:
- Put the per-flake tilt into the shading normal together with orange peel (faded by flake_vis), and drop the hard-coded light and the albedo boost.
- Add the flop term and an optional albedo_texture, so atlas-textured paint (Mustang) keeps its texture.

C) Glass:
- blend_premul_alpha with a Fresnel ALPHA, for both CarFinish and CarBody glass.

D) Cockpit (NB only, as Drive.gd scopes it):
- Windscreen strip, with an explicit CarWear hook.
- Rim material set to a steering-wheel leather role.
- Normal and roughness detail textures for the trim.
- Cockpit shadows enabled only together with a view-dependent shadow-off on the matching exterior parts.
- An oil needle driven by rpm. Leave temperature and fuel alone until those models exist.

E) Mirrors:
- Keep one rear camera at 6.7 Hz on Low and Medium.
- On High and Ultra: a centre-mirror camera every 2nd frame, plus left and right door cameras sharing the other frames at 3-frame cadence. Restate the gate per mirror.
- Use a clipped near plane at the mirror, and measure the cost (directional shadow re-render) before committing to +0.5 ms.

F) Tyres:
- Separate the tread material (UVs along the grooves, block normal, its own roughness) from the sidewall on SportWheel.

G) Lamps:
- Projector cut-off on the existing NB low beams first, then lamp_front-based low beams for the generated cars.

Gates unchanged except the mirror cadence. CarTrack, LiftOff and TrafficLineup must stay bit-identical or pass.
