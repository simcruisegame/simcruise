# Lighting director: study notes

Role: light, exposure, HDR and colour-science expert for SimCruise.
Question this role answers: is the light in the frame physically correct, and does the picture reach the display the way a mastering colourist ("Dolby style") would deliver it?
Style: ASD-STE100 Simplified Technical English. Short sentences. Active voice.
Status of facts: each fact has a source in section 7. "(unverified)" marks a fact that we did not check in the primary source.
Boundary: the `cinematographer` owns cameras, lens, motion blur and the creative grade. The `magician` owns perception tricks and pop-in. This role owns photometry (lux, cd/m², EV), light sources, the exposure chain, the tone curve, display encoding (SDR and HDR) and the lighting cost on the GPU. Do not repeat their notes. Read them.

## 1. Core idea

Light the world in real units. Meter it like a real camera. Encode it like a mastering suite.
Three layers. Keep them separate.

1. Scene: light sources and materials in physical units (lux, cd, cd/m², albedo).
2. Camera: exposure in EV100. It maps scene luminance to a relative signal.
3. Display: a tone curve and an encoding (sRGB/BT.709 for SDR, PQ for HDR). It maps the signal to nits on a real screen.

Rule: fix an error in the layer where it is. Do not fix a wrong lamp with exposure compensation. Do not fix a wrong exposure with the light colour.

## 2. Rules and numbers

### 2.1 Photometric units in Unreal

- Directional light: illuminance in lux, normal to the beam [L2].
- Sky light: luminance in cd/m² (an HDR value of 1.0 × intensity) [L2].
- Point, spot and rect lights: candela, lumen or unitless. 1 cd = 1 lm/sr [L2].
- Rule: use candela for spot lights. Epic: lumens of a spot light depend on the cone angle; candela does not [L2].
- Emissive material: 1.0 = 1 cd/m² before exposure (SimCruise uses this; `SimCruiseCarMaterialLibrary.cpp:895`).
- Rule: with `SkyAtmosphere`, set the sun at its illuminance above the atmosphere. The atmosphere applies the transmittance per pixel. SimCruise: 127.8 klx at 1 AU (`SimCruiseDaylightModel.cpp:66-70`). (Epic guidance on this is in the SkyAtmosphere docs, unverified page.)
- Rule: keep the sun colour white at the top of the atmosphere. The atmosphere reddens it. Do not add a temperature to the sun as well (double reddening).

### 2.2 Daylight and night numbers (ground, horizontal)

| Condition | Illuminance | Grey-card EV100 (K = 12.5) |
|---|---|---|
| Clear tropical noon, sun + sky | 100-125 klx | 15-16 |
| Overcast noon | 15-30 klx | 12.5-14.5 |
| Sunset, sun at 0° (clear) | about 800 lx | about 8.6 |
| End of civil twilight (-6°) | about 3.4 lx | about 0.6 |
| End of nautical twilight (-12°) | about 0.008 lx | about -8 |
| Full moon high in the sky | about 0.1-0.3 lx | -4 to -2.5 |
| Moonless, clear, no city | about 0.001 lx | about -11 |

Source: IES daylight model as coded in `SimCruiseDaylightModel.cpp`, checked by `SimCruise.Environment.Daylight.Illuminance` [L6, L7].

- Meter equation (ISO 2720): EV100 = log2(L × 100 / K), K = 12.5. A grey card: L = 0.18 × E / π [L6].
- Unreal histogram: the target exposure is the mean log luminance of the pixels between Low % and High %. Defaults since 4.25 are 10 % and 90 % [L3].
- Unreal since 4.25 targets 18 % middle grey; grey renders at about sRGB 117 [L3].
- Lens attenuation `r.EyeAdaptation.LensAttenuation` defaults to 0.78; it makes the EV100 ↔ luminance mapping "unitless". Older value 0.65 gave the factor 1.2 [L3]. Rule: write the full constant chain (K, q, 18 %) in one place in code. Check it against the engine shader source for the engine version in use.
- Frostbite chain, for reference: EV100 from mean luminance with K = 12.5; exposure = 1 / (1.2 × 2^EV100) [L5].

