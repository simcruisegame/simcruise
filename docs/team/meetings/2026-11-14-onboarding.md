# Team onboarding and "works at a random US place" review (2026-11-14)

Owner's asks (00:47Z): "have the team read through the docs and code so they all get it", and "keep in mind the world generates as they drive anywhere in the US, so how are we going to achieve this everywhere; make sure you test random parts of the US".

All 16 experts read `docs/team/how-the-game-works.md`, the charter, the docs for their area and the real code for their area. Each wrote a "What I now know" section at the end of `docs/team/<role>.md`: how the area works (with file:line), whether it works at a random US place, ranked gaps, three first actions with a done-when test, and docs that are wrong. This was a desk study of main at fc6ddca. Nobody ran the game; judgments without a run are marked (estimate) in each note.

## What changed in this commit

- Charter rule 5 (`docs/team/ownership.md`): it must work at a random US place; hero roads are test cases only. "Launch and look" step 3 now adds `random` to the pre-flight roads.
- `data/test-places.json`: 18 ordinary US places (suburb, farm grid, city, desert suburb, mountain town, interstate, dense city, farm, small town, steep city, coast village, Great Basin, Appalachia, Ozarks, Central Valley, big interchange, Alaska, Hawaii).
- `scripts/preflight.ps1`: `-Roads us-<key>`, `-Roads random` (`-RandomCount`, default 3, seeded by `-Seed`, default today's date; one seeded shuffle that gives the same draw in PowerShell 5.1 and 7), `-Roads pool` (a seeded start-pool road). Each test place drives at its own local solar noon on the `-Utc` date unless `-Utc` is given (the fixed 17:00Z was twilight at Anchorage and a low sun at Hilo). The seed is in `report.md`.
- `docs/team/how-the-game-works.md`: the one-page overview every role reads first.

## How we make it work everywhere (the team's answer)

The world is already general in its base: Web Mercator tiles at any lat/lon, OSM roads, USGS 3DEP ground (all 18 test places have a 3DEP cell), aerial photos for the lower 48, Hawaii and SE Alaska, Meta CHM v2 tree positions, NOAA/Open-Meteo weather, the sun and moon for any place and time, and physics that reads no place rule. The gaps are where a rule is a hand-drawn box, a Miami constant or a missing data path. The plan is to replace each one with a US-wide data source or a rule that reads the place graph, and to prove it at random places every wave.

## Ranked findings across teams (desk study; each needs a run to confirm)

| # | Sev | Finding | Owner team (lead) | Where |
|---|---|---|---|---|
| 1 | P0 | Gauges turn with the wheel: the steering cut cylinder (axial -0.12..+0.09 m, 1.08 R) contains the NB speedometer (axial about -0.088 m, radial 0.194 m). Wheel off axis: the pivot is a vertex mean of the horn ring, not a circle fit of the rim. | Picture (technical-artist) with the car team | `SimCruiseCarModelRig.cpp:461-520, 1557-1650`; `CarPresentation.json` |
| 2 | P0 | No safety net for a missing or failed tile ahead: the far terrain has no collision, so the car can fall through where data is slow or fails. | Engineering (game-developer) | `SimCruiseWorldStreamer.cpp:532-559`, `SimCruiseRegionalTerrain.cpp:262` |
| 3 | P0 | Forward reach is 1 tile (294-576 m, set by latitude) at any speed; at 130 km/h a cold tile may not arrive in time; evicted tile jobs keep their helper slot up to 90 s. | Engineering (game-developer, performance-engineer, magician) | `SimCruiseTileManager.cpp:40-95`, `SimCruiseWorldStreamer.h:78-84` |
| 4 | P0 | No release gate needs a random-place pass; `playtest.ps1` accepts any PASS row. | QA and tools (tools-engineer, qa-playtester), Production | `scripts/playtest.ps1:54`, `RELEASE-CHECKLIST.md` |
| 5 | P1 | Climate and region come from 15 boxes and a latitude band: Wyoming, Utah and Alaska fall in no box; WV and KY are "midwest"; New England wood houses become brick. Use Köppen-Geiger 1 km (Beck et al. 2023, CC BY 4.0) and EPA ecoregions. | World (gis-data-scientist, environment-artist) | `data/knowledge/place-graph.json`, `SimCruisePlaceGraph.cpp:268-282` |
| 6 | P1 | Tree species are lat/lon rules (2 meshes plus coconut palms in 2 boxes); `helper/forest-form.mjs` (LANDFIRE EVT + NLCD) exists but the game never calls it. | World (environment-artist) | `SimCruiseTrees.cpp:292-304`, `SimCruiseTileDetail.cpp:230-237` |
| 7 | P1 | Surface: gravel only where OSM says `surface=unpaved`; untagged farm roads drive and sound like asphalt; no snow or ice grip; wet asphalt (0.70) sounds and rumbles like gravel. | Car team (vehicle-dynamics-physicist), World, Audio | `helper/atlas.mjs:130`, `SimCruiseWorldTile.cpp:96-141`, `SimCruiseRoadNoiseSynth.cpp:80` |
| 8 | P1 | Rural minor roads never bank: the code tests class "unclassified" but tiles say "minor". | World (gis-data-scientist) | `SimCruiseRoadNetwork.cpp:56, 898` |
| 9 | P1 | Engine power ignores air density: full sea-level power at 2100 m (Rawlins; real loss about 19 %, estimate). C8 and Mustang engine braking is far too low on mountain descents. | Car team (automotive-engineer) | `SimCruiseVehicleSim.cpp:755`, `CarDefinition.h:173-177` |
| 10 | P1 | Haze and ground fog are referenced to sea level, so inland fog and smoke vanish; the cloud layer is a fixed 1.2-7.2 km above sea level. | Picture (lighting-director) | `SimCruiseEnvironment.cpp:254-331` |
| 11 | P1 | Trees pop: no fade, 3 LOD rings overlap, a whole synchronous rebuild on every canopy arrival and rebase (hitch in dense forest). Traffic spawns in view at 160-260 m and vanishes in view at 320 m. The landscape mask is a fixed 550 m, not the tile size. | World (magician), Engineering (performance-engineer) | `SimCruiseTrees.cpp:425-639`, `SimCruiseTraffic.cpp:776, 898`, `SimCruiseRegionalTerrain.cpp:68-78` |
| 12 | P1 | Camera: a view jump on every rebase (every 2 km); chase collision is a line trace that pulls in at once on every tree trunk. | Picture (cinematographer) | `SimCruiseGameMode.cpp:690-706`, `SimCruisePlayerCameraManager.cpp:198-217` |
| 13 | P1 | "Surprise me" and the default start reach only 36 scenic roads; a random place has no name and no destination; search has no US bbox or location bias. | Design (game-designer) | `SimCruiseGameMode.cpp:185, 376`, `helper/atlas.mjs:240` |
| 14 | P1 | Pad steering ignores surface grip (half stick asks 0.89 g on gravel or snow); pad apply rate about 1510 deg/s at the hand wheel when parked; HUD Glance fade fails contrast over concrete and snow. | Design (ux-accessibility) | `SimCruiseDriverShaping.cpp:58-94, 166`, `SimCruiseHUD.h:27` |
| 15 | P1 | Pre-flight cannot see a thin world (blank ground, no buildings, empty roadside), pops past 150 m, trees, or the packaged build; random-place runs are not repeatable (shared cache). | QA and tools | `SimCruisePreflight.cpp:76-99`, `SimCruisePreflight.h:96` |
| 16 | P1 | No ambience anywhere; Mustang and C8 share one V8 voice. | Audio (sound-designer) | `Audio/`, `CarPresentation.json:818, 926` |
| 17 | P2 | Signals at every tertiary-or-higher crossing (even in North Dakota fields) though OSM signal nodes are already in the tile; one street-light spec for the US; Miami sand and water colours everywhere; lawns always green. | World, Picture | `SimCruiseSignals.cpp:26-29`, `SimCruiseWorldResources.cpp:186, 301` |

## Next step

Each owner thread takes its rows. The first random-place pre-flight wave (`scripts\preflight.ps1 -Roads ocean-drive,dragon,nebraska,big-sur,random -Cars nb2001_aaron,meridian_z,mustang65`) confirms or rejects the estimates, and every sheet is read before any row is called fixed.
