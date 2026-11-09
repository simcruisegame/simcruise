# Independently verified building gaps

> Former name: this study was written when SimCruise was called "Sunday Driver"; the app, paths and commands it mentions under that name are now SimCruise.

The base map misses some small buildings. Overture's buildings theme includes additional outlines extracted by Microsoft from images. A footprint can also mark a demolished structure, vegetation or a roof extending beyond the walls. Sunday Driver uses independently classified airborne lidar to verify presence and estimate height above ground before it adds a building.

| Game tile and observed structure | Evidence from 2021 lidar | Admitted height |
| --- | --- | --- |
| 18166,27914 in Miami, GERS `cad38762-afd2-444f-9895-1c5919ef4e26` | 272 building returns, all ten eligible cells, 109 nearby ground returns | 5.91 m |
| 19818,24238 in Allston, GERS `544b5e70-b520-467b-8b2b-7a6d265341d7` | 791 building returns, all eight eligible cells, 1,450 nearby ground returns | 4.39 m |

Both are single-structure meshes with flat roofs at the median roof level and neutral exterior walls. The 3D geometry includes wall colliders. Roof slope and façade details have no independent measurements in these records. The second candidate in Miami yielded 461 roof returns but lacked a sufficiently level local ground reference. It remains absent. Surveys from previous years do not establish a structure's condition today.

## How to reproduce or extend coverage

From the source root, load the ordinary game tile into a cache, or use the helper directly. Substitute the appropriate zoom-16 tile and cache paths. The helper reads current OpenFreeMap footprints and current official Overture PMTiles ranges. This release's receipts pin October 21, 2026. Newer data may produce different candidates.

    bun install --frozen-lockfile
    bun helper/atlas.mjs tile /tmp/road-atlas-cache 18166 27914 > /tmp/miami-base.json
    bun lab/lidar/footprint_candidates.mjs /tmp/road-atlas-cache 18166 27914 /tmp/footprints.json
    python lab/lidar/verify_footprints.py /tmp/footprints.json miami-dade-2021 /tmp/pointcache /tmp/verified.json

The offline verifier uses Python with `numpy`, `pyproj`, `shapely`, `laspy` and `lazrs`. The second source ID is `ma-central-eastern-2021`. A new public survey can be registered with its own acquisition period, horizontal CRS from EPT metadata, documented vertical units, data license, campaign link and EPT address. The verifier accepts the public USGS/NOAA EPT stores and stops a run over 14 requests, 6 MB transferred or 24 local point-cloud nodes. The resulting JSON includes SHA-256 hashes of its exact project metadata, hierarchy and compressed point nodes. Review those measurements before adding the output certificate fragment under the tile key in `helper/footprint-surveys.json`. No extra library or lidar download is required during gameplay.

**Matching.** A simple, above-ground, single-part polygon must occupy 16–2,000 m² and end at least 3 m inside its tile. Reject any intersection or approach within 2 m of the existing building polygons, including hidden buildings, parts and neighboring tile-owned outlines. The footprint must clear each game's road half-width plus 1.5 m and every other Overture footprint by 1.5 m. A recognized whole-record geometry source and license are required. The current outline must reproduce the verified ring hash. Godot also compares the original vector file fingerprint before appending to the rendered base rows. Changed geometries and additions already present in a new base map therefore abstain. Ordinary height, use and material updates continue to address the original stable indices.

**Measurement.** The verifier uses classified building returns inside the outline, nearby classified ground within a 7 m buffer and a 4 × 4 coverage grid. It requires ≥120 classified roof returns, ≥45 ground returns and a minimum of three roof returns in ≥80% of the eligible cells, covering at least five cells. The difference between 90th and 10th percentile nearby ground elevations must be ≤1.5 m. The difference between median roof and ground elevations must be 2–85 m. Catchment distances are in the source projection's horizontal metres. Web Mercator stretches ground distances locally. The estimated footprint confidence, training dataset version and download date are never treated as a field survey or measurement date.

**Geographic check.** In the five previously selected districts, the initial overlap, road and border checks yielded eight candidates in Bethel, Maine, two in Miami, one in Allston, one in Lincoln, Nebraska, and none in Lewisburg, West Virginia. The Bethel and Lincoln detector records supplied no confidence value. The Allston detector predicted 1.77 m for a structure whose verified lidar height is 4.39 m. Only the two structures in the table have passed the complete verification process for this release.

The two lidar study areas used five additional cached-node misses totaling 1,585,403 bytes during authoring. Both certificate runs replayed offline with zero requests. The five study districts reused existing Overture range and baseline caches. Runtime uses the ordinary optional building-enrichment request, adds no lidar network traffic, and preserves road loading during upstream outages. The base road atlas retains its independently streamed roads, labels and location controls.

- The [receipt file](verified-footprint-receipts.json) records source URLs, classification counts, heights, rejected candidates and file digests.
- Microsoft ML Buildings and Overture Maps Foundation supply the geometry under ODbL 1.0. [Dataset](https://github.com/microsoft/GlobalMLBuildingFootprints) · [Overture buildings theme](https://docs.overturemaps.org/guides/buildings/)
- Miami-Dade County ITD and NOAA Office for Coastal Management distribute the CC0 Florida survey. Flight dates spanned April through June 2021. [Source](https://www.fisheries.noaa.gov/inport/item/76028)
- USGS 3DEP and MassGIS distribute the spring 2021 Central and Eastern Massachusetts lidar. [Survey](https://www.mass.gov/info-details/massgis-data-lidar-terrain-data) · [USGS terms](https://www.usgs.gov/faqs/what-are-terms-uselicensing-map-services-and-data-national-map)
