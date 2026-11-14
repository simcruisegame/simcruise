# The scan engine: contract v1 (14 November 2026)

Owner: Aaron ("do what Flight Simulator does"; "make that awesome"). Branch
`world-detail-v2`. Plan: [world-detail-v2-plan.md](world-detail-v2-plan.md).
C++ types: `Source/SimCruise/Public/World/SimCruiseScan*.h`. Python prototypes
in `lab/scan/` write the same file.

## Idea

One engine reads everything we know about a tile and writes one **tile scan**.
One renderer path reads it. Rules and legal models ("eyes") read aerial photos
and map data; the game then makes ground, trees, objects and buildings in the
local style (the Flight Simulator pattern). Open data and legal models only.
FlightGear's osm2city is GPL: we use its documented ideas (regional building
rules, roof types, texture by age and use), never its code.

## Pipeline (per zoom-16 tile, on workers, 4 s or more before the car)

1. **Fetch**: tile data (OSM / Overture), aerial photo, heights (3DEP lidar
   or 10 m DEM), Köppen, NLCD, tree data, roadside OSM.
2. **De-light**: remove baked sun and shadow from the aerial (the existing
   `lab/unbake-sun` method), clamp albedo per class.
3. **Eyes** (optional): models read the photo into channels and points.
4. **Fuse**: map geometry > lidar heights > eyes (when more confident) > rules
   (NLCD, Köppen, slope, road class). Rules always give a full scan; a missing
   eye only lowers confidence.
5. **Generate**: trees, objects, buildings, ground offsets.
6. **Self-check**: draw the tile from above on the CPU (simple splat), compare
   with the real aerial per cell, correct (tree density and crown colour, roof,
   grass and soil colour); at most 3 rounds; the error goes in META.
7. **Write the cache**, then **upload**: 1 ms or less of game thread a frame,
   one mip a frame.

Seeds are stable (OSM id and station, or quantised world position; never row order).

## File

`Saved/Scan/v<SCAN_VERSION>/<z>/<x>/<y>.scan`, little-endian.
Cache key: (tile, SCAN_VERSION, rules version, data hash).

Header: magic `SCN1`, version u16, tile z/x/y, bounds (lon/lat), cell size (m),
build time, data hash, rules version, chunk count. Chunk table: tag (4 chars),
offset, size, codec (`raw` or `zstd`).

## Chunks

Rasters are north-up, tile-aligned, 1 m cells, with an 8-cell border copied
from the neighbours so edges match.

| Tag | Content |
|---|---|
| `CLAS` | RGBA8: R surface class, G wear/age, B dryness/season, A confidence |
| `ALBD` | RGBA8 sRGB: RGB de-lit albedo clamped per class, A canopy cover |
| `HGHT` | RG16: R ground offset (signed, 1 cm: banks, cuts, ditches, kerbs, micro relief); G canopy top (10 cm) |
| `INST` | 32-byte records: int32 x, y, z (cm); u16 kind; u16 subtype; u16 height (dm); u16 crown (dm); u8 yaw; u8 flags; u16 string id; u32 seed; u8 confidence; 3 reserved |
| `BLDG` | Records: u64 source id; footprint polygon; height (dm); floors; ground-floor height; roof shape (flat, gable, hip, pyramid, shed, dome, mansard); roof RGB; wall RGB; wall material (brick, stucco, wood siding, concrete, glass, metal, stone); use; era (year/10); style region; shop name string id; flags; confidence |
| `ROAD` | The per-32 m road context records (`FSimCruiseCellContext`, item 1) |
| `STRS` | UTF-8 string table |
| `META` | JSON: sources and licences, eyes and versions, timings, self-check error, warnings (road steps ...) |

### Surface classes

0 unknown, 1 asphalt, 2 concrete, 3 paving, 4 gravel, 5 dirt track, 6 bare
soil, 7 rock, 8 scree, 9 sand, 10 grass short, 11 grass tall, 12 dry grass,
13 crop, 14 shrub, 15 forest floor broadleaf, 16 forest floor conifer,
17 wetland, 18 water, 19 snow/ice, 20 roof, 21 parking, 22 rail, 23 kerb,
24 shoulder gravel, 25 road marking; 26-63 reserved. `data/scan/classes.json`
gives each class its albedo range and detail-kit slice.

## Eyes

`ISimCruiseScanEye`: name, version, licence (weights **and** training data),
`CanRun(inputs)`, cost estimate (s), `Run(inputs, out channels, out points)`,
worker only.

`FSimCruiseScanInputs`: tile z/x/y, cell size, de-lit aerial RGB (+NIR when
there is one), base terrain, lidar DSM/DTM when there is one, map facts.
`FSimCruiseScanChannel`: name, size, uint8 data, confidence.

Standard channels: `canopy_prob`, `canopy_height` (0-255 = 0-51 m),
`building_prob`, `roof_type`, `road_prob`, `water_prob`,
`surface_logits_<class>`; points: `crown_points`.

