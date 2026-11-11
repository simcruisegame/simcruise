# Unreal client architecture

How the Unreal client (`SimCruise.uproject`, C++ module `Source/SimCruise`) is put together, one section per area. Building, launching and running the tests are in [unreal-building.md](unreal-building.md).

## Light, sky and rendering

Code: `Source/SimCruise/{Public,Private}/Environment/`. Config: the rendering part of `Config/DefaultEngine.ini` and `Config/DefaultScalability.ini`. No assets: the sky is built in C++ at run time from engine components and Epic's engine content (`/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst`).

### World frame

The sun and moon are placed in an east-south-up frame: **+X east, +Y south, +Z up** (north is -Y), the left-handed frame Cesium for Unreal uses for a local tangent plane. Unreal yaw 0 looks east; a compass heading *h* is yaw *h* - 90. `ASimCruiseEnvironment::NorthYawDeg` turns the map if a world stream ever puts north elsewhere. `FSimCruiseSolarPosition::ToWorldDirection` / `FromWorldDirection` convert azimuth and elevation.

### Pieces

| Class | Job |
|---|---|
| `FSimCruiseSolarPosition` | Sun from latitude, longitude and a UTC instant with the NOAA solar-calculator algorithm (Meeus), about 0.01° (the test reproduces NREL's SPA reference case to 0.03°), with NOAA's refraction. Moon from Meeus chapter 47 (largest terms, about 0.1°) with topocentric parallax and the phase of chapter 48. `SunFractionalYear` keeps the Godot client's formula (`game/SurveyDaylight.gd` `solar_for_day`) for comparison: its Spencer declination runs about 0.45° low in late April. |
| `FSimCruiseDaylightModel` | Photometry in SI units: extraterrestrial sun 1361 W/m² × 93.9 lm/W = 127.8 klx at 1 AU; IES clear / partly cloudy / overcast daylight for the horizontal illuminance; log-linear twilight (about 3.4 lx at -6°); Allen's lunar phase law (0.32 lx full moon above the air); ISO 2720 reflected-light EV100; Koschmieder visibility (2 % contrast). |
| `FSimCruiseWeather`, `FSimCruiseWeatherConditions` | Reads the object `helper/conditions.mjs` writes (`cloud`, `visibility_m`, `wet`, `station`, …; an optional raw `wx` string splits rain from snow). A METAR "10SM" cap is not taken as measured haze. Road surface water rises with a 3-minute time constant in rain and dries with 15 minutes (proposal 18). Maps sky cover to the cloud material's coverage. |
| `ASimCruiseEnvironment` | The sky actor: sun and moon directional lights (only one casts virtual shadow maps at a time), `SkyAtmosphere`, volumetric clouds, height fog, a sky light with real-time capture and cloud occlusion, and an unbound post-process volume for the camera. `SetPlace`, `SetUtcTime` / `FollowSystemClock` (default: the real clock), `TimeScale`, `SetWeather`, `GetRoadWetness`, `GetSunDirection`, `Describe`. |
| `USimCruiseEnvironmentSubsystem` | Spawns one environment in every game world (unless the level has one), turns off any other sun, sky, cloud, fog and sky light in the level and in every streamed level or World Partition cell added later (`FWorldDelegates::LevelAddedToWorld`), applies the command line and the graphics preset, and owns the `SimCruise.*` console commands. |
| `FSimCruiseGraphics` | The Low / Medium / High / Ultra presets and the auto-pick. |
| `FSimCruisePerfSampler` | Frame, GPU (RHI timestamp queries, what `stat unit` shows), game- and render-thread times over a window; logs `SimCruisePerf:` and appends `Saved/Profiling/SimCruisePerf.csv`. |
| `ASimCruiseLightingLab` | The measurable test street and its capture / timing script (below). |

### Light in physical units

- **Sun**: 127.8 klx scaled by 1/R² (Earth-Sun distance from the ephemeris), angular diameter 0.533°, white; `SkyAtmosphere` applies the per-pixel transmittance, so low suns redden and dim by themselves. Below -1° it stops casting shadows; below -18° it is switched off.
- **Moon**: Allen's phase law × (mean distance / distance)², slightly cool (the eye's Purkinje shift; physically it is a little redder than the sun). It is the second atmosphere light (index 1), casts the shadows once the sun has set, and is off while the sun is above 10°.
- **Sky light**: real-time capture of the rendered atmosphere and clouds, lower hemisphere from the atmosphere's lit ground (`GroundAlbedo` 0.12), cloud ambient occlusion over 50 km.
- **Haze**: the atmosphere's Mie (aerosol) extinction comes from the visibility: 3.912 / V minus clean-air Rayleigh (0.0116 /km at 550 nm), single-scattering albedo 0.93, Ångström exponent 0.7, scale height 1.2 km, asymmetry 0.76. Unreal's default aerosol (0.0044 /km) is an 880 km visibility; ours defaults to 40 km (aerosol optical depth about 0.1) when nothing is observed. Below 8 km the rest of the extinction becomes a 150 m ground fog (`ExponentialHeightFog` density = σ[1/km] / 100, because Unreal stores density × 1000 per centimetre).
- **Clouds**: Epic's simple volumetric cloud material, layer 1.2–7.2 km (Florida fair-weather cumulus base), coverage from the sky cover. Its `Cloud_GlobalCoverage` is an offset, not a fraction: a sweep of lab captures put the knots at CLR -0.5, FEW (0.19) -0.25, SCT (0.44) -0.12, BKN (0.78) 0.0, OVC 0.35 (0.2 still let the sun through).
- **Camera**: histogram auto exposure in EV100 (`r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange`), metering the middle 10–90 % of the histogram. The environment computes the reference EV100 (an 18 % grey horizontal card under this sky; 15.6 at a clear Miami noon, "sunny 16") and limits the camera to reference -3 … -0.5 EV with +0.7 compensation. The band comes from photographs: on sunny Ocean Drive, Commons EXIF shows cameras exposing 1.1 EV (December noon, f/1.8 1/2527 s ISO 50) and 1.6 EV (January afternoon, shaded street, f/3.6 1/800 s ISO 160) below the grey-card reading, never above it. Below reference EV 10 the camera compensates only 55 % of the loss (`DisplayEV100`), so a full-moon night shows about 6 EV darker than day rather than as daylight, and colour fades with the light (saturation down to a third by EV -2, rod vision). Local exposure 0.8 / 0.8 compresses the tropical contrast as a phone camera's HDR does; bloom 0.25, no lens flare, vignette 0.15, no chromatic aberration.
- **Renderer** (`DefaultEngine.ini`): Lumen global illumination and reflections on mesh distance fields (software Lumen; hardware ray tracing is off), virtual shadow maps, TSR, `SkyAtmosphere` affecting the height fog, no static lighting.

