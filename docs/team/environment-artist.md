# Environment artist: study notes

## Rules
- Judge every frame against a licensed real photo of the same place, time and season.
- Road design is real: lane widths, shoulders, ditches, guard-rail ends, sign heights and spacing come from the standards, not taste.
- Vegetation follows region and season (climate class and land cover), never one green for everywhere.
- Detail goes where the driver looks: road edge 5-40 m and the curve ahead; nothing pops in view (magician notes).

## Data
- Signs, markings, delineators: MUTCD. Guard rails, clear zones: AASHTO Roadside Design Guide. Lanes, crowns, banking: AASHTO Green Book.
- Land cover: USGS NLCD (public domain). Climate: Köppen-Geiger 1 km (Beck et al. 2023, CC BY 4.0).

## Sources
- FHWA, Manual on Uniform Traffic Control Devices: https://mutcd.fhwa.dot.gov/
- AASHTO, *Roadside Design Guide*, 4th ed., 2011; *A Policy on Geometric Design of Highways and Streets* (Green Book), 7th ed., 2018.
- USGS NLCD: https://www.usgs.gov/centers/eros/science/national-land-cover-database
- H. E. Beck et al., "High-resolution (1 km) Köppen-Geiger maps for 1901-2099", *Scientific Data* 10:724, 2023.

## What I now know (onboarding, 2026-11-14)

**How my area works today.**
- Art Deco is not the default. Two hard boxes gate it. `StyleForPlace` (Source/SimCruise/Private/World/SimCruiseTileDetail.cpp:219-227) returns MiamiBeachDeco only for 25.763-25.800 N, 80.146-80.125 W. The graph region `miami_beach_deco_district` in data/knowledge/place-graph.json uses the same box.
- Building recipe (`BuildBuildings`, SimCruiseTileDetail.cpp:474-503): the graph plan picks `recipe:deco`, `resort_tower`, `house` (height <= 14 m), `main_street` or `downtown` (>= 20 m). If no recipe gets >= 0.5, the building is a plain flat box. Storey heights are 3.1 m (deco), 3.0 m (house), 4.2 m (main street), 3.8 m (downtown) and 3.2 m (plain). The default chances are pitched roof 0.85, cornice 0.8, curtain wall 0.6 and tile roof 0.1 (:1067).
- The graph has 94 rules and 16 palettes. It names `recipe:mediterranean` and `recipe:industrial`, but the C++ has no code for them, so those buildings fall back to plain boxes. Wall colour comes from the strongest `palette:` fact if its confidence is >= 0.3 (SimCruisePlaceGraph.cpp:701-716). Otherwise it comes from 12 paints and 4 bricks (TileDetail.cpp:173-189). All flat roofs are one grey, 0.42/0.41/0.39 (:1113).
- Place facts come from 15 hand-drawn region boxes (`RegionFacts`, SimCruisePlaceGraph.cpp:268-282). Where no box sets a climate, a latitude band does: < 23.5 tropical, < 35 humid subtropical, < 55 humid continental. The building form uses the raw height (:590). A 5 m "unknown" height becomes `form:lowrise`.
- Trees: crown positions and heights come from Meta CHM v2, which covers the whole globe (helper/canopy.mjs:52-61). Only two species meshes exist, `leaf` and `pine` (SimCruiseTrees.cpp:374), plus coconut palms. `ConiferShare` (:292-304) is a lat/lon rule. It gives 0.9 west of 117 W and north of 40 N. West of 102 W it gives 0.9 above 1500 m and 0.5 below. North of 44 N it gives 0.55, in the coastal south-east 0.45, and elsewhere 0.15. The tree code does not read the place graph.
- Palms: `IsCoconutPalmRegion` (TileDetail.cpp:230-237) is a box for South Florida and a box for Hawaii. Inside these boxes, half of all canopy crowns become coconut palms (Trees.cpp:539-555). Lawns also get one palm per 10.5 m cell in 42 % of cells (`BuildPalms`, :1228).
- Shrubs: USDA RAP shrub cover and MRLC RCMAP shrub height (helper/shrub-cover.mjs:59-71). The data is used only for 24-50 N, 125-87.54 W.
- Ground colour: one green for vegetation, `float3(0.052,0.088,0.026)` (SimCruiseWorldResources.cpp:186). `GrassDry` is a command-line constant with default 0 (SimCruiseWorldTile.cpp:381, SimCruiseRegionalTerrain.cpp:400).
- Street furniture (SimCruiseStreetFurniture.cpp:244-340): the code draws mapped OSM items from the place-graph Overpass fetch. By rule it adds a hydrant every 91.44 m on curbed streets. Hydrant, bench and bin colours come from region facts (Florida yellow, Northeast, Midwest and California rules). Street lights (TileDetail.cpp:1260-1310) stand every 33 m on non-rural edges: 8.5 m davit-arm LED poles everywhere, and 5.2 m lanterns only in the Deco box.
- Guard rails (SimCruiseGuardRails.cpp:22-27) use only a terrain rule: the ground drops > 3 m within 8 m, and the run is >= 12 m. Posts are 1.905 m apart and the rail top is 0.79 m. Signals (SimCruiseSignals.cpp:26-29, 150-205) put a 6.4 m mast arm on each approach of every crossing of two trunk, primary, secondary or tertiary roads. Bridges: 0.85 m F-shape parapet, piers every 35 m (SimCruiseBridges.cpp:10-15).