### 2.3 Artificial light (night)

- Road lighting, ANSI/IES RP-8-14 via TxDOT [L8]:
  - Intersections, collector/local, high pedestrian conflict: 22.6 lx average.
  - Local/local, high conflict: 19.4 lx. Uniformity Eavg/Emin 3-6.
  - Continuous roadway luminance: commercial arterial 1.2 cd/m², commercial collector 0.8, commercial local 0.6.
- Rule: a lit commercial street renders its asphalt at about 0.6-1.2 cd/m². Check the rendered value, not only the lamp setting.
- Road luminance from low beams alone, 0-32 m ahead: 0.41 cd/m² is the minimum drivers accept; 0.88 cd/m² reads "bright" at standstill; a full-output LED low beam reached 2.08 cd/m² (Erkan et al. 2021) [L9]. A 2001 halogen low beam gives less (unverified; assume 0.3-0.8 cd/m²).
- Rule: the driver adapts to the road luminance, about 0.3-1 cd/m² with low beams on an unlit road. Use that luminance for the night meter. Do not use the illuminance on a card that faces the lamps.
- Halogen low beam: peak about 15-20 kcd, CCT about 3200 K (unverified for the NB; SimCruise uses 15 000 cd).
- LED street luminaires: 3000-4000 K; high-pressure sodium about 2000-2100 K (unverified).
- Real street luminaires (cobra heads, IES Type II/III) throw their peak at about 60-75° from nadir, not straight down. Use an IES profile when cost allows; Unreal spot/point lights take IES textures (unverified for runtime-made lights).
- Unreal spot attenuation: the cone term is Square(saturate((cos θ − cos outer) / (cos inner − cos outer))). It multiplies any light function. The distance window is Square(saturate(1 − (d/r)^4)) for inverse-square lights (engine shader, unverified for 5.8).
- Rule: a light function that already holds the beam pattern needs inner ≈ outer cone, or the pattern is attenuated twice.
- Rule: set the attenuation radius where the light is negligible on screen, not at 1 lx. At half the radius the window already takes 12 %; at 0.75 of it, 50 %.

### 2.4 Tone curve and SDR output

- SDR target: BT.709 primaries, D65, sRGB/BT.1886 encoding, reference white about 100 cd/m² (Dolby SDR trim target is Rec.709 at 100 nits) [L10].
- Unreal default output: `r.HDR.Display.OutputDevice` 0 (sRGB), gamut 0 (Rec.709/sRGB D65) [L1].
- Unreal's filmic tonemapper is ACES-based (Slope, Toe, Shoulder, Black/White clip). See the cinematographer notes for the grade.
- Rule: a sunlit near-white surface (albedo 0.7-0.8) must land near the top of the SDR range (sRGB 225-245). Specular glints, the sun disc and lamps may clip. If daylight frames never pass sRGB 215, the curve or local exposure is too compressive: the image looks grey and hazy.
- Rule: a night frame must keep shadow detail above code 3. More than about a quarter of pixels at 0-3 means crushed blacks on most laptop panels (threshold is our rule of thumb, unverified).
- Rule: local exposure highlight contrast below about 0.6 flattens large bright areas (white walls, sand) as well as the sky. Epic gives 0.6-1 as the typical range (see cinematographer notes).

### 2.5 HDR output ("Dolby style")

