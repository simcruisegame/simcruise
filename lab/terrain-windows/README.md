# How closely does the streamed ground reproduce a hill?

These three 188 m windows compare two levels of the freely hosted Terrarium elevation pyramid with USGS 1 m elevation products over rolling streets. Each array contains 48 × 48 samples spaced 4 m apart. Lewisburg came from the rural street-photo sample. Pittsburgh and Seattle were deliberately selected to exercise steeper terrain. The comparison covers the whole ground patch, including yards and slopes alongside the road.

| Site | Relief | Elevation error z13 → z14 | Local grade error z13 → z14 |
| --- | ---: | ---: | ---: |
| Lewisburg, WV | 13.61 m | 0.693 → 0.689 m | 2.15 → 2.19 percentage points |
| Pittsburgh, PA | 38.48 m | 0.492 → 0.369 m | 3.10 → 2.18 percentage points |
| Seattle, WA | 14.27 m | 0.340 → 0.251 m | 2.21 → 1.68 percentage points |

A higher tile resolution helped in Seattle and Pittsburgh. It slightly worsened the local grade metric at Lewisburg. These results test the source, including source-age and vertical-datum differences. They do not establish the shape of a specific traffic lane or directly measure the car. Production 1.0.2 continues to use its validated terrain and detailed surveyed districts.

Run `python3 compare.py` with NumPy to replay the metrics from the included arrays. The reference windows were read using GDAL through Rasterio over bounded HTTP byte ranges. The larger of the three downloaded 2.07 MB from a 327 MB USGS TIFF. Both candidate layers were bilinearly sampled at the same projected USGS pixel centers. The grade metric measures change in elevation error across a 20 m baseline in each cardinal grid direction and averages the absolute values. The DEM products can share survey ancestry.

The collection dates, source identifiers, coordinates, download URLs and transfer sizes are recorded for the three source products in [sources.json](sources.json). Original Terrarium contributors and attribution are listed by the [Tilezen elevation project](https://github.com/tilezen/joerd/blob/master/docs/attribution.md). The reference grids come from the [USGS National Map](https://www.usgs.gov/programs/national-geospatial-program/national-map).
