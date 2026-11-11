# More open data for SimCruise (sweep 3, 2026-11-10)

This sweep looks for data that SimCruise does not pull yet and that would bring the generated world closer to the real place. It builds on [open-data-catalogue.md](open-data-catalogue.md) (sweep 2) and [../data-sources.md](../data-sources.md) (what reaches the game today). Sweep 2 sources come back here only when this sweep found a cheaper access path or measured something new.

Every source here meets the project rule: no key and no login at runtime, a licence that allows runtime fetching (public domain, CC0, PDDL, CC BY, or ODbL fetched and cached on the player's machine and never bundled), and access in small tiles or ranges.

**Method.** Every "measured" figure in the tables came from a request made on 2026-11-10 from the development machine with `bun` and the helper's own readers (`helper/overture-archive.mjs`, `helper/vector-tile.mjs`), or from `curl`. Licence wording comes from the publisher's page or service metadata unless marked **[secondary]** (a third-party catalogue record) or **[unverified]** (not confirmed). Cost per km² is compressed bytes on the wire. Tile areas allow for latitude. Nothing in this document changes game code.

## Findings first

1. **Overture's base tile already holds most of the street furniture the game invents or leaves out.** The z13 `base.pmtiles` tile that `signal-controls.mjs` reads (with `--signal-controls`) has, for one 13 km² tile of Cambridge MA: 2,745 crossings (1,177 tagged `crossing:markings=zebra`), 1,126 kerbs, 1,819 mapped trees, 421 fire hydrants, 418 parking areas (322 `parking=surface`), 293 street lamps (with `lamp_mount`), 203 bus stops, 138 stop signs (with `direction`), 131 fences, 41 walls, 26 power poles and 24 hedges. On the Phoenix (Tempe) tile it has 294 power poles and 182 street lamps. On the Golden CO tile it has the same 22 guard-rail and jersey-barrier ways (4.9 vs 5.0 km) that the opt-in Overpass query in `guard-rails.mjs` returns. Rural coverage is thin: the central Iowa tile held 2 stop signs and nothing else.
2. **`--signal-controls` cannot read dense downtown tiles today.** `signalControls()` allows 768 KiB per tile and `archiveTile()` allows 1.2 MiB per range. Measured on the 2026-10-21.0 release, the base tile fetch was 954 KB for Cambridge MA. The largest single range was 1.31 MB for downtown Denver, 1.51 MB for Midtown Manhattan, 1.64 MB for the Chicago Loop and 1.61 MB for San Francisco's Civic Center. All of these fail and are marked "unavailable" for 15 minutes. LA, Seattle and Houston downtowns (0.65-0.76 MB) and Queens (742 KB) fit. This needs fixing before anything else is built on the base tile (see design 1).
3. **The Esri OSM replica that `lane-counts.mjs` already queries carries more road tags.** It has `surface`, `smoothness`, `lit`, `sidewalk`, `width`, `incline`, `tracktype`, `crossing` and `cycleway`, and adding them to `outFields` costs no extra request. Fill rates on major and minor roads, measured with `outStatistics`:

   | Area | `surface` | `maxspeed` | `width` | `lit` | `sidewalk` |
   |---|---|---|---|---|---|
   | Cambridge MA | 79 % | 55 % | 59 % | 21 % | 7 % |
   | Tempe AZ | 29 % | 14 % | 0 % | 4 % | 1 % |
   | Rural Iowa | 6 of 20 ways | — | — | — | — |

4. **HPMS per-state sections are cheap and carry measured roughness and traffic.** Sweep 2 put HPMS at 8 days of work. One bounding-box query of `HPMS_FULL_VA_2025` in PBF with 19 fields was 4.6 KB for 2.5 km² (1.8 KB/km², a freeway-only patch near Richmond). It returned I-895 with IRI 97 in/mi, cracking 1 %, rutting 0.1 in, surface type 2, 6 through lanes of 12 ft, a 10 ft right shoulder, AADT 16,583 (712 single-unit and 914 combination trucks), K-factor 9 and directional split 53 %. `RoadRoughness.gd` has been off since it was written ("until measured roughness (HPMS IRI) replaces its class estimate"), and this is that measurement.
5. **Two statewide high-resolution orthoimagery sets have clear open terms and an efficient tile path.**
   - MassGIS 2025 (15 cm, 7.5 cm on Cape Cod, leaf-off, "no restrictions") is an ArcGIS Online tile cache: about 16.6 KB per z19 tile, 5.2 MB/km².
   - Indiana's current imagery (CC0) sits in a public S3 bucket as ArcGIS Compact Cache V2 bundles. Two range reads (index entry, then tile) gave a 10.7 KB z19 JPEG of Monument Circle, 3.1 MB/km².

   Today the game's ground is one 1024 px USGS export per z14 tile, about 1.8 m per pixel, so either set is 8-12 times sharper under the car.
6. **Overture's transportation tiles add crosswalk lines, parking aisles, sidewalks, mapped widths and surfaces, but cost more.** At z14 they ran 150-290 KB/km² in cities and 10 KB/km² in rural Iowa. In Cambridge, 651 of 3,234 segments carry `width_rules`, 666 carry `speed_limits` and 2,783 carry `road_surface`. They are worth fetching only around the car, and only where the Esri replica cannot supply the same thing.

## Ranked table

Priority (P) combines what the player sees or feels, how much of the US it covers, and the cost to fetch and build: **P1** do next, **P2** soon, **P3** when its feature comes up, **Lead** check first. **Status**: *new* (not in sweep 2), *path* (in sweep 2, but this sweep found a better path or measured it), *sweep 2* (unchanged; listed for completeness).

| # | Source | Licence | Coverage | Access | Cost per km² and caching | What it fixes in the game | P | Status |
|---|---|---|---|---|---|---|---|---|
| 1 | [Overture base `infrastructure` + `land` layers](https://docs.overturemaps.org/schema/reference/base/infrastructure/) (OSM-derived): `street_lamp`, `utility_pole`, `power_pole`, `fence`, `wall`, `retaining_wall`, `hedge`, `guard_rail`, `jersey_barrier`, `crossing`, `pedestrian_crossing`, `kerb`, `stop`, `give_way`, `parking`, `parking_space`, `fire_hydrant`, `bollard`, `bus_stop`, `street_cabinet`, `tree`, `tree_row` | ODbL 1.0 (fetch at runtime, never bundle) | Worldwide. Dense in mapped US cities, near-empty in rural areas | PMTiles z13 range reads from `overturemaps-extras-us-west-2` (S3, no key); the reader exists in `overture-archive.mjs` | Measured: 3 KB/km² (rural IA), 23 KB/km² (Tempe), 73 KB/km² (Cambridge), up to 124 KB/km² (Chicago Loop). Free when `--signal-controls` already fetched the tile. Cache the raw range under the existing `ov-base-<release>-*` keys for 45 days; parse once per release into a small per-z13 JSON | Street lamps (and night light pools) at real positions; power and utility poles; fences, walls and hedges along lots; guard rails without Overpass; zebra and plain crosswalks at mapped crossings; stop bars on the right approach; parking-lot surfaces; hydrants and bollards; real tree positions with leaf type | **P1** | path (sweep 2 "lead") |
| 2 | [FHWA HPMS per-state sections](https://geo.dot.gov/server/rest/services/Hosted/HPMS_FULL_VA_2025/FeatureServer/0) `HPMS_FULL_{ST}_{YEAR}` | US public domain (USDOT) | All 50 states plus DC and PR. Federal-aid roads in full; pavement items are measured on the full extent of interstates and the NHS, and on sample sections for other classes | Keyless FeatureServer query, PBF, 2,000 records per call | Measured: 1.8 KB/km² (freeway-only area); expect 5-20 KB/km² on an urban grid. HPMS is annual, so cache 180 days per state per 4 × 4 z14 group | Measured IRI, cracking, rutting and faulting drive `RoadRoughness.gd` (off today for want of this); surface type (asphalt or concrete) sets tyre noise; AADT, truck AADT, K and D factors set traffic density, truck share and peak direction; lane width, shoulder width and type, median type; posted speed where OSM lacks it | **P1** | path (sweep 2: 8 d; measured here) |
| 3 | [Esri OSM North America highways replica](https://services6.arcgis.com/Do88DoK2xjTUCXd1/ArcGIS/rest/services/OSM_NA_Highways/FeatureServer/0), extra fields `surface`, `smoothness`, `lit`, `sidewalk`, `width`, `tracktype`, `incline` | ODbL 1.0 | US, Canada, Mexico | The query `lane-counts.mjs` already sends | No extra request: only extra attribute bytes in the cached `lane-counts-v1` response (re-key to v2) | Gravel vs paved on minor roads; lit streets get lamp spacing where no lamp points are mapped; sidewalk presence and side; mapped carriageway width ahead of the class guess in `road-widths.mjs` | **P1** | new |
| 4 | [MassGIS 2025 aerial imagery](https://www.mass.gov/info-details/massgis-data-2025-aerial-imagery) (tile service `tiles.arcgis.com/tiles/hGdibHYSPO59RG1h/arcgis/rest/services/Massachusetts_Aerial_Imagery_2025/MapServer/tile/{z}/{y}/{x}`) | "No restrictions apply to these data" (MassGIS) | Massachusetts; 15 cm (7.5 cm Cape Cod); leaf-off, 18 Mar - 23 Apr 2025 | Keyless ArcGIS Online tile cache (Web Mercator, LOD 7-20, 256 px JPEG) | Measured: 16.6 KB per z19 tile, 313 tiles/km², so 5.2 MB/km² (2.0 MB/km² at z18). A ±60 m corridor along the road costs about 0.6 MB per road km at z19. Cache tiles 365 days; the flight does not change | Paint wear, patches, crack sealing, manholes, driveway aprons and kerb lines under the car; real parking-lot striping; leaf-off ground shows what is under the trees | **P1** | new |
| 5 | [Indiana statewide orthoimagery](https://registry.opendata.aws/in-imagery/) (bucket `gisimageryingov`, `imagerycache/arcgiscache/CacheWebMercator_IndianaCurrentImagery/tile/L{zz}/R{row}C{col}.bundle`) | CC0 (AWS Open Data registry; IGIO) | Indiana; 6 in (3 in in some areas); 4-year county cycle, with archives back to 2005 | Public S3 with no signing. Compact Cache V2 bundles, 128 × 128 tiles each, read with HTTP Range: 8-byte index entry at `64 + 8·(r·128 + c)`, then the JPEG | Measured: 10.7 KB per z19 tile, 289 tiles/km², so 3.1 MB/km². Cache each bundle's 128 KB index once (one bundle covers about 56 km² at z19), then one range per tile, or one range per run of adjacent tiles | As row 4. COGs of the source tiles are also in the bucket (`imageryoptimized/statewide/{year}/`) for an exact-date look | **P1** | new |
| 6 | Vermont VCGI 15 cm leaf-off cache (`maps.vcgi.vermont.gov/arcgis/rest/services/EGC_services/IMG_VCGI_CLR2023_WM_CACHE/ImageServer`, LOD to 19) | Service says "VCGI"; a VCGI post calls all state orthoimagery public domain **[secondary; confirm with VCGI]** | Vermont; 15 cm towns and villages, 30 cm elsewhere | Keyless cached ImageServer | About 5 MB/km² at z19, by analogy with row 4 (not measured) | As row 4 | Lead | new |
| 7 | NC OneMap, Iowa (CC BY), North Dakota and Maine county or state imagery | As cleared in sweep 2 | Those states | ImageServer `exportImage` | 170-310 KB per 1024 px over 500 m (sweep 2) | As row 4 | P2 | sweep 2 |
| 8 | Maryland six-inch (`mdgeodata.md.gov/imagery/rest/services/SixInch/...`), New York `orthos.its.ny.gov` (12 in served), Connecticut 2023 3 in (`cteco.uconn.edu/ctraster/rest/services/images/Ortho_2023/ImageServer`), New Hampshire 2021-22 6 in | No licence in the service metadata (`licenseInfo` empty or a disclaimer only) | Those states | Keyless services | n/a | Would be as row 4 | Lead (ask each publisher) | new |
| 9 | [Overture transportation segments](https://docs.overturemaps.org/schema/reference/transportation/segment/) `subclass` = `crosswalk`, `sidewalk`, `parking_aisle`; `width_rules`, `road_surface`, `speed_limits`, `road_flags` | ODbL 1.0 (plus other open sources per row, listed in `sources`) | Worldwide | PMTiles z14 range reads (`transportation.pmtiles`, max zoom 14) | Measured: 10 KB/km² (rural IA), 150 KB/km² (Tempe), 271-291 KB/km² (Cambridge, Denver). Fetch only for the car's tile and its neighbours; cache 45 days per release | Crosswalk lines drawn where they are mapped; parking-aisle layout in lots; per-segment width (`width_rules` on 20 % of Cambridge segments); surface changes inside one way (`between`) | P2 | new |
| 10 | [NCEI 1991-2020 hourly climate normals](https://www.ncei.noaa.gov/access/services/data/v1?dataset=normals-hourly-1991-2020) | US public domain (NOAA) | Several hundred US first-order stations | Keyless JSON or CSV API by station and day range | Per station, not per area: a ±7-day window is a few hundred KB of JSON (CSV is smaller). Cache forever; normals change every 10 years | A typical sky for this place, date and hour (`HLY-CLOD-PCTCLR/FEW/SCT/BKN/OVC`, temperature, dew point, wind) when live METAR is off, or for a chosen past date. Keeps Phoenix mostly clear and Seattle mostly broken without hand-tuning | P2 | new |
| 11 | [NASA POWER hourly point API](https://power.larc.nasa.gov/api/temporal/hourly/point) (`ALLSKY_SFC_SW_DWN`, `CLRSKY_SFC_SW_DWN`, `ALLSKY_SFC_SW_DIFF`) | NASA open data; acknowledgement requested **[terms page not read]** | Global, about 0.5° cells, 2001 to near-present | Keyless JSON | Measured: about 1.5 KB per day for 3 parameters at one point. Cache by 0.5° cell and date, permanently | Real global and diffuse sunlight for the hour, which sets sun and sky intensity and how hard shadows are. A "taste reality" reference for exposure on a dated street photo | P2 | new |
| 12 | [Annual NLCD Collection 1 (1985-2024), Source Cooperative mirror](https://source.coop/kylebarron/usgs-landcover) `data.source.coop/kylebarron/usgs-landcover/annual-nlcd/c1/v1/cu/mosaic/Annual_NLCD_{LndCov,FctImp,...}_{year}_CU_C1V1.tif` | US public domain (USGS). The official bucket is requester-pays; this copy is community-hosted | CONUS, 30 m, yearly | Keyless COG range reads (512 px internal tiles, about 15 × 15 km each) | Measured: 51 KB for one 512 px block (about 0.2 KB/km²). Cache per block per year, permanently | Fractional impervious cover per 30 m cell, for the paved share of lots and yards; land cover for the simulated year, not 2021 (a new subdivision is not still a cornfield) | P2 | new |
| 13 | [FHWA NHTS 2017 public-use vehicle file](https://nhts.ornl.gov/assets/2016/download/csv.zip) (and the smaller 2022 file) | US public domain (FHWA) | National sample: about 256,000 household vehicles with make, model, model year and body type, by state, and by CBSA for large metros | Bake from the 83.7 MB zip (the 2022 file is 3.9 MB) | One-time download. Ship a table of under 100 KB | A traffic mix by state and metro: pickups in Texas, Subarus in Vermont, older fleets in rural counties | P2 | new |
| 14 | [NY DMV registrations (SoQL aggregates)](https://data.ny.gov/resource/w4pv-hbkt.json) | OPEN-NY terms (custom and revocable); ship aggregates only | New York State, by county and ZIP | Keyless SoQL `$select=make,body_type,count(*)&$group=…` | Measured: 587 B per county aggregate (Kings: Honda SUV 46,002, Toyota SUV 44,366, …). Rows include `color` and `model_year` | Calibrates the national NHTS mix, plus the real colour mix (white, black, grey, silver) | P2 | path (sweep 2: bake) |
| 15 | [Urban tree inventories of 63 US cities](https://datadryad.org/dataset/doi:10.5061/dryad.2jm63xsrf) (McCoy et al. 2022, eLife) | Dryad publishes all data under CC0 (repository policy) | 63 cities, 5.66 M trees: species, DBH, height for some, lat/long | Bake: 63 CSVs, 1.24 GB (NYC alone 168 MB) | Bake into one CC0 range-readable PMTiles of about 60 MB as a release asset; fetch per z13 tile | Species, size and position for street trees in 63 cities instead of a generic crown. It generalises `civic-trees.mjs`, which has two cities | P2 | new |
| 16 | City pavement-condition datasets: [San Francisco PCI](https://data.sf.gov/resource/5aye-4rtt.json) (PDDL), [San Diego OCI](https://data.sandiego.gov/datasets/streets-overall-condition-index) [licence to check], Tempe PQI (CC BY) | As listed | Those cities, per block | Keyless SODA or FeatureServer | Measured: about 350 B per SF block record | Block-level wear for local streets, which HPMS samples do not cover. Use only as a calibration set for a general IRI-from-class-and-age model, not as per-city code | P3 | new |
| 17 | FHWA TMAS hourly counts | US public domain | National stations | Bake | sweep 2 | Hour-of-week traffic factors (pairs with the HPMS K and D factors) | P2 | sweep 2 |
| 18 | [EPA EnviroAtlas meter-scale urban land cover](https://catalog.data.gov/dataset/us-epa-enviroatlas-meter-scale-urban-land-cover-mulc-data-characteristics) | Varies by community (Pittsburgh CC0; others "PUBLIC" or EPA Science Hub) | About 30 communities | WMS and downloads | n/a | 1 m tree, grass, impervious and bare classes for those communities; mainly a check on the aerial classifier | Lead | new |
| 19 | [NOAA C-CAP 1 m land cover](https://coast.noaa.gov/digitalcoast/data/ccaphighres.html) | Not stated for the 1 m product (produced by Ecopia) **[unverified]** | Coastal states | Bulk GeoTIFF by state | n/a | 1 m impervious and canopy along the coasts | Lead | new |
| 20 | Utility distribution poles and lines | No national open dataset exists. HIFLD has transmission only (sweep 2); OSM `power=pole`/`utility_pole` is mapped in places (row 1) | — | — | — | Poles must still be inferred elsewhere (along roads with no mapped poles, by road class and region). Keep the inference, and use row 1 where poles are mapped | Gap | new |

Not repeated here: everything else in sweep 2 (NBI bridges, FRA crossings, WZDx, LANDFIRE, CDL, VIIRS night lights, USNO, osm-legal-default-speeds and the rest) is unchanged.

## Top 5 recommended integrations

Each design follows the helper's existing pattern:
- an ES module that returns `{status, records, release}` with status `available`, `empty`, `unavailable` or `unsupported`;
- reads through the shared `read(key,url,ttl,range,timeout,size)` cache with a byte budget;
- a miss file for cooldown;
- parsed output cached per release;
- a Godot consumer that draws or applies nothing when the status is not `available` (no guessing on top of missing data).

### 1. `helper/street-furniture.mjs`: Overture base infrastructure (P1, about 3 days)

**What it fixes.** Street lamps, poles, fences, walls, hedges, crosswalk paint, stop bars, parking-lot surfaces, hydrants and bollards appear where they really stand, and mapped guard rails no longer need the volunteer Overpass service.

**Fetch.**
- Share the z13 tile with `signal-controls.mjs` (same `archiveTile(mx,my,release,bounded,'base')` call and same `ov-base-<release>-<start>-<length>` range keys), so the two features download the tile once.
- **Fix finding 2 first.** Raise `signalControls()`'s per-tile budget from 768 KiB to 2 MiB, and raise `archiveTile()`'s 1.2 MiB per-range cap to 2 MiB. With that change, the measured worst tile (Chicago Loop, 1.64 MB) fits.
- Hold the decoded tile (up to 7.1 MB measured, inside the 10 MB gunzip cap) only while it is parsed.

**Parse.** One pass over `infrastructure` and `land`. Keep a feature only when its `sources` names a single OSM record with ODbL, as `observedControl()` already requires. Write `sf-v1-<release>-<mx>-<my>.json` with:
- `lamps`: points with mount (`bent_mast`, `straight_mast`, `pole`, `ground`) and an optional `direction`;
- `poles`: points with `power_pole` or `utility_pole` and the material;
- `barriers`: polylines with kind (`fence`, `wall`, `retaining_wall`, `hedge`, `guard_rail`, `jersey_barrier`), `fence_type`, `material` and `height`;
- `crossings`: points with `markings` (`zebra`, `lines`, `yes`, `no`) and `signals`;
- `stops`: points with `stop` or `give_way`, `direction` and `all`;
- `parking`: rings with `parking` (`surface`, `multi-storey`, `street_side`) and `surface`;
- `hydrants`, `bollards`, `bus_stops`;
- `trees`: points with `leaf_type`, `leaf_cycle`, `height` and `species` when present.

Cap each list (for example 4,000 crossings and 6,000 barrier vertices per tile). Hand each feature to the z14 tile that owns its first vertex, as `ownedControls()` hands out signals, so no feature is drawn twice.

**Game side.**
- A new `StreetFurniture.gd`, built like `SignalControls.gd`, draws each tile's lists as MultiMeshes, one mesh per kind, with LOD fade.
- Crosswalk paint is snapped to the road ribbon where the crossing node lies on a road. Stop bars go on the approach named by `direction`, using `TrafficDirector`'s stop-bar offset.
- Lamps get an emissive head and, on High and Ultra, a light whose cone is cut off by the mount type.
- `GuardRails.gd` takes `barriers` of kind `guard_rail` or `jersey_barrier` ahead of its slope inference, as it already does for the Overpass rows.

**Cost.** Free when `--signal-controls` is on. Otherwise 3-124 KB/km² (measured), once per release (about monthly) per 13 km² tile.

**Check.** A headless check parses a stored Cambridge tile fixture (cut to the ODbL rows the test needs, kept under `game/checks/fixtures/` with a credit) and asserts the counts above. A second check asserts that a 1.64 MB range is accepted.

**Taste reality.** Compare lamp and pole positions with the KartaView or Panoramax frames that `street-samples.mjs` and `panoramax.mjs` already fetch for the tile. Compare zebra versus plain crossings with the same photos.

### 2. `helper/hpms-sections.mjs`: measured roughness and traffic (P1, about 3 days)

**What it fixes.**
- `RoadRoughness.gd` turns on with measured IRI instead of a class guess. A 60 in/mi new interstate and a 250 in/mi city street then feel different, and the default stays smooth where HPMS has no measurement.
- `TrafficDirector.gd` density follows AADT, the share of trucks follows the single-unit and combination AADT, and peak direction follows the D factor.
- Concrete (surface types 3-5) changes tyre noise.

**Fetch.**
- One query per 4 × 4 z14 group to `https://geo.dot.gov/server/rest/services/Hosted/HPMS_FULL_{ST}_{YEAR}/FeatureServer/0/query` with:
  - `f=pbf` (the arcgis-pbf reader in `esri-pavement.mjs` can be extended from polygons to polylines);
  - `geometryType=esriGeometryEnvelope`, `inSR=outSR=3857`, `maxAllowableOffset=1`, `geometryPrecision=1`;
  - `outFields=route_id,f_system,facility_type,through_lanes,lane_width,shoulder_type,shoulder_width_r,shoulder_width_l,median_type,median_width,speed_limit,aadt,aadt_single_unit,aadt_combination,k_factor,dir_factor,iri,psr,surface_type,cracking_percent,rutting,faulting,year_last_improvement`;
  - page with `resultOffset` past 2,000 records.
- Pick the state from the tile centre with a bundled 50-state bounding-box list; query both states near a border.
- Find the year from the service directory: start with the current year, fall back one year, and cache the answer per state for 30 days.

**Cache.** `hpms-v1-<ST>-<year>-<gx>-<gy>` for 180 days. HPMS is an annual submission. Negative results are cached for 7 days.

**Match.** Snap HPMS sections to road ribbons, as `matchLaneCounts()` snaps lane ways: 6 m distance and a direction cosine of at least 0.94. Then write per ribbon station `{iri, surface, aadt, truck_share, k, d, posted}`. Leave stations HPMS does not cover empty. Do not interpolate IRI from neighbouring roads; roughness is local.

**Licence.** US public domain; credit FHWA HPMS in `licenses/Road Atlas.txt` and `Credits.txt`.

**Check.** A fixture test confirms that I-895 near Richmond comes back with IRI 97, 6 lanes and AADT 16,583. The existing calibration bands must not move with `--smooth-roads`.

**Taste reality.** Build the road profile from IRI with ISO 8608 road classes and the quarter-car definition of IRI (Sayers 1995, TRR 1501). Drive a high-IRI and a low-IRI section with the same car and compare the vertical acceleration PSD with published ride-quality data for those IRI values.

### 3. Extend `lane-counts.mjs` with surface, lit, sidewalk and width (P1, about 1 day)

**What it fixes.**
- Gravel and dirt roads get their real surface (grip, dust, sound).
- Lit streets get street lamps at the regional spacing where row 1 has no mapped lamps.
- Sidewalks appear on the mapped side only.
- Mapped `width` replaces the class guess in `road-widths.mjs` when it is between 3 and 40 m.

**Change.**
- Add `surface,smoothness,lit,sidewalk,width,tracktype` to `outFields` and bump the cache key to `lane-counts-v2-`.
- In `decodeLaneWays()`, keep only whitelisted values:
  - `surface` ∈ {`asphalt`, `concrete`, `paved`, `paving_stones`, `sett`, `gravel`, `unpaved`, `dirt`, `compacted`, `ground`, `fine_gravel`};
  - `lit` ∈ {`yes`, `no`};
  - `sidewalk` ∈ {`both`, `left`, `right`, `no`, `separate`};
  - `width` parsed to metres.
- Matched ways pass these values to the same per-road rows the game already repaints.

**Cost.** None beyond a few bytes per way.

**Check.** Extend the lane-count unit check with a fixture way that carries each new tag.

**Taste reality.** On the field-test routes (`docs/research/field-test-2026-11-10.md`), compare gravel and paved transitions and lamp presence with Panoramax frames.

### 4. `helper/state-ortho.mjs`: open statewide orthoimagery near the car (P1, about 4 days)

**What it fixes.** The ground under and just beside the car shows real paint, patches, crack sealing, manholes, kerbs and driveway aprons at 8-15 cm per pixel, instead of 1.8 m USGS pixels stretched over a procedural texture.

**Sources.** An allow-list, never discovery at runtime. Each entry has:
- a code;
- a licence string and credit;
- a Web Mercator extent;
- a `tile(z,x,y)` URL builder or bundle reader.

Entries:
- MassGIS 2025: tiles from the ArcGIS Online cache, `.../MapServer/tile/{z}/{y}/{x}`.
- Indiana (CC0): Compact Cache V2 bundles on S3, read as in finding 5.
- NC OneMap and Iowa, as cleared in sweep 2.
- Vermont only after VCGI confirms its terms.
- Maryland, New York, Connecticut and New Hampshire stay out until a written licence exists.

**Fetch.**
- Only z19 tiles within ±60 m of the ribbon of the road the car is on, plus the next 300 m ahead along the route the car is following (`RoadLayout`).
- For Indiana, read and cache the bundle's 16,384-entry index once (128 KB) under `ortho-in-idx-<bundle>`. Then fetch adjacent tiles in one range when their offsets are contiguous.
- Budget: 1.5 MB per second of driving, 64 MB per session on disk.
- Cache tiles 365 days under `ortho-<code>-<z>-<x>-<y>.jpg`; the flights do not change.
- `cache-eviction.mjs` evicts these first.

**Game side.** `AerialPavement.gdshaderinc` samples a near-field 2048² atlas around the car when one is present and blends it in only on paved ground, so trees, cars and shadows in the photo never get painted onto open ground. It keeps the shader's existing colour transfer, so the photo's light matches the game sun. Leaf-off imagery (MassGIS, Vermont) is preferred for ground because canopy does not hide the road.

**Check.** A unit test reads one Indiana tile from a bundle fixture header and index (no network) and decodes its JPEG. A capture at `--quality=ultra` on a Cambridge and an Indianapolis start gets a side-by-side with the same Panoramax frame.

**Taste reality.** This is the most direct case: the aerial photo is the real place.

### 5. `authoring/traffic-mix.mjs` (bake) plus a runtime HPMS hook: real traffic make-up (P2, about 3 days)

**What it fixes.** The mix of body types, ages and colours in traffic and parking matches the state and metro: pickups and full-size SUVs in Texas, small cars and older fleets in rural Vermont, many white and grey cars everywhere. Traffic count and truck share follow HPMS (design 2).

**Bake.**
- From the NHTS 2017 vehicle file (83.7 MB zip, public domain), compute weighted shares by state and by large CBSA:
  - body class (car, SUV, pickup, van, motorcycle);
  - age band;
  - make (top 20).
- Calibrate the NY rows against NY DMV SoQL aggregates (587 B per county query, aggregates only, under the OPEN-NY terms). Use the same aggregates to add a colour distribution, the only open source with colour.
- Ship `game/data/traffic-mix.json`, under 100 KB, credited to FHWA NHTS and NYS DMV.

**Runtime.** `AmbientLife.gd` and `TrafficDirector.gd` draw each spawned car's body class, age band and colour from the tile's state or CBSA row. They map body class to whichever of the kept traffic models fit, and never name a real maker on a model (the trademark rule in sweep 2). Spawn rate scales with the matched HPMS AADT × the hour factor (TMAS, sweep 2) when design 2 has data, and with the class default otherwise.

**Dependency.** The traffic model set is being changed in another branch, so map body classes, not specific models.

**Taste reality.** Count body classes in 10 street photos per test place (Panoramax) and compare with the table's shares for that state.

## Next after the top 5

- **Sky climatology** (rows 10-11): a `helper/climate-normals.mjs` that picks the nearest normals station (bundled station list, tens of KB) and returns a cloud-class distribution for the date and hour. NASA POWER irradiance then sets sun and sky intensity for a dated photo comparison.
- **Annual NLCD impervious and land cover** (row 12) for the paved share of lots and land cover in the simulated year, read like `forest-form.mjs` reads LANDFIRE.
- **63-city tree inventory** (row 15) baked to one PMTiles, generalising `civic-trees.mjs`.

## Licence notes

- Overture base and transportation rows are OSM-derived (ODbL). Fetch them at runtime and cache them per player, as `signal-controls.mjs` does, and never bundle them. Keep only rows whose `sources` cite OSM under ODbL, or add each other licence to the credits.
- MassGIS: "No restrictions apply to these data". Credit MassGIS anyway.
- Indiana: CC0. Credit IGIO and IndianaMap as a courtesy.
- NHTS, HPMS, NCEI normals and Annual NLCD: US public domain. The Annual NLCD copy is a community mirror of a requester-pays bucket. If it disappears, fall back to MRLC's own services, or to nothing.
- NY DMV: OPEN-NY terms. Ship statistics only, never rows.
- Dryad: CC0. San Francisco: PDDL.

## Sources consulted

- Overture schema: [infrastructure](https://docs.overturemaps.org/schema/reference/base/infrastructure/), [infrastructure classes](https://docs.overturemaps.org/schema/reference/base/types/infrastructure_class/), [segment](https://docs.overturemaps.org/schema/reference/transportation/segment/); release `2026-10-21.0` from [stac.overturemaps.org](https://stac.overturemaps.org/catalog.json).
- [MassGIS 2025 aerial imagery](https://www.mass.gov/info-details/massgis-data-2025-aerial-imagery); service description read from its MapServer JSON.
- [Indiana imagery on the AWS Open Data registry](https://registry.opendata.aws/in-imagery/); the bucket listing and `root.json` (Compact Cache V2, packet size 128, LOD 20).
- [VCGI cached service](https://maps.vcgi.vermont.gov/arcgis/rest/services/EGC_services/IMG_VCGI_CLR2023_WM_CACHE/ImageServer); [Maryland six-inch item info](https://mdgeodata.md.gov/imagery/rest/services/SixInch/SixInchImagery2023_2024_2025/MapServer/info/iteminfo?f=pjson); [CT ECO 2023 metadata](https://cteco.uconn.edu/download/metadata/CT_ortho_2023_metadata.htm); [NYS orthos](https://orthos.its.ny.gov/arcgis/rest/services/wms/Latest/MapServer).
- [HPMS VA 2025 FeatureServer](https://geo.dot.gov/server/rest/services/Hosted/HPMS_FULL_VA_2025/FeatureServer/0) (field list and a measured query).
- [Esri OSM NA highways](https://services6.arcgis.com/Do88DoK2xjTUCXd1/ArcGIS/rest/services/OSM_NA_Highways/FeatureServer/0) (field list and `outStatistics` fill rates).
- [NCEI data service](https://www.ncei.noaa.gov/access/services/data/v1); [NASA POWER](https://power.larc.nasa.gov/); [Annual NLCD on Source Cooperative](https://source.coop/kylebarron/usgs-landcover); [NHTS downloads](https://nhts.ornl.gov/); [NY DMV registrations](https://data.ny.gov/d/w4pv-hbkt); [Dryad 63-city trees](https://datadryad.org/dataset/doi:10.5061/dryad.2jm63xsrf); [SF PCI](https://data.sf.gov/resource/5aye-4rtt.json); [EnviroAtlas MULC](https://catalog.data.gov/dataset/us-epa-enviroatlas-meter-scale-urban-land-cover-mulc-data-characteristics); [NOAA C-CAP high resolution](https://coast.noaa.gov/digitalcoast/data/ccaphighres.html).
