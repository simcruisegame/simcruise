# Lighting technology survey: surfaces, GI, shadows, reflections, exposure, display

Owner: lighting director. Date: 2026-11-12. Repo state: origin/main.
Style: ASD-STE100 (short, active sentences). Each claim has a source.
"Computed" = from code constants. "Measured" = from a capture. "Unverified" = not checked in a primary source.

Purpose: the owner asked us to "look into all the different lighting tech out there and how it works in games on different surfaces". This file lists the techniques, their cost, and their fit to SimCruise on the RTX 2070 Super Max-Q at the 28 W cap (about 2.6 times slower than the 90 W numbers, `docs/unreal-architecture.md:767`).

## 1. PBR surface models

### 1.1 The common model

- Unreal uses a metal/roughness model: BaseColor, Metallic, Roughness, Specular. Specular 0.5 gives F0 = 0.04 (IOR 1.5). F0 = 0.08 x Specular. [R1]
- A dielectric (paint, stone, plastic, rubber, glass) has F0 of about 0.02-0.05. Water has F0 0.02 (IOR 1.33). [R2]
- A metal has no diffuse colour. Its F0 is its colour: aluminium about 0.91, chromium about 0.55, iron about 0.56 (linear, per channel near-grey). [R2]
- Fresnel: every surface reflects almost 100 % at grazing angles. This is why a dull road becomes a mirror near the horizon. [R2]
- Roughness controls the size of the highlight and the blur of the reflection. Unreal maps roughness to GGX alpha = roughness^2. [R1]
- Albedo rule: dark surfaces stay above about 0.03 linear; non-snow surfaces stay below about 0.8 linear. Fresh snow is about 0.8-0.9. [R3]
- Rule: author albedo from a measured or photographed chip, converted sRGB to linear. A paint brand's on-screen hex is a display rendition. The paint's LRV (light reflectance value) is the better number (unverified as a general rule; LRV is the CIE Y of the chip).

### 1.2 Albedo and roughness per surface (linear)

| Surface | Albedo | F0 | Roughness | Note | Source |
|---|---|---|---|---|---|
| Fresh asphalt | 0.04-0.06 | 0.04 | 0.8-0.95 | Binder is black | [R3], unverified range |
| Worn asphalt | 0.09-0.15 | 0.04 | 0.75-0.9 | Aggregate shows | unverified range |
| Concrete (new) | 0.25-0.40 | 0.04 | 0.8-0.95 | Weathers darker | [R3] |
| White stucco/paint, fresh | 0.75-0.88 | 0.04 | 0.7-0.9 | LRV 80-90 | unverified range |
| White stucco, weathered | 0.55-0.75 | 0.04 | 0.8-0.95 | Dirt, algae, chalking | unverified range |
| Red brick | 0.10-0.25 | 0.04 | 0.8-0.95 | | [R3] |
| Grass/foliage | 0.08-0.25 | 0.03-0.04 | 0.6-0.9 | Two-sided, transmits | [R3] |
| Tyre rubber | 0.02-0.05 | 0.04 | 0.7-0.9 | Sidewall dull | unverified range |
| Chrome | (metal) | 0.55 | 0.02-0.1 | Mirror | [R2] |
| Window glass | 0 (thin) | 0.04 | 0-0.05 | Transmits about 85-90 % | [R2] |
| Water | 0.02 diffuse | 0.02 | 0.0-0.1 | | [R2] |

### 1.3 Car paint

