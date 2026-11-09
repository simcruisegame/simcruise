# Princeton Road, Lancaster County, Nebraska

In the game, open **Find a place** and select **Princeton Road · Lancaster County, NE**. Typing “Princeton gravel” in the search box opens the same eastbound start.

The Miata drove east on the mapped right lane from 40.56724° N, 96.59090° W, just beyond South 96th Street. It crossed three previously unbundled z16 boundaries, then braked to 0.5 m/s before South 110th Street. The road runs between fields and has one mapped farm driveway. The controller aims for 13 m/s (29 mph). A posted speed limit was not verified.

**The road surface affected the car.** OpenFreeMap records Princeton Road as unpaved. Lancaster County Engineer independently identifies the complete section as gravel, feature 1636, surface code 3. The original decoder dropped the surface field, and all four tires received paved-road grip. The game now carries an explicit paved or unpaved tag to the wheel-contact query. The unpaved modifier of 0.76 reuses its existing off-pavement assumption. A local tire test would be needed to establish the real coefficient or stopping distance. [Source evidence and county query](sources.json) · [County road surface service](https://gis.lincoln.ne.gov/public/rest/services/CountyEngineer/RoadSurfaceType/MapServer) · [OpenMapTiles transportation schema](https://openmaptiles.org/docs/schema/#transportation)

The controller sends ordinary throttle, steering and full service-brake inputs to the five-speed assisted transmission. It selects the named street once at startup. The drive then advances entirely through 60 Hz Jolt integration. The harness measures horizontal displacement independently of the game odometer, wheel contacts, individual tire-grip observations, lane deviation, changes at each boundary, frame intervals and individual HTTP fetches. Optional live weather, recent satellite imagery and street photography were switched off for repeatability. The normal map, terrain, canopy, aerial and available building workers ran during the test.

| Observed result | Value |
| --- | ---: |
| Continuous distance through service-brake stop | 1,452.15 m |
| Tile boundaries crossed | 388.38 m, 853.02 m, 1,317.58 m |
| Minimum rolling wheel contacts | 4 of 4 on all 6,885 sampled rolling physics ticks |
| Largest vertical step on an actual crossing | 0.0066 m |
| Maximum lateral distance from right-lane target | 0.075 m |
| Tile-wait ticks / teleports during the drive | 0 / 0 |
| Rolling median speed | 12.84 m/s (28.7 mph) |
| Braking from 12.0 to 0.5 m/s, original → corrected | 8.35 → 9.93 m |
| Cold real-time physics run | 31 actual source requests, 1,589,999 declared response bytes |
| Warm fixed-step headless / warm Metal | 0 / 0 source requests |
| Warm 960 × 600 Metal median / p95 / p99 frame interval | 16.68 / 22.62 / 35.82 ms |

The cold run made six map requests including the manifest, two Terrarium elevation requests, eighteen canopy range reads and five USGS aerial requests. It drove across tile x=15184 through 15187 at y=24675. Source work may finish in a different order when a fixed-step headless test runs faster than wall time. The warm Metal run lasted 116.48 seconds through the stop and reached the same matched-speed stopping distance as headless. These frame intervals include reloading nearby terrain and three screenshot readbacks. They describe this Apple GPU, this scene and this resolution. [Run and seam measurements](measurements.json)

The shader now uses the registered aerial colour where an explicitly unpaved road has visible ground. It reduces baked lighting before the engine illuminates the road. Known loose road surfaces also suppress generated lane paint. At a mixed junction, existing painted features and a confirmed paved carriageway retain priority. At the farm access, the confirmed gravel continues across its unclassified entrance. The nearest mapped paved crossroad can still take precedence at a wheel contact. Older untagged tiles retain their existing rendering and grip. No camera, tire force curve or road elevation was changed for this route.

At 100 m the old material's median road RGB was [107, 107, 107], the new material was [204, 201, 190], and the registered national aerial image was [208, 202, 186]. This first patch guided the brightness check. At the later 770 m and 1,350 m viewpoints, mean absolute eight-bit channel difference fell from 86.4 to 5.8 and from 65.6 to 14.6 over 152 and 147 central road pixels respectively. These comparisons check whether the engine preserves the broad colour in its input imagery. They do not establish ground-level colour accuracy. KartaView and Panoramax returned no local street view. In the same survey, the county reference centerline runs about 8 m north of the visible road. The drive uses the OpenFreeMap centerline, which falls on the bright gravel strip in the aerial image. [Paired cockpit captures](princeton-road.webp) · [USGS aerial service](https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer)

## Reproduce the drive

Run these commands from the source root with Python 3, Bun and the Godot 4.7.2 editor. On macOS, set `GODOT` to the executable in `Godot.app/Contents/MacOS/Godot`. The runner snapshots the source into the scratch directory, installs the locked helper dependencies there and imports the project's assets. It creates a separate Godot QA user-data directory and uses a cache under the scratch directory. Use a new work directory for a fresh source snapshot or a cold cache.

```sh
python3 lab/sustained/run_route.py --work /tmp/sunday-princeton --godot "$GODOT" --name cold --mode realtime
python3 lab/sustained/run_route.py --work /tmp/sunday-princeton --godot "$GODOT" --name warm --mode fixed
python3 lab/sustained/run_route.py --work /tmp/sunday-princeton --godot "$GODOT" --name metal --mode metal
"$GODOT" --headless --path /tmp/sunday-princeton/game --script res://checks/SustainedSurfaceCheck.gd
```

Each run produces `result.json`, a decimated `trace.csv`, per-call `source.jsonl` and a network summary. The Metal option opens a separate background window and saves three driver-view JPEGs and their ray-intersection samples. The headless fixed-step option measures physics repeatability; its frame intervals do not measure GPU performance. The isolated user preferences leave the player's settings intact. The map and road surface come from © OpenStreetMap contributors through OpenFreeMap under ODbL. The county lookup supplied independent validation only. [OSM attribution](https://www.openstreetmap.org/copyright) · [OpenFreeMap](https://openfreemap.org/)

To repeat the registered aerial-color calculation, install NumPy and Pillow and run `python3 lab/sustained/score_aerial.py CONTROL_METAL_DIRECTORY CANDIDATE_METAL_DIRECTORY WORK/cache` using 960 × 600 matched views.