### Graphics presets

Each preset sets every Unreal scalability group to one level and the TSR screen percentage; `DefaultScalability.ini` adds the volumetric-cloud shadow and sky-occlusion budgets and gives Ultra a 100 % TSR history.

| Preset | Scalability | Screen % (TSR) | Lumen GI / reflections | Cloud shadow map / steps |
|---|---|---|---|---|
| Low | 0 | 50 | off / off (sky light + distance-field AO) | 192 / 12 |
| Medium | 1 | 58 | on / off | 256 / 16 |
| High | 2 | 67 | on / on | 384 / 24 |
| Ultra | 3 | 75 | on / on (Epic quality) | 384 / 32 |

Measured in the lighting lab on the owner's RTX 2070 Super with Max-Q Design (8 GB, driver 32.0.16.2114, on mains power, GPU power-capped at about 90 W and 1650 MHz), 1920 × 1080 fullscreen, i9-10885H, Development editor build in `-game`, 10–12 s windows:

| Scene | Low | Medium | High | Ultra |
|---|---|---|---|---|
| Clear noon, street view (FEW cloud) | 225 fps (GPU 3.2 ms) | 141 fps (6.1 ms) | 95–99 fps (9.7 ms) | **72 fps** (GPU 13.6 ms, frame p95 15.4 ms) |
| Broken cloud (BKN), 17:30 EDT, props view | 198 fps (4.7 ms) | 123 fps (7.7 ms) | 88 fps (11.0 ms) | **65 fps** (GPU 15.1 ms, frame p95 17.4 ms) |

