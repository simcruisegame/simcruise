# Hyper detail at road level: ranked proposals (14 November 2026)

Innovation team, for the project owner: "how can we create hyper detail", "is this first in the world? what would be best, can you do both?" and "doing it over everything is a visual trick but may allow us to efficiently do it better with even more detail?"

Read with: [flight-simulator-study.md](flight-simulator-study.md), [technique-survey.md](technique-survey.md), the scan engine contract `world-detail/scan-map-spec.md` (v1), `world-detail/world-detail-methods.md`, `team/docs/research/realtime-context.md`.

Rules that shape every proposal: generative first, any US place, never hand-fix a road; data, APIs and models legal for a free game (weights AND training data), recorded in `docs/data-sources.md`; local CPU work on workers, never on the game thread; GPU budget for the whole detail layer about 1.5 ms on the laptop (RTX 2070 Super Max-Q); test at the highest settings and look at real frames.

Costs marked (estimate) are not measured. Each proposal has a first experiment that measures it.

---

## 1. Summary

**How Flight Simulator works (short).** It streams 2-2.5 PB of Bing aerial photographs, a global elevation model, photogrammetry meshes for about 500 cities and OpenStreetMap from Azure. Offline, cloud machine learning (Blackshark.ai) turned the photographs into 1.5 billion buildings and detected trees; biome rules choose species. In the game, each map tile holds about 90 data layers, each a small state machine that can fail and fall back to its parent tile. The 2024 version adds 3D ground near the aircraft (displacement, stones, gravel), 27-30 biomes and a rolling cache. It is made for a view from hundreds of metres; up close, buildings are weak and the ground is soft.

**The driver's problem.** At 1.2 m eye height, one screen pixel covers 4 mm of ground at 5 m. A 0.6 m aerial photograph is 150 times too coarse there, and most of the driver's screen is vertical surfaces that no aerial photograph shows. So near the road, detail must be **generated from facts** (soil, rock, road design, tree shape, building type), not copied from photographs.

**Top 5 proposals (ranked by realism gained for cost):**

1. **The road's real cross-section.** Build every road from US design standards and national road statistics: lane, edge line, paved or gravel shoulder, edge drop, rumble strip, foreslope, ditch, backslope, mow line, guard rail where the rules require one. Fit it to lidar where lidar exists. The tyres feel what the eye sees. Fixes "no road shoulder".
2. **Ground from the soil and the rock.** Bare ground takes its colour from the US soil survey (Munsell colour of the top soil layer), and cut faces take their rock type from the USGS geologic map; lidar slope decides where bare soil or rock shows. Fixes the "smooth lime-green bank".
3. **Process-true corridor texture (our bold idea).** A unique, never-repeating detail texture for the 60 m corridor ahead of the car. Each texel comes from a cheap simulation of the process that made it (water flow, tyre-thrown gravel, cracks by pavement age and frost, leaf fall), cached in a runtime virtual texture and dropped behind the car. This is the project owner's "trick over everything", done so that it gives more detail for less GPU time.
4. **Trees grown to their own lidar.** Each tree near the road gets its real height, crown base and crown shape from the lidar points, and a species from the US forest data; a growth algorithm fills that envelope with branches. Fixes "painted-blob trees".
5. **Facades from the structure inventory.** The US Army Corps of Engineers' National Structure Inventory gives, per building, the construction type (masonry, wood, steel, manufactured), stories, foundation type and height; the Census gives the era. The facade grammar builds the right porch steps, siding or brick, and window rows.

**Is it first in the world?** Not as a whole idea. Flight Simulator and Blackshark (cloud), Outerra (procedural planet that you can drive) and military simulators (whole-Earth terrain from open data) did large parts. What we found no shipping product doing: building the world **on the player's own PC as they drive**, from **open US national inventories** about the **vertical world and the first 30 m beside the road**, with **clean-licence models only**, a **self-check of each tile against the aerial photograph and against road-level photograph statistics**, and **one record that feeds the picture, the tyres and the sound**. We cannot prove that nobody did it; we can say that we searched and found no one.

