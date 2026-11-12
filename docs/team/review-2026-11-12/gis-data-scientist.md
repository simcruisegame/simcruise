# Review 2026-11-12: gis-data-scientist

Scope: World/* (road network, tile streaming, terrain, seams, frame), the road under the tyres (ground query and grip), data licences and rate limits.
Method: code reading at the current clone, bundled tile data (`data/tiles/*.json`), existing automation tests. No build was run. Numbers marked [estimate] come from the code, not from a measurement.

Checks that passed:
- Seam contracts and the rule "edge neighbours never build at the same time" (`SimCruiseTileManager.cpp`, TakeRequests) prevent seam steps. `SimCruise.World.SeamContinuity` checks heights within 1 cm and edges within 2 cm.
- Crown and adverse-crown removal are correct (`SimCruiseRoadNetwork.cpp:883-888`). The sign of superelevation (positive lowers the right side on a right-hand bend in the x-east/y-south frame) is consistent.
- Lane widths by class are plausible (`SimCruiseRoadNetwork.cpp:372-378`).
- Mercator tile maths and the per-session tile size are correct. The scale error is under 1 % for drives under about 100 km at Miami latitude.
- OSM/ODbL attribution is shown in the atlas UI (`SimCruiseAtlasUI.cpp:339`). Sources and licences are listed in `docs/data-sources.md`. The helper backs off on 429/503 and sends a User-Agent (`helper/atlas-cache.mjs:76-80`).

## Findings (ranked)

### G-01 P1 Superelevation is forced to zero within 30 m of every tile seam
Evidence: `Private/World/SimCruiseRoadNetwork.cpp:879-882` multiplies E by a run-out that is 0 at any boundary end (`bStartOnBoundary`/`bEndOnBoundary`), and `:903` sets `St.E = 0.0` on a contracted end. Tiles are about 550 m wide in Miami (`SimCruiseGeo.cpp:23`).
Impact on driving: a banked curve that crosses a tile edge goes flat for about 60 m (30 m each side) and then banks again. On motorway, trunk, primary and secondary curves the driver feels the car lean out, then settle, in the middle of the bend. This happens at a fixed grid, unrelated to the real road.
Proposed fix: the first-built tile keeps its computed E at the seam (no run-out). It already publishes HL/HC/HR in the seam contract (`FSimCruiseSeamEnd`). The contracted tile copies HL/HR at the seam station and blends its own E toward the contract E over the 30 m span instead of running out to zero. Keep the run-out only where there is no neighbour tile at all.
How to verify: new unit test: a synthetic R = 150 m secondary curve that crosses a tile edge. Build tile A, then tile B with A's contract. Pass: |E(seam) - E(curve)| < 0.005, and the edge-to-axis relative gradient <= 0.6 % everywhere. Then drive the NB at 60 km/h through such a curve and log the roll angle; no dip at the seam.

### G-02 P1 Rural minor roads have no superelevation and keep an adverse crown on curves
Evidence: `SimCruiseRoadNetwork.cpp:48-53`: `DesignSpeedKmh` returns 0 for tertiary, minor, unclassified and service, so `E = 0` (`:850-868`). The comment cites AASHTO low-speed urban streets, but the rule also applies to rural roads. The code already knows town vs rural (`IsTown`, `:591`, used for the edge type at `:921-923`).
Impact on driving: winding country roads (the "favourite real roads" in the vision) are drawn with a 2 % crown that falls away on the outside of every bend. On an R = 80 m bend at 60 km/h the outside half needs about 0.37 side friction instead of about 0.30 with a 6 % bank [estimate, `docs/team/gis-data-scientist.md` section 7]. The car feels nervous and the bends look wrong.
Proposed fix: use the existing rural/town flag. Rural stations of every paved class get a design speed (from OSM `maxspeed` when the helper has it, else tertiary 70 km/h, minor/unclassified 50 km/h) and emax 0.08. Town stations keep the current rule (crown kept below 60 km/h).
How to verify: unit test on a synthetic rural `minor` curve, R = 80 m: E > 0.03 toward the inside. Re-run `SimCruise.World.SeamContinuity` and `SimCruise.World.RoadCollision` (must still pass).

### G-03 P1 Seam pin can lift the road by tens of centimetres on hillsides, giving a crest at each seam
Evidence: `SimCruiseRoadNetwork.cpp:812-822`: without a contract, the seam section is raised so that its whole width plus 0.5 m clears the highest ground under it plus 7 cm. The lift fades over 30 m (`:874-878`). The first-built tile of every pair has no contract, so every seam gets this lift.
Impact on driving: on flat Miami data the lift is small. On a 10 % cross slope with a 4 m half width the lift is about 0.6 m, a grade change of up to 3 % over 30 m [estimate]. The car crests and dips every 550 m on hill roads. No test can see this today (see G-04).
Proposed fix: bound the lift and lengthen the fade. Lift only the low edge (the high edge can cut into the ground, and the terrain under a seam road is conditioned by both tiles from the shared natural ground along the seam). Choose the fade length from a vertical-curve rule, L >= K x A, so the grade change stays below 1 %.
How to verify: hilly fixture (G-04). Pass: max |d2z/ds2| within 50 m of a seam is no larger than 1.5 x the median of the same road away from seams.

### G-04 P1 All world tests use flat terrain; road geometry on hills is untested
Evidence: every bundled tile is Miami Beach (`data/tiles/18178_27905.json`: heights -0.12 to 4.96 m). `SimCruiseWorldTests.cpp:58-82` (BuildAll), `:240-347` (SeamContinuity), `:349-405` (RoadCollision) all use these tiles.
Impact on driving: profile fit, grade, seam pins (G-03), superelevation (G-01, G-02) and bridge spans are not tested where they matter most: hill and mountain roads.
Proposed fix: add a 3 x 3 hilly test fixture under the existing bundling rules (ODbL derivative, 3DEP public domain), or a synthetic generated tile set with a known road. Keep it small.
How to verify: the new fixture runs in `SimCruise.World.*` tests. Add the checks from G-01 and G-03, and a grade sanity check (max grade per class).

### G-05 P1 Wheel ground traces run on the physics thread against the game-thread scene; a rebase can give one wrong step [speculation, needs a test]
Evidence: the ground query lambda calls `World->LineTraceSingleByChannel` from `OnPreSimulate_Internal` (`SimCruiseVehicleMovementComponent.cpp:262-283`, `:313`). Rebase moves all tiles on the game thread with `TeleportPhysics` (`SimCruiseWorldStreamer.cpp:343-355`) and then moves the car (`SimCruiseGameMode.cpp:636-652`). An Epic forum answer states that scene queries outside the game thread are not supported yet.
Impact on driving: if a physics step runs between the tile move and the car move, the wheel rays see ground 2 km away: no contact or a wrong height for one or more steps. The driver may feel a jolt every 2 km.
Proposed fix: first measure (see verify). If confirmed: carry a rebase generation into the physics state and skip ground hits whose generation differs, holding the last contact for that step; or, because UE 5 uses double positions, raise `RebaseDistanceM` (`SimCruiseWorldStreamer.h:84`, 2 000 m) to 10-20 km so rebases are rare. Coordinate with performance-engineer finding P-04.
How to verify: automation test with the real vehicle pawn at 30 m/s across the rebase distance. Log per-step `WheelContactCount` and suspension loads. Pass: 4 contacts on every step, no load spike > 20 % across the rebase. The existing `SimCruise.World.Rebase` test (`SimCruiseWorldTests.cpp:574-624`) moves a point, not a physics car.

### G-06 P2 Surface grip relies on collision face order; no test covers it
Evidence: `SimCruiseWorldTile.cpp:196-226` builds the grip map assuming the cooked collision numbers faces section by section, in section order. `SurfaceGripAtFace` (`:92-137`) uses `Hit.FaceIndex`. A grep finds no test that calls `SurfaceGripAtFace` or checks grip under a trace. The trace uses `bTraceComplex=false` with complex-as-simple collision (`SimCruiseVehicleMovementComponent.cpp:423-424`).
Impact on driving: if cooking removes degenerate triangles or remaps faces, a tyre on asphalt can read gravel (0.7) or ground (0.8) grip, or the reverse. The driver feels random grip loss.
Proposed fix: add an automation test only (no code change unless it fails).
How to verify: install a bundled tile with async cooking off, trace down onto known asphalt, gravel shoulder (vertex colour R) and terrain points, and check grip 1.0 / 0.7 / 0.8.

### G-07 P2 The road-texture hook is never filled; roads feel perfectly smooth
Evidence: `FSimCruiseGroundHit::RoadProfileM` and `RoadProfileRateMps` (`Public/Vehicle/SimCruiseVehicleSim.h:73-74`) are used by the wheel model (`SimCruiseVehicleSim.cpp:473, 515`) but no code sets them (grep finds only the header and the sim). The mesh is smooth below about 4 m wavelength (2 m stations).
Impact on driving: no road texture in the steering, seat or sound. Real roads are ISO 8608 class A-C. This is a gap for "feels like the real car".
Proposed fix: next phase (see proposals). Not a bug fix.
How to verify: see proposal N-01.

### G-08 P2 Face normals from the trace are not smoothed
Evidence: the hit normal is `Hit.ImpactNormal` (`SimCruiseVehicleMovementComponent.cpp:275`): a triangle face normal. The tyre force frame uses it (`SimCruiseVehicleSim.cpp:513-543`). Ribbon quads are split into two triangles; a twisting quad (superelevation transition) gets a crease along its diagonal.
Impact on driving: small on the 2 m road mesh [estimate: under 0.5 deg]. Larger off the road on the 2.2 m terrain mesh that is a bilinear upsample of an 8.6 m grid (creases every 8.6 m).
Proposed fix: none now. Measure first: log the normal change per step at 100 km/h on a transition.
How to verify: telemetry plot of the contact normal; pass if the step-to-step change is < 0.2 deg on asphalt.

### G-09 P2 Superelevation design speed for motorways is low
Evidence: `SimCruiseRoadNetwork.cpp:50`: 90 km/h for motorway and trunk. US freeways are designed for 100-130 km/h.
Impact on driving: with the 0.5 V^2/(127 R) rule and the 6 % cap, most motorway curves still reach the cap, so the effect is small. Long gentle curves (R > 1 000 m) get less bank than real.
Proposed fix: 110 km/h for motorway, 100 km/h for trunk; cap 0.08 rural. Do this together with G-02.
How to verify: unit test on R = 1 000 m motorway curve: E about 0.05.

## Next phase proposals

### N-01 Road surface texture from ISO 8608 (rank 1, size M)
Goal: fill `RoadProfileM` and `RoadProfileRateMps` with a deterministic, world-anchored road profile.
Why for the vision: the driver feels the road through the wheel and the seat. A real Miata on a real road is never on glass.
Scope: a seeded ISO 8608 profile per lane line (class by road class and surface tag: motorway A, primary B, minor C, unpaved D), plus discrete events from map data where known (bridge joints, rail crossings, manholes later). Same value for the same world position across tiles and rebases.
Acceptance test: a unit test computes the PSD of the generated profile and matches the class within one class band; a drive log shows vertical wheel acceleration at 90 km/h in a realistic range for the class; the profile is equal across a seam within 1 mm.
Risk: too much vibration at the 360 Hz step; force feedback noise. Start with a low class and a user scale.

### N-02 Curve-true road geometry on hill and mountain roads (rank 2, size M)
Goal: superelevation, grade and vertical curves that follow AASHTO on rural roads of every class, continuous across seams.
Why for the vision: "drive your favourite real roads anywhere" means winding hill roads first.
Scope: G-01, G-02, G-03, G-09 together, plus a hilly fixture (G-04) and checks for grade and vertical curve K.
Acceptance test: fixture tests pass (seam bank and grade continuity, e > 0 on rural curves, max grade per class); a drive on the fixture shows no roll or pitch event at seams.
Risk: changes the shared seam contract; needs care with existing bundled tiles.

### N-03 Use measured road geometry where it exists (rank 3, size L)
Goal: take crown and cross slope from 3DEP lidar (already used for "road camber" per `docs/data-sources.md:19`) and lane counts from OSM `lanes` tags for every road, not only markings.
Why for the vision: the real road, not a model of it.
Scope: helper side mostly; the game reads the measured E and crown per station when present and falls back to the model.
Acceptance test: on a lidar-covered test road, the built cross slope is within 0.5 % of the lidar fit.
Risk: lidar coverage varies; noisy points near cars and trees.

### N-04 Rebase-free world up to 20 km (rank 4, size S)
Goal: rely on UE 5 double precision and remove most rebases.
Why for the vision: fewer discontinuity events on long drives.
Scope: raise `RebaseDistanceM`, measure physics and rendering precision at 20 km, keep rebase as a fallback.
Acceptance test: a 20 km drive with no jitter in the car, the camera or the wheels (telemetry and video).
Risk: some engine paths still use float (e.g. some render-relative paths, Chaos internals) [unverified].

## Sources

- AASHTO minimum radius equation: https://www.webpages.uidaho.edu/niatt_labmanual/chapters/geometricdesign/professionalpractice/MinimumRadius.htm
- TxDOT superelevation transition: https://www.txdot.gov/manuals/des/rdw/chapter-9--mobility-corridor-facilities--5r-/9-2-cross-sectional-elements/9-2-10-superelevation-transition.html
- USGS 3DEP vertical accuracy: https://www.usgs.gov/faqs/what-vertical-accuracy-seamless-3dep-dems
- OSMF attribution guidelines: https://osmfoundation.org/wiki/Attribution
- Overpass usage guidance: https://dev.overpass-api.de/overpass-doc/en/preface/commons.html
- Epic forum on scene queries outside the game thread: https://forums.unrealengine.com/t/uprimitivecomponent-linetracecomponent-in-async-physics-tick/2349680
- ISO 8608 summary: https://link.springer.com/article/10.1007/s40534-017-0122-1
