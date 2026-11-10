# Real-world detail: a working plan (November 2026)

SimCruise already builds the drivable US from public data. This note asks what "real" means for a game like this, where the remaining realism will come from, and how to pay for it on an ordinary laptop. The baseline is a 2020 gaming laptop: RTX 2070 Super Max-Q (8 GB, Turing), i9-10885H, 1080p, Godot 4.7.2 Mobile renderer on Vulkan. Each section separates what has **shipped** from what is **next**. Licence findings are as of 10 November 2026; check them again before baking any data.

## 1. What is a game, and where does the compute go?

In a frame of SimCruise on the bundled Miami Beach start (High preset, windowed, `checks/QualityBench.gd`):

| | ms per frame |
|---|---|
| GPU, 3D viewport | 9.8 |
| Render submission (CPU) | 0.8 |
| Physics (Jolt plus the tyre model, 60 Hz) | 1.2 |
| Ambient traffic and birds | ~0.5–1.0 |
| Streaming and tile bookkeeping | < 0.2 |

**Finding on this laptop:** on battery it runs at exactly 30 fps however light the scene is. NVIDIA Battery Boost caps the frame rate. The GPU sat in its P8 power state using 10 of its 33 ms. Plugged in, the same scene has room for 60 fps. This is a driver setting, not engine work.

Hand-written assembly or "closer to machine code" is the wrong lever here. The hot paths already run in native code: Jolt physics, the Vulkan renderer, FastNoiseLite and MultiMesh. A long frame almost always comes from doing too much work, rather than doing work slowly:

- too many draw calls (each car part was its own call until this change);
- too many pixels (resolution);
- too much shadow-map area;
- GDScript loops that should be data on the GPU.

The rule that follows is to **move per-object work into data the GPU animates**. Tonight's changes apply it:

- Traffic models are baked into one multi-surface mesh per model, so a car is one transform per frame (2.4 ms became 0.5 ms).
- Birds are animated entirely in a vertex shader (0 CPU per bird).
- Detail and resolution follow measured GPU time.

Native code (GDExtension in C++ or Rust) is worth it in one place: the per-sample audio synthesis in `InlineFourSound.gd` and `RoadNoise.gd`, if a profile ever shows it matters.

## 2. What does "real" look like, and how do we measure it?

"Real" is a measured difference from a photograph of the same place, not an impression. The project already does this (`lab/holdout/`, `lab/landscapes/`): it renders the pose of a dated, licensed street photograph and scores the difference. The proposal is to make this a single **realism score** that every visual change must not lower:

1. **Geometry.** Silhouette IoU of buildings, trees and road edges against a semantic segmentation of the photo (`lab/semantic/segment.py` exists).
2. **Tone.** Shadow-to-sun luminance ratio and sky colour (already measured: photographs 0.18–0.32).
3. **Texture energy.** Band-pass energy per surface class, which catches both plastic-smooth and noisy surfaces.
4. **Motion.** A real street is never still. Count moving objects per minute against traffic counts (HPMS AADT).

Panoramax is keyless (`https://api.panoramax.xyz/api/search?bbox=…`), and each item carries its own licence link. It is the reference source to use, item by item.

## 3. Cars

**Shipped:**

- `authoring/cars/sketchfab.mjs` searches Sketchfab keylessly (`/v3/search?type=models&downloadable=true&license=by|cc0&categories=cars-vehicles&archives_flavours=true&max_face_count=…`).
- With your own `SKETCHFAB_API_TOKEN` it downloads the glTF archive, unpacks it into `game/art/traffic/`, records author, licence and URL in `manifest.json`, and writes `licenses/Traffic models.txt`.
- `AmbientLife.gd` loads every manifest model. It normalises scale, axis and ground height from the stated length, so a model in centimetres along X drives correctly (`checks/AmbientCheck.gd`).

**Rules learned:**

- **Licence.** Only `cc0` and `by` can be committed to this MIT repository and shipped. `by-sa` could only ever be fetched at runtime; `nd`, `nc` and the Store/Editorial licences cannot be used at all.
- **Download terms.** Sketchfab's download terms require the downloading user's own token; anonymous runtime download needs Sketchfab's approval. So models are fetched once by a maintainer, committed, and credited. They are never fetched by players.
- **Trademarks.** CC licences do not license trademarks (CC BY 4.0 §2(b)(2)). The tool refuses models with a car maker in the title unless `--allow-brand` is given, and then reminds you to strip badges. Tags that only cite a maker are shown as a note.
- **Candidates found tonight** (CC BY, generic names, under 120k faces):
  - "Generic passenger car pack" `20f9af9b8a404d5cb022ac6fe87f21f5`
  - "Generic Sedan Car" `58c33766470d46e7b2aed542650494e5`
  - "Lowpoly Generic SUV" `edc994ad28ed438cb365c0e0389ac177`
  - "Pickup Truck" `047615f53e2d45b9a1a2a4dd203d459c`
  - "Generic Car 8" `1af682d01fa04fd08485069ebe71ceab`
  - "Generic civil service vehicles pack" `8ff2a13f30914932a70c7950cfa58465`

  Look at each before committing.