- PQ (SMPTE ST 2084) encodes absolute luminance, 0.0001 to 10 000 cd/m² [L11].
- HDR reference white (graphics white, diffuse white) is 203 cd/m², at 58 % PQ signal and 75 % HLG (ITU-R BT.2408) [L12; number from memory, verify in the report text]. 18 % grey then sits near 26 cd/m² (unverified).
- Dolby Vision grading: ST 2084 working space; mastering gamut P3-D65 or Rec.2020; a 1000-nit reference monitor is the common example; the Rec.709 100-nit SDR trim comes first and anchors the trims; do not clip highlights in the HDR master [L10].
- Per-shot metadata L1 (min, mid, max) drives the display mapping. HDR10 static metadata: MaxCLL, MaxFALL [L10].
- Unreal [L1]:
  - `r.HDR.EnableHDROutput 1` recreates the swap chain for HDR.
  - `r.HDR.Display.OutputDevice` 3 = ACES 1000-nit ST 2084 (PQ), 4 = 2000-nit PQ, 5/6 = scRGB.
  - `r.HDR.Display.ColorGamut` 1 = DCI-P3 D65, 2 = Rec.2020 D65.
  - `r.HDR.UI.CompositeMode 1` and `r.HDR.UI.Level` (Epic suggests 1.5-2) keep the UI from looking washed out.
  - Set through `UGameUserSettings` at run time. Epic's page says borderless/windowed HDR does not work; check this for 5.8 (unverified).
- Rule: the SDR picture is the reference. HDR adds headroom above diffuse white. It must not change mid-tones. A 203-nit paper white keeps HDR mid-tones close to SDR on a bright display.
- Rule: real highlight levels for this game: sun glint on paint and chrome, lamp lenses (brake lamp lens thousands of cd/m²), wet-road glare. These go into the 203-1000 nit headroom.
- Rule: never ship HDR without an SDR check of the same frame, and a UI brightness setting.

### 2.6 Physically based materials that carry the light

- Car paint: base coat under a clear coat. Clear coat F0 about 0.04 (IOR 1.5). Clear-coat roughness about 0.02-0.06 on a polished car (unverified range).
- A dark paint (British Racing Green) is mostly reflection: what the clear coat reflects decides the look. Missing environment in reflections makes dark paint look like plastic.
- Wet asphalt: albedo falls to about 0.6 of dry (Lekner and Dorf 1988, as cited in `SimCruiseWorldResources.cpp:151`); a water film makes it a mirror at grazing angles.
- Asphalt dry albedo about 0.05-0.12; concrete 0.25-0.35; fresh white paint 0.8 (unverified range; the lab uses 0.085 and 0.30).

### 2.7 Global illumination and reflections on the 28 W GPU

- Software Lumen traces mesh distance fields. A mesh without a distance field is invisible to off-screen Lumen traces; only screen traces see it.
- Lumen traces reflection rays on every pixel with roughness below 0.4 [domain-ideas F27].
- Hardware Lumen traces triangles and needs no distance fields. Its cost on the RTX 2070 Super Max-Q at a 28 W cap is not measured.
- At the 28 W cap the GPU runs about 2.6 times slower than the measured table (`docs/unreal-architecture.md:767`). High (9.7 ms at 90 W) is then about 25 ms.
- Rule: a lighting feature must state its cost at 28 W before it ships.

### 2.8 Colour

- Working colour space in Unreal: linear Rec.709/sRGB primaries, D65 white.
- Rule: author albedo from sRGB chips with an sRGB-to-linear conversion (`FLinearColor::FromSRGBColor`; the lab does this).
- Rule: signal colours (brake red, amber) can lie outside sRGB. Keep them saturated and bright in the red channel only, so they do not tone-map to salmon (SimCruise does this).
- Rule: perceptual shifts (Purkinje blue at night, mesopic desaturation) belong in the grade, not in the light colour. Physically, moonlight is slightly redder than sunlight (unverified CCT about 4100 K).
- Rule: white balance follows the illuminant a camera would balance for. Sunlit grey in a daylight photo is slightly warm (Commons Ocean Drive photo: sunlit asphalt sRGB 136, 130, 121; `docs/unreal-architecture.md:461`).

## 3. How to judge SimCruise

