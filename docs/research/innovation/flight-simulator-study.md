# How Microsoft Flight Simulator builds the Earth (study, 14 November 2026)

Innovation team, for Aaron's question "how is Microsoft Flight Sim working and how can we create hyper detail". This note says what is public about Microsoft Flight Simulator 2020 and 2024 (MSFS), what we infer, and what SimCruise must copy, change or do better. The next two notes are the survey of game techniques ([technique-survey.md](technique-survey.md)) and the ranked proposals ([hyper-detail-proposals.md](hyper-detail-proposals.md)).

Source marks: **[V]** = we read the source today. **[S]** = we saw only the headline or a search summary. **[I]** = our inference, not a public fact.

## 1. Summary in six lines

1. MSFS streams about 2-2.5 PB of Bing aerial photographs, a global elevation model, Bing photogrammetry meshes for a few hundred cities, and OpenStreetMap vectors from Azure. [V]
2. Offline, in the cloud, machine learning (Blackshark.ai) turns the 2D photographs into 1.5 billion buildings (footprint, height, roof) and detects trees; rules by biome choose the species. [V]
3. In the game, a quadtree of Mercator tiles holds about 90 kinds of data layer ("markers"); each layer is a small state machine that can fail, fall back to its parent tile, and hot-reload. [V]
4. The renderer draws the photograph on the ground, adds "detail maps" per pixel, draws trees as one-vertex impostors, and generates grass by surface type. [V]
5. MSFS 2024 added a 3D ground near the aircraft (displacement, stones, gravel, grass), 27-30 biomes, hundreds of plant species with seasons, and a rolling cache instead of a large install. [V]
6. It is designed for a view from hundreds of metres. Neumann says buildings are weak close up; players report blurry ground. The view from a car at 1.2 m needs 100-1000 times more ground detail per screen pixel. That gap is our opportunity. [V] + [I]

## 2. The data layers

| Layer | What MSFS uses | Facts | Mark |
|---|---|---|---|
| Aerial photographs | Bing Maps satellite and aircraft photographs | 2+ PB (Asobo GDC 2022); 2.5 PB (Game Developer 2021, Engadget 2021); 2,500,000 GB streamed (TweakTown on MSFS 2024) | [V] |
| Photogrammetry ("TIN") | Bing 3D city meshes from oblique aerial cameras | "about 400 cities" in 2020 (TechCrunch); "over 500 TIN cities" and "more than 100,000 km2 of photogrammetry countryside" in 2024 (Xbox Wire) | [V] |
| Elevation (DEM) | Global DEM, improved per World Update | "Specialist teams gathered digital elevation maps across the planet" for 2024 (Xbox Wire). The talk does not give the resolution. | [V] |
| Vectors | OpenStreetMap and other sets | Water, roads, footprints, single trees (Asobo GDC 2022) | [V] |
| Land class | Land-class data | Drives vegetation masks and meshes (GDC 2022) | [V] |
| Live data | Meteoblue weather, FlightAware traffic, ship traffic (2024) | Game Developer 2021; PC Gamer 2024 | [V] |
| Animals (2024) | GBIF occurrence data for wild animals; "field detection" for livestock | Xbox Wire 2024 | [V] |

The rule in the terrain engine is strict: **an area is either photogrammetry (TIN) or DEM plus aerial photograph**, never both mixed. [V]

## 3. Offline: the cloud pipeline (Blackshark.ai and Azure)

