# Open data catalogue for SimCruise (sweep 2)

Free data that helps "drive anywhere in the US and it feels like I'm really driving there". Each domain had a scanner and a verifier, both checked on 2026-11-08. Figures are the verifier's corrected values. Sources from sweep 1 appear only where a better access path turned up. Sweep 3 ([more-data-2026-11.md](more-data-2026-11.md), 2026-11-10) adds measured costs and new sources: Overture street furniture, HPMS roughness and traffic, statewide 15 cm orthoimagery, climate normals and the traffic mix.

- **Keyless**: the game can fetch it anonymously at runtime.
- **Bake**: download once at authoring time and ship the result.
- **d**: estimated integration effort in days.
- **[unverified]**: nobody could confirm the claim.

## Top 10 by impact per effort

| # | Source | What the player sees or feels | Licence | Keyless? | Effort (d) | Impact ÷ effort | Link |
|---|---|---|---|---|---|---|---|
| 1 | USNO Astronomical Applications API | Sunrise, sunset, civil twilight and moon phase at the right minute for the place and date; used to check a local MIT/BSD ephemeris | US Government work (no licence text) | Yes | 1 | 5 ÷ 1 | https://aa.usno.navy.mil/api/rstt/oneday |
| 2 | National Register of Historic Places (NPS) | 74,636 listed buildings and districts keep period facades, with names on landmark callouts | US Government work | Yes | 1 | 5 ÷ 1 | https://mapservices.nps.gov/arcgis/rest/services/cultural_resources/nrhp_locations/MapServer |
| 3 | Iowa Environmental Mesonet tile cache | Live radar and satellite as ready PNG tiles: rain intensity drives wipers and spray, storm cells show on the map | Public domain (IEM disclaimer) | Yes (university host, no SLA) | 1.5 | 7 ÷ 1.5 | https://mesonet.agron.iastate.edu/cache/tile.py/1.0.0/nexrad-n0q-900913/{z}/{x}/{y}.png |
| 4 | USGS Wind Turbine Database | Wind farms at real positions with real hub height and rotor size, blades turning, red aviation lights at night | USGS public domain ("Use constraints: None") | Yes (or bundle 1.4 MB) | 2 | 8 ÷ 2 | https://energy.usgs.gov/uswtdb/ |
| 5 | USFS Urban Tree Database | Street trees get believable height and crown for their species and trunk size; young plantings stay small | USFS: usable "without additional permissions or fees"; cite | Bake (keyless download) | 2 | 8 ÷ 2 | https://www.fs.usda.gov/rds/archive/catalog/RDS-2016-0005 |
| 6 | NOAA STAR GeoColor CDN | Today's cloud field as one ready JPEG per region, mapped onto the sky dome | NOAA data; credit CIRA/NOAA | Yes | 1.5 | 6 ÷ 1.5 | https://cdn.star.nesdis.noaa.gov/GOES19/ABI/SECTOR/ne/GEOCOLOR/ |
| 7 | Census ACS 5-year housing tables (B25034, B25035, B25024) | Each block group gets its housing era and building mix: pre-1940 porches, 1960s ranches, triple-deckers, mobile-home parks | Public domain | Bake (keyless download) | 1.5 | 6 ÷ 1.5 | https://www2.census.gov/programs-surveys/acs/summary_file/2024/table-based-SF/data/5YRData/ |
| 8 | EPA Level III/IV ecoregions | A named region per tile switches the regional palette: grass tint, fences, barns, default trees | Public domain (EPA) | Yes | 1 | 4 ÷ 1 | https://geodata.epa.gov/arcgis/rest/services/ORD/USEPA_Ecoregions_Level_III_and_IV/MapServer |
| 9 | USFS National Forest System Roads + MVUM | Dirt and gravel surfaces, washboard rumble, dust, single-lane width, Forest Road numbers and winter closures | CC BY 4.0 (USDA Forest Service) | Yes | 2 | 7 ÷ 2 | https://apps.fs.usda.gov/arcx/rest/services/EDW/EDW_RoadBasic_01/MapServer/0 |
| 10 | USGS PAD-US 4.1 | "Entering ... National Forest" and brown park signs, fences and cattle guards at boundaries, empty wilderness | Public domain (USGS) | Yes | 2 | 7 ÷ 2 | https://services.arcgis.com/v01gqwM5QqNysAAi/arcgis/rest/services/Manager_Name_PADUS/FeatureServer/0 |

The ranking divides the scanners' impact score (1-10) by effort. Ties are broken by impact, then by fewer verifier caveats. Two more sources score 3.5 but miss the cut: the BTS noise map and NWI wetlands (NWI's REST queries are fragile). Geofabrik taginfo scores 6 but is left out because players never see it. High-impact sources just below the cut: FAA DOF, the Cropland Data Layer and ResStock/ComStock (9 ÷ 3 each), LANDFIRE (10 ÷ 4), and HPMS (10 ÷ 8).

## Kept sources by domain

Each source appears once. Where several scanners found the same source, it is listed under the closest domain.

### Structures and infrastructure

