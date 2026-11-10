# Data sources and licences

The MIT license in [LICENSE](../LICENSE) covers the original game code, shaders, the helper's JavaScript, the Python authoring tools and the tests. Everything geographic or photographic keeps its own terms, recorded per source in [`licenses/`](../licenses/) (start with `licenses/Road Atlas.txt`) and summarised in [Credits.txt](../Credits.txt). The rule for bundling is simple: ODbL and ShareAlike data is fetched at runtime and cached on the player's machine, never redistributed in the MIT repository, except the small bundled offline-start tiles, atlas tiles and start pool under `game/data/` that are marked as derivative databases in `licenses/Road Atlas.txt`.

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
| Lane counts and posted speeds for road markings (`lanes`, `lanes:forward`, `lanes:backward`, `oneway`, `maxspeed`) | OpenStreetMap contributors via Esri's OSM North America highways replica, one query per 2 × 2 tile group, cached 28 days | ODbL 1.0 | At runtime |
| Guard rails and concrete barriers (`barrier=guard_rail`, `barrier=jersey_barrier`) | OpenStreetMap via public Overpass mirrors (or `SIMCRUISE_OVERPASS_URL`), one query per z14 area, cached 90 days | ODbL 1.0 | At runtime, never bundled |
| Weather and sky | NOAA Aviation Weather Center METAR; NWS observations via Iowa Environmental Mesonet; NOAA GOES cloud mask; NASA GIBS snow | US public domain | At runtime, optional |
| Street photographs | KartaView, Panoramax (thumbnails and credits only) | CC BY-SA 4.0 | At runtime, optional |
| Car, props, textures, sky, fonts | Original Miata model with Wikimedia lamp textures; Poly Haven and OpenGameArt textures; Comrade1280 parked cars; the UI and title typefaces credited in `Credits.txt` | CC BY-SA 4.0; CC0; CC BY 4.0; SIL OFL | Bundled in `game/art/` |

Paths in this repository that are not MIT: `game/data/*.json` map tiles and `game/data/observed-honolulu.json` (ODbL derivative databases), `game/art/miata.glb` and its textures (CC BY-SA 4.0), `game/art/sedan.glb` and `game/art/pickup.glb` (CC BY 4.0), `game/art/title.ttf` and `game/art/ui.ttf` (SIL OFL), and the CC0 textures and HDR listed in `Credits.txt`. Third-party code in the engine, runtime and helper (Godot, Jolt, Bun, Martini, GeoTIFF.js, proj4js, earcut, fast-png, pbf, laz-perf, h5wasm) has its license texts in `licenses/` and ships inside the app under `Contents/Resources/Credits`.