- **Buildings.** Blackshark.ai reconstructed "1.5 billion buildings from 2D satellite images" with a team of about 50 people. Machine learning finds footprints, also where trees hide them. Height is the hard part: where GIS data is missing they use shadows (difficult, because Bing photographs carry no time stamp), roof equipment as a clue for building type, and area averages (for example typical floor counts for malls). Point clouds were not used. Two or three people labelled the training data for the whole planet. (TechCrunch, 17 August 2020) [V]
- **Roofs.** Microsoft hired a specialist in roof reconstruction from 2D photographs (Neumann in Game Developer 2021). [V]
- **Night.** Building semantics place windows and lights at night, because the photographs have day light baked in. (TechCrunch) [V]
- **Trees.** Trees, bushes and grass look alike from above, so the tree detector needed a lot of training; Neumann expected ML to rebuild trees "in fine detail" later. (Game Developer 2021) [V]
- **Scale of compute.** Blackshark orchestrates "over 100 machines in parallel" with Flyte on Kubernetes (Union.ai case study) [V]. Its CTO says the backend can process imagery of the whole planet "in less than 72 hours" (VentureBeat 2022) [V]. Neumann: "we rebuild the planet every 72 hours", about 2 trillion trees and 2 billion buildings (Engadget 2021) [V].
- **Colour.** Community posts say Blackshark also does blending and colour correction as cached data, which is why a new Bing photograph appears late in the game. No staff confirmed this. [S, community]
- **Accuracy.** Blackshark's CEO said "it might be that 20% of the buildings are off" (a guess, not a measurement). Normal QA does not work for statistical output at this scale. (TechCrunch) [V]
- **Famous failure.** In Melbourne one OpenStreetMap entry said 212 floors instead of 2, and the game built a 212-storey tower (GamesRadar, The Register, August 2020) [S]. One bad tag made one very visible error, because no rule checked plausibility.

## 4. In the game: the terrain engine (Lionel Fuentes, GDC 2022)

Source: "Designing the Terrain System of Flight Simulator: Representing the Earth", Lionel Fuentes (Technical Director, Asobo), GDC 2022 Advanced Graphics Summit. Slides on asobostudio.com. [V]

- **Tiles.** A quadtree on Web Mercator, the same scheme as Bing Maps tiles. LoD = number of subdivisions. Tiles are cut at up to 2x2; the general case uses a CPU-made mask texture and a pixel-shader discard (the talk calls it expensive). Skirts hide cracks; skirt size depends on slope.
- **Which LoD to load.** From the estimated screen size of the tile **and the measured network bandwidth**, corrected all the time.
- **Draw the best you have.** `RecurseRequest()` asks for tiles in the frustum; `RecurseDraw()` draws the best available data; a tile can fall back to its parent's data. Tiles near the camera stay in memory.
- **Markers (about 90 types).** Each tile holds markers: DEM, TIN, BingAerialTex, VectorData, VegetationMask, WaterMask and more. Each marker is a state machine (REQUESTED, PROCESSING with sub-states INIT, DRAW_TO_GPU, COPY_TO_CPU, MERGING, DONE, then AVAILABLE or FAILED). Markers depend on other markers. Hot reload makes a new versioned marker, swaps it in when ready and reloads the markers that depend on it.
- **Synthesised aerials.** Where the photograph is missing or poor, the engine synthesises one from altitude, slope and tree presence.
- **Edits as vectors.** Terraforming (airports, roads) is stored as points, rectangles and polygons with heights and influence radius. It never changes the source DEM: a distance field is generated and merged at load.
- **Water.** Drawn on the terrain with pixel-shader branches; a distance field drives blending, flow, foam and flattening; flattening strength scales with height difference to reduce popping.
- **Trees.** Per-tile markers at LoD 13 and 15, made in Azure with ML. Positions use Halton sampling, so density can change at draw time without a new set. Species from biome rules, colour sampled from the aerial. Drawn as 3D impostors: **one draw call per species per tile, one vertex per tree**. 23 million trees generated, 6.3 million in the frustum.
- **Grass.** Spawned by surface type and coloured from the aerial.
- **GameRender textures.** High-quality per-tile textures for airports and runways, made each frame, stored at LoD 15 and **read back to the CPU for collision and for tree and grass placement**.
- **Compression.** BC3 for colour in YCoCg (Y 6 bits, Co and Cg 5 bits) to reduce colour shift; BC2 for flags.
- **Shadows.** Cascaded shadow maps near; a top-down height map with ray marching for terrain shadows over 300 x 300 km; screen-space ray-marched shadows for small detail.
- **Detail maps.** Near, mid and far detail maps for dirt, asphalt and grass, chosen per pixel at run time.
- **Large coordinates.** CPU in 64-bit doubles; GPU in an "anchor space" at the camera with Y along the Earth normal; reversed depth.