- **Other sources:**
  - Kenney Car Kit: CC0, stylised, can be bundled.
  - Quaternius: its 2026 licence forbids redistributing raw assets, so do not commit them.
  - BlenderKit Royalty Free: not redistributable.
- **Dimensions** for the Blender generator come keylessly from vPIC `GetCanadianVehicleSpecifications/?Year=…&Make=…&format=json`: length, width, height, wheelbase, track and curb weight.

**Next:** a level-of-detail chain per traffic model (the same mesh decimated to 30 % and 8 %, plus an impostor past 250 m), and a paint palette weighted by real US colour shares.

## 4. Houses, down to the neighbourhood

| Detail | Source | Status |
|---|---|---|
| Footprint, height | Overture (Microsoft ML footprints, OSM), lidar roofs | shipped |
| Use (single family, duplex…) | National Structure Inventory | shipped |
| Roof shape, roof/facade colour and material | Overture `building` and `building_part` fields. They come only from OSM, so they are filled mostly in well-mapped cities. Measure the fill rate per release with DuckDB. | partly used (`ObservedBuildings.gd`) |
| Era (year built) | Census ACS `B25035_001E`, median year built per block group. The API now needs a free key; the summary files do not. | next |
| Exact year built, stories | County assessor parcels. Free statewide layers exist (NY, WI V-parcels, FL DOR via FGDL). Regrid is licensed and cannot be used here. | next, state by state |
| Street-level appearance | Panoramax (per-item licence); Mapillary (CC BY-SA, needs a token, runtime only) | partly used |
| Photoreal 3D | Google Photorealistic 3D Tiles: key required, no caching, no offline use. It can be streamed live with attribution, but never baked. | not planned |

**The plan for houses** is a *recipe*, not a mesh library. Era (from ACS or a parcel) + use (NSI) + region (state) + footprint shape picks:

- roof pitch and overhang;
- siding (clapboard, brick, stucco, vinyl);
- window rhythm and porch;
- garage placement on the side facing the driveway;
- a palette sampled from the aerial roof colour.

A 1925 Chicago bungalow, a 1958 California ranch and a 2005 Texas brick two-storey come out of one small CC0 texture atlas plus rules. A wrong recipe is visibly wrong, so the realism score in section 2 judges it.

**Bridges and road roughness:**

- **Bridges.** NBI per-year ASCII files give structure type, deck width, year built and skew. They are US government works. The 2026 files move to the new SNBI layout, so pin parsers to the year. OSM `bridge:structure=*` gives beam, truss, arch or suspension. Together they choose the bridge kit, so a 1930s steel truss stops being a concrete slab.
- **Roughness.** HPMS reports IRI per road section (public domain, per-state services). See section 6.

## 5. Animating the world efficiently

**Shipped in `AmbientLife.gd`:**

- **Traffic on the mapped streets.**
  - Cars keep to the right lane; one-way roads are respected.
  - Speed follows the road class. Cars hold two-second gaps and stop behind the player.
  - Cars choose plausible turns at junctions and never appear within 140 m in plain view ahead.
  - The six nearest carry moving collision boxes, so you cannot drive through them.
  - Cost: one draw call per model and about 0.5–1 ms of CPU for 20 cars.
- **Birds as a light show.** Like drones in a sky show, each bird flies a precomputed path: a breathing Lissajous orbit with wing beats and glides. Its phase, radius, height and speed sit in the instance data, and the GPU turns time into position. The CPU moves three flock centres. Hundreds of birds cost three draw calls.

**Next, in order of value per millisecond:**

1. **Pedestrians** on mapped sidewalks with vertex-animation textures. The antzGames VAT plugin is MIT and extends MultiMesh; the Godot docs show thousands of fish in one draw call. Density can come from Overture places (shops attract people) and the time of day.
2. **Traffic signals that cars obey.** The signal positions are already streamed (`SignalControls.gd`).
3. **Traffic volume from HPMS AADT**, so an interstate is busy and a cul-de-sac is empty.
4. **Wind.** One global shader parameter driven by METAR wind, read by tree, grass and flag shaders. It is free, and it makes the whole scene move together.

## 6. Making the car feel like it is really driving

**Shipped:** `RoadRoughness.gd`. Real roads are rated by IRI (m/km). Each tyre now rides a band-limited profile with an ISO 8608 shape. It is fixed in map coordinates, so the rear wheels hit the same patch one wheelbase after the fronts.

- The IRI comes from road class: motorway 1.0, residential 2.6, track 6.5.
- A per-street condition scales it from 0.6× (fresh asphalt) to 1.9× (worn), and a slow regional field means whole districts can be rough.
- RMS heights are 2.5 mm per IRI unit: 2.5 mm on a new motorway, 6.5 mm on a typical street, 16 mm on gravel.
- The profile feeds the suspension length and damper velocity, not the collider, so it cannot launch the car (`checks/AmbientCheck.gd` caps the peak).
- `--smooth-roads` turns it off for A/B tests.

