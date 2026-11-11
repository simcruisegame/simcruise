# Data sources and licences

The MIT license in [LICENSE](../LICENSE) covers the original game code, shaders, the helper's JavaScript, the authoring tools and the tests. Everything geographic or photographic keeps its own terms, recorded per source in [`licenses/`](../licenses/) (start with `licenses/Road Atlas.txt`) and summarised in [Credits.txt](../Credits.txt). The rule for bundling is simple: ODbL and ShareAlike data is fetched at runtime and cached on the player's machine, never redistributed in the MIT repository, except the small bundled offline-start tiles under `data/tiles/` (the Miami Beach start) and `data/observed/`, which are derivative databases (ODbL) as `licenses/Road Atlas.txt` records.

The table lists what the map helper (`helper/atlas.mjs`, shipped as `tools/atlas.exe`) can fetch; the Unreal client does not use every layer yet (see [unreal-migration-ledger.md](unreal-migration-ledger.md)).

| Layer | Source | License | Reaches the game |
|---|---|---|---|
| Roads, buildings, land use, water, place names | OpenStreetMap via [OpenFreeMap](https://openfreemap.org/) vector tiles | ODbL 1.0 | At runtime; the offline-start tiles and the start pool bundled |
| Place search | [Photon](https://photon.komoot.io/) geocoder on OSM data | ODbL 1.0 (data) | At runtime |
| Ground elevation | Mapzen/Tilezen Terrarium tiles; USGS 3DEP 1/3-arc-second and 1 m products (The National Map) | US public domain | At runtime |
| Aerial imagery | USGS The National Map imagery; USDA NAIP (Hawaii, low-vegetation detection); USDA Forest Service Alaska orthophotos | US public domain | At runtime; offline-start imagery bundled |
| Recent surface colour | Copernicus Sentinel-2 L2A via Element 84 Earth Search | Copernicus Sentinel data terms (free, attribution) | At runtime, optional |
| Tree canopy heights | WRI/Meta High Resolution Canopy Height Maps v2 | CC BY 4.0 | At runtime |
| Forest type, shrub cover | USGS/USDA LANDFIRE EVT; MRLC NLCD and RCMAP; USDA Rangeland Analysis Platform | US public domain | At runtime |
| City street trees | City of Lincoln NE and City of Cambridge MA inventories | City open-data terms; PDDL 1.0 (Cambridge) | At runtime |
| Building heights, roofs, extra footprints | Overture Maps buildings (OSM, Microsoft ML, Esri Community Maps); Esri's OSM North America replica | ODbL 1.0; CC BY 4.0 for Esri entries | At runtime |
| Building use | US Army Corps of Engineers National Structure Inventory | US public domain | At runtime |
| Measured roofs and road camber | USGS 3DEP lidar point clouds (EPT on AWS); Miami-Dade County lidar | US public domain; CC0 (Miami-Dade) | At runtime; 15 Miami roofs bundled |
| Traffic signals, street objects | Overture transportation theme; optional self-hosted Overpass | ODbL 1.0 | At runtime |
| Lane counts and posted speeds for road markings (`lanes`, `lanes:forward`, `lanes:backward`, `oneway`, `maxspeed`) | OpenStreetMap contributors via Esri's OSM North America highways replica, one query per 2 × 2 tile group, cached 28 days, fetched in the background after the tile is drawn, for every streamed tile around the car nearest first | ODbL 1.0 | At runtime |
| Guard rails and concrete barriers (`barrier=guard_rail`, `barrier=jersey_barrier`) | OpenStreetMap via public Overpass mirrors (or `SIMCRUISE_OVERPASS_URL`), one query per z14 area, cached 90 days | ODbL 1.0 | At runtime, never bundled |
| Weather and sky | NOAA Aviation Weather Center METAR; NWS observations via Iowa Environmental Mesonet; NOAA GOES cloud mask; NASA GIBS snow | US public domain | At runtime, optional |
| Street photographs | KartaView, Panoramax (thumbnails and credits only) | CC BY-SA 4.0 | At runtime, optional |
| Cars, ground textures, fonts | The cars are Sketchfab models listed in `licenses/Traffic models.txt` (the 1999-2005 Miata by Nieve5677, the Mustang, Corvette, Model 3, F-150, Grand Cherokee, Odyssey, CR-V, Tahoe, Accord); Poly Haven Asphalt Floor, Grass Ground and Sandy Gravel 02; Unreal Engine's Roboto font | CC BY 4.0; CC0; Apache 2.0 | Bundled in `data/art/`; the font with the engine |

Paths in this repository that are not MIT: `data/tiles/` and `data/observed/` (ODbL derivative databases), `data/art/traffic/` (each model under its own CC BY 4.0 credit), `data/art/ground/` (CC0). A packaged build carries `LICENSE.txt`, this file as `DATA-SOURCES.md` and a `CREDITS.txt` listing every model's credit. The map helper's third-party code (Bun, Martini, GeoTIFF.js, proj4js, earcut, fast-png, pbf, laz-perf, h5wasm) has its license texts in `licenses/`. Unreal Engine is used under the Unreal Engine EULA.