## 5. What changed in MSFS 2024

| Change | Facts | Mark |
|---|---|---|
| Detail | "Detail of its virtual environment by a factor of 4,000" (marketing; method not given) | [V] Xbox Wire, TweakTown |
| 3D ground | "Ground is no longer a simple heightfield": stones, rocks, gravel and grass in 3D, and they act on the wheels. Neumann: "We can basically get every rock on Earth now". | [V] Xbox Wire |
| Displacement setting | On in High and Ultra presets. One third-party test: about 10 % lower frame rate (37.0 vs 40.6 fps, RTX 5070 Ti, 4K) and about 0.2 GB more video memory; a big cost in VR. | [V] msfsaddons.com FAQ (one test only) |
| Biomes | 27 biomes (Xbox Wire) or 30 (PC Gamer); hundreds of species; grass, rocks and flowers change with seasons | [V] |
| Scale | About 3 trillion trees, 1.5 billion buildings, 40,000 airports (procedural); about 150 airports and 900 oil rigs by hand | [V] Xbox Wire |
| Streaming | Install about 23-50 GB (sources differ) instead of up to 500 GB; high-detail areas stream only along the flight path; LoDs load only when needed; content keeps working for a while if the server connection is lost (warning after 30 s) | [V] Xbox Wire, Stormbirds dev-stream recap, PC Gamer |
| Launch failure | "A very big database, and there's a cache, and that cache is currently getting saturated" (Wloch). Load tests used 200,000 simulated users. If blocking data is missing, loading stalls at 97 %. | [V] PC Gamer, Wikipedia |
| Player complaints | Ground and buildings softer than in 2020, aggressive LoD; no staff explanation found | [V] official forum thread (users only) |

## 6. Costs and limits

**Public:** about 50 people at Blackshark for buildings; "massive" cloud compute; 100+ machines in parallel; 2-2.5 PB of imagery; a world rebuild in under 72 hours. Microsoft has not published money costs. **We do not guess them.**

**Limits that matter for a driving game:**

1. **Made for height.** Neumann: buildings look acceptable from high altitude, weaker up close (Game Developer 2021) [V]. Outside photogrammetry cities "everything is just a height field with nothing on it" (2020) [V].
2. **Photographs carry their own light.** Baked shadows, clouds, seasons and cameras; this needs cleaning and is never perfect (GDC 2022 lists these as known issues) [V]. We solved part of this already ("unbake the sun", world-detail-methods.md).
3. **From above, a tree, a bush and a lawn look alike** [V]. Trunks, crown shapes, understory and facades are not in a top-down photograph at all [I].
4. **Statistical errors are visible.** One bad map tag made the Melbourne tower; about 20 % of buildings may be wrong [V]. At 1.2 m every error is in the driver's face [I].
5. **Server dependence.** The 2024 launch failed at the cache [V]. SimCruise has no money for servers, so our cache must be local and our sources must be public servers that we use within their limits [I].
6. **Photogrammetry meshes melt at low height.** Bing city meshes are made for aerial views; at street level trees and cars are merged into walls ("melted"). This is a common observation, not a published measurement [I]. Our own earlier test of a lidar "skin" saw the same look (world-detail-methods.md, method 11).

## 7. Why the driver's view is a different problem (our calculation)

A pixel on a 1600 x 900 screen with a 74 degree horizontal field of view covers about 0.0008 rad. The ground that one pixel covers:

