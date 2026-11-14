# The start of a drive: order and times (v2.0.3)

What happens between choosing a place and the car on the road (`ASimCruiseGameMode::TryPlacePendingPlayers`,
`ASimCruiseWorldStreamer`, `SimCruiseParkedCars`, `SimCruiseCarAssets`). Times: editor `-game` runs on the
release-test PC (release test 20261114-0725-53b290c and the runs before it); a packaged game builds car
models faster.

| # | Step | Where | Time |
|---|---|---|---|
| 1 | The place starts: the start tile and its four corners fetch and build together (start boost: five at once; edge neighbours never at the same time) | helper processes, worker threads | 5-12 s each, in parallel |
| 2 | Once three start tiles are under way (or after 4 s): the player's car model | game thread | light car 0.2-2.6 s; a Miata (38 MB) 11-13 s on a quiet PC, 30-37 s on a busy one |
| 3 | Then the background car models, one a frame: every model a parked or traffic car can be (`SimCruiseParkedCars::AllCarIds`), light ones only: Corvette, Grand Cherokee, Model 3, F-150 Raptor, CR-V, Odyssey, '65 Mustang | game thread | about 5.5 s together |
| 4 | Meanwhile the edge tiles fetch and build as the first five finish (installed between frames) | helper, workers; install on the game thread | 5-12 s each |
| 5 | The car is placed once the 3 x 3 start tiles have collision and step 3 is done (180 s at most): the best start by the start rules, confirmed by a ground trace; then two tiles at a time again | game thread | under 1 s |
| 6 | Traffic and parked cars appear with models already built | game thread | no build |

Expected start: light player car, about 20-30 s (the tiles are the long part); a Miata as the player's car,
about 35-45 s on a busy PC (its build overlaps the tile loads). Before: 98-100 s in Detroit and Doraville
(a parked Miata built mid-load, then the player's Miata after the tiles).

Rules that keep it short:
- **Nothing builds twice.** Built models are cached by car id; step 3 finds the player's model (step 2) in the
  cache when the player drives one of the background cars. The list has each model once (a fast test checks it).
- **No heavy unbuilt model is waited on.** A background car never takes a model over 20 MB of geometry that
  nothing has built (`SimCruiseCarAssets::BackgroundModelMaxBytes`): the Miata's share parks or drives a lighter
  car, so the list in step 3 never holds it. When the player drives the 1999 Miata, it is built in step 2 and
  the background cars may use it from then on (the 2001 Miata has its own id, so the 1999 one stays out).
- A parked car on a tile installed before step 3 reaches its model builds that light model then (once).

Tests: `SimCruise.World.Start.TilesTogether` (five start tiles, no two edge neighbours),
`SimCruise.World.Start.NoHeavyBackgroundCars` (the start list: each model once, light or already built; the
Miata's share parks a lighter car), `SimCruise.World.Start.OnRoadNotDeadEnd` (the start rules).