Before the budgets, Ultra ran 52 fps (19.0 ms): `ProfileGPU` showed the cloud shadow map and cloud sky occlusion at 6.0 ms (Unreal's default 128-step march; the cost follows the map size, 4.6 ms at 768 texels) and TSR at 4.0 ms (a 200 % history at 3840 × 2160 for a 1080p output). The lab street is light (about 1 500 boxes, no foliage or cars), so streamed real places will cost more; these numbers are the lighting's own cost. Windows where another Unreal process shared the GPU (every pass, even tonemapping, took 3–10 times longer) were discarded; repeat any run that disagrees with its twin. A review re-run of the broken-cloud props view (1920 × 1080 windowed, final config) gave Ultra 63–65 fps (frame p95 17.5–18.6 ms) and High 90 fps.

**Auto-pick** (`FSimCruiseGraphics::AutoPick`, run once per process unless `-SimCruiseGraphics=` names a preset or `keep`; skipped without a renderer (`-nullrhi`, servers) and in Play-In-Editor, which shares the editor's own scalability, unless a preset is named): software and integrated adapters → Low; RTX 40/50, RTX 30 (3050: Medium), RX 7000/9000, RTX 2070/2080, RX 6000 → Ultra; RTX 2060 → High; Arc, GTX 16, RX 5000, Apple M → Medium; GTX 10 → Low. A laptop part (Max-Q, Laptop, Mobile) is one tier below its desktop name, so the owner's RTX 2070 Super Max-Q starts on **High**. Ultra needs 7 GB of VRAM, High 4.5 GB. Unknown adapters use Unreal's synthetic benchmark.

### Lighting lab

`-SimCruiseLab` builds a measurable street at the world origin from engine basic shapes: an asphalt road (albedo 0.085) running north-south with dashed lane lines and a double yellow centre line, 15 cm kerbs and concrete sidewalks (0.30), a row of pastel stucco hotels on the west side (Ocean Drive's paint chips, eyebrow ledges, dark glass), a lawn (0.045), the beach walk, sand (0.45) and sea to the east, an 18 % grey card on a stand and a horizontal one, white / 18 % / black / chrome spheres, and a car-sized box (the 1999 MX-5's 3.95 × 1.68 × 1.23 m). In the editor build (`UnrealEditor.exe -game`) the surfaces are a tiny parametric material made at run time; a cooked game falls back to `BasicShapeMaterial`.

Script (all optional; see `ASimCruiseLightingLab::StartScriptFromCommandLine`):

```text
-SimCruiseLabShots=name@2025-04-24T17:19:00Z[#console cmd|cmd],...   times to capture (~ stands for a space in commands)
-SimCruiseLabViews=street,props,facade,sky,road                     camera poses
-SimCruiseLabCapturePresets=ultra                                   presets to capture with
-SimCruiseLabPerf=low,medium,high,ultra -SimCruiseLabPerfSeconds=12 -SimCruiseLabPerfView=street
-SimCruiseLabOut=<dir> -SimCruiseLabSettle=6 -SimCruiseLabQuit
```

Each capture writes `<shot>_<view>_<preset>.png` and a JSON sidecar with the sun, the reference EV and the screen position of named probe points (grey cards, sphere tops, road, facade, car side) for pixel readback. Settling waits for shader and asset compilation first.

Console commands: `SimCruise.Env.Time <ISO UTC> [timescale] | now`, `SimCruise.Env.Place <lat> <lon> [elev m]`, `SimCruise.Env.Weather <cloud> [visibility km] [rain]`, `SimCruise.Env.Report`, `SimCruise.Env.Look <name> <value>` (look development: cloud material scalars, layer, aerosol, exposure, saturation), `SimCruise.Graphics <auto|low|medium|high|ultra>`, `SimCruise.Perf <seconds> [label]`, `SimCruise.Capture <name>`. Command line: `-SimCruiseLat= -SimCruiseLon= -SimCruiseElevation= -SimCruiseUtc= -SimCruiseTimeScale= -SimCruiseConditions=<file> -SimCruiseCloud= -SimCruiseVisibilityKm= -SimCruiseRain -SimCruiseGraphics= -SimCruiseNoEnvironment`.

![The lab street at Miami Beach (25.778, -80.131) on 24 April 2025: 10:35, solar noon, 17:30, 19:10 and dusk EDT, and a full-moon night on 12 May](images/unreal-lighting/lab-times-of-day.jpg)

![Sky cover from the weather hook at 15:00 EDT: scattered, broken, overcast with 8 km visibility, and morning mist at 2 km](images/unreal-lighting/lab-weather.jpg)

### Against photographs of Ocean Drive

Compared at matching times of day with [Miami Beach - Hotels along Ocean Drive (2023)](https://commons.wikimedia.org/wiki/File:Miami_Beach_-_Hotels_along_Ocean_Drive_(2023).jpg) (P. Hughes, CC BY 4.0; 9 December 2023, 11:49) and [South Beach Ocean Drive Strip, January 2018 - 01](https://commons.wikimedia.org/wiki/File:South_Beach_Ocean_Drive_Strip,_Miami_Beach_Florida,_January_2018_-_01.jpg) (Infrogmation of New Orleans, CC BY 2.0; 16 January 2018, 15:39). Links only; the photographs are not in the repository.

- **Sunlit ground is close, a little bright.** Sunlit asphalt reads sRGB (136, 130, 121) in the December photograph and (146–161, 142–157, 132–149) across the road in the lab's street view at noon (two separate runs), both warm-grey: 0.3–0.5 EV brighter in linear light; sunlit concrete and sand sit where the photograph's do. In the 17:30 props view the shaded lawn reads 0.31 of the sunlit lawn (display luminance; the sidewalk 0.40, its sunlit part already in the tone curve's shoulder), near the 0.18–0.32 shadow-to-sun ratio the Godot client measured from photographs.
- **The sky is paler than the photographs.** At 25° elevation away from the sun the lab sky is (126, 155, 181), the December photograph (98, 150, 213). Raising the visibility from 40 to 200 km or the saturation by 15 % moves it only part of the way; some of the gap is the phone's rendering (saturation, sky polarisation), some may be the aerosol model. Open.
- **Afternoon shade is darker and bluer than the January photograph.** Shaded asphalt reads (53, 67, 86) in the lab against (115, 115, 119) in the photograph, shaded stucco (148, 143, 160) against (177, 187, 193). The photograph's camera exposed for the shade (1.6 EV under the grey-card reading) and its street is lit by sunlit facades the lab does not have; with average metering the lab's open sky holds the exposure down. Open: a camera white balance that follows the illuminant mix, and real streets around the lab.
- **Twilight** turns the whole scene magenta at -3° (the atmosphere's twilight sky); real dusk photographs keep a bluer zenith. Open.
- Moonlit nights are dim and almost colourless, with the moon's aureole in the haze; there are no stars yet.

### Threads and lifetime

Everything here runs on the game thread: the environment ticks in `TG_PrePhysics` and only sets component properties, which Unreal hands to the render thread itself. `GetRoadWetness()` is game-thread state; a tyre model stepping on the physics thread (Chaos async physics) must copy it into its per-step inputs on the game thread rather than read the actor. The perf sampler runs on the core ticker, which outlives worlds, so the lab reaches itself from its callback through a weak pointer.

### Tests

`SimCruise.Environment.*` (12 automation tests, headless with `-nullrhi`): `Sky.Actor` (an environment in a throwaway game world: the sun light points along the ephemeris direction at extraterrestrial lux, shadows move from sun to moon at night, the noon reference EV, a level's own sun turned off by the level-scoped takeover), `Solar.SpaReference` (NREL SPA case, zenith 50.11162°, azimuth 194.34024°, equation of time, declination), `Solar.GodotAgreement` (the taste test's Panoramax frame, the Godot formula within 0.75°, the almanac declination), `Solar.Calendar` (solstice declination, overhead sun on the tropic, equation of time in November and February, Miami solar noon, a night sun), `Solar.Refraction`, `Solar.WorldFrame`, `Moon.Meeus` (examples 47.a and 48.a, a full moon), `Daylight.Illuminance` (noon 95–125 klx and EV100 15–16, overcast, low sun, monotonic twilight, civil dusk, moonlight, the night display curve), `Daylight.Visibility`, `Weather.Conditions` (the helper's JSON, rain vs snow, the 10 SM cap, empty and invalid input), `Weather.RoadWetness` (time constants, frame-rate independence, cloud coverage knots), `Graphics.AutoPick` (tiers, laptop step-down, VRAM caps, parsing, percentiles).

### Next

Stars and a moon disc with phases; wet road materials reading `GetRoadWetness`; METAR polling through the helper at run time; a white balance that follows the light; rain and snow particles; the lab's comparisons repeated on the streamed Miami Beach tiles once the world stream draws them, at the exact Panoramax pose of the Godot taste test (4b6d216b, 2025-04-24 14:34:52 UTC, heading 7°, 70.8° horizontal field of view; the lab's `morning` shot uses that time).