| Viewer | Distance | Across the view | Along the view |
|---|---|---|---|
| Driver, eye 1.2 m | 2 m (beside the door) | 1.6 mm | 2.7 mm |
| Driver | 5 m | 4 mm | 17 mm |
| Driver | 20 m | 16 mm | 0.27 m |
| Driver | 100 m | 8 cm | 6.7 m |
| Pilot at 300 m above ground | 300 m below | 0.24 m | 0.24 m |
| Pilot at 1,000 m | 1,000 m below | 0.8 m | 0.8 m |

So a 0.6 m NAIP photograph or a 0.3 m Bing photograph matches the pilot's pixel. For the driver, at 5 m, the ground needs about **4 mm per texel: 150 times finer than NAIP**, and nothing measured at that size exists for the whole US. Also, from 1.2 m most of the screen is **vertical** surfaces (facades, cut faces, trunks, hedges, guard rails) that a photograph from above never sees. The grazing angle also hides flat textures and shows height (stones, blades, kerb edges).

Conclusion: MSFS's measured layers are the right base for the far world (beyond about 100 m). Near the road, detail must be **generated** from facts that describe what is there (soil, rock, road design, tree shape, building type), not copied from a photograph.

## 8. What SimCruise takes from MSFS

| MSFS method | Take it? | How in SimCruise |
|---|---|---|
| Markers: one state machine per data layer, dependencies, parent fallback, versioned hot reload | **Yes, high value** | The scan engine's layers (NAIP, 3DEP, OSM, footprints, Köppen, NLCD, TreeMap) as markers. Parent fallback also answers the P0 "missing tile ahead" finding (onboarding row 2): draw the coarser tile and its collision, never nothing. |
| LoD request from screen size and measured bandwidth | Yes | Request radius from speed and measured fetch rate per source (we have several public servers, each with its own limit). |
| Synthesised aerial where data is missing | Yes (we have it) | Keep; feed it the soil colour (proposal 1) instead of a palette. |
| Edits as vectors merged at load (terraforming) | Yes | The road cross-section (proposal 2) as vector edits merged into the ground at load; never change the source DEM. |
| Halton tree positions, density changes at draw time | Yes | Use for scatter (proposal 3); quality setting changes density without a new set. |
| One-vertex impostors, one draw per species per tile | Yes, for far trees | Our near trees must be real meshes (proposal 4). |
| GameRender texture read back to CPU for collision and placement | **Yes, new use** | The shoulder, gravel, rumble strip and drop-off that we draw are also what the tyres feel. One record, three senses (domain-ideas idea 2). |
| Detail maps chosen per pixel | Yes (we have the detail layer) | Choose by scan-map class, soil and rock, not by colour guess. |
| YCoCg BC3 for colour | Yes, cheap | For the scan map and the de-lit aerial. |
| ML from photographs in the cloud | No (no money; licence of training data) | We measure (lidar) and use rules; small open models only offline and only with clean training data. |
| Bing photogrammetry meshes | No | Licence and look at street level. Lidar gives better ground shape in the US for free. |
| Displacement near the ground | Yes, in a 30 m corridor only | Proposal 3; measure the cost; MSFS's own number is about 10 %. |
| Biome kits with seasons | Yes | Köppen + NLCD + LANDFIRE + phenology (already planned by the world threads). |
| Plausibility checks | **We must do better** | Every fact gets a range check against other sources before use (a 212-floor house is rejected because NSI says 2 stories and lidar says 8 m). |

**Where we can beat MSFS.** In the US we have public-domain lidar (3DEP) for most of the land, a public-domain 0.6 m photograph (NAIP), national inventories of structures (NSI, USA Structures), soil (SSURGO), geology (SGMC), forests (TreeMap, LANDFIRE) and roads (HPMS). MSFS is global, so it cannot depend on US-only data; we can. The driver needs facts about the vertical world and the first 30 m; these US datasets have exactly those facts.

## 9. Sources

