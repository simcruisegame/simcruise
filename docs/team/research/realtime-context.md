# Real-time context for fine details (team answer, 13 November 2026)

Owner question (Aaron): "How do we context engineer in real time better to
create the fine details?" Team round: gis-data-scientist, magician,
lighting-director, performance-engineer, game-developer. Code reading only;
nobody ran the game. Costs marked (estimate) are not measured.

## The short answer

Today each generator (palms, furniture, grass, roads) guesses on its own. The
place graph makes a plan per building only, keyed by tile. Fix this with
**one context record per road segment**, built off the game thread before
the car arrives, that every detail generator and material reads. Then feed
it the road-edge facts that are missing today, and spend the detail where a
driver looks.

## Ranked plan

| # | What | Why | How it runs as the car moves | Cost |
|---|---|---|---|---|
| 1 | One context record per road segment (about 32 m): surface, wear, edge type, land use left/right, climate, season, species weights, a fixed seed | Generators ignore the graph today: palms use only a tile hash (`SimCruiseTileDetail.cpp:1243-1255`), furniture reads its own data (`SimCruiseStreetFurniture.cpp:266`), grass dryness is a command-line flag (`SimCruiseWorldTile.cpp:377-381`) | Built in the existing worker bake (`SimCruiseTileDetail.cpp:373`), cached by (tile, rules version, data hash); generators only read it | ~2 days |
| 2 | Fetch the road-edge tags from OpenStreetMap: surface, smoothness, shoulder, sidewalk, kerb, lit, guard rail / fence / wall, maxspeed; plus poles, power lines, signs, street lamps, culverts snapped to the road | Road rows carry only class, name, points, width; none of the roadside is fetched (`SimCruisePlaceFacts.cpp:75`) | Same Overpass query, cache and back-off as today (ODbL); stored as (road, station, side); rules fill gaps at real spacing | S |
| 3 | Real climate and land cover: Köppen class (Beck et al. 2023, CC BY 4.0), USGS NLCD land cover (public domain) | Climate is a latitude band today (`SimCruisePlaceGraph.cpp:276-280`); this is why fields are neon green in November | One value per tile / per road side, baked into the record | S-M |
| 4 | Light-correct inputs: an albedo range per material (asphalt 0.05-0.12, concrete 0.25-0.35, grass 0.08-0.25, white paint ≤ 0.8); remove sun and shadow already baked into aerial photos before use | Stops "too bright" and neon detail; photos lit twice look fake | Clamp generator output in linear space; normalise each tile once on a worker | ~0 GPU |
| 5 | One global material parameter set: wet, frost, season hue, dust | Live weather loops over every material every 3 s today (`SimCruiseLiveWorldSubsystem.cpp:510-525`) | Wet darkens to ~0.6 of dry albedo, lower roughness | ~0 ms |
| 6 | Per-instance context to materials: 4-8 floats in ISM custom data, a small RGBA8 context map per tile | Tile-wide values only today (`SimCruiseWorldTile.cpp:363-381`) | Written when the tile builds | ~1 day |
| 7 | Spend detail where the driver looks: road edge 5-40 m (lower third of screen, gives the sense of speed); look-ahead 15-80 m around the curve's tangent point first | Land and Lee 1994; Gibson 1950. Detail > 150 m off the road, above the horizon, or > 40° to the side at speed is wasted | The record also takes speed, heading and the route 2-3 s ahead | low |
| 8 | No pop-in: make detail at least 4 s ahead (110 m at 100 km/h), fade 0.3-0.5 s, never inside a 10° cone around the road ahead; swap behind masks | Change blindness needs a mask (Simons and Levin 1997) | Order and timing only | none |
| 9 | Run the AI on the CPU in route order, not newest-first | The queue is last-in-first-out (`SimCruiseNeuralDetail.cpp:340`), so work for tiles behind the car still runs | Score cells by arrival time on the route; drop passed cells; ≤ 2 cores, ≤ 1.5 GB; install ≤ 1 ms game thread per frame, one mip per frame | — |
| 10 | Stable seeds | Building seeds hash tile id + row index (`SimCruisePlaceGraph.cpp:709`), so a refetch changes every building | Seed from OSM id or quantised position | ½ day |

## Budget for the one AI detail layer (28 W laptop)

- GPU: ≤ 1.5 ms (estimate). AI inference stays on the CPU (NNE default).
- CPU: ≤ 2 worker cores, ≤ 1.5 GB RAM.
- Game thread: ≤ 1 ms per frame; no frame > 33 ms.
- Use our own C++ with instanced meshes; no runtime PCG, no Nanite foliage until a 28 W profile says yes.

## Tests

- 95 % of cells finished before the car is within 300 m.
- Second run of the same drive: zero new AI inference (cache works).
- Same place, two visits: same details (stable seeds).
- Daylight capture: generated patch luma / albedo within ±10 % of the plain material.
- Pop-in button test on the pre-flight video: fewer seen swaps than today.