### 3.1 Files to read first

1. `Source/SimCruise/Private/Environment/SimCruiseDaylightModel.cpp` and its header (units, meter, night display curve).
2. `Source/SimCruise/Private/Environment/SimCruiseEnvironment.cpp` (`UpdateSky`, `ApplyExposure`, `ApplyFog`, `SetArtificialIlluminanceLux`).
3. `Source/SimCruise/Public/Environment/SimCruiseEnvironment.h` (exposure band, compensation, aerosol).
4. `Config/DefaultEngine.ini` lines 7-52 (auto exposure, local exposure, Lumen, pre-exposure).
5. `Config/DefaultScalability.ini` and `SimCruiseGraphics.cpp` (Lumen per preset).
6. `Source/SimCruise/Private/Vehicle/SimCruiseVehicle.cpp:135-221` (low beams, night adaptation) and `Public/Vehicle/SimCruiseVehicle.h:127-133`.
7. `Source/SimCruise/Private/Presentation/SimCruiseLowBeamPattern.cpp` (beam pattern).
8. `Source/SimCruise/Private/World/SimCruiseWorldStreamer.cpp:380-480` (street lights, sky glow).
9. `Source/SimCruise/Private/World/SimCruiseWorldResources.cpp` (asphalt wetness, window and facade emissive).
10. `Source/SimCruise/Private/Presentation/SimCruiseCarMaterialLibrary.cpp` (paint, lamps in nits).
11. `docs/unreal-architecture.md` sections "Light in physical units", "Against photographs of Ocean Drive", "Light that is not the sky's".
12. `Source/SimCruise/Private/Environment/Tests/SimCruiseEnvironmentTests.cpp`, `Private/Tests/DriveIntegrationTests.cpp` (`SimCruise.Drive.LowBeams`).

### 3.2 Checks

1. Every light has a unit and a source. Search for `SetIntensity` without `SetIntensityUnits`.
2. Sun at top-of-atmosphere lux, white. Moon with phase law. No double reddening.
3. Grey card at clear noon: reference EV100 15-16. Rendered grey card in the lab (probe in the capture JSON) at sRGB 117 ± 10 with compensation 0.
4. Sunlit white (albedo ≥ 0.7) reaches sRGB 225-245 in a daylight capture. p99 of luma above 225.
5. Night: rendered asphalt under low beams 0.3-1 cd/m² averaged 0-32 m ahead; under street lights 0.6-1.2 cd/m². Measure with a luminance probe in linear scene colour before exposure.
6. Night capture: fewer than 25 % of pixels at code 0-3; sky above roofs by ≥ 3 codes.
7. Spot lights with a light function: inner cone ≥ outer − 2°. Attenuation radius ≥ 2 × the distance that matters.
8. Exposure chain constants written in one place and tested.
9. Lumen sees what it must reflect: check `bAffectDistanceFieldLighting`, distance-field generation of runtime meshes, `r.Lumen.*.Allow` per preset.
10. Display: output device and gamut set on purpose; HDR only with paper white 203 nits, UI level, and an SDR check.
11. Cost of each lighting change at the 28 W cap.

### 3.3 Test methods

- Lighting lab: `-SimCruiseLab -SimCruiseLabShots=name@<UTC> -SimCruiseLabViews=street,props,road`. Read the probe points in the JSON sidecar. Convert sRGB to linear before you compare.
- `ShowFlag.VisualizeHDR 1` and `r.EyeAdaptation.VisualizeDebugType 1`: show the histogram, target EV100 and current EV100 [L3].
- `SimCruise.Env.Report`: logs reference EV and band.
- Drive capture: `-SimCruiseDriveCapture` at noon and at 22:00 local. Run the histogram script below on the PNGs.
- Photo match: licensed photos of the same place and hour (Commons with CC licence, Panoramax). Compare sunlit grey, shade grey, sky at 25° elevation, histogram percentiles. Same metric in both.
- Histogram script (Python, Pillow + NumPy): luma Y = 0.2126 R + 0.7152 G + 0.0722 B on sRGB codes; report p1, p50, p99, % at ≥ 254, % at ≤ 3.
- Automation: `SimCruise.Environment.*`, `SimCruise.Drive.LowBeams`, `SimCruise.Presentation.LowBeamPattern`.