**Does it work at a random US place?** (all desk study, estimate)
- General: crown positions (CHM v2), terrain-based guard rails, hydrant spacing, stop signs and the graph's house and main-street recipes work at any place. Miami Deco and palms stay inside their boxes. They do not leak into other places.
- Gilbert AZ (about 380 m, 111.8 W): `ConiferShare` returns 0.5, so half the trees are pines. There are no palo verde, mesquite or Washingtonia palms. Lawns are full green in November. Roofs are grey shingle, with tile roofs at the 0.1 default, because the arid rules only set `flat_roof`, which the code never reads. The code has no block walls. Edinburg TX has the same palm gap.
- Stonington ME: the region is `us_northeast`, humid continental, so `palette:brick_red` 0.55 is the strongest palette. Small white clapboard houses get red-brick walls. The conifer share of 0.55 is plausible.
- Bronx, Grand Concourse (40.837 N): the place is outside `lower_manhattan` (40.70-40.76), so it gets no `dense_core`. Mid-rise apartments with no POI get no recipe: brick boxes with a 0.5 m parapet and no cornice (cornices are only on main-street blocks).
- Casselton ND: tertiary section-line crossings get full mast-arm signals in open fields. Farm buildings that are large and low-rise get `recipe:industrial`, which has no code, so they become plain boxes. Lawns are green in November unless MODIS shows snow.
- Hilo HI: half the crowns become coconut palms and lawns get palm groves. The tropical rules set `flat_roof`, which the code ignores, so houses get grey shingle roofs. Rawlins WY, Delta UT and Anchorage AK are in no region box. They fall to the latitude band ("humid_continental", so brick red; Anchorage "subarctic", generic paints).
- Poor map data: the place-graph fetch is one tile at a time with a 3 s pause (SimCruisePlaceGraphSubsystem.cpp:300-330, 412), and no code rebuilds a tile when its facts arrive. On a first drive, the mapped furniture and the building records are often missing.

**Main gaps against "realistic everywhere".**
1. P1 | Tree species come from lat/lon rules, not data | SimCruiseTrees.cpp:292-304, SimCruiseTileDetail.cpp:230-237 | helper/forest-form.mjs (LANDFIRE EVT 2025 + NLCD) exists, but the game never calls it. There is no desert tree, no fan palm and no deciduous winter state.
2. P1 | Climate and region come from 15 boxes and a latitude band | data/knowledge/place-graph.json `regions` | WV and KY fall in "midwest". The Bronx is not dense. Wyoming, Utah and Alaska get no box. The humid-continental rule makes New England wood houses brick.
3. P1 | A signal at every crossing of two tertiary-or-higher roads | SimCruiseSignals.cpp:26-29 | OSM `highway=traffic_signals` is already in the tile's `street_objects` (helper/street-objects.mjs:17), but no C++ parses it. `building_finishes` (OSM wall and roof material and colour) is also not used.
4. P1 | One green and no season for ground vegetation | SimCruiseWorldResources.cpp:186, SimCruiseWorldTile.cpp:381 | NLCD class and date do not drive dryness.
5. P2 | Mapped guard rails and median barriers are off | helper/atlas.mjs:98 needs `--mapped-guard-rails`, but the game never sends it (SimCruiseAtlasClient.cpp:144). Snow fences, utility poles, mailboxes, delineators and fences or block walls do not exist.
6. P2 | Roofs | TileDetail.cpp:1067, 1113 | One flat-roof grey. Tile roofs appear only where the climate is Mediterranean. The code reads no OSM `roof:colour` or `roof:material` and has no metal roofs.

**My first three actions.**
1. Choose trees from data. Call `forest-form` (helper/forest-form.mjs) from SimCruiseTrees.cpp and map the EVT classes to the leaf, pine and palm meshes. Add a desert broadleaf and a fan palm for NLCD developed classes in arid places. Done when: `scripts\preflight.ps1 -Roads us-gilbert-az,us-hilo-hi,us-casselton-nd,us-stonington-me` logs the species counts per tile, Gilbert shows < 5 % pine, and each contact sheet matches a licensed street photo of that place.
2. Get climate from Köppen-Geiger 1 km data (Beck et al. 2023, CC BY 4.0) in the helper, not from boxes. Add rules for painted wood in New England coast towns and a dense district for the Bronx. Done when: `scripts\preflight.ps1 -Roads random -RandomCount 3` plus us-rawlins-wy and us-bronx-ny log a climate fact that matches the Köppen map, and Stonington house walls come from `painted_wood`.
3. Parse `street_objects.signals` in the atlas client. Put signals only at mapped OSM nodes. Put stop signs, not signals, at unmapped rural crossings. Done when: us-casselton-nd logs 0 signals at section crossings, and us-bronx-ny logs a signal count within 10 % of the OSM node count for the same tiles.

**Docs that are wrong or old.**
- docs/team/ownership.md rule 5 points to `docs/team/test-places.md`. That file does not exist. The list is data/test-places.json.
- docs/team/how-the-game-works.md, step 4, says the tile builder reads "tree species" from the place graph. The trees use `ConiferShare` and `IsCoconutPalmRegion`, not the graph.
- docs/unreal-architecture.md, Round 2, says "Elsewhere the generic palette stays". The graph palettes now replace it outside the Deco box (SimCruisePlaceGraph.cpp:707-716).
- The architecture doc names `recipe:house`, `main_street` and `downtown` as the recipes outside Miami. It does not say that the graph's `recipe:mediterranean` and `recipe:industrial` have no code.
