# Lighting director review, 2026-11-12

Reviewer: `lighting-director` (photometry, exposure chain, tone curve, SDR/HDR output, colour science, lighting cost).
Scope: `Source/SimCruise/Private/Environment/*`, low beams, street lights, `Config/*.ini` exposure and Lumen settings, `docs/unreal-architecture.md` light sections, `lab/daylight`, captures in the project files folder.
Inputs: repo at origin/main (2026-11-12), captures (read-only). The game was not run. Numbers marked "computed" come from the code's own constants; "measured" come from the capture PNG/JPG files.
Phase: bugs and driving experience. Proposals for the next phase are in the last section.
Overlap: the cinematographer review has night exposure (its finding 6), window clipping (7) and the local-exposure doc mismatch (8). This review gives the photometric cause and numbers. It does not repeat their fixes.

## Summary

The daylight chain is good work: real sun at the top of the atmosphere, IES sky, ISO meter, Koschmieder haze, all tested. The problems are at night and at the ends of the tone range. The night meter uses the wrong photometric quantity, the low beams are attenuated twice, Lumen cannot see the streets, and daylight never reaches display white. There is no display contract (SDR or HDR) yet.

Measured from the captures (luma on sRGB codes):

| Capture | p1 / p50 / p99 | % at 0-3 | % at ≥ 254 |
|---|---|---|---|
| buildings-night-breakwater.jpg | 0 / 1 / 240 | 60.7 % | 0.37 % |
| buildings-night-edison-cafe.jpg | 0 / 1 / 234 | 61.0 % | 0.20 % |
| buildings-ocean-drive-heights.jpg (day) | 28 / 120 / 210 | 0.0 % | 0.02 % |
| buildings-breakwater-front.jpg (day) | 25 / 107 / 212 | 0.1 % | 0.02 % |

## Findings (ranked)