## 4. Common failure modes

- Illuminance used where luminance is meant (a card facing a lamp vs. a road at grazing light).
- A beam pattern attenuated twice (light function × cone falloff).
- Exposure compensation used to hide a calibration offset. The next change then breaks the look.
- Local exposure set so strong that whites never reach white: a "hazy" SDR picture.
- Reflections that only see the sky: dark car paint looks like plastic; wet roads look matte at night.
- Night crushed to black on a laptop panel: fine on an OLED, invisible on an IPS.
- HDR switched on without paper white and UI level: washed-out UI, over-bright mid-tones.
- Perceptual shifts baked into light colours, then graded again.
- A lighting feature tuned at 90 W that costs 2.6 times more at 28 W.

## 5. Severity guide

- P0: wrong units that break exposure (black or white frame), or a crash from lighting code.
- P1: light or exposure error visible in most frames of a normal drive (day or night).
- P2: visible in some conditions, or a calibration risk with no visible error yet.

## 6. Known open items (2026-11-12)

- Twilight goes magenta at -3° (`docs/unreal-architecture.md:464`). Cause not traced.
- Shade is darker and bluer than the reference photo (`docs/unreal-architecture.md:463`).
- Tile meshes have no distance fields; reflections of the street show only the sky (`docs/unreal-architecture.md:707, 741`).

## 7. References