- Lionel Fuentes, "Designing the Terrain System of Flight Simulator: Representing the Earth", GDC 2022 Advanced Graphics Summit. Slides: https://www.asobostudio.com/files/inline-images/Designing_Terrain_System_Fuentes_Lionel.pdf ; GDC Vault: https://gdcvault.com/play/1027581/Advanced-Graphics-Summit-Designing-the [V]
- TechCrunch, "Meet the startup that helped Microsoft build the world of Flight Simulator", 17 August 2020: https://techcrunch.com/2020/08/17/meet-the-startup-that-helped-microsoft-build-the-world-of-flight-simulator [V]
- Game Developer, "Head in the cloud: Flight Simulator's cutting-edge tech, today and tomorrow", 2021: https://www.gamedeveloper.com/game-platforms/head-in-the-cloud-i-flight-simulator-i-s-cutting-edge-tech-today-and-tomorrow [V]
- Engadget, "Breaking down the AI wizardry of Microsoft Flight Simulator", CES 2021: https://www.engadget.com/flight-sim-interview-ces-2021-ai-photogrammetry-bing-224546632.html [V]
- Union.ai, "Blackshark.ai scales Earth's digital twin with Flyte": https://web.union.ai/case-study/why-blackshark-ai-chose-flyte-to-support-earths-digital-twin [V]
- VentureBeat, "Blackshark and Maxar partner on Earth-scale digital twins", 21 April 2022: https://venturebeat.com/2022/04/21/blackshark-and-maxar-partner-on-earth-scale-digital-twins [V]
- Xbox Wire, "Microsoft Flight Simulator 2024 preview", 19 September 2024: https://news.xbox.com/en-us/2024/09/19/microsoft-flight-simulator-2024-preview/ [V]
- PC Gamer, "More details on MS Flight Simulator 2024, including full 3D landscapes in 30 biomes": https://pcgamer.com/games/sim/more-details-on-ms-flight-simulator-2024-including-full-3d-landscapes-in-30-biomes [V]
- TweakTown, "Microsoft Flight Simulator 2024 detailed: 4000x more detail": https://www.tweaktown.com/news/100626/microsoft-flight-simulator-2024-detailed-4000x-more-detail-new-career-mode-and-so-much/index.html [V]
- PC Gamer, "Despite running load tests that simulated 200,000 users...": https://pcgamer.com/games/sim/despite-running-load-tests-that-simulated-200-000-users-microsoft-flight-simulator-2024-devs-admit-that-they-completely-underestimated-how-many-players-would-actually-want-to-play-their-game [V]
- Stormbirds, "A recap of the MSFS 2024 developer stream", 26 September 2024: https://stormbirds.blog/2024/09/26/a-recap-of-the-msfs-2024-developer-stream/ [V]
- MSFS Addons, "What is displacement mapping in MSFS 2024?": https://msfsaddons.com/msfs-2024-faq/displacement-mapping/ [V]
- Wikipedia, "Microsoft Flight Simulator 2024" (launch problems, with its own sources): https://en.wikipedia.org/wiki/Microsoft_Flight_Simulator_2024 [V]
- Official forum, "Ground textures are blurry, LOD system is too aggressive... in 2024": https://forums.flightsimulator.com/t/ground-textures-are-blurry-lod-system-is-too-aggressive-and-loss-of-detail-in-2024-streamed-scenery-and-clouds-looks-worse-than-2020/676315 [V, users only]
- GamesRadar, "Microsoft Flight Simulator has a ridiculously tall skyscraper caused by a single typo": https://www.gamesradar.com/microsoft-flight-simulator-has-a-ridiculously-tall-skyscraper-caused-by-a-single-typo [S]
- FSNews, "MSFS 2024 Developer Stream: World Update 23 revealed", 17 September 2026 (no new world technology in 2026 streams): https://fsnews.eu/msfs-2024-developer-stream-world-update-23-revealed/ [V]
