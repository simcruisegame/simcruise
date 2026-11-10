# SimCruise roadmap: how we get to real

This is the design-panel report written on 8 November 2026 against the 1.0.8 code, kept as the working roadmap for SimCruise 1.1 and beyond. The panel read the code (file and line references are to the 1.0.8 code), drove the current build and compared it with photographs and published test numbers. The problems it set out to fix: roads bumpy enough to launch the car, a Miata that did not feel real (a half-second keyboard tap at 45 mph rolled it after leaving the road), a picture that looked washed out and flat, and a world that needed more real-world context.

Each item carries an estimate in brackets: `[days · impact 1-5 · data/licence]`. **today** marks work meant for the first day on the current build; **[unverified]** marks claims the panel did not confirm in-engine. Items the 1.1 streams have started are tracked in the [changelog](../CHANGELOG.md); the calibration that must not regress while doing any of this is the headless `checks/Track.gd` run described in [CONTRIBUTING.md](../CONTRIBUTING.md).

## Progress on 10 November 2026

Done (see the [changelog](../CHANGELOG.md) "Unreleased"): graphics presets with adaptive resolution; moving traffic and birds; engineered road ribbons (Tail of the Dragon kinks 34.6 → 6.4 per km); inferred guard rails; natural driver lens; 1965 Mustang and Corvette C8 with real models; flake paint and road grime; houses with siding, brick and shingles; non-repeating ground textures; the S-bend handling test. Since then: the slip-ratio tyre curve with working traction control and ABS (three new checks), and the fix for the 1999 roadster's lift-off spins (the automatic selected reverse on a light brake; `LiftOff.gd` now gates every car), and roads with nothing solid on them and bridge decks that meet their approaches and each other at tile seams (field-test problems B and C; `RoadClearance.gd` in the default suite). In progress in parallel branches: an AI traffic director, mapped OSM guard rails, house geometry (porches, garages, overhangs), road-wear masks, and a random-place drive campaign across every region. The detail-engine plan is in [research/detail-engine.md](research/detail-engine.md); the realism notes in [research/real-world-detail.md](research/real-world-detail.md).

## Next: the plan after the November 2026 work

What landed in 1.1 is in the [changelog](../CHANGELOG.md): road benches and bridge decks, the real-road test drive, five new cars with calibrated physics and generated models, a Windows build and steep-slope ground. This is the ordered list that follows, with what each item is measured by.

**Driving**
1. ~~**Slip-dependent tyre curve**~~ — done (November 2026): peak and fall-off, load-dependent relaxation, stable traction control, ABS in both directions. Open: the ABS cars stop 0.6–3 ft longer than before (all in band), and at 0.7 g the Volta still spins on a lift or trail brake (reported by `LiftOff.gd`, not gated).
2. **Volumetric tyres**: replace the single ray per wheel with a shape cast so kerbs, potholes and bridge joints are felt over the contact patch. Gate: `RoadDrive` jolt counts on the Dragon and I-180 do not rise.
3. **Engine voices per car**: a V6, a V8 and an electric motor whine next to the four-cylinder; the electric car is silent today.
4. **Tunnels**: carve the terrain along mapped tunnels instead of dropping them; roads currently end at portals.
5. **Floating origin check**: confirm long drives do not jitter far from the session origin, and rebase the world if they do.