- [L1] Epic Games, High Dynamic Range Display Output: https://dev.epicgames.com/documentation/en-us/unreal-engine/high-dynamic-range-display-output-in-unreal-engine
- [L2] Epic Games, Using Physical Lighting Units: https://dev.epicgames.com/documentation/unreal-engine/using-physical-lighting-units-in-unreal-engine
- [L3] Epic Games tech blog, How Epic Games is handling auto exposure in 4.25 (2020): https://www.unrealengine.com/en-US/tech-blog/how-epic-games-is-handling-auto-exposure-in-4-25
- [L4] Epic Games, Auto Exposure: https://dev.epicgames.com/documentation/en-us/unreal-engine/auto-exposure-in-unreal-engine
- [L5] Lagarde and de Rousiers, Moving Frostbite to Physically Based Rendering 3.0, SIGGRAPH 2014 course notes: https://blog.selfshadow.com/publications/s2014-shading-course/frostbite/s2014_pbs_frostbite_slides.pdf
- [L6] ISO 2720:1974, General purpose photographic exposure meters (K = 12.5). Paid standard; value as used in the code and in [L5].
- [L7] IES Lighting Handbook daylight model (as cited in `SimCruiseDaylightModel.h`). Not checked in the primary text.
- [L8] TxDOT Highway Illumination Manual, Illumination Levels (ANSI/IES RP-8-14 tables): https://www.txdot.gov/manuals/trf/hwi/lighting_design_and_layout/illumination_levels-i1004163.html
- [L9] Erkan et al., Determination of Speed-Dependent Roadway Luminance for an Adequate Feeling of Safety at Nighttime Driving, Vehicles 3(4) 2021, CC BY: https://www.mdpi.com/2624-8921/3/4/49
- [L10] Dolby, Dolby Vision Best Practices for Color Grading v4: https://professional.dolby.com/siteassets/pdfs/dolby_vision_best-practices_colorgrading_v4.pdf
- [L11] SMPTE ST 2084:2014, High Dynamic Range EOTF of Mastering Reference Displays (paid standard; range from the standard's scope, unverified here).
- [L12] ITU-R Report BT.2408, Guidance for operational practices in HDR television production: https://www.itu.int/dms_pub/itu-r/opb/rep/R-REP-BT.2408-6-2023-TOC-HTM-E.htm
- [L13] TRL PPR2069, Glare from road vehicle lighting: https://www.trl.co.uk/uploads/trl/documents/PPR2069-Glare-from-road-vehicle-lighting.pdf (not read in full).
- [L14] Lighting technology survey (surfaces, GI, shadows, reflections, tone mapping, HDR, upscaling; fit at 28 W; top 8 changes): `docs/team/research/lighting-tech.md`.
- Licences: Epic and Dolby pages are read for reference only. Erkan et al. is CC BY. ISO, SMPTE and IES standards are paid; quote numbers only, with the source.

## What I now know (onboarding, 2026-11-14)

**How my area works today.**
- Sun position: `FSimCruiseSolarPosition::Sun/Moon` (NOAA/Meeus, refraction, topocentric moon) works for any latitude, longitude and UTC. The NREL SPA test passes to 0.03°. `UpdateSky` runs every 2 s (`SimCruiseEnvironment.cpp:372-407`).
- Sun light: 1361 W/m² × 93.9 lm/W = 127.8 klx / R², white, set at the top of the atmosphere. `SkyAtmosphere` applies the transmittance per pixel (`SimCruiseDaylightModel.cpp:7-8,66-70`; `SimCruiseEnvironment.cpp:383-390`). Shadows are on above -1°. The light is off below -18°.
- Moon: Allen's phase law, 0.32 lx at full moon (`SimCruiseDaylightModel.cpp:118-125`). The light colour is (0.86, 0.93, 1.0), so the Purkinje shift is in the light and not in the grade (`SimCruiseEnvironment.cpp:92`).
- Meter: the IES daylight model gives the horizontal lux. Then an 18 % card, K = 12.5, gives `ReferenceEV100` (`SimCruiseDaylightModel.cpp:88-109`). `DisplayEV100` has a knee at EV 10 and a slope of 0.55 below it (`:111-116`). Histogram 10-90 %. Band is Display-3 to Display-0.5. Bias is +0.2, plus -0.3 by day (`SimCruiseEnvironment.cpp:437-451`; `SimCruiseEnvironment.h:156-181`). Saturation falls to 0.35 at night (`:479-481`).
- Haze: Mie = 3.912/V - 0.0116/km, floor 0.014/km. If CAMS AOD exists and V ≥ 10 km, Mie = τ / 0.7 km. SSA 0.93, g 0.76, Ångström 0.7 (`SimCruiseEnvironment.cpp:31,257-274`; `.h:193-197`). Below V = 8 km, the remaining extinction is a ground fog with a 150 m scale height (`:296-315`).
- Clouds: Epic's simple material. The layer is fixed at 1.2 km to 7.2 km ("Florida fair-weather cumulus") (`SimCruiseEnvironment.cpp:329-331`). The coverage knots run from CLR -0.5 to OVC 0.35 (`SimCruiseWeather.cpp:104`).
- Weather input: `USimCruiseLiveWorldSubsystem::ApplyWeather` merges Open-Meteo, a METAR within 40 km and CAMS, then calls `SetWeather` (`SimCruiseLiveWorldSubsystem.cpp:833-848`). The road wets with τ = 180 s and dries with τ = 900 s (`SimCruiseWeather.cpp:91-97`).
- Time: `StartInDaylight` sets 10:00 local solar time from longitude/15 (`SimCruiseEnvironment.cpp:204-218`; `.h:62`). No time-zone data is necessary.
- Altitude: world Z is the height above sea level in NAVD88 (`SimCruiseTileBuilder.cpp:25`). `ElevationM` stays 0 in normal play, so the atmosphere's ground is at Z = 0. Rayleigh at altitude is correct.
- Street lights: `BuildStreetLights` puts a pole every 33 m, staggered side to side, on every non-service road edge that is not rural, with 8.5 m poles or 5.2 m Deco poles (`SimCruiseTileDetail.cpp:1261-1345`). A pool of 24 spot lights is used: 1800 cd straight down, cone 50/74°, radius 32 m, 3000 K, no shadows. The lights fade in as the sun goes from 2° to -4° (`SimCruiseWorldStreamer.cpp:402-480`). Sky glow = 0.06 cd/m² × min(lamps/300, 1) (`:388-399`).
- Low beams: 15 000 cd per lamp, 3200 K, aimed 1.5° down, cone 30/32°, radius 250 m. A light function gives a -0.57° cut-off and a 15° rise on the kerb side (`SimCruiseVehicle.cpp:194-224`; `SimCruiseLowBeamPattern.cpp:24-31`). The lamps come on when the sun is below -1° (`SimCruiseVehicle.cpp:622-631`). The meter adds 0.5 cd/m² × π / 0.18 = 8.7 lx (`SimCruiseVehicle.h:153-155`). The street lights do not feed the meter.

**Does it work at a random US place?**
- General: sun, moon, twilight, exposure and time of day work at any latitude. Anchorage in December, at 10:00 solar time, has the sun at about 2° (computed). The reference EV is 10.8 when clear and 9.0 when overcast, so the frame is low-sun daylight and not black (computed). At Hilo at noon the reference is 15.9 (computed). The weather feeds cover the whole US. At Casselton or Welch there is often no METAR within 40 km, so Open-Meteo is the base.
- Hard-wired to sea level: haze and ground fog have their reference at Z = 0, because the environment spawns at `FTransform::Identity` (`SimCruiseEnvironmentSubsystem.cpp:312`). This is the ratio at the car to the observed value (computed; elevations are estimates):
  - Ground fog, exp(-h/150 m): Rawlins (2100 m) about 1e-6, Delta (1400 m) 1e-4, Welch (400 m) 0.07, Gilbert (380 m) 0.08, Ankeny (290 m) 0.14, Seattle (120 m) 0.45.
  - Mie, exp(-h/0.7 km): Rawlins 0.05, Delta 0.14, Welch 0.56.
  - Result: observed mist or smoke is lost inland. Only coastal places (Miami, Stonington, Hilo, Anchorage) get it right.
- Hard-wired to Miami: the cloud base is 1.2 km above sea level. At Rawlins and Delta the car is inside the cloud layer, and at Hilo the trade-cumulus base is too high (estimate; the engine rule that clouds use the atmosphere's planet is unverified). Street lights are one Miami Beach spec: 3000 K, 1800 cd straight down, 33 m spacing. The sky glow uses the Miami Beach count of 300 lamps as full scale, so the Bronx glows the same as Selma (estimate). Ground albedo is 0.12 and the Ångström exponent is 0.7 (marine) everywhere. A snow-covered Rawlins or Anchorage gets no brighter bounce from the ground.
- Poor map data: the street-light rule reads only the road-edge kind, not OSM `lit=*` or `highway=street_lamp`. Kerbed cul-de-sacs in Ankeny and Selma's side streets are all lit every 33 m (estimate). The Welch hollow with "Rural" edges gets no lamps. The tests use only Miami (`SimCruiseEnvironmentTests.cpp:90-94`). No test checks altitude, high latitude or polar night.

**Main gaps against "realistic everywhere".**
1. P1: Haze and ground fog have their reference at sea level, not at the ground under the car (`SimCruiseEnvironment.cpp:254-272,314-315`). Observed fog, mist and wildfire smoke are lost at most inland places.
2. P1 (estimate): The cloud layer is a fixed 1.2-7.2 km above sea level (`SimCruiseEnvironment.cpp:329-331`). High places sit in the cloud. The METAR ceiling and the Open-Meteo low/mid/high cloud data are not used.
3. P2: One street-light spec everywhere, with no open-data lamp source (`SimCruiseTileDetail.cpp:1265-1268`; `SimCruiseWorldStreamer.cpp:412-438`). Computed for Lambertian asphalt (ρ 0.085): about 25 lx at the pole base and about 1 lx between staggered poles, so Lmax/Lmin ≈ 25 against the RP-8 limit of 6. Lamps do not feed the meter, although `docs/unreal-architecture.md` says they should.
4. P2: The sky glow is scaled to Miami Beach and not to measured night light (`SimCruiseWorldStreamer.cpp:388-399`). NASA Black Marble VIIRS (public domain, whole US) could set it per place (licence not checked in detail).
5. P2: These do not come from the place: ground albedo 0.12 with no snow (`SimCruiseEnvironment.h:189`) and Ångström 0.7 (`SimCruiseEnvironment.cpp:31`). `-SimCruiseElevation` moves the atmosphere down a second time, because world Z is already above sea level (`SimCruiseEnvironment.cpp:254`; `SimCruiseTileBuilder.cpp:25`).
6. P2: There are no environment tests for altitude, 61° N or 19° N. `StartInDaylight` at 10:00 solar time gives night above the Arctic Circle in winter.

**My first three actions.**
1. Put haze and fog at the ground. In `SimCruiseEnvironment.cpp`, add an observer ground height, fed from `SimCruiseVehicle.cpp:451` (HeightM). Scale the Mie by exp(h/H) and set the HeightFog Z to h. Done when a new test `SimCruise.Environment.Sky.HazeAtAltitude` gives an extinction at the observer of 3.912/V ±5 % at h = 0, 400 and 2100 m. Also done when `scripts\preflight.ps1 -Roads us-rawlins-wy,us-welch-wv,us-seattle-wa -ExtraArgs "-SimCruiseVisibilityKm=2"` sheets show the same fog contrast at all three places.
2. Get the cloud base from the place. In `ApplyClouds`, set bottom = ground km + base above ground. Use the METAR ceiling or Open-Meteo low cloud, else a climate default. Done when a unit test gives the base above ground for h = 2100 m. Also done when `scripts\preflight.ps1 -Roads us-rawlins-wy,us-delta-ut,us-hilo-hi -ExtraArgs "-SimCruiseCloud=0.78"` sheets show a cloud base above the terrain and clear air at the car.
3. Do a light audit at random places. Run `scripts\preflight.ps1 -Roads random -RandomCount 3`, then `-Roads us-anchorage-ak -Utc 2026-12-21T22:00:00Z` (solar noon), then a night pass for `us-bronx-ny,us-ankeny-ia` at local 22:00 solar time. Measure with a Pillow/NumPy histogram script and probe patches. Done when every daylight sheet has luma p99 ≥ 225 and every night sheet has ≤ 25 % of pixels at codes 0-3. Record the road Lmax/Lmin under the street lights.

**Docs that are wrong or old.**
- `docs/unreal-architecture.md` "Light that is not the sky's" says 33 lx and EV100 3.9 (band 3.7..6.2). The code has 8.7 lx, which gives EV100 2.0, display 5.6 and band 2.6..5.1 (computed; `SimCruiseVehicle.h:153-155`). It also says "street lighting, when the world stream places it". The stream places lamps now, but the code does not add them to the meter.
- `docs/unreal-architecture.md` "Camera" says "+0.7 compensation" and a band around the reference. The code has +0.2, plus -0.3 by day, and a band around `DisplayEV100` (`SimCruiseEnvironment.h:172,181`; `.cpp:437-443`).
- `docs/unreal-architecture.md` "Haze" says the scale height is 1.2 km. The code has 0.7 km (`SimCruiseEnvironment.h:193`).
- `docs/team/handoff-world-look-2026-11-13.md` says motion blur amount 0.3 and max 2 %. The code has 0.15 and 1.0 (`SimCruiseEnvironment.cpp:472-474`).
