<!-- Random-place drive campaign, 2026-11-10: six testers, 24 real places in every US region, RoadDrive at 0.4 g, Ultra captures, Panoramax references where available. Statements marked unverified are hypotheses. -->

# SimCruise field test: 24 places on real US roads

The six testers drove 24 places with RoadDrive at a lateral target of 0.4 g and 20 m/s unless stated otherwise. Only 3 places had a usable real photo: Allston, Two Harbors and Moab. Every other "real life" comparison comes from general knowledge and is unverified.

## 1. Places driven

| Place | Road | Result |
|---|---|---|
| Stowe VT | VT-108 | All 3 runs finished. Miata 2.4–3.2 g jolts and air at a bridge end. Bison 3-wheel air. |
| Allston MA | Cambridge St | **All runs failed**: wall hit, or a car hanging with no wheel contact. |
| Allston MA | Western Ave | **All runs failed**: 16.8 g / 11.3 g dead stop at a bridge face. |
| Storm King NY | NY-218 | Miata rev **spun**, 7.3 m off road. Miata fwd and tora_rs clean. |
| Tail of the Dragon TN | US-129 | Miata **spun** in 3 of 4 runs (impact, stuck, 11.3 m off road). Corvette, Mustang, kestrel and bison clean. |
| Vidalia GA | US-280 | Finished. 1–1.8 g jolts and air at overlapping trunk strips. |
| Brickell, Miami FL | Brickell Ave | Finished. Up to 5.1 g and air with both cars. |
| Breaux Bridge LA | Rees St | Clean. |
| Cottonwood Falls KS | K-177 | Clean. |
| Millersburg OH | OH-39 / unnamed primary | Clean, except one wheel lift at a square corner. |
| Two Harbors MN | MN-61 | Clean. |
| Galena IL | US-20 | Clean. Meeker St has a 0.9 m step (measured only). |
| Bozeman MT | N 7th / Main | First fetch: crash at a tile seam. Refetched: clean. |
| Moab UT | US-191 | **All runs blocked** on the bridge, both directions, both cars. |
| Independence Pass CO | CO-82 | Clean. Miata 7 bump-stop ticks. |
| Jerome AZ | AZ-89A | **All runs stopped dead** at the same junction, both cars. |
| Embudo NM | NM-68 | Clean. |
| Eureka CA | US-101 Broadway | Miata fwd stuck. Air, up to 2.2 g, kestrel 10 m off road. |
| Leavenworth WA | US-2 | Finished. Miata 0.5–0.74 g jolts, bison none. |
| Leakey TX | RR 337 | Miata fwd **spun and rolled**. 120 m jolt zone in every run. |
| Guthrie OK | US-77 | Finished. 2.0 g and a wheel lift at a 5 m/s corner. |
| Kearney NE | US-30 | Clean. |
| Frisco TX / Papillion NE | Residential, 11 m/s | Clean. |

## 2. Driving problems by root cause, ranked

**A. The 1999 Miata spins when the driver lifts off the throttle or brakes lightly. This is a crash, seen in 5 runs on 3 roads.**
- **Evidence:** On smooth roads (≤1.04 deg/m) it happens at 0.15–0.5 g with steering ≤0.06 and brake 0.02–0.07. Side velocity reaches 4–12 m/s. On the same roads, two other rear-drive cars (Mustang, Corvette), tora_rs, kestrel and bison stay clean. It ended in a rollover at Leakey and a car stuck off-road on the Dragon.
- **Cause (unverified):** Not yet known. Candidates are rear brake bias, how the diff locks when coasting, rear toe or camber, and tyre load sensitivity.
- **Contributing harness behaviour (shared by all cars):** The driver cuts the throttle from 0.5 to 0 in 0.1 s, and the speed plan raises the target speed again while the car is still sliding.
- **Fix:** Run scripted tests on a flat pad: hold a steady corner at 0.3/0.5/0.7 g, then lift, and separately apply a light trail brake. Compare yaw rate and slip angle with published NB data. Make this test a release gate for every car spec. Ramp the RoadDrive pedals at realistic rates.

**B. Static collision placed across a through road. This is a crash: 2 places, every run.**
- **Evidence:**
  - Moab: a service-bridge parapet ends inside the US-191 deck. It still blocks the car with guard rails turned off.
  - Jerome: guard rails from side streets run diagonally across the AZ-89A junction. With debug on, the car is touching two ConcavePolygonShape3D bodies.
- **Fix:** Clip every collision-bearing prop (rails, parapets) against the footprints of all drivable road strips. Add a tile validator that sweeps a car-sized box along every drivable centreline and fails on any hit.

