# Reality vs. game — 4 hero roads (13 Nov 2026)

Game commit: `4aaf1fc9952fa0d58e4c8ea0d8fd71e0390b16fa` (origin/main), worktree `D:\sc\lane-compare`, branch `compare/photo-match`, Ultra, 1920x1080, no HUD, moving traffic off.
Frames: `game\` (47 PNG + `manifest.json`). Real photos: `real\` (22 Commons JPG + `sources.json`). Side by side: `side-by-side_<road>.jpg`.

## What the frames show

| Road | Close to reality | Biggest gaps |
|---|---|---|
| Ocean Drive | Street layout, pastel deco blocks, palms, park, parking, night neon on facades | No green bike lane, no yellow kerbs; sidewalk cafés are a few umbrellas and nobody is there; facades are flat boxes (no porches, real signs or window depth); the bundled start and the atlas start build the same hotels in different colours |
| US 129 Dragon | Road ribbon, curves, lines, camber; light at golden hour | Forest is sparse blobs with painted crowns, not a dense wall of trunks at the road edge; cut slopes are smooth green ramps; Deals Gap has none of its many signs, guard rails or gantries |
| Nebraska (Princeton Rd) | Sky, flat horizon, gravel colour | Fields are smeared green with no crop rows, stubble or November colour; no ditches, power poles, wires, fences, mailboxes or field entrances; the gravel road is a wide clean band with no crown or loose stone |
| CA-1 Big Sur / Bixby | CA-1 geometry, a bridge in the right place, sea on the horizon | Bixby is a generic girder deck (the real one is an open-spandrel concrete arch with a balustrade); hills are smooth with no rock cliffs, scrub or exposed rock; the coast has no surf line or beach; one lone tree |
| All | Exposure is mostly sane, the sky stays blue | Midday looks flat and hazy; distant ground is blurry where satellite photos are stretched |

Tool notes: realism poses only load if the pose is in the same tile as the car. The eye height is fixed at 1.65 m. The Bixby default and bridge starts put the car on Old Coast Road, not CA-1. The `p02` overlook pose lands inside a bridge pier.

## Recommended path: generative systems, hero roads as the test

Each gap is one system that works anywhere from open data. The data is OSM tags, road class, land cover (ESA WorldCover), canopy height, climate (Köppen), terrain (Copernicus DEM / USGS 3DEP), building footprints and age (Overture/OSM), and POIs. The place knowledge graph picks styles by region and era. Local CPU AI fills tags that are missing and tunes against licensed photos.

% = rough share of the world's driven road length the system visibly improves (order of magnitude, my estimate).

| # | System | Fixes | Inputs | Reach |
|---|---|---|---|---|
| 1 | **Vegetation synthesis**: biome- and species-aware tree, shrub and scrub kits; density, edge walls and canopy height from data; seasonal colour | Dragon blobs, Big Sur scrub, Nebraska shelterbelts, palms | WorldCover, canopy height, Köppen, date, OSM natural/landuse | ~70% |
| 2 | **Roadside corridor rules**: cross-section by road class and rurality (shoulder, ditch or kerb), utility poles and wires (OSM `power=*` where mapped, rules elsewhere), fences, mailboxes, driveways, field gates, guard rails, signs | Nebraska ditches/poles, Deals Gap signs, all rural roads | Road class, surface, rurality, region and era from the place graph, OSM power/barrier | ~80% |
| 3 | **Ground cover and agriculture**: field parcels from landuse/cropland masks, crop rows along parcel edges, stubble and bare soil by season, gravel crown with loose stone | Nebraska fields, verges everywhere | WorldCover cropland, OSM farmland, latitude and date | ~45% (rural) |
| 4 | **Look calibration from reality**: auto exposure, tone and grade fitted on the CPU to licensed photos by climate zone, time and weather (roadmap I1, made global) | Flat midday, haze, colour | Panoramax/Commons photos near the place, sun position | 100% |
| 5 | **Street cross-section from OSM lane tags**: `cycleway=lane` (with each country's colour), parking lanes, sidewalks, kerb paint, medians | Ocean bike lane and yellow kerbs, every mapped city street | OSM lane/cycleway/parking/sidewalk tags; AI guesses when untagged | ~25% of km, ~55% of km driven |
| 6 | **Terrain detail**: rock and cliff synthesis from slope plus lithology, erosion detail, road cut faces, coast surf and beach | Big Sur cliffs, Dragon cuts | DEM, global lithology (GLiM), OSM coastline/natural=cliff | ~20% |
| 7 | **Street life and frontage**: café seating, awnings, umbrellas and pedestrians from shop/amenity POIs and time; night lighting and neon by building use and nightlife | Ocean cafés, people, real neon; every main street | OSM/Overture POIs, opening hours, local time | ~15% of km, high km driven |
| 8 | **Facade grammar**: window rhythm, storefronts, porches and balconies from footprint, age, use and regional style; AI style labels from street photos | Ocean depth, every town | Overture age/height/use, place graph, Panoramax | ~30% |
| 9 | **Parametric landmark structures**: arch, truss, suspension and girder bridges from `bridge:structure`, material and Wikidata | Bixby; famous bridges anywhere | OSM bridge tags, Wikidata | <1% of km, high wow |

**Order:** 2 and 3 first (cheap, biggest rural reach; this is roadmap 5.4 with 3 added). Then 1 (biggest visual gap), with 4 running in parallel (global and cheap). Then 5 and 7 for cities, 6 for mountain and coast, and 8 and 9 last. Re-run this capture after each system (it is scripted; see below) and score each frame against the photos.

## Re-capture

`powershell -File <scratchpad>\phase2.ps1 -OnlyText ocean,dragon,dragongap,nebraska,bixby` (with `toolsun-capture.ps1`; copies in `runs\*\cmdline.txt`), then `stage.py`.