**Can we do both (3D detail and an AI repaint of each frame)?** Yes, but not a full AI repaint. The best published real-time version (REGEN, 2025) needs 33 ms per frame at 960 x 512 on an RTX 4090; our laptop GPU is several times slower, and the training data (Cityscapes) is non-commercial. Our recommendation: (a) real detail in the world, as above; (b) the cheap middle way: an AI-fitted "photo look" per place (a colour and tone table, computed one time offline from licensed photographs), applied as a normal post-process at about 0.05 ms; (c) later, an optional NVIDIA DLSS 5 switch for players with RTX 50 cards (it does not run on the project owner's laptop). AI teaches the cheap trick offline; it does not paint every frame.

---

## 2. Real 3D where it counts, shading tricks where they are cheap (the project owner's idea)

The project owner is right: detail that is a trick of shading can give more detail per millisecond than geometry. The eye is the judge, so the question for each trick is: **where does the driver's eye catch it?** From research and our own frames, the eye catches a fake in four places:

1. **Silhouettes** (the outline of a rock, a kerb edge, a post, a crown against the sky). A shading trick has no outline. Silhouettes must be geometry.
2. **Grazing angles.** At 1.2 m the road and ground ahead are seen at 2-10 degrees. Parallax and relief mapping break up at grazing angles (steps, swimming, flat outlines) unless they take many samples. So parallax is weak on the ground ahead, but it is good on **vertical surfaces that face the driver**: banks, cut faces, walls, facades, where the view is near the surface normal.
3. **Motion parallax at speed.** As the car moves, near layers slide over far layers. A flat texture with painted depth does not slide. Inside about 15 m at the side of the car the eye gets parallax from motion; but at 25 m/s the near side is also blurred, so it needs coarse true shape, not fine detail.
4. **Repetition and wrong cause.** One repeated pattern, or dirt where no water runs, says "game".

### 2.1 Each trick, judged from the driver's seat

| Technique | How real at 1.2 m, at speed | GPU cost (estimate, laptop) | Stability | How it joins the scan map | Verdict |
|---|---|---|---|---|---|
| Detail normal + height maps per surface class (made one time offline, hex-tiled) | Good for surfaces seen face-on; flat at grazing angles; no silhouette | 0.1-0.3 ms for 2-3 taps within 40 m | Stable if mips are correct | `CLAS` class picks the kit slice (in v1) | **Keep (in use)**; drive the choice by soil, rock and process (proposals 2, 3) |
| Triplanar projection on terrain | Needed on banks and cuts, where UV stretch shows today (Bixby) | 3 x taps on steep pixels only | Stable | Slope from `HGHT` | **Use on slopes > 30 degrees only** |
| Runtime virtual texture (RVT) of the corridor | Lets many layers (decals, wear, cracks, gravel, wet line) cost one fetch | 0.1-0.4 ms for page updates while moving; fetch is cheap | Stable; pages can lag at high speed if the budget is low | New chunk `PROC` drives the page material | **Core of proposal 3** |
| Parallax occlusion / relief mapping | Very good on rock faces, banks, brick and stone walls seen face-on; poor on the road ahead | 0.2-0.5 ms if limited to < 40 m and to surfaces whose normal faces the camera (estimate) | Swims at grazing angles; restrict by angle | `HGHT` + rock family pick the height map | **Use on vertical faces only**, fade by view angle |
| Real displacement (Nanite tessellation or a CPU-refined mesh) | The real thing for silhouettes of cut edges, kerbs, ditch lips | MSFS 2024: about 10 % frame rate in one test; ours unknown | Nanite tessellation still has landscape bugs in 5.8 (UE-367789) | `HGHT` R channel and the cross-section records | **Use only in a 30 m corridor**, on our own tile meshes, not on Landscape |
| Scattered small meshes (stones, gravel heaps, grass tufts, litter, posts) | Gives true silhouettes and parallax | 0.2-0.6 ms with instancing and distance culling (estimate) | Stable with Halton positions and fades | `INST` records and `PROC` densities | **Use**, placed by process rules (proposal 3) |
| Interior mapping for windows | Very convincing at 5-50 m, also at speed | ~0.05 ms (a few ALU per window pixel) | Stable | `BLDG.use` + era choose the room atlas | **Use** (already planned) |
| Impostors for far trees and buildings | Good beyond about 150 m | Very low | Must cross-fade to avoid pops | Tree archetype id (proposal 4) | **Use** beyond 150 m |
| Shell / blade texturing (our grass field) | Good near grass, inside one pixel | 0.1-0.3 ms | Stable | `CLAS` grass classes + dryness `B` | **Keep (in use)** |
| Per-place colour and tone transform (LUT) fitted offline | Makes the whole frame read like a photo of that place; adds no detail | ~0.05 ms (one 3D LUT lookup, in the existing post chain) | Very stable if blended slowly between places | `META` place cluster id picks the LUT | **Use (middle way for approach B)** |
| Light screen-space "camera" pass (local contrast, sensor noise at the real ISO, mild lens softness at the edges, vignetting) | Real cameras do this; players read it as "photo" | 0.1-0.3 ms | Stable if noise is temporal-stable | none | **Use, with the picture thread** |
| Full neural repaint of each frame (EPE, REGEN) | Very photo-like in papers; changes things, invents detail | 33 ms on an RTX 4090 at 960 x 512 (REGEN); far more on the laptop | Flicker risk; no measured stability | none | **Do not use now** (section 4) |

### 2.2 The combination we propose, by distance band

| Band (from the camera) | What the eye needs | Real 3D | Shading trick |
|---|---|---|---|
| 0-5 m, beside and under the car | Blurred at speed; the road surface ahead under the tangent point | Kerb and shoulder edge, rumble strip grooves, edge drop (also physics) | Road micro-texture, wet line, crack seal from the corridor texture |
| 5-40 m (the money zone; tangent point 1-2 s ahead) | Silhouettes, parallax, cause | Cross-section shape, stones, gravel heaps, grass tufts, posts, guard rails, tree trunks, porch steps | Corridor texture (unique), parallax on faces that face the driver, triplanar on slopes, interior mapping |
| 40-150 m | Shape and colour, no fine texture | Trees (real meshes), building masses, cut outlines | Detail normals fade out; corridor texture coarser mips |
| 150 m and more | Colour, haze, light | Impostors, terrain mesh | De-lit aerial, soil colour, per-place LUT, distance haze |

**The rule in one line:** outlines are geometry, the inside of a surface is shading, and every detail has a cause in the data.

---

## 3. What the scan engine contract v1 misses, and concrete additions

The contract (`scan-map-spec.md`) is a strong base: one file per tile, a CPU eye interface with a recorded licence, fusion with confidence, a self-check, determinism and seam tests. It is made from top-down data, so it misses what a driver sees from the side and up close.

| # | Gap in v1 | Why it matters at 1.2 m | Concrete addition (SCAN_VERSION 2) |
|---|---|---|---|
| G1 | 1 m base cell everywhere | The shoulder edge, kerb, rumble strip and ditch lip are 0.1-0.5 m features; at 5 m, one pixel is 4 mm | New chunk `CORR`: a 0.25 m raster in a band of ±20 m around each road centre line, in road (station, offset) coordinates, so it follows curves without waste. Channels: R = ground offset (mm), G = surface sub-class, B = process mask id, A = confidence. About 12 bytes per metre of road per side (zstd). |
| G2 | No road cross-section | "No road shoulder" in the project owner's screenshot; tyres feel nothing at the edge | Extend `ROAD` records with a cross-section profile: lane width, edge line, shoulder type (paved, gravel, turf, none, curb-and-gutter), shoulder width, edge drop (cm), rumble strip type and offset, foreslope ratio, ditch depth and width, backslope, mow line, barrier type and offset, each field with a source flag (map, lidar, HPMS prior, rule). Proposal 1. |
| G3 | Surface classes have no soil or rock identity | "dirt" and "rock" have one colour everywhere; the bank is lime green | New chunk `GEOL`: per cell u8 rock family (from the geologic map), u8 rock fragment %, RGB8 soil albedo (dry) from the soil survey, u8 bedding dip class. Split class 6 "bare soil" and 7 "rock" by this chunk, not by new enums. Proposal 2. |
| G4 | No water and process layer | Dirt, moss, gravel washout, puddles and leaf drifts follow water and gravity | New chunk `PROC`: flow accumulation (log), wetness index, sky-view factor (for occlusion, method 8 in world-detail-methods.md), downslope direction (2 bits), "tyre throw" zone, litter density. Proposal 3. |
| G5 | `INST` is 32 bytes: a tree is only height and crown size | A tree is read by its outline: crown base, crown shape, lean | New chunk `TREE` (48 bytes): position, species group, height, crown base height, crown radius in 8 directions (u8 each), crown density (lidar return ratio), lean (2 x i8), archetype id, seed. `INST` stays for small things. Proposal 4. |
| G6 | `BLDG` has no foundation, construction type, condition or facade parts | Porch steps, crawl-space skirts, basement windows, siding vs brick are the first things a driver reads on a house | Extend `BLDG`: construction type (NSI), foundation type and height (NSI), stories (NSI vs OSM vs lidar, with the chosen source), era (Census block-group median year built when no `start_date`), eave depth class, roof pitch class (from lidar roof planes), porch and garage flags, siding exposure class, trim colour, condition 0-255. Proposal 5. |
| G7 | No vertical surfaces | Cut faces, retaining walls, hedges and fences fill the side windows | New record `VERT`: polylines of vertical or near-vertical faces (from lidar breaklines, OSM `barrier`, retaining walls), with height, material and rock family. |
| G8 | Weather and season are inside the static file | A rain shower would invalidate the cache | Keep the scan static. Add a tiny run-time `DYN` texture per tile (wetness, snow, leaf fall, mown-days), written from live data, never cached. |
| G9 | Fusion has no plausibility gate | One bad tag made Flight Simulator's 212-floor tower | Add step 4b "plausibility": every numeric fact is checked against the other sources (OSM floors vs NSI stories vs lidar height; road width vs lanes; tree height vs canopy height). Outliers go to the next source and to `META.warnings`. |
| G10 | National inventories have no place in the priority | NSI, SSURGO, HPMS and TreeMap are measured, but coarse or statistical | Priority becomes: (1) map geometry, (2) lidar for heights and shapes, (3) **national inventories for attributes** (construction type, soil, rock, species, pavement), (4) eyes, (5) rules. An inventory value can be a distribution (for example HPMS shoulder width by state and road class) sampled by the stable seed. |
| G11 | Self-check only from above | The top-down check cannot see facades, cuts, shoulders or tree shape | Add a road-level check (proposal 7): render the tile at the driver's height at fixed poses; compare image statistics with licensed street photographs of the same region. |
| G12 | Eye licence field records weights and training data, but some planned models fail that rule | SDXL (LAION), Real-ESRGAN (DIV2K) and SAM (SA-1B) have training-data limits | Add `TrainingDataLicence` as its own field and a CI check that refuses an eye whose training data is not open. Proposal 9 gives a clean replacement. |

Budget check: `CORR`, `GEOL`, `PROC`, `TREE` add about 1-3 MB per tile before zstd (estimate), CPU about 1-2 s per tile on one worker (flow accumulation, cross-section fit, tree segmentation), inside the 3 s eye budget only if the eyes are light. If the budget is tight, the tree segmentation (G5) can run one tile later than the rest, because trees beyond 150 m are impostors anyway.

---

## 4. Approach B: an AI that repaints the finished picture

**The research.**

- Richter, AlHaija, Koltun (Intel), "Enhancing Photorealism Enhancement", 2021: a network reads the game's frame and its G-buffers and makes GTA V look like Cityscapes or Mapillary photographs. It looks very real in the video. [V] https://isl-org.github.io/PhotorealismEnhancement
- REGEN (2025) distils that into a faster image-to-image network: **33.5 ms per frame at 960 x 512 on an RTX 4090**, 11.5 GB of memory, no TensorRT; the original method takes about 1.1 s per frame. Temporal stability was not measured. Trained on GTA V frames and Cityscapes. [V] https://arxiv.org/pdf/2508.17061
- NVIDIA DLSS 5 ("3D-guided neural rendering", announced March 2026, in games from about September 2026): a last rendering stage that adds lighting and material detail from the engine's colour and motion vectors; deterministic, one frame in and one out; developer controls for "structure" and "tone" intensity and masks; **RTX 50 series only**. [V] https://nvidia.com/en-us/geforce/news/dlss-5-3d-guided-neural-rendering/

**Assessment for SimCruise.**

| Question | Answer |
|---|---|
| Cost on the laptop (RTX 2070 Super Max-Q) | REGEN needs 33 ms on a GPU with several times more tensor throughput; on the laptop we estimate 100-250 ms per frame (estimate). Our whole frame budget is about 16 ms. **Not possible now.** |
| Stability at speed | Unknown for REGEN (not measured); image-to-image models tend to flicker and to "invent" things that change between frames. A driving game at 100 km/h shows this at once. DLSS 5 uses motion vectors and claims no shimmer; we cannot test it on the laptop. |
| Licence | EPE and REGEN targets use Cityscapes (non-commercial terms) and GTA V frames; **fails the rule**. DLSS 5 is NVIDIA's own SDK; DLSS has been used in free games before, but the DLSS 5 terms must be checked [check]. |
| Fit with our promise | A repaint can make a wrong world look real (wrong rock colour, missing shoulder). It hides the data instead of fixing it. Our self-checks would test the paint, not the world. |

**Recommendation.**

1. **Now: the middle way, "AI teaches the trick".** Offline, fit a per-place "photo look": a 33³ colour and tone table (3D LUT) plus 4-6 camera parameters (local contrast, black level, saturation by hue, sensor noise) for each cluster of (Köppen class x land cover x season x sun height x sky state). The fit matches render statistics to statistics of licensed photographs of that kind of place (colour distribution transfer; Reinhard et al. 2001, Pitié et al. 2007; no neural weights needed, or a small model trained only on CC0 and CC BY photographs). Store about 200 tables (about 30 MB, estimate); blend between them as the car moves. Cost about 0.05 ms. **Guard:** the table must not correct albedo; materials and soil colours are fixed first (proposal 2), and the table is limited to camera-like changes, so it cannot hide a wrong world. Owner: the picture thread, with this team's data.
2. **Later, optional: a DLSS 5 switch** for players with RTX 50 cards, off by default, after a licence check and a stability test at 130 km/h. It does not run on the project owner's laptop.
3. **Not now: our own frame-repaint network.** Revisit when a model with clean training data runs under 4 ms on the laptop.

---

## 5. Prior art: what is old and what is new in our approach A

| Who | What they do | What is the same as ours | What is different |
|---|---|---|---|
| Microsoft Flight Simulator + Blackshark.ai | Cloud ML turns aerial photographs into buildings and trees for the planet; streamed from Azure [V] | Data in, world out; rules by biome; fallback | Cloud, not the player's PC; made for height; photographs, not inventories; no road-level work |
| Outerra (engine, 2010s) | Whole planet from a global DEM, refined procedurally to centimetres on the GPU; you can drive vehicles [S] https://outerra.blogspot.com/2012/02/outerra-tech-demo-released.html | Procedural near detail on a real planet, on the client | Fractal detail, not facts; few real roads, buildings or species |
| Bohemia Interactive Simulations (VBS, Blue IG, Mantle ETM) | Whole-Earth terrain for military training from open and licensed data [S] https://architosh.com/2022/05/bisim-releases-mantle-etm-enterprise-terrain-management | Open data to terrain at scale | Server and tool pipeline, paid, not driver-level realism |
| X-Plane autogen, Ortho4XP | OSM-driven autogen buildings, orthophoto tiles [K] | OSM to buildings | Generic kits, pilot view |
| Google Earth, Cesium 3D Tiles | Photogrammetry meshes [K] | Real shape | Melted at street level; licence forbids caching and derived objects |
| Research: inverse procedural modelling (Vanegas et al. 2012; Stava et al. 2014), CityDreamer, Streetscapes (2024) | Fit grammars by render-and-compare; generate cities or street views with diffusion [K] | Our self-check is render-and-compare | Research, offline; diffusion models have unclear training data |
| Driving games (Test Drive Unlimited Solar Crown, Forza Horizon, The Crew) | Hand-finished real regions [S] | Real places | One region by hand; not anywhere |

**What we can honestly call new** (no shipping product found that does it):

1. A **driver-height world built ahead of the car on the player's own CPU**, with no server of our own.
2. **US national inventories as the source of near-road and vertical detail**: road design statistics (HPMS), soil (SSURGO), rock (SGMC), structures (NSI, USA Structures), forest (TreeMap, LANDFIRE), plus lidar for the shape.
3. **Clean-licence AI only**, with the training data recorded and checked by a test.
4. A **self-check of each tile** at build time against the aerial photograph (above) and against road-level photograph statistics (from the side).
5. **One record for three senses**: the same shoulder, gravel and rumble strip is drawn, felt by the tyres and heard.
6. **Process-true unique texture** in the corridor (proposal 3).

---

## 6. The ranked proposals

Score = realism gain (1-5, our estimate of what a driver sees) x share of US road length that gets the full gain / cost (S = 1, M = 2, L = 3, plus 1 for each 0.5 ms of GPU). A higher score comes first.

| Rank | Proposal | Gain | Coverage | Cost | GPU ms (estimate) | Score | Fixes in the project owner's screenshot |
|---|---|---|---|---|---|---|---|
| 1 | The road's real cross-section | 5 | 1.0 | M | 0.1-0.2 | 2.5 | no road shoulder |
| 2 | Ground from the soil and the rock | 4 | 0.95 | S-M | 0 | 2.5 | lime-green bank, no rock |
| 3 | Process-true corridor texture (bold idea) | 5 | 1.0 | M-L | 0.3-0.5 | 1.7 | smooth bank, soft picture |
| 4 | Trees grown to their own lidar | 5 | 0.75 (lidar), 1.0 with fallback at gain 3 | L | 0.5-1.0 | 1.1 | blob trees |
| 5 | Facades from the structure inventory | 4 | 0.35 (built-up road length) | M-L | 0-0.1 | 0.6 (high in towns) | (towns) |
| 6 | Pavement that ages by physics | 3 | 1.0 | M | 0 (inside 3) | 1.5 | |
| 7 | Road-level self-check | enabler | 1.0 | M | 0 (dev time) | — | stops regressions |
| 8 | Sharpness at 1.2 m (texel density contract) | 3 | 1.0 | S | -0.2 to +0.3 | 3.0 (not new) | soft picture |
| 9 | Our own clean-data material model | enabler (legal) | — | M | 0 | — | — |
| 10 | Per-place photo look (LUT) | 2 | 1.0 | S | 0.05 | 2.0 | soft, flat picture |
| 11 | Roadside furniture from lidar | 3 | 0.6 | M | 0.1 | 0.9 | |
| 12 | Verge management (mow line, season, salt, dust) | 2 | 1.0 | S | 0 (grass field) | 2.0 | lime-green bank (colour) |

Proposals 8, 10 and 12 score high because they are cheap, but they are known methods; they belong to the picture and world threads and are listed so nothing is lost. Proposals 1-5 are the original work.

---

### Proposal 1. The road's real cross-section ("the 15 m the tyres and eyes see")

**Idea.** Every road gets an engineering cross-section, built the way a US engineer designs one: travel lanes, edge line, shoulder (paved, gravel, turf, none, or curb and gutter), edge drop-off, rumble strip, hinge point, foreslope (for example 1:4 or 1:6), ditch bottom, backslope, mow line, clear zone, and a guard rail where the foreslope is steep or the drop high (the Roadside Design Guide warrant logic). Where 3DEP lidar exists, the profile is fitted to the lidar every 5 m (ditch depth, slope ratios, edge position). Where it does not, the profile comes from rules and statistics. The cross-section is a vector edit merged into the ground at load (the MSFS terraforming pattern), and it writes `CORR`.

**Learn from the sample, apply everywhere.** HPMS has shoulder type and width, lane width and median only for its Sample Panel sections, not for every road (FHWA field manual, [V]). So we turn the sample into distributions: P(shoulder type, width | state, functional class, urban or rural, terrain type). Each road without data draws from its distribution with its stable seed. The result is statistically correct for the state even where no road has data, and measured where it exists (OSM `shoulder=*`, `width`, lidar).

**Why new.** Games hand-model shoulders or use one generic verge. Flight Simulator does not need them. We found no game that builds US cross-sections from design standards and national road statistics for every road.

**Three senses.** The same profile feeds: the tyre contact (edge drop of 5-10 cm is a real hazard and a real feel; gravel grip on the shoulder), the rumble (the rumble strip at its real spacing gives a tone of speed / spacing), and the sound (gravel spray). Domain-ideas idea 2 ("one road, three senses") gets its geometry.

**Data and licences.** FHWA HPMS (US government, public domain); OSM (ODbL); USGS 3DEP lidar (public domain); state DOT standard drawings and AASHTO values as published numbers (facts; we cite and do not copy drawings); MUTCD (public domain).

**Cost.** CPU: profile fit per 5 m station, about 0.1-0.3 s per tile (estimate). GPU: about 2-4 thousand more triangles per 100 m near the car; under 0.2 ms (estimate). Physics: more contact samples per tyre near the edge (about 4 per tyre), a small cost on the 360 Hz loop.

**Fallback.** No lidar: rules by road class, terrain type (from 10 m slope) and state distributions. No HPMS sample for that class in that state: the national distribution.

**First experiment.** Nebraska rural road, Tail of the Dragon and two random rural test places: build profiles with and without lidar; measure RMS error of the rule-only profile against the lidar profile at 25 m stations (goal under 0.25 m); capture driver frames and compare with licensed photographs. Then drive with two wheels on the shoulder and check that rumble and grip change.

**Owner.** World thread (scan engine `ROAD` and `CORR`) with the road-engineering owner; car team for contact.

---

### Proposal 2. Ground from the soil and the rock

**Idea.** The colour of bare ground is a measured fact in the US. The NRCS soil survey describes each soil horizon with Munsell colour (hue, value, chroma, moist and dry) and the share of rock fragments; the USGS State Geologic Map Compilation gives the bedrock type under every point of the conterminous US. We use them:

- Bare soil albedo = the dry Munsell colour of the surface horizon of the dominant soil component, converted to linear albedo (Munsell renotation to sRGB to linear, clamped by the class range in `classes.json`). Moist colour on wet days (from `DYN`).
- Where lidar slope is steep (over about 35 degrees) or the cross-section has a cut, the face shows rock or subsoil: rock family from the geologic map (granite, schist, shale, sandstone, limestone, basalt, unconsolidated), with the matching detail kit (fracture pattern, bedding, colour range). Bedding dip class sets the angle of layers in road cuts.
- Rock fragment % sets the density of stones in the scatter (proposal 3).
- Vegetation cover on banks comes from NLCD and season; bare share rises with slope and with how recently the cut was made (the road's age, when known).

**Why new.** Games pick a palette per biome by hand. Flight Simulator samples colour from the photograph, which mixes grass, soil and shadow in one 0.3-0.6 m pixel. Soil science records the true colour of the dirt at that place, and geology says what the rock is. We found no game that uses them.

**Data and licences.** NRCS SSURGO and STATSGO2 (US government, public domain), through Soil Data Access web queries per tile, cached; USGS SGMC (public domain), through its feature service or one download. [check]: the exact SSURGO table and field names for horizon colour, and the Munsell conversion table licence (we implement the conversion ourselves; do not link GPL code).

**Cost.** CPU: two lookups per tile, a few milliseconds after the fetch. GPU: 0 (it changes material inputs). Data: a per-map-unit colour table (small) plus polygons per tile.

**Fallback.** SSURGO gaps (some public lands): STATSGO2 (all US, coarser). No soil data at all: Köppen and NLCD palette. No geologic map (Alaska and Hawaii in SGMC? [check]): rock family from the global lithology map GLiM [check licence].

**First experiment.** Tail of the Dragon (red-brown clay and metasedimentary rock), Big Sur (Franciscan rock, tan soils), Nebraska (loess, dark loam) and three random places: print the soil and rock answers, render a bank and a cut, put the frames beside licensed photographs of road banks in the same county (Wikimedia Commons). Goal: a viewer picks the right county pairing better than chance.

**Owner.** World thread (`GEOL` chunk, ground shader inputs).

---

### Proposal 3. Process-true corridor texture (our bold original idea)

**Idea.** The project owner's "trick over everything", made better: **a unique texture for the corridor ahead of the car, in which every texel comes from the process that made it, so nothing repeats and nothing is without a cause.**

How it works:

1. The scan engine writes the process fields for each tile (`PROC`, `GEOL`, `CORR`): water flow and wetness from the lidar or 10 m ground, the tyre-throw zone beside each lane (where gravel lands), the leaf and needle supply (canopy above, species, season), rock fragment %, pavement age and climate (frost cycles), traffic volume (AADT).
2. A runtime virtual texture covers the road corridor (±20-30 m). When a page is needed (the RVT feedback says so), the GPU renders it **once** with a "process material" that reads those fields: gravel density falls off from the shoulder edge as a throw curve; rills and washout follow flow lines; fine dirt collects where flow slows; leaves drift down-slope and into the gutter; moss on the shaded, wet side; frost-crack networks on old asphalt; tyre polish in the wheel paths. Each process is a few lines of maths (noise shaped by the field), not a simulation in time.
3. The page is cached and reused while the car is near; pages behind the car are freed. So the cost is paid once per page, not every frame, and the detail is **unique**: no hex tiling is needed in the corridor because no texel is a copy.
4. The same pages give the height for parallax on faces that face the driver and the normal for lighting; the scatter meshes (stones, gravel heaps, leaf piles, litter) are placed from the same fields, so the mesh and the texture agree.
5. Like Flight Simulator's GameRender texture, a small CPU copy of the "gravel" and "wet" channels under the wheels goes to the tyre model.

**Why new.** id Software's MegaTexture (Rage, 2011) gave unique texture, but it was painted by artists and stored on disc; it was not generated from facts. Unreal's RVT is a cache for layered materials, normally driven by painted masks. Horizon Zero Dawn places objects from density rules on the GPU. We combine them in a new way: **unique texture generated ahead of the car from physical process fields measured from open data, shared by the picture, the scatter and the tyres.** We found no game or paper that does this for real places.

**Data and licences.** Our own fields from 3DEP and 10 m DEM (public domain), OSM (ODbL), HPMS AADT (public domain), soil and rock (proposal 2), climate (Köppen CC BY 4.0; frost days from NOAA normals, public domain). Detail kits: CC0 or our own (proposal 9).

**Cost (estimate).** GPU: RVT page rendering while moving 0.2-0.4 ms; fetch about 0.05 ms; parallax on facing faces 0.1-0.2 ms. Memory: an RVT pool of 64-128 MB. CPU: flow accumulation and fields about 0.3-0.5 s per tile on a worker. To be measured in the first experiment.

**Stability.** Pages appear by mip, coarse first; at 130 km/h the corridor ahead must be requested early (prefetch along the route, 4 s ahead). Risk: page pop at high speed; test with the pop-in button test.

**Fallback.** No lidar: fields from the 10 m DEM (fewer rills, same rules). Low settings: no RVT, the current hex-tiled detail kit.

**First experiment.** One 300 m stretch of the Dragon and one random suburban place: build the fields in the Python lab (`lab/scan/`), render a page set offline at 8 mm, show still frames beside licensed photographs; then measure the GPU cost of RVT page updates in game on High and Ultra at 100 km/h, and count pops.

**Owner.** World thread (scan fields) with the technical artist and the picture thread (RVT, material).

---

### Proposal 4. Trees grown to their own lidar

**Idea.** For every tree within about 150 m of a road:

1. From the 3DEP point cloud (vegetation returns), segment single trees on the CPU with classic methods (local maxima on the canopy height model plus watershed and point clustering; no learned weights). Measure height, crown base height, crown radius in 8 directions, crown density (share of returns inside the crown), and lean.
2. Species group from USFS TreeMap (forest plot data imputed to 30 m), LANDFIRE existing vegetation type and Köppen; in towns, OSM trees and city inventories where licensed.
3. Grow the tree with space colonisation (Runions et al. 2007) inside the measured envelope, with per-species parameters (branch angle, leaf size, bark). Cluster similar envelopes so a tile needs about 50-200 variants; bake LoDs and an octahedral impostor for each variant on the CPU worker or once offline.
4. Below 3 m, lidar returns give the understory density: shrubs and saplings fill the forest edge, which is what the driver sees most.

**Why new.** Games place species meshes by density. Flight Simulator detects tree positions and uses impostors. We found no game that fits each real tree's outline to its own lidar crown at scale.

**Data and licences.** USGS 3DEP point clouds (public domain); USFS TreeMap and LANDFIRE (public domain); Meta and WRI canopy height 1 m (CC BY 4.0) for fallback; DeepForest (MIT, NEON training data) as an optional eye on NAIP.

**Cost (estimate).** CPU: segmentation 0.5-1.5 s per tile (can run one tile later). GPU: about the same as today's trees if variants are capped and impostors start at 150 m: 0.5-1.0 ms in dense forest. Memory: variant meshes per region.

**Fallback.** No lidar: height from the 1 m canopy height map, crown from DeepForest on NAIP or from the species default; no aerial data: TreeMap and Köppen density with species defaults.

**First experiment.** A 400 m square at the Dragon start (lidar already measured, 4.4 points/m²): segment, grow, render from the driver's seat; A/B against the current blobs; ask three viewers which is real; measure GPU.

**Owner.** World thread (trees first, as the project owner said); this proposal improves the plan it already has.

---

### Proposal 5. Facades from the structure inventory

**Idea.** For each building, fuse: NSI (construction type: masonry, wood, steel, manufactured; stories; foundation type: slab, crawl, basement, pier; foundation height; occupancy), USA Structures (occupancy, CC BY 4.0), OSM tags, Overture height, lidar roof planes (pitch, ridge direction), and the Census median year built of the block group when there is no `start_date`. A style grammar per (region x era x type) then builds what a driver reads first: porch and steps to the real floor height (a crawl space at 0.6-0.9 m means 3-4 steps), skirt or basement windows, siding exposure or brick coursing, window rows per story, garage door on the street side for post-1950 houses, eave depth and roof pitch from lidar.

**Proportions from measured drawings.** The Library of Congress Historic American Buildings Survey (HABS) has measured drawings of US vernacular buildings, mostly public domain [check per item]. We take numbers from them (window height to width, sill height, eave depth, porch depth) per region and era, so the grammar has real proportions, not artist guesses.

**Why new.** Flight Simulator infers roofs and heights from photographs. Games use artist kits per city. We found no game that uses the US structure inventory for construction type and foundation height.

**Data and licences.** NSI (USACE; public access to primary fields [check terms]); USA Structures (CC BY 4.0); Census ACS (public domain); OSM (ODbL); 3DEP (public domain); HABS (mostly public domain [check]).

**Cost.** CPU: grammar per building, milliseconds. GPU: kit instances and the facade shader; similar to today plus interior mapping (about 0.05 ms).

**Fallback.** NSI missing: occupancy from USA Structures, era from the Census, stories from lidar height / 3 m, construction type from the region's distribution in NSI (learn from the sample again).

**First experiment.** 1 km of street in two random test places (one suburb, one small town): count wall types in licensed street photographs (Mapillary or Wikimedia, by hand) against the scan answer; goal above 70 % agreement on wall type and stories.

**Owner.** Buildings and world thread (`BLDG` v2).

---

### Proposal 6. Pavement that ages by physics

Pavement type and condition from HPMS where it exists (surface type, cracking %, rutting, IRI for NHS and some other roads [V]), OSM `surface`, lidar intensity (world-detail-methods.md, method 3); else rules. Climate and traffic make the pattern: transverse thermal cracks at 5-15 m spacing in frost climates, alligator cracking in wheel paths under heavy traffic, block cracking under hot sun, crack seal ("tar snakes") on roads of middle age, patches, polished and darker wheel paths, an oil-drip line in the lane centre, chip seal on low-volume rural roads in states that use it, joints every 4.5 m on concrete. Written into the corridor texture (proposal 3) and the road surface record that the sound and rumble read. GPU 0 beyond proposal 3. First experiment: crack-type counts on 30 licensed photographs from three climates against the game.

### Proposal 7. Road-level self-check

The v1 self-check looks from above. Add a check from the driver's seat: at fixed poses per tile (every 100 m, both directions), render a small frame and compute statistics that need no AI: amplitude spectrum slope by band (real photographs fall as about 1/f), colour distribution in a de-lit space, edge density, share of vertical structure, sky share, and NVIDIA FLIP (BSD-3) against a reference where one exists. Compare with the same statistics from licensed street photographs of the same kind of place (Köppen x road class x land use), not the same spot. Photographs are used only at development time to compute statistics, never shipped; each use is logged with its attribution in `META`. [check]: whether statistics from CC BY-SA images are free of share-alike duties (we think yes, statistics are facts, but ask before shipping the tables). This is the regression test for every proposal above.

### Proposal 8. Sharpness at 1.2 m (texel density contract)

Not new, but it is the cheapest fix for the soft picture. Targets by band: about 2 mm per texel at 2 m, 4 mm at 5 m, 16 mm at 20 m (section 7 of the study). Check: anisotropic filtering at 16x on ground materials, texture mip bias set for TSR's render scale, TSR thin-geometry detection on High (lighting-tech.md item 5), screen percentage on Ultra, and motion blur limits. Owner: picture thread.

### Proposal 9. Our own clean-data material model

The detail kit today uses SDXL, whose training data (LAION) fails the project owner's rule. Train a small material generator (albedo, normal, height, roughness at 512-1024 px) only on CC0 data: the CC0 part of MatSynth (about 4,000 materials from ambientCG, Poly Haven, CGBookCase, ShareTextures, TextureCan) [V], plus our own photographs (the lowered car, garage, local roads, taken by the team). Add Infinigen (BSD-3, maths only) for rock and plant meshes. Training runs offline on the PC over hours to days; run time cost is 0. Weights and data are ours, so the licence is clean by construction. If Public Diffusion (public domain and CC0 data) is released with open weights, test it as a second source.

### Proposal 10. Per-place photo look (LUT)

Section 4, recommendation 1. Owner: picture thread, with this team's statistics.

### Proposal 11. Roadside furniture from lidar

Poles, sign posts, guard rails and mailboxes are tall, thin objects near roads. In QL2 lidar (about 2-8 points/m²) a utility pole shows as a vertical column of returns. Detect columns within 15 m of roads (classic shape tests, no weights), write `INST` with "from lidar" flags, and let OSM and rules fill the rest at real spacing. Coverage depends on point density; fallback is the roadside rules (roadmap 5.4).

### Proposal 12. Verge management

Real verges are managed: a mown strip beside the shoulder (about one mower width), taller grass beyond, a season (dormant tan, cured straw, green), salt burn near highways in snowy states in spring, dust on leaves near unpaved roads, wildflower plantings in some states. Rules by state, road class, climate and season into the grass field's inputs (`CLAS.B`, `DYN`). GPU 0.

---

## 7. Legal flags in the current plans

| Item | Problem | Action |
|---|---|---|
| SDXL in the detail kit | Training data LAION (web-scraped) | Replace with proposal 9; until then do not ship new SDXL outputs [check with the project owner's rule] |
| Real-ESRGAN for aerial super-resolution | DIV2K training data, research use | Stop; use classic sharpening or proposal 9 |
| SAM 2 (plan-v2 lists it) | SA-1B images, research licence | Do not use as an eye |
| Moondream2, Florence-2 | Training data not fully published | Do not ship until documented |
| Mapillary and Panoramax photographs | CC BY-SA | Development-time statistics only, logged; no images shipped |
| NSI | Public access to primary fields; exact terms not read | Read the USACE terms before use |

---

## 8. First experiments, in order

1. Proposal 2 (soil and rock): Python lab, three hero roads plus three random places, frames beside photographs. One day.
2. Proposal 1 (cross-section): rule profiles vs lidar profiles on four roads; RMS error. Two days.
3. Proposal 3 (corridor texture): offline page render from fields, then an RVT cost test in game at 100 km/h. Three to five days.
4. Proposal 4 (trees from lidar): the Dragon 400 m square. Three days.
5. Proposal 7 (road-level check): statistics script, run on every experiment above.
6. Proposal 5 (facades): two random towns, wall-type agreement. Three days.

All results go to `docs/research/innovation/` with frames, and each one says what a driver sees, not only numbers.

## 9. Sources (new here; the study and the survey list the others)

- Richter, AlHaija, Koltun, "Enhancing Photorealism Enhancement" (Intel, 2021): https://isl-org.github.io/PhotorealismEnhancement [V]
- "REGEN: Real-Time Photorealism Enhancement in Games via a Dual-Stage Generative Network Framework" (2025): https://arxiv.org/pdf/2508.17061 [V]
- NVIDIA, "DLSS 5: 3D-guided neural rendering" (2026): https://nvidia.com/en-us/geforce/news/dlss-5-3d-guided-neural-rendering/ [V]
- Outerra tech demo (2012): https://outerra.blogspot.com/2012/02/outerra-tech-demo-released.html [S]
- BISim Mantle ETM (2022): https://architosh.com/2022/05/bisim-releases-mantle-etm-enterprise-terrain-management [S]
- FHWA HPMS Field Manual, data item extent: https://www.fhwa.dot.gov/policyinformation/hpms/fieldmanual/page02.cfm [V]
- USACE National Structure Inventory fields: https://gee-community-catalog.org/projects/nsi/ [V]
- FEMA/ORNL USA Structures (CC BY 4.0): https://gee-community-catalog.org/projects/usa_structures/ [V]
- USGS State Geologic Map Compilation: https://www.usgs.gov/data/state-geologic-map-compilation-sgmc-geodatabase-conterminous-united-states [S]
- MatSynth: https://huggingface.co/datasets/gvecchio/MatSynth [V]
- Spawning, Public Diffusion: https://spawning.substack.com/p/a-fireside-chat-with-the-creators [V]
- Mapillary licence: https://help.mapillary.com/hc/en-us/articles/115001770409-CC-BY-SA-license-for-open-data [V]
- Unreal issue UE-367789: https://issues.unrealengine.com/issue/UE-367789 [S]
- Known references, not re-read today [K]: Runions, Lane, Prusinkiewicz 2007 (space colonisation); Müller et al. 2006 (CGA shape); van Muijden, GDC 2017 (Horizon Zero Dawn placement); Reinhard et al. 2001 and Pitié et al. 2007 (colour transfer); Andersson et al. 2020 (FLIP); id Software Rage (MegaTexture, 2011); Vanegas et al. 2012 (inverse procedural modelling); AASHTO Roadside Design Guide.