| Source | Licence | Access | Use in game | d |
|---|---|---|---|---|
| [FAA Digital Obstacle File](https://services6.arcgis.com/ssFJjBXIUyZDrSYZ/arcgis/rest/services/Digital_Obstacle_File/FeatureServer/0) | US Government work; FAA asks for acknowledgement and forbids implied endorsement | Keyless FeatureServer; 56-day zip (31.5 MB) and daily CSV | 656,884 towers, stacks, water tanks, silos and grain elevators at their real height, with real night lighting (steady red or white strobe). Drop records with Verified='U' and set a default where lighting is N or U | 3 |
| [USGS Wind Turbine Database](https://energy.usgs.gov/api/uswtdb/v1/turbines) | USGS public domain | Keyless API; or bundle the 1.4 MB national GeoJSON and refresh quarterly | 77,839 turbines at real hub height and rotor diameter. About 5,000 lack those two values, so fall back to model defaults | 2 |
| [National Bridge Inventory (NTAD)](https://services.arcgis.com/xOi1kZaI0eWDREZv/arcgis/rest/services/NTAD_National_Bridge_Inventory/FeatureServer/0) | US Government work, unrestricted; credit FHWA and BTS | Keyless; data as of 20 Jun 2025 | 624,193 bridges with deck length, width, span count, material, design family, clearance signs and the name of what they cross. Deck type also drives sound: steel-grid hum, timber rumble, joint thumps. Filter out 0,0 points and expect a schema change when SNBI data arrives | 5 |
| [National Tunnel Inventory](https://www.fhwa.dot.gov/bridge/inspection/tunnel/inventory/download.cfm) | US Government work | Bundle the 2026 XLSX (0.6 MB) | 580 tunnels with length, lanes, clearance and width; tunnel lighting and acoustics on entry. Only one portal point is given, so take the bore alignment from OSM/Overture | 4 |
| [HIFLD Next](https://hifld.publicenvirodata.org/) | Underlying layers are US Government works (DHS records: usa.gov/government-works); the host adds no licence | Keyless PMTiles and OGC API from a nonprofit host with no SLA; mirror the small PMTiles | 94,619 transmission lines (pylon size by voltage class), cell towers, ASR towers, power plants. No substations layer was found. Credit OSM where a feature's SOURCE field cites it | 4 |
| [NTAD rail lines](https://services.arcgis.com/xOi1kZaI0eWDREZv/arcgis/rest/services/NTAD_North_American_Rail_Network_Lines/FeatureServer/0) + [railroad bridges](https://services.arcgis.com/xOi1kZaI0eWDREZv/arcgis/rest/services/NTAD_Railroad_Bridges/FeatureServer/0) | US Government work, unrestricted | Keyless | 302,774 rail segments with track count, owner, trackage rights and passenger flag (freight or Amtrak liveries). 69,510 rail bridges (2022 points) typed girder, truss or trestle | 4 |
| [FAA airports and runways (ADDS)](https://services6.arcgis.com/ssFJjBXIUyZDrSYZ/arcgis/rest/services/Runways/FeatureServer/0) | US Government work ("for public use") | Keyless; ADDS refreshes every 56 days. NASR APT CSV (8.5 MB) for baking | Runway and apron polygons with surface and lighting, rotating beacons and approach lights at night, grass strips | 3 |
| [National Inventory of Dams](https://nid.sec.usace.army.mil/) | US Government work (NTAD mirror: unrestricted; NID's own wording [unverified]) | Keyless JSON API; national CSV 69 MB | Earth or concrete dams at real height and length, with spillway and lock walls. Take the crest alignment from OSM/Overture | 3 |
| [USCG Light List GeoJSON](https://navcen.uscg.gov/msi) | US Government work; the "as is" wording was not found [unverified] | Keyless daily files (about 1.9 MB per chunk). NOAA ENC data via bulk S-57 at authoring time only (ENC Direct failed 40-50% of requests) | Lighthouses and range lights flashing their real pattern; drawbridge vs fixed bridge with real clearance | 3 |
| [FCC Antenna Structure Registration](https://data.fcc.gov/download/pub/uls/complete/r_tower.zip) | Federal public record; FCC terms page blocked (403) [unverified] | Bake (37.9 MB weekly zip, no spatial API) | Guyed, lattice or monopole mesh and the FAA paint and lighting spec on DOF tower points. Field codes [unverified] | 3 |
| [DC Buildings 3D 2024](https://opendata.dc.gov/maps/DCGIS::buildings-3d-scene-2024) | CC BY 4.0 (DC OCTO) | Keyless I3S; Collada per ward 1.8-6 GB; bake | Lidar-derived massing for every DC building; real roof shapes (LOD2) [unverified]. The White House area is redacted, so fill it from Overture | 6 |
| [Boston 3D Smart Model](https://www.bostonplans.org/3d-data-maps/3d-smart-model/3d-data-download) | PDDL | Keyless OBJ/SKP zip per tile; bake | Roof forms for downtown, Back Bay and Seaport. LOD, and whether unbuilt projects are included, are [unverified]; check the documentation before baking | 5 |
| [NYC 3-D Building Model](https://data.cityofnewyork.us/d/tnru-abg2) | NYC Open Data Law (no use restrictions; cite source and version) | Bake (CityGML 916 MB) | Five-borough skyline with setbacks and rooftop water tanks. It reflects the 2014 survey, so reconcile against Overture for newer towers | 6 |

### Roads and traffic

| Source | Licence | Access | Use in game | d |
|---|---|---|---|---|
| [FHWA HPMS per-state services](https://geo.dot.gov/server/rest/services/Hosted/HPMS_FULL_VA_2025/FeatureServer/0) (better path for the planned HPMS) | Public domain (USDOT) | Keyless; pattern HPMS_FULL_{ST}_{YEAR}, all 52 for 2025 | Roughness (IRI) drives suspension and tyre rumble; also lanes, shoulders, medians, speed limits and AADT for traffic density. Speed limit is filled on only 20-47% of sections, so statutory defaults are still needed | 8 |
| [USDOT WZDx work-zone feeds](https://data.transportation.gov/d/69qe-yiui) | CC0 on 18 feeds (WA, MN, MD, DE, WI, MO, St Charles MO, HI, KS, IN, MS, ND, LA, KY, NJ, Austin, SD, Maricopa) | Keyless GeoJSON, up to 6 MB per state; poll every 5-10 min with conditional GET | Real cone tapers, arrow boards, lane closures and reduced work-zone speeds where DOTs report work today | 6 |
| [FRA crossing inventory (Form 71)](https://data.transportation.gov/d/m2f8-22s6) + [NTAD crossings](https://services.arcgis.com/xOi1kZaI0eWDREZv/arcgis/rest/services/NTAD_Railroad_Grade_Crossings/FeatureServer/0) | Public domain | Keyless SODA (query with within_box) and FeatureServer; join on CrossingID | 242,073 at-grade crossings: gates, flashers, bells, crossbucks, surface bump and whistle ban (blank means unknown). Train frequency comes from daily counts | 4 |
| [MassDOT Road Inventory](https://gis.massdot.state.ma.us/arcgis/rest/services/Roads/RoadInventory/MapServer/0) | Public domain ("can be used by anyone for any purpose") | Keyless; 505,299 records | Every Massachusetts public road: posted and opposite-direction speed limits, lanes, curbs, sidewalks, shoulders, medians, truck routes | 7 |
| [Caltrans CWWP2 feeds](https://cwwp2.dot.ca.gov/documentation/cms/cms.htm) | Public domain (Caltrans Conditions of Use); CCTV photos not cleared | Keyless JSON per district; message signs every 1 min, lane closures every 5 min (District 7 file 9.9 MB) | California message signs show the text they display right now; cones and ramp closures where Caltrans has them this week | 3 |
| [NYC DOT signs](https://data.cityofnewyork.us/d/nfid-uabd) + [Seattle SDOT signs](https://services.arcgis.com/ZOyb2t4B0UYuYNYH/arcgis/rest/services/SDOT_Street_Signs/FeatureServer/1) | NYC Open Data Law; Seattle partly confirmed [unverified] | Keyless SODA and FeatureServer | Real parking, one-way and turn signs on the right pole, facing the right way. NYC coordinates are EPSG:2263 feet | 4 |
| [NYC planimetrics: curbs, sidewalks, pavement edge](https://data.cityofnewyork.us/d/5xvt-8cbk) | NYC Open Data Law | Bake (runtime is about 17 s per tile for three layers) | Survey-grade curb lines, bulb-outs, medians and sidewalks. Last updated April 2024; the median, roadbed and parking-lot dataset IDs are [unverified] | 5 |
| [FHWA TMAS hourly counts](https://www.fhwa.dot.gov/policyinformation/tables/tmasdata/) | US Government work, unrestricted (NTAD) | Bake (about 26 MB per month) | Hour-of-week factors so traffic thins before dawn and peaks at rush hour; TMAS Class adds the truck share | 3 |
| [Geofabrik taginfo (US)](https://taginfo.geofabrik.de/north-america:us/keys/maxspeed) | Derived from OSM (ODbL); planning only | Keyless JSON | Players never see it. It shows maxspeed on about 6.6% and surface on about 25% of US highway ways (rough ratios), which justifies the fallbacks above | 0.5 |

### Land, water and vegetation

| Source | Licence | Access | Use in game | d |
|---|---|---|---|---|
| [LANDFIRE 2025 EVT/EVH/EVC/CC/CH/FDist](https://edcintl.cr.usgs.gov/geoserver/landfire_wcs/conus_2025/wcs) | CC0 / public domain (LF2025 metadata; LF2024 wording [unverified]) | Keyless WCS; a 17x17 px tile is 2.5 KB | The vegetation system, height, cover and recent fire or harvest in each 30 m cell, mapped to species palettes: sage steppe, pine flats, oak-hickory | 4 |
| [USGS 3DHP / NHDPlus HR](https://hydro.nationalmap.gov/arcgis/rest/services/3DHP_all/MapServer) | Public domain ("open and non-proprietary") | Keyless; waterbody polygons can be several MB, so simplify or clip | River names on bridge signs, river width and water sound from stream order, lakes and ponds OSM misses | 4 |
| [Cropland Data Layer (CroplandCROS)](https://pdi.scinet.usda.gov/image/rest/services/CDL_WM/ImageServer) | Public domain ("free to redistribute") | Keyless ImageServer (30 m) and CropScape WMS. GetCDLFile returned 502 in testing; bake 10 m per tile | Each field's crop and its look on the simulated date: corn tall in July, stubble in October, gold wheat in June, orchards in rows | 3 |
| [SSURGO via Soil Data Access](https://SDMDataAccess.sc.egov.usda.gov/Tabular/post.rest) + NRCS soil series descriptions | Public domain. SoilWeb API terms [unverified], so parse the NRCS text for shipped assets | Keyless POST, about 0.4 KB per point | Munsell soil colour for shoulders, cut banks and ploughed fields (red Piedmont clay, black Iowa soil); wet ditches by drainage class | 5 |
| [USFS forest roads + MVUM](https://apps.fs.usda.gov/arcx/rest/services/EDW/EDW_RoadBasic_01/MapServer/0) | CC BY 4.0 | Keyless | Surface, maintenance level, single-lane width, Forest Road numbers, seasonal closures | 2 |
| [PAD-US 4.1](https://services.arcgis.com/v01gqwM5QqNysAAi/arcgis/rest/services/Manager_Name_PADUS/FeatureServer/0) | Public domain (USGS) | Keyless; query attributes only | Park, forest and refuge entry signs, boundary fences and cattle guards, state parks nationwide | 2 |
| [NWI wetlands](https://fwspublicservices.wim.usgs.gov/wetlandsmapservice/rest/services/Wetlands/MapServer/0) | Public domain; FWS asks for acknowledgement | WMS works. REST geometry queries return HTTP 500 unless generalised, so bake from the state GeoPackages | Cattail marsh, cypress swamp with standing water, salt marsh along causeways | 2 |
| [MTBS burn severity](https://imagery.geoplatform.gov/iipp/rest/services/Fire_Aviation/USFS_EDW_MTBS_CONUS/ImageServer) | Public domain (USGS); the NIFC perimeter supplement has no licence [unverified] | Keyless; under 1 KB per tile | Black snags, bare soil and regrowth by years since fire on western highways | 3 |
| [SNODAS snow depth](https://noaadata.apps.nsidc.org/NOAA/G02158/) | NOAA product; citation required; public-domain status [unverified] | Keyless daily tar (about 20 MB); convert on a server | Adds today's snow depth to the snow cover already used: plough banks, snow on roofs and branches | 3 |
| [USGS SGMC geology](https://services.arcgis.com/v01gqwM5QqNysAAi/ArcGIS/rest/services/SB_5888bf4fe4b05ccb964bab9d_USGS_SGMC_feature/FeatureServer/3) | Mostly public domain, with some copyrighted state material | Keyless point query, about 1.3 KB | Rock colour in road cuts and cliffs: red sandstone, grey limestone, black basalt | 2 |
| [USA-NPN phenology](https://geoserver.usanpn.org/geoserver/wms) + GIBS NDVI (MODIS_Terra_NDVI_8Day) | CC BY 4.0 (USA-NPN); NASA (acknowledge GIBS) | Keyless tiles. MCD12Q2 needs an Earthdata login | Leaf-out, bloom and green-up matched to the simulated date. The VIIRS NDVI layer uses Level8 tiles | 3 |
| [EPA Level III/IV ecoregions](https://geodata.epa.gov/arcgis/rest/services/ORD/USEPA_Ecoregions_Level_III_and_IV/MapServer) | Public domain | Keyless, about 1 KB per point | Regional art palette switch | 1 |
| [NOAA CUSP shoreline](https://nsde.ngs.noaa.gov/) | NOAA (no access constraints; credit NOAA; not for litigation) | Keyless MVT tiles at nsde.ngs.noaa.gov/cusp/tiles/{z}/{x}/{y}.pbf (used by NOAA's viewer, undocumented) and 5° zips | Current shoreline with seawalls, riprap and marsh edges along coastal roads | 2 |
| [BLM SMA](https://gis.blm.gov/arcgis/rest/services/lands/BLM_Natl_SMA_LimitedScale/MapServer) + grazing allotments | Public domain (allotments confirmed; SMA by the federal-work default) | Keyless | Barbed wire and cattle guards at allotment and ownership lines, Open Range signs, checkerboard fencing | 2 |
| [iNaturalist](https://api.inaturalist.org/v1/observations) + GBIF | Per record; keep CC0 and CC BY only | Bake from the 36.7 GB AWS snapshot or from GBIF (free account for bulk). The API is not for scraping | Regional species mix inside each LANDFIRE class: live oak and palmetto, or ponderosa and manzanita | 5 |

### Imagery, sky and weather

| Source | Licence | Access | Use in game | d |
|---|---|---|---|---|
| [County and state ImageServers](https://www.arcgis.com/sharing/rest/search?q=orthoimagery%20type%3A%22Image%20Service%22%20access%3Apublic&f=pjson) | Varies by server. Cleared: NC OneMap, Iowa State NAIP 2025 (CC BY), ND 2024 (NAIP), Maine. No licence: Cook County, Martin County, Raleigh, so leave them out | Keyless exportImage, 170-310 KB per 1024 px over 500 m. Discover at authoring time; allow-list at runtime with NAIP fallback | 7.5-30 cm ground under the car: patches, paint, driveways | 6 |
| [NAIP keyless paths](https://imagery.geoplatform.gov/iipp/rest/services/NAIP/NAIP_plus/ImageServer) | Public domain with attribution (USDA FPAC) | Keyless national mosaic for 2021-2023; state servers for 2024/2025 (ND, MT, Iowa). The AWS buckets are requester-pays | Better path for the NAIP already in use, with fresher 30-60 cm ground in states with 2024/2025 flights | 2.5 |
| [GIBS true colour + HLS](https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/VIIRS_NOAA21_CorrectedReflectance_TrueColor/default/2026-11-07/GoogleMapsCompatible_Level9/{z}/{y}/{x}.jpg) | NASA (acknowledge GIBS); HLS S30 also credits Copernicus | Keyless, 6-8 KB per tile | Daily colour grade over NAIP: snow line, autumn colour, drought browning | 3 |
| [OpenAerialMap](https://api.openaerialmap.org/meta) | Per image: keep CC BY 4.0 and CC BY-SA 4.0; drop 403 BY-NC and 27 unlicensed | Keyless API, tiles and COGs | Centimetre-scale drone imagery where it exists (6,094 images in CONUS) | 2 |
| Lidar intensity from the 3DEP EPT already in use (usgs-lidar-public) | Public domain (USGS) | Bake | Shadow-free albedo: dark asphalt, bright concrete and lane paint under trees. Normalise per project | 3 |
| [GIBS GOES GeoColor + Band 13 IR](https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/GOES-East_ABI_GeoColor/default/default/GoogleMapsCompatible_Level7/{z}/{y}/{x}.png) | NASA/NOAA open data; credit GIBS and CIRA/NOAA | Keyless; 10-min frames, about 118 KB per tile, 45-60 min latency; cache by time key | Today's clouds on the sky dome, sun occlusion and cloud shadows, IR at night | 4 |
| [NOAA STAR GeoColor CDN](https://cdn.star.nesdis.noaa.gov/GOES19/ABI/SECTOR/ne/GEOCOLOR/) | NOAA; credit CIRA/NOAA; the "not copyrighted" statement [unverified] | Keyless JPEG, about 1 MB per 1200 px frame | A simpler sky texture with no time API | 1.5 |
| [NOAA HRRR on AWS](https://noaa-hrrr-bdp-pds.s3.amazonaws.com/) | NOAA open data; attribute, no endorsement | Keyless byte-range reads (0.24-0.95 MB per field) or hrrrzarr chunks (7-23 KB) | Cloud base height and three cloud layers, sun-disc hardness from direct and diffuse light, fog distance, smoke haze. CONUS and Alaska only | 5 |
| [NOAA MRMS on AWS](https://noaa-mrms-pds.s3.amazonaws.com/) + [NWS QPE service](https://mapservices.weather.noaa.gov/raster/rest/services/obs/mrms_qpe/ImageServer) | NOAA open data | Keyless 2-min GRIB2 (rate 0.5 MB, type 0.15 MB) or a 2 KB identify call | Rain or snow falling now and how wet the road is: wipers, spray, puddles, roof noise, grip. The game has no rain code yet. QPE units [unverified] | 4 |
| [IEM tile cache](https://mesonet.agron.iastate.edu/cache/tile.py/1.0.0/nexrad-n0q-900913/{z}/{x}/{y}.png) | Public domain; credit appreciated | Keyless PNG, 5-min cache, no SLA | Live radar and satellite with no decoding. The HRRR forecast layer is named hrrr::REFD-F0060-0 | 1.5 |
| [GOES-19/18 Level-2 cloud products + GLM](https://noaa-goes19.s3.amazonaws.com/) | NOAA open data | Keyless NetCDF: 0.3-4 MB per 5-min scan; GLM 0.45 MB per 20 s | Cloud-top height and phase (cumulus vs cirrus), optical depth, smoke. Lightning flashes with thunder delayed by distance | 5 |
| [VIIRS night lights via GIBS](https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/VIIRS_Black_Marble/default/2016-01-01/GoogleMapsCompatible_Level8/{z}/{y}/{x}.png) | NASA (CC0 for NASA-led data) | Keyless 2012/2016 composites and daily Day/Night Band | Town glow on the horizon, lit-window density, star visibility. Replaces the non-commercial World Atlas | 3 |
| [USNO API](https://aa.usno.navy.mil/api/rstt/oneday) | US Government work | Keyless JSON, 1.3 KB; years 1700-2100 | Sun and moon times, moon phase. Civil twilight only | 1 |

### Buildings and places

| Source | Licence | Access | Use in game | d |
|---|---|---|---|---|
| [ResStock 2025.1 + ComStock 2025.3](https://oedi-data-lake.s3.amazonaws.com/nrel-pds-building-stock/end-use-load-profiles-for-us-building-stock/2025/resstock_amy2018_release_1/metadata_and_annual_results/) | CC BY 4.0 (citation.txt). A disclaimer says "research purposes only"; CC BY governs | Bake per-county tables (under 1 MB) from the parquet files | County-typical wall finish, roof, stories, garage and foundation: stucco and tile in Phoenix, brick in Baltimore, metal buildings at rural businesses | 3 |
| [FEMA/ORNL USA Structures](https://services2.arcgis.com/FiaPA4ga0iQKduv3/ArcGIS/rest/services/USA_Structures_View/FeatureServer/0) | CC BY 4.0; credit ORNL and FEMA GRO | Keyless, about 28 KB per 500 m tile; cache (no published rate limit) | Use of every structure over 450 sq ft (storefront, loading dock, barn, campus) plus heights where Overture lacks them | 3 |
| [Overture addresses](https://overturemaps-extras-us-west-2.s3.us-west-2.amazonaws.com/tiles/2026-10-21.0/addresses.pmtiles) | Per source (CC BY, CC0, PDDL, public domain). Licence text for NAD-derived rows [unverified] | Keyless PMTiles z14, 10-22 KB per 500 m. The release path changes monthly | Real house numbers on doors and mailboxes, suite numbers. 126 M US points, partial coverage, Alpha | 2.5 |
| [Wikidata Query Service](https://query.wikidata.org/sparql) + Commons | CC0 data; Commons images per file (drop NC/ND) | Keyless with a User-Agent; 5 parallel queries per IP. Endpoint moves in 2026-2027 | Landmark names, architectural style, floors and heights; photo cards with credit | 3 |
| [ACS 2020-2024 housing tables](https://www2.census.gov/programs-surveys/acs/summary_file/2024/table-based-SF/data/5YRData/acsdt5y2024-b25034.dat) | Public domain | Bake (B25034, B25035 and B25024 files: 50, 15 and 48 MB) | Housing era and building-size mix by block group. Handle jam values such as -333333333 | 1.5 |
| [NYC PLUTO](https://data.cityofnewyork.us/resource/64uk-42ks.json) + footprints + LPC database | NYC Open Data Law; cite source and version | Keyless SODA | Brownstone stoops, cast-iron fronts, limestone; floors and roof height per building | 3 |
| [DC CAMA](https://maps2.dcgis.dc.gov/dcgis/rest/services/DCGIS_DATA/Property_and_Land_WebMercator/FeatureServer/25) + Boston FY2026 assessment | CC BY 4.0 (DC); PDDL (Boston) | Bake; strip owner and mailing columns | Wall and roof material and condition per building: rowhouse roofs, Boston triple-deckers, weathering | 2 |
| [NRHP locations](https://mapservices.nps.gov/arcgis/rest/services/cultural_resources/nrhp_locations/MapServer) | US Government work; rights to photos inside nominations [unverified] | Keyless | 74,636 listed properties and districts: period facades, names, plaques | 1 |
| [USGS TNM Structures](https://carto.nationalmap.gov/arcgis/rest/services/structures/MapServer) + [NCES schools](https://nces.ed.gov/opengis/rest/services/K12_School_Locations/EDGE_ADMINDATA_PUBLICSCH_2425/MapServer/1) | Public domain | Keyless | Fire stations with bay doors, post offices, courthouses, hospital signs. School-zone signs, beacons and bus traffic scaled by enrolment | 1.5 |
| [MassGIS parcels](https://services1.arcgis.com/hGdibHYSPO59RG1h/arcgis/rest/services/Massachusetts_Property_Tax_Parcels/FeatureServer/0) + MHC inventory | Public domain per the MassGIS FAQ (mass.gov blocks scripts); MHC terms [unverified] | Keyless; drop owner fields | New England house styles by era, three-deckers, history of village-centre buildings | 2 |
| [LOC HABS/HAER/HALS](https://www.loc.gov/collections/historic-american-buildings-landscapes-and-engineering-records/) | No known restrictions on US Government images; check each item's rights | Bake; limit 20 requests/min | Measured drawings and photos for signature bridges, courthouses and mills | 2 |
| [name-suggestion-index](https://cdn.jsdelivr.net/npm/name-suggestion-index@8.0.20261101/dist/json/nsi.min.json) | BSD-3-Clause; trademarks are not covered | Bake a US subset (under 2 MB) | Correct brand spelling and building type (fuel canopy, drive-thru). Never draw real logos | 1 |

### Trees, vehicles, props and sound

| Source | Licence | Access | Use in game | d |
|---|---|---|---|---|
| [USFS Urban Tree Database](https://www.fs.usda.gov/rds/archive/products/RDS-2016-0005/RDS-2016-0005.zip) | Usable "without additional permissions or fees"; cite | Bake (1.1 MB zip) | Height and crown from trunk diameter for 171 species in 16 regions | 2 |
| [EZ-Tree](https://github.com/dgreenheck/ez-tree) | MIT | Bake meshes; the Godot port is marked unstable | A different tree shape for each species instead of one reused oak | 3 |
| [Poly Haven models](https://api.polyhaven.com/assets?type=models) | CC0; send a unique User-Agent | Bake with smaller textures and LODs | Photoscanned hydrants, street lamps, utility poles, barriers and utility boxes | 2.5 |
| [EPA Test Car List](https://www.epa.gov/compliance-and-fuel-economy-data/data-cars-used-testing-fuel-economy) + fueleconomy.gov vehicles.csv | Public domain (EPA Data License) | Bake | Coasting, top-gear rpm and pull-away matched to real models; body class and cylinder count choose meshes and engine sound. Coefficients are in lbf and mph | 3 |
| [NY DMV registrations](https://data.ny.gov/d/w4pv-hbkt) | OPEN-NY Terms (custom and revocable); ship only aggregates | Bake | Real paint colour, body type and age mix for parked and passing cars | 1.5 |
| [BTS National Transportation Noise Map](https://geo.dot.gov/server/rest/services/Hosted/NTAD_Noise_2020_CONUS_Road/MapServer) | US Government work (CC0 on data.gov) | Keyless tiles; one z12 tile (about 14 KB) covers about 8 km | Distant traffic hum plus airport and rail rumble by location. A background estimate, not a measurement | 2 |
| [FHWA TNM 3.0 REMELs](https://rosap.ntl.bts.gov/view/dot/48763) | US Government work | Bake (Appendix A, Table 6) | Loudness vs speed per vehicle and pavement: concrete louder than asphalt, semis and motorcycles louder than cars. RoadNoise.gd has only paved, gravel and verge today | 2 |
| [NPS Natural Sounds](https://www.nps.gov/subjects/sound/gallery.htm) | Public domain; credit NPS | Bake | Crickets, robins, elk, thunder and geysers when the windows are down | 1.5 |
| [Overpass font](https://github.com/RedHatOfficial/Overpass) + [FHWA Standard Alphabets](https://mutcd.fhwa.dot.gov/SHSe/Alphabets.pdf) | OFL 1.1; public domain | Bundle with OFL.txt | Highway Gothic-style lettering for street names, exit numbers and distances | 1.5 |
| [Freesound](https://freesound.org/docs/api/resources_apiv2.html) | Per sound; keep CC0 and CC BY only | Free account and token; authoring only | Rain on the roof, wipers, turn-signal relay, crossing bells, pass-bys | 4 |
| [engine-sim](https://github.com/ange-yaghi/engine-sim) | MIT | Windows-only tool; authoring only | Calibrate V6, V8 and flat-four synthesis from recorded rpm sweeps. Nothing from it ships | 4 |

## Authoring-only (account or licence needs care)

The game never calls these. Each needs a free account or a licence check before anything derived from it ships.

| Source | Issue | Handling |
|---|---|---|
| Freesound | Free account and token needed even to search | Download CC0 and CC BY sounds only; credit CC BY |
| GBIF bulk downloads | Free account | Keep CC0 and CC BY 4.0 records only |
| MODIS MCD12Q2, NASA Black Marble VNP46A4/VJ146A4 (2012-2025) | Earthdata Login; product-level CC0 marking [unverified] | Bake seasonal curves and a 500 m sky-glow raster |
| EOG VIIRS VNL v2.2 | OpenID login; whether v2.2 is CC BY [unverified] | Prefer Black Marble |
| USGS EarthExplorer (HRO and other aerial collections) | Free account | Authoring reference only |
| County ImageServers | Licence varies by server | Index only servers with an explicit open statement or NAIP origin; keep Cook, Martin and Raleigh out until their owners confirm |
| WZDx feeds from IA, FL depot, ID, NH/VT/ME | No licence field [unverified] | Do not redistribute until confirmed |
| State DOT layers for FL, WA, VA, UT, NC | Not cleared. WSDOT: "For Planning use only" plus an indemnity clause. VDOT: official Commonwealth business only. FDOT and UDOT: no reuse grant. NCDOT: unconfirmed | Use HPMS; ask each DOT for written permission |
| HIFLD Next | Host adds no licence and has no SLA | Rely on the DHS government-works records; mirror the PMTiles |
| FCC ASR | FCC terms page blocked [unverified] | Bake attributes onto DOF points only |
| Overture addresses (NAD rows) | NAD licence text unreadable [unverified] | Ship the Overture per-source credit list |
| NY DMV registrations | Custom, revocable terms | Ship statistics only |
| Seattle SDOT signs | Disclaimer only [unverified] | Confirm the 2016 open-data policy covers it |
| USGS SGMC | Some copyrighted state material | Ship lithology and age; check REFERENCE before shipping polygons |
| NIFC fire perimeters | No licence [unverified] | Build assets from MTBS |
| SoilWeb API | No terms [unverified] | Parse NRCS soil series text instead |
| Caltrans CCTV stills | Photographs may need permission | Do not display |
| OpenAerialMap, iNaturalist, Commons images | Licence set per item, some non-commercial | Filter to CC0, CC BY and CC BY-SA |
| ResStock/ComStock | Disclaimer says "research purposes only" | CC BY 4.0 governs; record the caveat |
| NOAA ENC (S-57) | Redistributing official charts is regulated (15 CFR 995) | Bake props only; never ship ENC cells |
| Third-party lidar intensity (Oregon DOGAMI, Indiana, Wisconsin) | Licences unconfirmed; Indiana needs a login | Use 3DEP EPT only |
| Microsoft Planetary Computer | Anonymous token works, but Microsoft preview terms apply | Not a runtime dependency |
| Brands (name-suggestion-index, Wikidata logos) | Trademark and trade dress | Text or generic signage only |

## Rejected and why

| Why rejected | Sources |
|---|---|
| Key, token or login required at runtime | Bing Maps; Google Maps Platform (imagery, Places, Street View, Roads, Photorealistic 3D Tiles); Cesium ion; Mapillary; OpenCelliD (also CC BY-SA); Regrid; Census Data API (use the summary files); xeno-canto API v3 (also many NC); 511 SF Bay; WSDOT Traveler API [unverified]; UDOT FI_* MapServers; Esri traffic and imagery exports [unverified]; Copernicus Data Space and Sentinel Hub; Google Earth Engine; Sketchfab API at runtime; OpenWeather Agro and Ambee |
| Keyed WZDx feeds | CO, TX, OH, OR, NC, MI, VA, MTC Bay Area, NPS, PA Turnpike, IL Tollway, MassDOT CWZ, IDOT CWZ; plus NY 511 (HTML shell, key needed), OK and FL one.network (401 without embedded keys) |
| Account, agreement or government-only access | NPMRDS; Caltrans PeMS; HIFLD Secure; DataLumos/ICPSR HIFLD copy; PHMSA NPMS; Virginia DHR V-CRIS; King County Assessor (click-through, RCW 42.56.070(9)); OpenAddresses batch downloads (403); Foursquare OS Places direct download (already in Overture); ORNL Model America (no licence [unverified]); NatureServe [unverified] |
| Requester-pays buckets | NAIP on AWS; Landsat Collection 2 on AWS; USGS Annual NLCD bucket |
| Non-commercial or no redistribution | Maxar Open Data (CC BY-NC); Falchi World Atlas 2015 (CC BY-NC); Planet/NICFI; TxDOT portal (NC, no third-party distribution); BBC Sound Effects; Sonniss GDC bundles; Pixabay sounds; RainViewer; Roadgeek 2005 fonts (2014 repo provenance unclear); HMdb; Poly Pizza API; lightpollutionmap.info |
| Proprietary terms | Esri World Imagery, Living Atlas 3D and Esri-hosted copies (Esri MLA); EIA Energy Atlas hosted layers; Mapbox, HERE, TomTom, INRIX, Waze for Cities; Nearmap, EagleView, Vexcel (also excluded when served by counties); commercial parcel vendors (ATTOM, Cotality, LightBox, ReportAll) [not re-checked]; Quixel/Fab and Clearview [not re-checked]; FreePBR [unverified]; PPG/Axalta colour reports [unverified] |
| Discontinued or unreachable | HIFLD Open (shut down Aug-Sep 2025); Uber Movement; OpenTraffic [unverified]; SharedStreets CurbLR [unverified]; Chicago Traffic Tracker (no updates since Apr 2026); NASA NCCS HIFLD mirror; USCG weekly Light List XML |
| Licence unclear | Philadelphia 3D buildings (check metadata.phila.gov); California DMV fuel-type counts; state licence-plate artwork; real brand logos |
| Redundant or not useful | Open City Model, Microsoft footprints and All The Places (already in Overture); state DOT bridge files (in NBI); "USGS 3DEP building data" (no such product); ISRIC SoilGrids; USDA PLANTS; ESA WorldCover; HydroSHEDS; NYC DOT Traffic Speeds (15-20 h old) |
| Out of scope or no live feed | NHTSA FARS/CRSS; traffic signal timing and SPaT |
| Gap (no source exists) | No open national data on street-name-sign colours and typefaces, or on house-number plaques. Use KartaView/Panoramax imagery plus MUTCD defaults |

## Verifier-added leads to check next

Verifiers found these but did not run them through a full scanner pass. Check them before cataloguing.

| Lead | Licence | What it adds | d | Link |
|---|---|---|---|---|
| Overture base "infrastructure" layer | ODbL (credit OSM) | Every pylon, power line, substation, mast, water tower, street lamp, guard rail and toll booth. OpenFreeMap has none of these, and the layer is on the PMTiles host SimCruise already reads | 3 | https://overturemaps-extras-us-west-2.s3.us-west-2.amazonaws.com/tiles/2026-10-21.0/base.pmtiles |
| USGS solar PV database (USPVDB) | Public domain | Panel rows with real axis and tilt inside real fenced footprints | 2-3 | https://energy.usgs.gov/uspvdb/ |
| USGS RCMAP rangeland cover | CC0 | Bare ground, sagebrush and cheatgrass between shrubs in the West | 2 | https://www.mrlc.gov/geoserver/ows |
| NWS/NCEP opengeo radar WMS | Public domain | Official precip type and reflectivity, including AK, HI and PR, with no decoding; fallback for IEM | 1 | https://opengeo.ncep.noaa.gov/geoserver/conus/conus_bref_qcd/ows |
| NOAA RTMA/URMA | NOAA open data | Sky cover, ceiling and visibility for Hawaii, Puerto Rico and Guam; observation check on HRRR | 2 | https://noaa-rtma-pds.s3.amazonaws.com/ |
| NOAA HMS smoke polygons (+ HRRR-Smoke) | US Government (no licence text) | Brown-orange sky and a dim sun on wildfire-smoke days | 1.5 | https://satepsanone.nesdis.noaa.gov/pub/FIRE/web/HMS/Smoke_Polygons/ |
| HYG v4.4 stars + ESO Milky Way panorama | CC BY-SA 4.0; CC BY 4.0 (credit ESO/S. Brunier) | The real night sky by sidereal time, faded by sky glow and moonlight | 2 | https://codeberg.org/astronexus/hyg |
| NOAA SWPC aurora nowcast | US Government | Aurora on the northern horizon at night in Alaska and the northern states | 1.5 | https://services.swpc.noaa.gov/json/ovation_aurora_latest.json |
| USGS latest river gauges + NOAA NWPS | USGS mostly public domain; NWPS [unverified] | River level, exposed gravel bars, flood closures | 2 | https://api.waterdata.usgs.gov/ogcapi/v0/collections/latest-continuous/items |
| JRC Global Surface Water | Copernicus, free with acknowledgement | Reservoir bathtub rings, exposed lakebeds, playas | 2 | https://global-surface-water.appspot.com/download |
| NASS Crop Sequence Boundaries | Public domain | Real field outlines for crop textures, row direction and fence lines | 3 | https://www.nass.usda.gov/Research_and_Science/Crop-Sequence-Boundaries/index.php |
| USFS Insect and Disease Survey | [unverified] (data.gov record 404) | Grey and red beetle-killed stands by host and severity | 2 | https://apps.fs.usda.gov/arcx/rest/services/EDW/EDW_InsectandDiseaseSurvey_01/MapServer/1 |
| HIFLD Next oil and gas wells + platforms | EIA public domain | Pumpjacks, tank batteries, offshore platforms. No Texas coverage; the Permian would need a Texas source [not checked] | 2 | https://hifld.publicenvirodata.org/api/collections/hifld/datasets/oil-and-natural-gas-wells |
| HISDAC-US first-built year | CC0 (built partly from Zillow ZTRAX: provenance caveat) | Era-correct building kits where ACS block groups are too coarse | 1 | https://dataverse.harvard.edu/dataset.xhtml?persistentId=doi:10.7910/DVN/HHFM5E |
| HUD Public Housing Buildings | Public domain | The real form of public-housing courts | 1 | https://services.arcgis.com/VTyQ9soqVukalItT/arcgis/rest/services/Public_Housing_Buildings/FeatureServer/0 |
| NPS Points of Interest | Public domain | Named overlooks and mileposts on the Blue Ridge Parkway, Skyline Drive and Natchez Trace | 1 | https://mapservices.nps.gov/arcgis/rest/services/NationalDatasets/NPS_Public_POIs/FeatureServer/0 |
| NTAD National Transit Map stops + routes | CC BY 3.0 US | Bus stops and shelters, buses on real routes (no timetables) | 4 | https://services.arcgis.com/xOi1kZaI0eWDREZv/ArcGIS/rest/services/NTAD_National_Transit_Map_Stops/FeatureServer/0 |
| osm-legal-default-speeds | CC BY-SA 2.0 data; BSD-3 code | Statutory default speed limits per state where OSM and HPMS are blank | 2 | https://github.com/westnordost/osm-legal-default-speeds |
| NGSIM trajectories (+ TGSIM, licence blank [unverified]) | CC BY-SA 3.0 | Car-following and lane-change model fitted to US traffic; ship fitted parameters with credit | 3 | https://data.transportation.gov/d/8ect-6jqj |
| BigSoundBank | CC0 | Rain on the roof, pass-bys and cabin sound with no account needed (French recordings) | 1.5 | https://bigsoundbank.com/licenses.html |
| Wikimedia Commons audio | Per file (CC0 to CC BY-SA) | US train horns (K5LA) and crossing bells | 1 | https://commons.wikimedia.org/w/api.php |
| NPS geospatial sound model (IRMA DataStore 2217356) | Licence and format [unverified] | Natural quiet levels in parks | ? | (no URL confirmed) |
| UDOT Sign_Faces / Sign_Assemblies on ArcGIS Online | [unverified]; named by the roads scanner but never checked | Sign positions in Utah | ? | (no URL confirmed) |