- Real automotive paint is a stack: primer, base coat (pigment and, for metallics, aluminium or mica flakes), clear coat (about 40-50 um, IOR about 1.5). [R4], unverified thickness
- The clear coat gives a sharp, F0 0.04 reflection on top. Its roughness on a polished car is about 0.02-0.06 (unverified).
- The base coat gives the colour and a wider lobe. Metallic flakes add a sparkle that varies with angle (the "flop": bright face-on, dark at grazing for silver). [R4]
- Orange peel is a low-frequency ripple in the clear coat (wavelength mm to cm). It wobbles the reflected horizon lines. It does not blur them. [R4]
- Dark paint is mostly reflection. If the reflection has no environment, dark paint looks like plastic.
- Gran Turismo 7 measures manufacturer paint samples with a spectrometer. [R13] Forza Motorsport (2023) samples paint with a spectrophotometer for colour, flake and gloss. [R15]
- Unreal: the Clear Coat shading model has two lobes and an optional bottom normal (`r.ClearCoatNormal`). Substrate (Unreal's layered material system) can stack slabs; Epic released 280 Substrate automotive materials for UE 5.7. [R5] Substrate cost and status in 5.8: unverified.
- SimCruise: `SimCruiseCarMaterialLibrary.cpp:119-256` already does flakes under a clear coat with orange peel, clear-coat roughness clamp 0.01, and `r.ClearCoatNormal=True` (`Config/DefaultEngine.ini:13`). The model is right. The missing part is the environment in the reflection (section 4).

### 1.4 Wet asphalt

- Porous surfaces darken when wet. Water fills the pores and light makes more internal bounces before it escapes. Dark asphalt falls to about 0.6 of its dry albedo (Lekner and Dorf 1988, as cited in `SimCruiseWorldResources.cpp:151-152`; primary text unverified).
- Roughness falls as a water film fills the micro-texture. Puddles are a separate layer: water with F0 0.02, roughness near 0, flat normal. [R6]
- The darkening makes the specular term dominate. A wet road at night shows long, vertical streaks of each lamp (stretched GGX highlights on a slightly rough water film). [R6]
- Lagarde's "Water drop" series (Remember Me) is the reference game method: modify the dry BRDF inputs per porosity and wetness, do not add a new lighting model. [R6]
- SimCruise: the asphalt shader darkens to 0.6, sets roughness 0.35 when damp and 0.04 in puddles, and flattens the puddle normal (`SimCruiseWorldResources.cpp:154-159`). This matches [R6]. Two gaps: (a) the puddle keeps F0 0.04 (default Specular 0.5), water is 0.02; (b) a wet road reflects the street, and the street has no distance fields, so Lumen reflects only the sky (section 4).

### 1.5 Dry asphalt and concrete

- Dry asphalt is rough (0.8-0.95). Its look comes from the aggregate texture and the macro variation, not from specular.
- At grazing angles even dry asphalt shows a sheen toward the sun (Fresnel and a rough GGX lobe). It is the "road glare" in a low sun.
- SimCruise: asphalt roughness 0.80-0.95 (`SimCruiseWorldResources.cpp:148`), concrete 0.84-0.92 (`:176`). Correct range.

### 1.6 Stucco and building walls

- Stucco is a rough dielectric. Its brightness on screen is albedo x (sun + sky + bounce) x visibility.
- Real walls in a street receive less sky than a wall in an open field. The street and the opposite building block part of the sky (sky view factor). Bounce from dark asphalt is low.
- SimCruise, computed: the Deco palette `F5F1E6` is linear albedo 0.88, `F2F0EA` 0.87, `ECE9E1` 0.82 (`SimCruiseTileDetail.cpp:143-145, 185-188`). Grain and stain lower this by only about 5-10 % (`SimCruiseWorldResources.cpp:235-236`). That is fresh paint. Weathered stucco on Ocean Drive is likely 0.6-0.75 (unverified).
- Answer to the owner's question ("buildings are too bright, would they absorb a bit?"): yes. Two causes. First, the albedo is fresh paint, about 15-25 % too high for a weathered wall. Second, the tile meshes have no distance fields, so software Lumen cannot compute the sky occlusion between buildings; walls in a street get the open-sky light (`docs/unreal-architecture.md:699, 707`). Fix the material and the occlusion. Do not fix it with exposure.

### 1.7 Glass

- Thin glass: two surfaces, F0 0.04 each. Reflection at normal incidence is about 8 % total, near 100 % at grazing angles. Transmission about 85-90 % for clear float glass. [R2], unverified transmission range
- Car windscreens are laminated and slightly green. Tinted architectural glass transmits 10-50 %.
- Games draw a room behind the pane with interior mapping (a ray-box trace in the shader). SimCruise does this (`SimCruiseWorldResources.cpp:330-411`).
- The reflection on the pane needs the street. Without it the pane reflects sky only.

### 1.8 Chrome and metals

- Chrome is a near-mirror metal (F0 about 0.55, roughness near 0). It shows only what it reflects. In a real-time engine its look depends 100 % on the reflection method.

### 1.9 Foliage

- Leaves transmit about 10-20 % of light and reflect about 10-15 % (unverified ranges). Back-lit leaves glow yellow-green.
- Unreal: Two Sided Foliage shading model (a transmission colour) or Subsurface. SimCruise uses two-sided foliage on palm fronds (`docs/unreal-architecture.md:692`).
- Software Lumen needs "Affect Distance Field Lighting" on foliage. [R7] SimCruise turns it off on tree rings (`SimCruiseTrees.cpp:339, 357, 395`); this is a known cost trade.

### 1.10 Water (ocean, canals)

- F0 0.02. Colour comes from what is reflected (sky) and from scattering below the surface. At grazing angles the sea is a mirror of the sky.
- SimCruise: water tint 0.025/0.15/0.17 and roughness 0.08 in the ground shader (`SimCruiseWorldResources.cpp:124, 128`). Plausible for coastal water (unverified).

## 2. Global illumination

| Method | How it works | Fit for a streamed, dynamic city |
|---|---|---|
| Baked lightmaps | Offline GI into textures | No. The sun follows the real clock and tiles are built at run time. `r.AllowStaticLighting=False`. |
| Light probes / irradiance volumes | Baked or updated probes, interpolated | Partial. Needs re-baking per time of day. |
| DDGI | Probes updated each frame by ray tracing, with depth to stop leaks [R8] | Possible, needs HW RT; not in stock Unreal. |
| SSGI | Screen-space bounce | Cheap, but misses off-screen light. Unreal has it as a fallback. |
| Lumen software | Screen traces, then mesh and global distance fields, lit from the surface cache (cards) [R7][R9] | Current choice. Needs mesh distance fields. Runtime tiles have none. |
| Lumen hardware | Traces triangles (BVH); no distance fields needed; supports more geometry; RTX 2000+ [R7] | Best fit for runtime tiles. Cost at 28 W unmeasured. |
| ReSTIR GI | Reuses path samples in space and time [R10] | Research and high-end PC only. |
| Path tracing | Full light transport, denoised; F1 25 "Ultra Max" ships it [R16] | No on this GPU. |
| MegaLights | Stochastic direct lighting for many shadowed lights, fixed per-pixel ray budget; needs HW RT; doc tags it experimental; directional light off by default; Epic advises VSM for strong sun [R11] | Not now. Maybe for many street lights in a later phase. |

Key Lumen facts [R7]:
- Software Lumen represents only static meshes, instanced static meshes, HISM and landscape. Walls thinner than 10 cm leak.
- Detail tracing uses mesh distance fields for the first 2 m, then the global distance field. The Lumen scene covers 200 m (up to 800 m).
- Pink areas in the Surface Cache view mean no coverage: they do not bounce light and are black in reflections.
- Hardware Lumen: Windows DX12 with RTX 2000 series or newer. It is "the only way to achieve high quality mirror reflections".
- Epic's High scalability targets 60 fps on current consoles; Epic GI and reflections are about 8 ms at 1080p internal on consoles.

SimCruise state: software Lumen, `r.Lumen.HardwareRayTracing=False` (`DefaultEngine.ini:20`). Tiles are static meshes made from render data on a worker thread with no distance field (`docs/unreal-architecture.md:707`). Palms are shared static meshes with distance fields (`:692`). So Lumen GI and reflections today see palms and the car, not the street. Low turns Lumen off; Medium keeps GI and drops reflections (`SimCruiseGraphics.cpp:67-78`).

## 3. Shadows

- Cascaded shadow maps (CSM): 2-4 cascades; cheap; resolution falls with distance; shimmer when the sun moves.
- Virtual shadow maps (VSM): 16k x 16k virtual per light, 128 x 128 pages, clipmaps for the sun from 64 cm to about 40 km. Cached; a moving light invalidates all its pages. Non-Nanite geometry is "much more expensive" to render into VSM. [R12]
- SMRT soft shadows: VSM traces the shadow map for contact-hardening penumbrae; Source Angle sets the sun's penumbra (the real sun is 0.53 deg). [R12]
- Contact shadows: screen-space; not needed with Nanite on VSM; still useful for grass and small detail. [R12]
- Ray-traced shadows: exact; cost per light; needs HW RT.
- Distance field shadows: for non-Nanite geometry beyond the dynamic shadow distance. [R12] They need distance fields, so SimCruise tiles get none.
- SimCruise: VSM on (`DefaultEngine.ini:11`). The sun moves with the real clock: a slow sun still invalidates cached pages when it moves (rate of update: unverified). Tiles are non-Nanite. Cost is known in `docs/unreal-architecture.md`. The cinematographer owns how hard the shadow edge looks; give them the 0.53 deg sun angle.

## 4. Reflections

| Method | Sees | Fails | Fit |
|---|---|---|---|
| SSR | Only what is on screen | Edges, occluded parts, the sky behind the camera | Cheap fallback |
| Reflection captures (cube maps) | Static environment at a point | Wrong parallax, stale with time of day | Poor for a streamed city |
| Planar reflection | One plane, exact | Re-renders the scene; one plane only | Puddles only, expensive |
| Lumen reflections (software) | Distance-field scene, surface cache lighting | No street (no DF); blurry on mirrors | Current |
| Lumen reflections (hardware) | Triangles; hit lighting optional | Cost | Best for paint and glass |
| RT reflections | Exact | Cost, noise | Showroom only on this GPU |

- Wet roads: the reflection is almost all at grazing angles. SSR covers most of it (the lamps and lit facades are on screen above the road). Lumen screen traces already do this before the distance-field trace. [R7]
- Car paint: the clear coat reflects the whole hemisphere, much of it off screen. It needs a world representation (Lumen scene or HW RT) or a capture.
- Forza Horizon 5 used ray tracing only in Forza Vista (the car viewer) at launch. [R14] Forza Motorsport (2023) shows RT reflections in races on Xbox (unverified detail). [R15]
- SimCruise: mirrors use a scene capture, because Lumen cannot see the street (`docs/unreal-architecture.md:741`). This is a good local fix. Paint and shop glass still reflect only sky.

## 5. Sky, atmosphere and sun

- Sky Atmosphere: physically based Rayleigh and Mie scattering, ozone absorption; the sun light takes the transmittance colour. Height fog can take the atmosphere colour. [L2]
- Physical units: the sun at the top of the atmosphere is about 128 klx (1361 W/m2 x 93.9 lm/W, computed in `SimCruiseDaylightModel`); the atmosphere reduces it at the ground.
- Volumetric clouds: ray-marched; cloud shadows and sky occlusion are the expensive parts. SimCruise measured 6.0 ms for cloud shadow and sky occlusion at Ultra before overrides (`Config/DefaultScalability.ini:6-7`) and cut it to about 0.56 ms at High (`:26-28`). Measured by the team.
- Sky light with real-time capture gives the sky's colour to shade. Lumen adds the occlusion. Without occlusion, shade is too bright and too blue.
- GT7 precomputes sunlight and skylight from atmospheric physics with no ad hoc changes. [R13]

## 6. Exposure, tone mapping and display

### 6.1 Auto exposure

- Unreal histogram auto exposure in EV100, with a K = 12.5 meter and an 18 % target. [L3][L4]
- Local exposure (UE 5.1+): a bilateral grid separates base and detail; HighlightContrastScale and ShadowContrastScale compress the base. [L4] SimCruise: 0.55 and 0.95 (`DefaultEngine.ini:34-35`). Strong highlight compression lowers the sun-lit wall versus the sky (good) but can flatten the scene (cinematographer's look).
- GT7 models two human adaptation times: short (seconds) and long (tens of minutes). [R13]
- SimCruise stores Lumen's pre-exposed lighting with `r.EyeAdaptation.CachedLightingPreExposure=7.5` to cover EV100 -4.5..15.5 (`DefaultEngine.ini:48`).

### 6.2 Tone mappers

- Unreal filmic: ACES-fitted curve with Slope, Toe, Shoulder, BlackClip, WhiteClip. Per-channel: bright saturated colours shift hue (red to orange/salmon) and go to white. [L1]
- ACES (RRT+ODT): a reference for film; same per-channel hue skew in 1.x. ACES 2.0 reduces it (unverified in Unreal 5.8).
- AgX (Blender 4.0 default since 2023): desaturates bright colours toward white along a path with less hue skew. [R17] (community reverse-engineering; no formal spec)
- GT7 (SIGGRAPH 2025, PDI): "Color Volume Mapping": blends a per-channel curve with a curve in a uniform colour space (ICtCp or Jzazbz), to keep brake-light hues; one operator for SDR and HDR; sample code under MIT licence. [R13][R18] BeamNG adopted it. [R18] BeamNG's doc says SDR paper white is "typically about 250 nits" (their value; BT.2408 uses 203 for HDR reference white). [R18][L12]
- SimCruise: signal colours are authored red-channel-only so the per-channel curve does not turn them salmon (`docs/unreal-architecture.md:336`). That is the right workaround for the filmic curve.

### 6.3 HDR output

- Unreal: `r.AllowHDR=1`, `r.HDR.EnableHDROutput=1`, `r.HDR.Display.OutputDevice` 3/4 = ACES 1000/2000-nit ST 2084 (PQ), 5/6 = scRGB; `r.HDR.Display.ColorGamut` 2 = Rec.2020. `r.HDR.UI.Level` 1.5-2 recommended; `r.HDR.UI.CompositeMode 1` is experimental. Only 1000 and 2000 nit paths. D3D11 HDR needs exclusive fullscreen. The film curve is disabled in HDR by default. [L1]
- BT.2408: HDR reference (graphics) white 203 cd/m2. [L12]
- Dolby Vision mastering practice: master on a PQ display, set diffuse white consistently, derive SDR by trims and check the SDR picture. [L10] Dolby Vision for games on PC: unverified.
- SimCruise: no HDR output today. SDR: sRGB, D65, 100-nit white. Keep it until the SDR contract tests pass.

### 6.4 Other driving games (public facts only)

- GT7: spectral paint data, physical sky, colour volume tone mapping, two-speed adaptation. [R13]
- Forza Horizon 5: RT in Forza Vista only at launch; RT added on PC later (unverified detail). [R14]
- Forza Motorsport (2023): dynamic time of day and weather, procedural clouds, spectrophotometer paint, RT reflections on Xbox. [R15]
- F1 25 (EGO): path-traced "Ultra Max" mode, about 300 000 lights, built by two engineers in 12 months. [R16]
- Assetto Corsa EVO: dynamic GI, day-night cycle, physical sky with volumetric clouds, HDR (publisher FAQ; method not public). [R19]
- Test Drive Unlimited Solar Crown (KT Engine): lighting, shadows, reflections and water shader under overhaul; method not public. [R20]
- Dirt Rally 2.0: no public lighting talk found. Not checked further.

## 7. Upscaling and denoising

- TSR: Unreal's temporal upscaler; SimCruise uses it on every preset at 50-75 % (`SimCruiseGraphics.cpp:67-86`).
- Thin geometry detection (`r.TSR.ThinGeometryDetection 1`): relaxes history rejection on sub-pixel lines (fences, railings, window frames), reduces flicker. Dense, uniform parallel lines (power lines) are a known limit; test them. Cost: extra compute passes. [R21]
- DLSS Super Resolution runs on all RTX GPUs, so on the 2070 Super Max-Q (unverified for the current DLSS version). DLSS Ray Reconstruction replaces denoisers for RT; useful only with HW RT.
- FSR (3.x, compute) runs on any GPU; FSR 4 ML is limited to newer AMD GPUs (unverified). XeSS has a DP4a path for non-Intel GPUs (unverified).
- Lumen and MegaLights depend on temporal denoising. Fast camera motion (a car at speed) shows ghosting on reflections and GI. Measure at speed, not at rest.

## 8. Technique table

| Technique | Quality | Cost (90 W, unless noted) | Fits 28 W GPU in UE 5.8? | Recommendation for SimCruise |
|---|---|---|---|---|
| Lumen software GI, no tile DFs (now) | Low (only palms/car in scene) | Paid, small gain | yes | Fix the missing scene (row below) |
| Lumen software + tile distance fields | Medium-high | Build cost per tile; trace cost unmeasured | maybe | Build a coarse DF per tile on the worker thread, or try HW |
| Lumen hardware (surface cache lighting) | High; sees runtime triangles | Unmeasured; Epic: about 8 ms GI+refl on consoles at Epic | maybe | Measure on High at 28 W. If <= 3 ms extra, ship on High/Ultra |
| Lumen hardware hit lighting | Highest reflections | High | no | Showroom/turntable only |
| SSGI | Low | Low | yes | Fallback for Low preset |
| DDGI / ReSTIR GI | High | High, needs plugin | no | Not now |
| Path tracing | Reference | Very high | no | Offline reference only |
| MegaLights | Many shadowed lights | Fixed budget, HW RT | no (now) | Re-test next phase for street lights |
| VSM | High | Non-Nanite tiles costly | yes (now) | Keep; measure sun-move invalidation |
| CSM | Medium | Low | yes | Low preset fallback (unverified need) |
| Contact shadows | Small detail | Low | yes | Car underside and kerbs only |
| SSR | Medium on wet roads | Low | yes | Keep as Lumen screen traces |
| Scene capture (mirrors) | Exact, local | Measured by team | yes | Keep |
| Planar reflections | Exact on one plane | Very high | no | Do not use |
| Clear coat + flakes | High | Low | yes | Keep; feed it a real environment |
| Substrate automotive | Higher | Higher, unmeasured | maybe | Evaluate next phase |
| Local exposure | Good HDR compression | Low | yes | Keep; tune with cinematographer |
| GT7-style colour volume tonemap | Fixes hue skew | Low | yes | Next phase, post-process material or engine change |
| HDR10 PQ output | High on HDR TVs | Low | yes | Next phase, after SDR tests pass |
| TSR + thin geometry detection | Stable thin lines | Low-medium | yes | Try on High |
| DLSS SR | Similar or better than TSR | Lower at same quality (unverified) | maybe | Optional plugin, next phase |

## 9. Top 8 changes for SimCruise

Ranked by visible effect in a normal drive, then by size. Now = this phase (bugs and driving experience). Next = next phase.

1. **Now. Give Lumen a street to trace.** Tiles have no mesh distance fields, so GI, sky occlusion and reflections miss the city (`docs/unreal-architecture.md:707, 741`; `Config/DefaultEngine.ini:14-20`). Option A: set `r.Lumen.HardwareRayTracing=True` (with `r.RayTracing=1` and SM6) on High/Ultra in `SimCruiseGraphics.cpp`; hardware Lumen needs no distance fields [R7]. Option B: build a coarse distance field per tile on the worker thread (`SimCruiseTileBuilder`). Test: Lumen Surface Cache view shows no pink on facades; shade/sun ratio on a street wall moves toward the photo. Cost: measure ProfileGPU at 28 W. Size M (A) / L (B).
2. **Now. Weather the facade albedo.** Deco and tower whites are linear 0.87-0.88 (computed, `SimCruiseTileDetail.cpp:143, 185-188`). Multiply wall paint by about 0.8 in the building shader (`SimCruiseWorldResources.cpp:235`) or cap linear albedo at about 0.75. Test: sunlit white wall at sRGB 225-245 with sky still blue; wall/asphalt luminance ratio near the Ocean Drive photo. Size S.
3. **Now. Set water F0 in puddles.** Puddles use default Specular 0.5 (F0 0.04); water is 0.02 [R2]. Set Specular 0.25 in the puddle mask (`SimCruiseWorldResources.cpp:156-159`). Test: lab probe of puddle reflectance at normal view. Size S.
4. **Now. Measure VSM cost while the sun moves.** The real-clock sun invalidates cached pages [R12]. Measure `stat VirtualShadowMap` over 60 s at 28 W; if needed, quantise the sun direction update (e.g. every 0.05 deg). Owner: performance-engineer with photometric limits from us. Size S.
5. **Now. Turn on TSR thin geometry detection on High.** `r.TSR.ThinGeometryDetection=1` in `DefaultScalability.ini` `[AntiAliasingQuality@2]`. Window frames, railings and wires flicker today (unverified, check a capture). Test: flicker count on a 10 s capture at 50 km/h. Size S.
6. **Next. Colour-volume tone mapping (GT7 style).** Replace the red-only signal workaround with a hue-preserving tone curve (ICtCp blend, MIT sample code) [R13][R18]. Test: brake lamp at 6000 nits keeps hue within 5 deg in ICtCp; grey card at sRGB 117 +/- 10. Coordinate with the cinematographer. Size M.
7. **Next. HDR10 output.** `r.HDR.Display.OutputDevice=3`, gamut Rec.2020, reference white 203 nits, `r.HDR.UI.Level` about 1.5 [L1][L12]; add an SDR check of the same frame. Size M.
8. **Next. Car paint reflections at speed.** After change 1, compare clear-coat reflections from hardware Lumen (surface cache) with the scene capture used for mirrors. If HW Lumen is too costly, extend the mirror capture to a low-resolution cube for paint in the driver's view. Evaluate Substrate automotive materials [R5]. Size M-L.

## 10. Sources

- [R1] Epic Games, Physically Based Materials: https://dev.epicgames.com/documentation/en-us/unreal-engine/physically-based-materials-in-unreal-engine (not fetched this session)
- [R2] Hoffman, Physics and Math of Shading, SIGGRAPH 2012 PBS course notes (F0 table): https://blog.selfshadow.com/publications/s2012-shading-course/hoffman/s2012_pbs_physics_math_notes.pdf ; course index https://blog.selfshadow.com/publications/s2025-shading-course/s2025_pbs_course_summary.pdf
- [R3] Lagarde and de Rousiers, Moving Frostbite to PBR 3.0 (albedo chart): https://blog.selfshadow.com/publications/s2014-shading-course/frostbite/s2014_pbs_frostbite_slides.pdf
- [R4] Kirchner, car paint appearance (EI 2019): https://library.imaging.org/ei/articles/31/14/art00002 ; Elek, Layered materials in real-time rendering (CESCG 2010): https://www.old.cescg.org/CESCG-2010/papers/ElekOskar.pdf (not read in full)
- [R5] Epic, Substrate Automotive Materials Pack (UE 5.7): https://www.unrealengine.com/en-US/news/get-over-280-production-ready-automotive-substrate-materials-for-ue-5-7-free-on-fab
- [R6] Lagarde, rain in Remember Me (fxguide summary): https://www.fxguide.com/?p=42356 ; Lagarde blog "Water drop 3b - Physically based wet surfaces" (2013, not fetched; values unverified here)
- [R7] Epic, Lumen Technical Details: https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine
- [R8] Majercik et al., Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields, JCGT 2019: https://research.nvidia.com/publication/2019-05_dynamic-diffuse-global-illumination-ray-traced-irradiance-fields
- [R9] Wright et al., Lumen, SIGGRAPH 2022 Advances: https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf (not read in full)
- [R10] Ouyang et al., ReSTIR GI, HPG 2021: https://research.nvidia.com/publication/2021-06_restir-gi-path-resampling-real-time-path-tracing
- [R11] Epic, MegaLights: https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine
- [R12] Epic, Virtual Shadow Maps: https://dev.epicgames.com/documentation/unreal-engine/virtual-shadow-maps-in-unreal-engine
- [R13] Yasutomi, Suzuki, Uchimura (Polyphony Digital), Driving Toward Reality: Physically Based Tone Mapping and Perceptual Fidelity in GT7, SIGGRAPH 2025 PBS course (abstract in [R2] index; slides https://blog.selfshadow.com/publications/s2025-shading-course/pdi/s2025_pbs_pdi_slides.pdf failed to load); talk summary: https://www.gtplanet.net/?p=141090
- [R14] TechRadar, Forza Horizon 5 uses ray tracing: https://www.techradar.com/news/forza-horizon-5-uses-ray-tracing-but-not-how-you-think
- [R15] PC Gamer, Forza Motorsport technical features: https://pcgamer.com/forza-motorsport-promises-ray-traced-reflections-fully-procedural-clouds-and-loads-of-other-technical-doodads
- [R16] GDC 2026, EA Sports F1 25: Path Tracing at 200mph (Hammersley): https://schedule.gdconf.com/session/ea-sports-f1-25-path-tracing-at-200mph/914867
- [R17] Reverse engineering Blender's AgX: https://thecodecofficial.github.io/Blog/AgES (community source)
- [R18] BeamNG, Tone Mapping and HDR: https://docs.beamng.com/modding/lighting/tonemapping/ ; GTPlanet on BeamNG using GT7 tone mapping: https://www.gtplanet.net/?p=145756
- [R19] 505 Games, AC EVO graphical features: https://support.505games.com/support/solutions/articles/150000199477-what-graphical-features-can-we-expect-in-ac-evo-
- [R20] Traxion, TDU Solar Crown updated graphics: https://traxion.gg/first-look-at-test-drive-unlimited-solar-crowns-updated-graphics/
- [R21] Epic, Thin Geometry Detection with TSR: https://dev.epicgames.com/documentation/en-us/unreal-engine/thin-geometry-detection-with-temporal-super-resolution
- [L1], [L2], [L3], [L4], [L10], [L12]: see `docs/team/lighting-director.md` section 7.
- Licences: Epic, Dolby and press pages are read for reference only. Papers are cited, not copied. Numbers marked "unverified" need a primary source before they become a test threshold.
