# Surveyed road cuts close to the car

At the US 163 camera, the USGS ground 75 m ahead and 15 m right of the centerline stands 1.83 m above the road. Both the ordinary mesh and the earlier warm bank mesh had it 0.27 m below the road. The original solver discarded a strip wider than a full 7.6 m cell diagonal and then also discarded terrain closer to a minor access road.

A qualifying cached USGS survey supplies 129 × 129 ground posts. The helper compares each post with the nearest eligible major road. It sends millimetre offsets from the coarse grid only for supported terrain outside every mapped roadway, bridge, water footprint, structure, and tile boundary. The source and road elevation are compared as local height differences. The downloader selects sources reporting a NAVD88 datum and ground resolution of one metre or finer. Roads carry the original grade. Flat triangles stay coarse. A shared midpoint joins each coarse triangle to its refined neighbors. The mesh supplies both the view and the collision surface. A certified local ground packet or authored roadbed takes precedence.

The correction uses the existing public survey and its request limits. Results appear when the survey is cached as the tile is constructed. A player entering cold coverage can see the coarse ground until a later visit. A missing, low-resolution or ineligible survey uses the ordinary grid.

The receipt records two geographic checks, same-pose Metal comparisons and controlled stock Miata runs. The West Virginia holdout used an archived source-locked raster. The live catalog had previously abstained there. The one-metre GeoTIFF is independent of the cached 129-grid resampling path but describes the same airborne survey.

Run from the project root:

    bun lab/verify-road-banks.mjs
    godot --headless --path game --script res://checks/RoadBankMeshCheck.gd
    godot --path game -- --landscape-drive /tmp/sd-bank-drive --bank-drive-proof --with-soil

Warm the normal road-banks helper command for tile 12744, 25485 in the same cache as the game before the final command. The game prints whether it consumed a refinement. The validation script takes a bare tile returned by the regular tile command with --without-road-banks, an enabled tile containing the sparse offsets, and the registered native one-metre source GeoTIFF or a local window thereof. An optional fourth argument is the previous warm coarse solver output. It needs NumPy, Rasterio, SciPy and pyproj for the offline study.

The comparison includes a cropped photograph by Awinda from 30 April 2023 and is available under CC BY-SA 4.0. The approximate photographer bearing is 233.2°. The rendered right bank is closer in shape. Shrubs, the far mesas and the color of the left turnout still depart from the photograph. [Original photograph](https://commons.wikimedia.org/wiki/File:Forrest_Gump_Highway_Monument_Valley.jpg) · [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) · [USGS Utah survey](https://www.sciencebase.gov/catalog/item/67243ab7d34e4f57573ea1c5) · [USGS West Virginia survey](https://www.sciencebase.gov/catalog/item/5eacfa6f82cefae35a25016a) · [OpenStreetMap contributors](https://www.openstreetmap.org/copyright)