Limits: ONNX Runtime on the CPU through NNE; 2 cores and 1.5 GB for all eyes
together; 3 s per tile or less; no static ORT objects with destructors; never
on the game thread. Candidate eyes (licence checked before use):
Moondream2 (Apache-2.0), Florence-2 base (MIT), SAM 2 small (Apache-2.0),
DeepForest tree crowns (MIT code; check the weights' model card first). Not
allowed: Depth Anything V2, Marigold, Depth Pro, SegFormer, anything trained
on Cityscapes or Mapillary Vistas.

## Readers

- Ground and road materials sample `CLAS`, `ALBD`, `HGHT` (one fetch each).
- Trees and objects come from `INST` (HISM).
- Buildings read `BLDG`.

## Tests

- Determinism: two scans of a tile are byte-identical except the build time.
- Every cell has a class.
- Border rows match the neighbours'; no road step over 2 cm in 0.5 m.
- 95 % of tiles ready 300 m before the car at 120 km/h.
- The self-check error goes down over its rounds.
- A second drive runs zero inference (the cache works).

## Order of work

1. Road-step fix, fallback ground, speed look-ahead (drive-breaking).
2. Scan core: the format, a rules-only scan, the cache, a debug view
   (`-SimCruiseScanView` shows the class map).
3. First readers: terrain shape and surface; trees (real procedural meshes with
   LODs and an impostor, species and size from the scan, smooth LOD fades).
4. Self-check. 5. Buildings. 6. Objects and signs.

Each step: push the branch; frames from the Dragon, Ocean Drive and random US
places at the highest setting; fps at Ultra and High.

## v2 additions (SCAN_VERSION 2)

Rule: outlines are real 3D; the inside of a surface can be shading; every detail
has a cause in the data. Parallax only on faces that face the driver (banks,
cuts, walls), never on the road ahead.

**Scan core (batch B)**
- `ISimCruiseScanEye::TrainingDataLicence()` is recorded for every eye. The hard
  gate is the **weights** licence: an eye whose weights are non-commercial,
  research-only or unlicensed is refused (a test enforces it). We obey the
  publisher's licence fully: attribution, share-alike, use restrictions.
- Fuse step 4b, plausibility: each number is checked against other sources
  (floors against lidar height, width against lanes, tree height against canopy
  height). An outlier falls to the next source and adds a META warning.
- Fusion priority: map geometry > lidar heights and shapes > national
  inventories for attributes (sampled from their distribution with the stable
  seed where only statistics exist) > eyes > rules.

**Terrain (batch C)**
- `ROAD` gains a cross-section profile per record: lane width, edge line,
  shoulder type (paved, gravel, turf, none, curb and gutter) and width, edge
  drop (cm), rumble strip type and offset, foreslope ratio, ditch depth and
  width, backslope, mow line, barrier type and offset, each with a source flag
  (map, lidar, HPMS, rule). Fitted to 3DEP lidar every 5 m where present, else
  FHWA HPMS distributions by state, functional class, urban/rural and terrain
  (public domain), else national. The same profile feeds tyre contact (edge
  drop, shoulder grip) and rumble and gravel sound.
- `CORR`: 0.25 m raster, ±20 m around the road centre line in (station,
  offset): R ground offset (mm), G surface sub-class, B process mask id, A
  confidence.
- `GEOL`: u8 rock family (USGS SGMC), u8 rock fragment %, RGB8 dry soil albedo
  (NRCS SSURGO via Soil Data Access, else STATSGO2, else a Köppen + NLCD
  palette), u8 bedding dip class. Our own Munsell to sRGB code (no GPL).
- `PROC`: flow accumulation (log), wetness index, sky-view factor, downslope
  direction, tyre-throw zone, litter density. Drives stone and gravel scatter,
  moss, washout.
- `DYN`: a run-time texture (wetness, snow, leaf fall), never cached.
- Detail kit: the 8 SDXL textures stay (OpenRAIL++-M allows them). CC0 material
  scans (ambientCG, Poly Haven) are an A/B test; a texture is replaced only when
  the scan looks better in real frames. Per-place colour transfer and stochastic
  (hex) tiling, no model.

**Trees (batch D)**
- `TREE` chunk, 48-byte records: position, species group, height, crown base,
  crown radius in 8 directions (u8), crown density, lean (2 × i8), archetype id,
  seed.
- Crowns from 3DEP points by classic segmentation (local maxima and watershed,
  no learned weights); fallback the Meta/WRI 1 m canopy height (CC BY 4.0) and a
  species default. Species from USFS TreeMap, LANDFIRE EVT, Köppen.
- Space-colonisation growth inside each envelope, 50-200 variants per tile,
  impostors beyond 150 m; this step may run one tile later than the rest.

**Later (not started)**: BLDG v2 from the National Structure Inventory (read the
USACE terms first), VERT records, a road-level self-check, a process-true
corridor texture in a runtime virtual texture.

## Eye status

- Trees come from 3DEP lidar and the Meta/WRI canopy height map with classic
  segmentation first. `crown_points` from an eye is read only behind a flag,
  off by default.
- DeepForest and Satlas may be eyes only when their weights licence allows it;
  read each model card and record the weights licence and the known
  training-data origin in `docs/data-sources.md` before use.
- Our AI team trains our own eye later, with the standard channel names.
- Every model goes in `docs/data-sources.md` with its weights licence and
  training-data origin.