**Places that look like the real place**
1. **Forest floor under canopy**: aerial photographs show treetops, often leaf-off, so forest floors render pale blue-grey (seen at Deals Gap). Use the WRI/Meta canopy height already downloaded: where canopy stands over 3 m, draw litter and shade instead of the photograph. Gate: before/after captures at the same place and pinned sun.
2. **Earthwork from the road bench**: the helper knows where it cut into or filled a hillside; pass that to the ground so cuts read as rock and fills as grass (the shader's steep-slope rock already covers the steepest faces).
3. **Sharper state imagery**: statewide 6-inch (15 cm) orthophotos where states publish them free (North Carolina's NC OneMap first, then others), four times sharper than the national 60 cm imagery; one source and licence at a time.
4. **Buildings with character**: occupancy from FEMA USA Structures, facade and roof colour, material and shape from Overture building parts, year built from county parcels (or Census median year built per block group) choosing an era; a procedural facade "recipe" from era, use, region and material, built from one small CC0 texture atlas.
5. **Lot detail from parcels**: driveways, fences, garages, mailboxes with house numbers.
6. **Real lane counts, shoulders and road roughness** from FHWA HPMS and OpenStreetMap lanes, so a two-lane interstate is two lanes and old pavement feels old. One-way carriageways and ramps already take a two-lane (ramp: one-lane) width with class shoulders; two-way roads still use one width per class. Lane counts must come from a source that is the same for every tile of a road (a bundled or versioned table, not a per-tile query that can fail), so seams and the survey road signature stay stable.
7. **A realism score**: render the exact spot of a licensed Panoramax street photograph and measure how close the game comes, so visual changes get numbers like the physics does.

**Runs everywhere**
1. **Quality presets** (Low, Medium, High, Ultra) with Low chosen automatically on 8 GB, small-GPU machines such as the MacBook Neo: render scale, shadow distance, canopy density, tile ring. Gate: `RenderCost` frame times on each preset.
2. **Level of detail** for the generated cars and distant buildings; impostors past 700 m.
3. **The Windows build tested on real Windows** before the first release that ships it (under Wine only the installer and the helper could be verified).

**Ideas collected from contributors** (not yet scheduled): speed-based camera field of view and subtle lean, wet and dirt maps from live weather, in-editor authoring tools in place of the Python scripts, quadtree terrain level of detail.

---

## The panel report: SimCruise — how we get to real (8 November 2026)

Brackets give [days·impact 1-5·data/licence]; **today** = one day on the current build; [unverified] = not yet run in-engine.

## 1. Make the Miata drive and feel real

- **Speed-aware keyboard steering (today).** A held key asks for 18.8° of steer at 45 mph where 3° is usable. On a flat pad the stock car slides but stays upright: the reported roof landing was a trip, not a grip rollover. Cap steer by speed (Assisted/Direct modes, real wheels exempt), ramp the pedals; gate: 0.5 s tap at 45 mph → heading under 30°, Track.gd unchanged. [1d (+0.5d retune)·4·MIT code, C&D 1999 test]
- **One-press recovery (today).** Detect roof-down or stuck, prompt "Press R", auto-right at 8 s; R searches main roads only; search all [unverified: freeze-order cause of the double press]. [0.75d·3·MIT]
- **Roll/trip bench (today).** Embankment, tree-trunk, parked-car cases; logs the tripping collider [unverified: mechanism]. [1d·3·USGS 1 m DEM, public domain]
- **Tyre curve, then a regression net.** Magic-Formula shape peaking at 6-8°, load-dependent relaxation: crisp breakaway, no free drifts; 120 Hz only if measured [3d·3·published equations; unverified; no Miata tyre data]. Handling suite: keyboard-tap bench, step-steer, quarter mile [3d·3; unverified: targets beyond three C&D numbers].
- **Engine identity.** Torque peak at 5500 rpm, limiter bounce, throttle-aware auto shifts; gearing checked against EPA N/V [unverified: VICS dip, cut rpm, EPA field]. [1.5d·3·Mazda spec; EPA, public domain]

## 1b. Make the Miata (and any created car) look real

- **Small truths.** Exterior seats black like the real Sport [0.2d, today]; one paint for body, bumpers, bonnet plus honest glass [1.5d]; passenger mirror [0.5d, today]; door-mirror camera later [1.5d; unverified]. [impact 3·MIT]
- **CarSpec: the car as data.** Every Miata constant (eleven files) moves into one car.json with a source per field, plus an auto-calibrator hitting published 0-60/70-0/skidpad in minutes; the foundation for any created car, no realism by itself [unverified: source databases]. [5-6d·4-5·EPA, NHTSA vPIC public domain]
- **Cabin that reads as vinyl.** Baked ambient occlusion, grain normal map, real shadows, windshield with dust and sun streak [unverified: no engine run; shadow cost]. [3.5d·4·ambientCG CC0]
- **Validator before art.** check_car.py gates dimensions ±2 %, roles, budgets, licence allowlist; TrackCompare fails on handling drift. [2.5d·4·brochure facts]
- **Wheels and tyres.** Sidewall markings, tread, brake dust, contact squish; procedural, no brands. [2d·3; unverified in-engine]
- **Create-car skill + retarget tool.** Facts from vPIC/EPA at authoring time, CC0/CC-BY geometry ranked, logos stripped; a retarget tool maps any glTF into the game's roles; meshes stay proxy-grade [unverified]. [3d+3d·4·MIT tooling]

## 2. Pull in more public data — legally and efficiently

- **Real clock for the sun (today).** Every drive is pinned to a June afternoon; the solar math exists, so use real time, clamped above 7° until night exists [unverified: HDR sky shot at one sun position]. [0.5d·3 (4 with night)·NOAA, public domain]
- **Sharper ground nationwide.** USGS already serves 1024 px per z16 tile (16× today's texels), NAIP too; check texture memory first. [inside ortho item·3·public domain]
- **Stop signs, crossings, lamps for free.** Already in the Overture tile fetched for traffic signals; parse them, build MUTCD signs; needs a hook for bundled tiles. [3.5-4d·4·ODbL runtime; FHWA art public domain]
- **Speed limits, surfaces, lanes.** The Esri OSM replica we already query carries maxspeed on a quarter of city ways: R2-1 signs where known, finer grip. [3d·4 signs, 2 grip·ODbL runtime]
- **Addresses, landmarks, businesses.** Census geocoder, per-state GNIS names [2d·3·public domain]; Overture places as plain-text storefront names, no logos, trademark call yours [3d·3-4·CDLA/Apache-2.0/CC0].
- **Inventory, then later.** FHWA HPMS lanes, shoulders, medians, roughness [4d·3; unverified: AADT→traffic]; NWS forecast, METAR precipitation flags [1.5d·3]; public domain. Later, impact 3 each [unverified]: 1 m crown from 3DEP samples [4-5d], licence-reviewed city orthos [4d+legal], municipal registry [3d].

## 3. Make the world and the experience real

- **Clean HUD and prompts (today).** Off/Glance/Full levels, nothing opaque, legend only after changes, prompts always visible; needs typed preferences. [1-1.5d·4·MIT]
- **Kill the double sun.** The sky panorama's unclipped sun adds unshadowed light on top of the real sun (shadow/sun ≈0.5, photographs 0.18-0.32) — the washed-out look. Clamp it in the cubemap pass, rebalance; A/B today via hdr_clamp_exposure. [2d·5·Poly Haven CC0]
- **Settings, shadows, measurement (today).** tonemap_white, debanding, 16× anisotropy [0.75d·3; unverified: debanding]; shadows to ~200 m with far trees casting [1d·4; unverified: PCSS]; one haze model [1d·3]; a vsync-off RenderCost run gates every budget and the Forward+ decision [1d·3; unverified estimates].
- **Trees and grass.** Canopy backlight, occlusion, warm tint, near fade, alpha-to-coverage [2d·4; unverified: BACKLIGHT on Mobile]; mown verges, textured swaying field clumps, fade ring [2.5d·3·CC0 textures].
- **Sound on by default.** Bus tree, cabin low-pass, procedural tyre/wind/gravel [2d·4]; engine cabin/exterior A/B, limiter stutter, pops [2d·3]; squeal, stones, rumble strips, diff whine [2.5d·3; unverified: rumble strips on a surveyed highway].
- **A head, not a weld.** Lagged head motion, partial horizon lock, gaze lead, micro-vibration, slider, physics interpolation on [1.75d·4]; FOV 67→55 with seat sliders [1d·3; unverified: eye height].
- **Roads and horizons.** Asphalt normals, wheel-path wear, wet roads once precipitation is parsed [4d·4·Poly Haven CC0]; far forest skirt [5d·4·CC BY 4.0; unverified]; far buildings past 700 m [4-6d·4·ODbL].

## Honest limits

- Render budgets are estimates: nothing ran in-engine for this review; the GPU timer reads 0 on this driver.
- "Exactly like a Miata" rests on three magazine numbers; no public tyre, damper, dyno data or CC engine recordings; CG height, driver mass, rolling radius unverified.
- "Exactly like real life": keyless imagery tops out near 0.6 m/px; no night or seasons yet; nothing stands beyond ~700 m.
- ODbL data stays runtime-fetched, never bundled in the MIT repo; business names, badges (the cockpit's "NARDI" text) and car shapes need a legal call.
- Create-car facts come keylessly, geometry does not: image-to-3D yields proxies. Headless checks have no log cap.