**Next:**

1. Fetch HPMS IRI per section through the helper and replace the class baseline wherever a state reports it.
2. Tie `RoadNoise.gd`'s rumble layer to the same IRI, so you hear what you feel.
3. Add discrete features: expansion joints at NBI bridge ends, manholes from city asset layers, and rumble strips on rural interstates.
4. The roadmap's slip-dependent tyre curve and contact-patch shape cast.

### Update: the road is its own surface

The biggest realism gap on mountain roads was not the profile but how it was stored. The helper fits a smooth, banked profile along each road, but only the terrain grid reached the game, and 3.9 m planar triangles cannot follow a banked hairpin; the tyres felt every facet. The helper now exports the profile itself and the game drives on a ribbon built from it (`RoadSurface.gd`). Roads are engineered objects, so the rule generalises to every paved road: fit the profile, bank it to the curve, build the road from that, and let the ground meet the road.

## 7. Hyper-detail without the cost: tricks from science, cinema and magic

| Idea | Where it comes from | What it means here | Status |
|---|---|---|---|
| **Detail where the eye is** | Magic (misdirection), foveated rendering | At speed the eye locks onto the road ahead and the periphery smears, so mesh LOD coarsens with speed (`GraphicsQuality.gd`). Next: Variable Rate Shading with a peripheral mask. VRS works on the Mobile renderer on Turing; measured about +10 % elsewhere. | shipped (LOD by speed) |
| **Spend to a budget, not a setting** | Broadcast TV, console dynamic resolution | The 3D resolution follows measured GPU time in 5 % steps with a dead band, so a dense downtown drops resolution, not frames. The HUD stays sharp. | shipped |
| **Pixels in the sky** | Drone light shows | Precomputed paths evaluated on the GPU (the birds). The same trick works for distant highway traffic as streaks of headlights. | shipped (birds) |
| **Hologram / light field** | Holography records the light, not the object | Octahedral impostors: render a building or tree from many directions once and show the right view on a single quad. Past 700 m this replaces thousands of triangles. zhangjt93/godot-imposter is MIT and works on the Mobile renderer. | next |
| **Gaussian splats** | Radiance fields | GDGS (MIT) renders millions of splats, but it needs Forward+, does not receive shadows, and there is no open US splat data. Worth trying only for hand-captured landmarks. | later |
| **Matte paintings** | Cinema | The 45 km regional terrain is already a draped "matte". Add a sky-and-horizon panorama per region, baked from distant lidar. | partly |
| **"Hallucinated" detail** | Procedural generation, texture synthesis | Fill what no data covers (sills, gutters, mailboxes, cracks) from rules seeded by the real data. It must be deterministic per place, so the same street looks the same every visit, and it must score better against photographs (section 2) before it ships. Generative models stay offline authoring tools: they produce textures and recipes, never runtime guesses about a real place. | principle |
| **Less detail, better choice** | Impressionist painting | Fewer, correctly placed things beat many wrong ones: a real tree inventory with 3 LODs beats a dense random forest. The project already works this way. | principle |

## 8. Graphics presets on this laptop

`GraphicsQuality.gd` reads the adapter the renderer actually opened and picks a preset. Change it with **F4** (Auto → Low → Medium → High → Ultra); the choice is remembered.

| Preset | 3D scale | MSAA | Shadow map / reach | Mesh LOD (px) | Traffic / birds | Adaptive | GPU ms (bundled start, on battery) |
|---|---|---|---|---|---|---|---|
| Low | 0.67 | off | 1024 / 70 m | 2.5 | 6 / 24 | yes (30 fps) | 6.2 |
| Medium | 0.80 | 2× | 2048 / 120 m | 1.5 | 12 / 60 | yes (50 fps) | 8.9 |
| High (auto on RTX 20-series) | 0.90 | 2× | 4096 / 180 m | 1.0 | 20 / 120 | yes (58 fps) | 9.8 |
| Ultra (auto on RTX 30/40/50) | 1.00 | 4× | 4096 / 260 m | 0.75 | 32 / 200 | no | 7.1* |

\* Ultra was measured after the others in the same session, with clocks still ramping. Re-run plugged in for stable numbers:

```
godot --path game -- --bundled-start --quality=high --quality-bench <dir>
```

Optimus laptops: on this laptop `RenderingServer.get_video_adapter_name()` confirmed that Vulkan opened the RTX (device #0, discrete), not the Intel iGPU, with no Windows graphics setting needed. Headless and QA runs keep the `project.godot` settings, so every existing capture and calibration check is unchanged.

**On Forward+.** It would bring SSAO, SSIL, volumetric fog, TAA and FSR 2. The research note says FSR is Forward+-only in Godot, and on the Mobile renderer `GraphicsQuality.gd` falls back to bilinear scaling. The switch is the single biggest visual lever left. It needs a RenderCost comparison on a Max-Q and an Apple M-series machine first, as the roadmap says.
