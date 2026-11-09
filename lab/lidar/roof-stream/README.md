# Streamed roof geometry

The native game starts with roads, terrain and the existing building model. The background building worker then requests one roughly block-sized neighborhood near the moving car, at most once per 30 seconds. It uses the USGS 3DEP work-unit index, a local index of public mirror directories, and the octree bounds to choose a few compressed point-cloud nodes. The laz-perf WebAssembly decoder ships inside the local helper. Players can use this without Python, a provider account, cloud AI or a downloaded state.

Only the anonymous EPT mirror is used. The raw requester-pays bucket and the Bismarck municipal data are outside this runtime. WESM metadata and immutable point nodes are cached separately. Per-host requests from helpers on this Mac are spaced by at least one second. The provider's Retry-After cooldown, negative-result caching, derived-geometry caching and the shared 96 MiB disk budget also apply. Cap per survey job is four attempted point-node URLs, three decoded nodes, and 2.25 MB of accepted compressed node bodies. Survey and tree-level metadata are extra. A canceled or failed download can still consume some bandwidth. Aggregate nationwide player traffic has not been load-tested.

Unclassified or building-coded returns are compared to the mapped footprint. Classified ground outside the outline provides a local sloped datum. Surfaces need spatial coverage, roof-edge evidence and agreement between fitted and reserved cells. A plane, gable or hip is triangulated against the actual footprint and holes. The worker installs the result only if the same tile instance and full mapped rings still match. A more detailed packaged local survey takes precedence. Survey dates vary and recent construction, dormers, tree cover, steep terrain and inaccurate footprints can defeat the fit. Vertical updates do not supply siding, windows or a street-level photo texture.

## Field observations

On twelve study districts, the ordinary selector accepted 41 of 202 candidate footprints. Seven districts supplied at least one roof. It withheld all candidates in the Hartford, Honolulu and Seattle patches. The Bismarck and Choteau patches had no matching mirrored survey. Initial city samples and auxiliary sites are identified in street-probes.json. These are selected patches and the counts do not estimate national coverage.

The independent score uses exact triangles rendered by the helper. Unused octree nodes from the same flight survey supply separate laser returns. The comparison uses the ordinary unenriched map-based house gable or flat roof formula. Where a better packaged observation already exists in-game it takes priority. On eight Lincoln buildings, 387 independent points reduced median height error from 1.168 to 0.167 m and P90 from 2.287 to 0.700 m. One building's P90 worsened. On Raven Wing Drive, 34 returns on one house scored 1.933 to 0.276 m. On an Anchorage roof, 28 returns scored 1.865 to 0.042 m. An independent flight or contemporary survey would provide stronger validation.

None of 151 displaced control footprints with clearance from known mapped buildings qualified. A harder unscreened displacement test produced five admissions in Lincoln after a uniform 13 m displacement along a closely spaced residential row. The model cannot establish the identity of a mapped building from a similar-height roof alone.

Two licensed August 2025 camera positions on the same Nebraska street were visited using the native app. The eastmost view showed zero pixels changing as its sightline missed the eight roofs. Moving 27 m west, a camera with reported 5 m accuracy, 91° heading and 74° horizontal field of view exposed the right-side ridge and wall changes. Its two baseline frames were pixel-identical. The trees, street furniture and morning flare still differ substantially. See the photographic comparison in ../../holdout/views/lincoln-lidar.

## Reproduce

From the source root run `bun install --frozen-lockfile`. The following commands use a separate directory for downloaded public data. Only the offline audit requires NumPy and Shapely.

```sh
mkdir -p /tmp/sunday-roof-audit
bun lab/lidar/roof-stream/replay.mjs /tmp/sunday-roof-audit Lincoln Raven Anchorage Bartlesville
cd /tmp/sunday-roof-audit
python /absolute/path/to/lab/lidar/roof-stream/score.py Lincoln Raven Anchorage Bartlesville
```

The replay checks current footprints against the recorded tile and survey, then reconstructs the profiles and fetches the independent audit nodes explicitly listed in audit-nodes.json. Footprints can change as OpenStreetMap is updated. The native-helper.json receipt records separate cold- and warm-cache runs of the compiled Apple Silicon helper. Use `python lab/lidar/roof-stream/catalog.py helper/lidar-projects.json` to refresh the mirror directory names. The game performs neither an S3 directory listing nor an independent audit while driving.

[USGS metadata](https://index.nationalmap.gov/arcgis/rest/services/3DEPElevationIndex/MapServer/24) · [Public EPT mirror](https://registry.opendata.aws/usgs-lidar/) · [Entwine Point Tile](https://entwine.io/en/latest/entwine-point-tile.html) · [laz-perf](https://github.com/hobuinc/laz-perf) · [Roofer authoring experiment](../roofer/README.md)