### 1. P1 | The night meter uses illuminance on a card that faces the lamps, not the road luminance the driver sees
Evidence: `SimCruiseVehicle.cpp:214-216` sets `2 × 15000 cd / 30²` = 33 lx; `SimCruiseEnvironment.cpp:397-398` adds it to the sky; `SimCruiseDaylightModel.cpp:105-109` turns it into a grey card: L = 0.18 × 33 / π = 1.9 cd/m², EV100 3.9. Test `SimCruise.Drive.LowBeams` (`DriveIntegrationTests.cpp:273-281`) locks this in. The lamps are on whenever the sun is below -1° (`SimCruiseVehicle.cpp:563`), so this is every night drive.
What is wrong: 15 000 cd / d² is the illuminance normal to the beam. The road gets the light at grazing incidence: computed horizontal illuminance from two lamps at 0.65 m is 0.7 lx at 30 m and 10 lx at 10 m. Measured road luminance under real low beams, 0-32 m ahead, is 0.41-2.08 cd/m² (Erkan et al. 2021, LED; a 2001 halogen is lower). The meter's 1.9 cd/m² grey is above that range.
What the player sees: the camera closes for a road brighter than the one rendered. Computed (Lambertian asphalt, albedo 0.085, the code's pattern and cone): the lamp-lit road 5-30 m ahead renders about 0.1 cd/m² on average, about 4 EV under the band's middle grey. With `DisplayEV100` the display EV moves from -1.6 (lamps off) to 6.6 (lamps on): 8 EV. Moon, sky glow (0.06 cd/m², `SimCruiseWorldStreamer.cpp:398`) and the unlit roadside fall 7-10 EV under middle grey. Result: 61 % of night pixels at code 0-3 in both night captures. This is the photometric root of cinematographer finding 6.
Fix: change the input from illuminance to an adaptation luminance. Smallest change: `SetArtificialLuminance(cd/m²)` (or convert in the vehicle: L_road ≈ 0.5 cd/m² for a halogen low beam, a named constant with the Erkan source), and add the street-light road luminance when the car is in a lamp pool (0.6-1.2 cd/m², RP-8). Update the test to assert a reference EV100 of about 2 (L = 0.5) instead of the 2..6 band. Then re-tune the night knee (`DisplayEV100`) once, with the cinematographer.
Verify: lab night shot with lamps; read linear scene luminance of the road probe before exposure: 0.3-1 cd/m² average over 0-32 m. Night drive capture: under 25 % of pixels at 0-3; sky above roofs by ≥ 3 codes.
Confidence: code path verified; luminances computed from code constants, not measured in game.

### 2. P1 | The low-beam pattern is attenuated twice, so the near and kerb-side road stays dark
Evidence: `SimCruiseVehicle.cpp:155-156` sets the inner cone 8° and the outer cone 32° (`SimCruiseVehicle.h:129-131`). `SimCruiseVehicle.cpp:164-171` also applies the light function that already holds the full pattern (`SimCruiseLowBeamPattern.cpp:23-34`). Unreal multiplies the light function by its own cone falloff, Square((cos θ − cos 32°)/(cos 8° − cos 32°)). The attenuation radius is the 1 lx distance, 122 m (`SimCruiseVehicle.cpp:158`).
What the player sees (computed): the cone term is 0.79 at 13° off axis, 0.42 at 20°, 0.17 at 25°, 0.02 at 30°. A real low beam spreads to about ±40° to light the kerb and the near verge. At 5 m ahead, a point 3 m to the side gets 0 % of the pattern's value. The 1 lx radius also cuts light by the distance window: -11 % at 60 m, -50 % at 90 m (distances where the beam is near its cut-off and still matters for seeing). The test `SimCruise.Presentation.LowBeamPattern` checks only the pattern, not pattern × cone.
Fix: let the light function shape the beam: inner cone 30°, outer 32° (or 38/40° if the pattern widens), and attenuation radius about 250 m. Keep the peak at 15 000 cd.
Verify: extend `SimCruise.Presentation.LowBeamPattern` to evaluate pattern × Unreal cone term at (elev -2°, az ±20°): ≥ 0.9 of the pattern alone. In the lab, horizontal illuminance on the road at (10 m, 3 m right) ≥ 3 lx.
Confidence: code verified; the cone formula is the engine's spot attenuation as we know it (check in 5.8 shader source).

### 3. P1 | Lumen cannot see the street: shade has no warm bounce, and paint and wet roads reflect only the sky
Evidence: software Lumen on mesh distance fields (`Config/DefaultEngine.ini:16-20`). The streamed tiles have no distance fields (`docs/unreal-architecture.md:707`, `:741`, `:699`). Car parts set `bAffectDistanceFieldLighting = false` (`SimCruiseCarPresentationComponent.cpp:344, 369`). The comment at `SimCruiseWorldTile.cpp:158-161` says editor builds get a distance field; the architecture doc (`:707`) says that build path was dropped. Medium and Low turn Lumen reflections off (`SimCruiseGraphics.cpp`, `Spec`).
What the player sees: off screen, the streets do not exist for GI and reflections. Shade gets blue sky and no light bounced from sunlit facades, which matches the open item "afternoon shade darker and bluer than the photograph" (`docs/unreal-architecture.md:463`: shaded asphalt (53, 67, 86) vs (115, 115, 119)). The NB's dark green clear coat reflects sky where it should reflect buildings, palms and the road. A wet road at night reflects only lamps that are on screen.
Fix (this phase, small): correct the stale comment at `SimCruiseWorldTile.cpp:159`; A/B one evening with `r.Lumen.HardwareRayTracing=1` on the RTX 2070 Super Max-Q at the 28 W cap (hardware Lumen needs no distance fields) and record the cost. Real fix is a next-phase item (N2).
Verify: lab street, 15:39 local, shaded asphalt within 0.5 EV of the January photograph; paint on the turntable shows a facade reflection; cost logged with `SimCruise.Perf` at 28 W.
Confidence: cause verified in code and docs; the share of the shade error it explains is inferred.

### 4. P2 | Daylight frames never reach display white
Evidence: both day captures: p99 luma 210-212, 0.02 % at ≥ 254. A sunlit white roof reads sRGB (200, 194, 178), sand (190, 184, 160) (`buildings-ocean-drive-heights.jpg`, measured). Local exposure highlight contrast 0.55 (`Config/DefaultEngine.ini:34`) plus a band that never closes above reference -0.5 EV (`SimCruiseEnvironment.h:160`).
What the player sees: a tropical noon looks grey and hazy; white stucco is light grey. The top 15 % of the SDR range is unused. The look is close to "phone HDR", not to a mastered picture.
Fix: A/B highlight contrast 0.55 / 0.7 / 0.8 against the Ocean Drive photographs. Judge two numbers together: sunlit albedo-0.8 white at sRGB 225-245, and sky at 25° elevation (the doc's metric). If the sky then clips, hold it with the tonemapper shoulder, not with local exposure. Owner of the final grade: cinematographer.
Verify: noon lab capture: p99 ≥ 225; white probe 225-245; sky hue error not worse than today.
Confidence: measured in captures (time of day of the captures unknown).

### 5. P2 | The exposure constant chain is mixed and undocumented
Evidence: SimCruise meters with K = 12.5 (grey card L = 0.125 × 2^EV) (`SimCruiseDaylightModel.cpp:9, 100-103`). Unreal 4.25+ targets 18 % grey with lens attenuation 0.78 (Epic auto-exposure blog). The band then closes at reference −0.5 EV (`SimCruiseEnvironment.h:160`) and adds +0.2 compensation (`:171`); `r.DefaultFeature.AutoExposure.Bias=1.0` (`DefaultEngine.ini:31`) is overridden by the component.
Risk: the −0.5 and +0.2 were tuned from photographs. Part of them may be a units offset (log2(0.18 / 0.125) = 0.53 EV, if the engine maps EV100 to an 18 % mean). Then any change to the engine version, the lens attenuation cvar or the meter moves the whole look.
Fix: read the 5.8 eye-adaptation shader; write the chain (K, lens attenuation, 18 %) in the `FSimCruiseDaylightModel` header; add one lab test: grey card at clear noon with compensation 0 renders sRGB 117 ± 10.
Verify: the lab capture JSON probe "grey card" plus the new test.
Confidence: offset size unverified (engine source not read).

### 6. P2 | No display target is defined: no SDR contract and no HDR path
Evidence: no `r.HDR.*` setting or `UGameUserSettings` HDR call anywhere in `Source/` or `Config/` (grep). Output is engine default sRGB / Rec.709 (Epic HDR page).
What the player sees: nothing wrong today on an SDR laptop. But the owner asked for "Dolby style" light, and nothing defines what a correct frame is on the display: no paper white, no black-level check, no brightness setting for night (finding 1 makes night depend on the panel's black).
Fix (this phase): write the SDR contract in `docs/unreal-architecture.md`: sRGB/BT.709, D65, 100-nit reference white, the capture checks of this review. HDR output is next-phase work (N1).
Verify: doc review.
Confidence: verified by search.

### 7. P2 | Street lights shine straight down with a flat cone and stay out of the meter
Evidence: `SimCruiseWorldStreamer.cpp:430-441`: 1800 cd uniform to 50°, fading to 74°, aimed at nadir, 24 nearest lamps. Not in the reference EV (cinematographer finding 6; `docs/unreal-architecture.md:473`).
Numbers (computed): 8.5 m pole, 33 m staggered spacing: 25 lx under a lamp, about 14 lx between. RP-8-14 asks 15-23 lx for collector/local intersections with high pedestrian conflict, uniformity 4-6. Average is plausible; the shape is not: a real cobra head (Type II/III) throws its peak at 60-75° from nadir, so a road reads evenly lit, not as bright discs under each pole.
Fix: this phase, only the meter part (finding 1). Next phase: an IES profile (N3).
Verify: lab night: road luminance under and between lamps, Lmax/Lmin ≤ 6.
Confidence: code verified; luminaire distribution of Miami Beach lamps unverified.

### 8. P2 | Moonlight is coloured blue in the light, then desaturated again in the grade
Evidence: `SimCruiseEnvironment.cpp:83-85` sets the moon light (0.86, 0.93, 1.0) for the Purkinje shift; `SimCruiseEnvironment.cpp:457-462` also desaturates for mesopic vision.
Risk: a perceptual effect is applied in the scene (it also tints the atmosphere the moon lights) and again in the grade. Physically moonlight is slightly redder than sunlight.
Fix: moon light white (or slightly warm); do the blue shift in the night grade only (one place, cinematographer).
Verify: full-moon lab shot: grey card chroma in the grade only; A/B with the cinematographer.
Confidence: verified in code; visual size not measured.

## Checked and fine

- Sun at 127.8 klx above the atmosphere, white, 0.533°; transmittance per pixel (`SimCruiseEnvironment.cpp:56-75`, `:379`). Correct practice.
- IES clear/partly/overcast sky, twilight 3.4 lx at -6° and 0.008 lx at -12°, Allen lunar phase law, Koschmieder haze, Mie from visibility (`SimCruiseDaylightModel.cpp`). Tested by `SimCruise.Environment.*`.
- Lamp lenses in nits, signal red kept in the red channel (`SimCruiseCarMaterialLibrary.cpp:888-923`).
- Wet asphalt darkens to 0.6 and gains gloss (`SimCruiseWorldResources.cpp:151-160`). Good; the reflections it needs are limited by finding 3.
- Pre-exposure 7.5 covers EV -4.5..15.5 (`DefaultEngine.ini:48`); moonless band floor -4.55 sits at its edge.
- `lab/daylight` is the Godot-era validation (sky MAE vs road cameras). Useful as a method; not wired to the Unreal build.

## Not checked

- Twilight magenta at -3° (open item in the architecture doc); cause not traced.
- Engine 5.8 shader constants (eye adaptation, spot attenuation); taken from Epic docs and earlier engine knowledge.
- Any frame of the running game; lighting cost at the 28 W cap.

## Next phase proposals (ranked)

These are proposals for the phase after the driving release. Do not start them now.

### N1. Display contract and an HDR10 / PQ output ("Dolby style")
- Goal: one defined picture: SDR BT.709 100-nit reference, and an optional HDR mode in PQ with real highlight headroom.
- Why: "looks exactly like the real car" includes sun glints on the clear coat, chrome and brake lamps. SDR caps them at paper white; HDR shows them at 5-10× diffuse white.
- Scope: settings page entry (via `UGameUserSettings`): HDR on/off, `r.HDR.Display.OutputDevice 3` (ACES 1000-nit ST 2084), `r.HDR.Display.ColorGamut 2` (Rec.2020 container), paper white 203 nits (BT.2408), `r.HDR.UI.CompositeMode 1` and UI level, a black/brightness calibration screen, and both SDR and HDR drive captures (EXR or PQ PNG). SDR stays the reference.
- Acceptance test: on an HDR display measured with a meter or the OS HDR tool: grey card 20-30 nits, white stucco 180-220 nits, sun glint on paint ≥ 600 nits, UI 180-250 nits; SDR captures unchanged (pixel diff 0) with HDR off.
- Risk: the owner's laptop panel may be SDR (unverified); exclusive fullscreen may be needed; UI blending; small GPU cost.
- Size: M.

### N2. Lumen sees the street
- Goal: shade, car paint and wet roads get the light and reflections of the real street.
- Why: dark British Racing Green paint is mostly reflection; shade colour is the most visible daytime error against photos.
- Scope: measure two routes at 28 W: (a) hardware Lumen (no distance fields needed); (b) cheap invisible proxy meshes with distance fields per tile (engine cube scaled to building boxes, not drawn in the main pass; check that Lumen accepts this in 5.8). Choose one per preset.
- Acceptance test: shaded asphalt and stucco within 0.5 EV and 0.02 chroma of the licensed Ocean Drive photo at its hour; paint shows facades in a turntable-on-street shot; cost ≤ +1.5 ms at 28 W High.
- Risk: GPU cost; proxy errors (wrong reflections) are worse than none.
- Size: M-L.

### N3. Night light in real units
- Goal: night drives meter, light and read like a night photograph of the same street.
- Why: cruising real roads at night is a core pleasure; today 61 % of a night frame is black.
- Scope: adaptation luminance from the rendered road (a small luminance probe ahead of the car, read back once a second) in place of the illuminance guess; IES Type II/III profiles for street lights; lamp pools in the meter; halogen vs LED low beams per car (NB halogen, C8 LED, Mustang sealed beam); sky glow from data (light-pollution atlas, licence check). Photometric half of cinematographer N3.
- Acceptance test: road under low beams 0.3-1 cd/m² (0-32 m), lit street 0.6-1.2 cd/m² (RP-8), night histogram: ≤ 25 % pixels at 0-3; blind "photo or game" d' at night falls by 30 %.
- Risk: probe read-back cost; licence of sky-brightness data.
- Size: M.

### N4. Photo-match lighting regression
- Goal: every lighting change is judged by numbers against licensed photos of the same places and hours.
- Why: today the comparison is by hand in the architecture doc and drifts.
- Scope: a script over lab and drive captures: grey-card code value, sunlit white, shade/sun ratio, sky colour at 25°, luma percentiles, % clipped, % crushed; a JSON baseline per scene; run in `check.ps1`.
- Acceptance test: the script flags finding 4 and finding 1 on today's build and passes after the fixes.
- Risk: photo exposure and phone processing differ; compare ratios, not absolutes.
- Size: S.

### N5. The owner's paint, measured
- Goal: the NB's paint matches the real car under sun and shade.
- Why: the owner sees his own car every frame.
- Scope: photograph the owner's car next to a grey card (and a colour checker if one can be borrowed) in sun and shade; fit base colour, flake and clear-coat roughness; turntable A/B.
- Acceptance test: paint hue and value within ΔE2000 ≤ 3 of the photo in the grey-card-normalised image, sun and shade.
- Risk: owner time; metallic flake is hard to fit from photos.
- Size: S.

## References

See `docs/team/lighting-director.md` section 7. Main sources for this review: Epic HDR display output and auto-exposure (4.25 blog); Erkan et al. 2021 (CC BY) for low-beam road luminance; TxDOT tables of ANSI/IES RP-8-14; Dolby Vision colour-grading best practices v4; ITU-R BT.2408.