**C. The tyre collider and `height_at()` disagree, and bridge ends do not blend into the approaches. Crash, about 8 places.**
- **Evidence:**
  - Allston: a bridge face of 1.5–2.7 m stops cars dead, and a car spawns under the deck.
  - Stowe: 0.48 m step at a bridge end.
  - Galena: a secondary bridge has no deck in `bridges[]`.
  - Moab: a kink at the deck end.
  - The collider-vs-`height_at` gap is 2–11.7 m on bridge tiles in MA, FL, KS, OH, IL and CA. Kearney has 24 deg/m in the collider against 0.28 in `height_at`.
- **Cause (hypothesis):** At Brickell, the spikes are probably geometry from the nearby I-95 decks.
- **Fix:**
  - Build both the collider and `height_at()` from one surface.
  - Ramp deck ends into the approach roads.
  - Give every road flagged as a bridge its own deck.
  - Add a validator that fails a tile when the two surfaces differ by more than 0.1 m along any road.

**D. Overlapping road strips with different crossfall. Major, 4–6 places.**
- **Evidence:** Vidalia has two 16 m trunk strips 12–16 m apart. The Eureka couplet split launches cars at 3–8 m/s. At the Millersburg and Guthrie corners a wheel lifts at 5 m/s; that cause is unverified.
- **Fix:** Merge overlapping strips into one graded junction surface. Set each road's width from its OSM lane count and one-way tag, not a 16 m default.

**E. Tile pipeline fails silently. Crash or major, 2 places.**
- **Evidence:**
  - Bozeman: a tile was emitted without 3DEP elevation data, leaving a 0.39 m seam.
  - Leakey: a road segment that crosses a tile from edge to edge has no interior vertex, so it is dropped from `road_surface` and cars drive on the creased raw elevation data.
- **Fix:** Reject or retry any tile that has no elevation source. Handle segments that cross a tile with no interior vertex. Run a self-check after each tile build.

**F. The Miata hits its bump stops on smooth roads. Minor, seen almost everywhere.**
- **Evidence:** 2–8 bump-stop ticks per run at 0.13–0.42 g. The other cars show 0.
- **Fix:** Check travel and ride height in nb1999.json against the real NB figures.

**G. Test harness limits.**
- RoadDrive uses 20 m/s everywhere instead of the road's speed limit.
- It joins one-way carriageways into a U-turn.
- Matching roads by name pulled in a motorway-class piece.
- The rail view picks the wrong spot.
- Miami never loaded in the Ultra view.

## 3. Visual realism gaps, ranked

1. **Road markings and lane counts are wrong or missing (about 10 places).** Two Harbors and Moab are drawn as 2-lane roads with a double yellow line; the photos show 4–5 lanes with white lane lines. Kearney US-30 has no markings at all. Edge lines are missing almost everywhere. **Fix:** Generate markings from OSM `lanes`, `oneway` and `turn:lanes`, with US defaults per road class.
2. **The aerial photo is draped onto asphalt and ground (about 12 places).** Shadows are baked in, there are purple smears and hard tile-boundary lines, and asphalt bleeds past the lane edges. The Allston photo shows uniform grey pavement. **Fix:** Use a procedural asphalt material on paved surfaces. Use the aerial only as a colour hint for land cover, after removing shadows.
3. **No street furniture (about 9 places).** There are no curbs, sidewalks, streetlights, utility poles, wires, signs or Jersey barriers. The Allston and Two Harbors photos show all of these. **Fix:** Place them by rules based on road class, urban density and OSM tags.
4. **Buildings are generic boxes (about 8 places).** Stowe, Bozeman, Jerome, Leavenworth, Eureka and Guthrie do not read as their real towns; this is unverified against photos. **Fix:** Regional facade sets chosen from region, building age and OSM tags, with storefront ground floors on commercial streets.
5. **Vegetation and terrain biome are wrong (about 7 places).** The Dragon slopes are bare, Leakey and Moab show lush lawns, the Moab photo shows red cliffs, and every place uses one broadleaf tree model. **Fix:** Drive tree species, density and ground material from a land-cover and biome map, and add rock cuts on steep cut slopes.
6. **Smaller issues:**
   - Rivers are flat sheets, with a cliff bank at Allston and lily pads at Embudo.
   - A stippled dither pattern appears at the Dragon and Leakey (cause unverified).
   - The renderer reports Forward Mobile even at Ultra quality.
   - Traffic bunches into convoys on remote roads.
   - Spawned cars overlap other cars.

## 4. Top 5 next tasks

1. Build the flat-pad lift-off and trail-brake test, find the cause of the Miata spins (A), and gate every car spec on it.
2. Build the drivable-corridor clearance validator and clip rails and parapets (B).
3. Build one shared surface for the collider and `height_at()`, with blended bridge ends and the 0.1 m gate (C).
4. Make the tile pipeline robust: reject tiles without elevation data, keep edge-to-edge segments, grade merged junctions and set widths from lanes (D, E).
5. Generate markings from OSM lanes and use procedural asphalt instead of the aerial photo on roads (visual gaps 1–2). Give RoadDrive per-class speed limits at the same time.
