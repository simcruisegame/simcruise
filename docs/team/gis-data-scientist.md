# GIS data scientist: study notes

Role: `gis-data-scientist`. Owner of real-world geodata, coordinate frames and the road geometry that the tyres drive on.
Written 2026-11-12 for SimCruise (Unreal Engine 5.8, C++).
Style: ASD-STE100 Simplified Technical English. Facts marked **[unverified]** were not confirmed from a primary source in this session.

## 1. Mandate

- Judge the data that builds the world: OpenStreetMap (OSM), Overture, elevation (USGS 3DEP, Mapzen/Tilezen, Copernicus).
- Judge the licences, the attribution and the rate limits of each source.
- Judge the coordinate frames: Web Mercator tiles, the local frame, the floating origin, double precision.
- Judge the road geometry: centreline, lane widths, crown, superelevation, grade, junctions, tile seams.
- Judge what the tyres feel: height, normal and surface grip under each wheel.
- Phase rule: the current phase is bugs and driving experience only. Do not propose new features unless the caller asks.

## 2. Key knowledge, distilled to rules and numbers

### 2.1 Web Mercator tiles and local frame

- A zoom-z tile is `C / 2^z * cos(lat)` metres wide on the ground. C = 40 075 016.686 m.
- Zoom 16 at the equator: 611.5 m. At Miami Beach (25.8 deg N): about 550 m.
- SimCruise uses one tile size for the whole session (`SimCruiseGeo::TileSizeMetres(StartLat)`, `Private/World/SimCruiseGeo.cpp:23`).
- Rule: the Mercator scale changes with latitude as `1/cos(lat)`. The relative scale error after a north-south drive of distance d is about `tan(lat) * d / R_earth`.
- Example: at 26 deg N, 100 km north gives about 0.8 % length error. Accept this for drives under 50 km. Check it for longer drives.
- Rule: the local frame is x east, y south, z up (tile frame). Keep the right-hand normal `R = (-T.y, T.x)` consistent everywhere.

### 2.2 Precision and the floating origin

- UE 5 uses Large World Coordinates (LWC): `FVector` is double. Chaos uses double positions.
- Single float has a 24-bit mantissa. At 2 km the step is about 0.12 mm. At 20 km it is about 2 mm. Both are below what a tyre can feel.
- Rule: a rebase is a discontinuity event. Each rebase moves every tile, the car and the cameras in one frame. Fewer rebases give fewer chances for a one-frame error.
- Rule: heights stored as `float` (tile `Heights`) are good to about 0.25 mm at 4 000 m. This is sufficient.

### 2.3 Elevation sources

- USGS 3DEP 1/3 arc-second seamless DEM: about 10 m posting. Vertical accuracy about 0.82 m RMSE (2022, against about 25 000 NGS OPUS points). Public domain. [USGS FAQ]
- USGS 3DEP 1 m DEM and lidar point clouds: public domain. Use for roads where available.
- The bundled Miami tiles use a 64-cell grid per tile (about 8.6 m cells), `h` with 4 225 values. Source string: "USGS 3DEP 1/3 arc-second".
- Rule: a 10 m DEM with 0.8 m noise can not give a road profile directly. Fit the profile along the centreline. SimCruise uses a Gaussian local-linear fit, sigma 16 m, one Tukey pass (`SimCruiseRoadNetwork.cpp:788-801`).
- Rule: a DEM does not see bridges, cuts and fills correctly. Treat mapped bridges as spans (SimCruise does this).
- Copernicus DEM GLO-30: 30 m, global. Licence requires attribution. [unverified: exact current licence text]

### 2.4 OSM, Overture and licences

- OSM data is ODbL 1.0. A Produced Work (the rendered game world) needs attribution. A Derivative Database (the bundled `data/tiles/*.json`) must be offered under ODbL.
- OSMF attribution guidelines: for games and simulations, attribution can be on a splash screen, in the game view, in the credits page or in the menu. Detailed information must be in a suitable location. [OSMF Attribution Guidelines]
- SimCruise shows the attribution in the atlas UI (`Private/Gameplay/SimCruiseAtlasUI.cpp:339`) and in `Credits.txt` / `docs/data-sources.md`.
- Overture buildings: ODbL for OSM-derived rows, CDLA/CC BY for other rows. Check each theme release notes. [unverified: per-row licence for 2026 releases]
- Rule: never bundle ShareAlike data into the MIT tree except the documented ODbL folders (`data/tiles/`, `data/observed/`).

### 2.5 Rate limits and terms

- Public Overpass instance: about 10 000 requests per day and about 1 GB per day per user. HTTP 429 means "slow down". [Overpass API User's Manual]
- Rule: cache every query. Back off on 429 and 503. Send an honest User-Agent. The helper does this (`helper/atlas-cache.mjs:76-80`).
- Rule: a long drive at 120 km/h crosses a zoom-16 tile about every 16 s. Count queries per hour per source before a release.

### 2.6 Road geometry (AASHTO Green Book and DOT manuals)

- Point-mass curve equation: `e + f = V^2 / (127 R)`. V in km/h, R in m, e and f as decimals. [AASHTO, via U. Idaho NIATT lab manual]
- Minimum radius: `Rmin = V^2 / (127 (emax + fmax))`.
- Typical emax: 4-6 % urban, 8 % rural, up to 10-12 % without snow and ice. [unverified here; standard Green Book values]
- Typical fmax: about 0.14 at 80 km/h falling to about 0.08 at 130 km/h. [unverified here]
- Normal crown: 1.5-2 % (SimCruise uses 2 %).
- Low-speed urban streets may keep the normal crown through curves (AASHTO low-speed urban method). Rural roads of every class are superelevated.
- Superelevation runoff: the edge rises relative to the axis at a maximum relative gradient of about 0.78 % at 20 km/h falling to about 0.40 % at 130 km/h. [unverified here; TxDOT confirms 0.50 % at 85-100 mph]
- Place about 2/3 of the runoff on the tangent and 1/3 on the curve. [unverified here]
- Adverse crown removal (tangent runout) first lifts the outside lane from -2 % to 0 %, then the section rotates. SimCruise does this correctly (`SimCruiseRoadNetwork.cpp:883-888`).
- US lane widths: 3.6 m (12 ft) freeways and arterials, 3.0-3.6 m other roads. SimCruise: 3.65 / 3.5 / 3.35 / 3.2 m by class (`SimCruiseRoadNetwork.cpp:372-378`).
- Maximum grade: about 3-6 % freeways, up to 8-12 % local roads in mountains. [unverified here]
- Vertical curves: length `L = K * A`. A is the algebraic grade change in %. K depends on design speed (stopping sight distance). [unverified here: K tables]

### 2.7 Road surface texture (what the tyre feels)

- ISO 8608 classifies road roughness by displacement PSD `Gd(n) = Gd(n0) (n/n0)^-w`, n0 = 0.1 cycle/m, w = 2. Paved roads are class A-D. [ISO 8608; Mucka 2017 summary]
- Class A geometric mean `Gd(n0)` is about 16e-6 m^3. Each class is 4x the class before. [unverified here: exact table]
- Rule: a mesh built from a 10 m DEM and 2 m stations is smooth below about 4 m wavelength. Without a texture model the car feels "on glass".
- SimCruise has a hook for this (`FSimCruiseGroundHit::RoadProfileM`, `RoadProfileRateMps`, `Public/Vehicle/SimCruiseVehicleSim.h:73-74`). Nothing sets it today.

## 3. How SimCruise feeds the road to the tyres

1. The helper builds a tile document (`helper/atlas.mjs tile`): roads, heights, buildings.
2. `FSimCruiseRoadNetwork::Build` chains roads, fillets the plan, fits the profile, adds crown and superelevation, pins seams (`Private/World/SimCruiseRoadNetwork.cpp`).
3. `SimCruiseTileBuilder::Build` conditions the terrain under each road 8 cm below the lowest surface and emits the ribbon mesh: 5 columns per station, a station every 2 m (`SimCruiseTileBuilder.cpp:495-627, 721-745`).
4. `ASimCruiseWorldTile::Install` makes a `UProceduralMeshComponent` with complex-as-simple collision. A hidden mesh, used only for collision (`SimCruiseWorldTile.cpp:150-260`).
5. On the physics thread, each wheel casts 3 rays (centre and both shoulders) with `World->LineTraceSingleByChannel` (`SimCruiseVehicleMovementComponent.cpp:262-283`, `SimCruiseVehicleSim.cpp:448-505`).
6. The hit gives point, face normal (not smoothed) and `FaceIndex`. `FaceIndex` maps to the surface layer for grip (`SimCruiseWorldTile.cpp:92-137`).

## 4. How to judge SimCruise (checks)

| # | Check | Threshold | Method | Where |
|---|---|---|---|---|
| G1 | Seam height match | <= 1 cm centre and edges | `SimCruise.World.SeamContinuity` | `World/Tests/SimCruiseWorldTests.cpp:240` |
| G2 | Seam profile continuity (grade and bank 30 m each side) | grade change <= 1 %, bank change rate <= AASHTO relative gradient | New test with a hilly fixture | `SimCruiseRoadNetwork.cpp:812-903` |
| G3 | Collision equals drawn surface | <= 1 cm, no misses | `SimCruise.World.RoadCollision` | `SimCruiseWorldTests.cpp:349` |
| G4 | Superelevation on rural curves | e > 0 toward inside, e <= emax | Unit test on a synthetic curve | `SimCruiseRoadNetwork.cpp:48-53, 848-868` |
| G5 | Grip map correct | asphalt 1.0, gravel 0.7, terrain 0.8 under a trace | New automation test | `SimCruiseWorldTile.cpp:92-137` |
| G6 | Lane centre between painted lines | <= 10 cm | `RightLaneCentreM` vs markings | `SimCruiseRoadNetwork.cpp:431-450` |
| G7 | Rebase is invisible to tyres | 4 wheels in contact every step | Drive the real car across `RebaseDistanceM` | `SimCruiseWorldStreamer.cpp:343-355` |
| G8 | Attribution present | OSM, Overture, USGS, Esri credited | Read UI and `Credits.txt` | `SimCruiseAtlasUI.cpp:339` |
| G9 | Query budget | < 10 000 Overpass requests per day per player | Count helper requests on a 1 h drive | `helper/*.mjs` |
| G10 | Profile sanity | max grade per class, no sawtooth from DEM noise | Plot station Z and dZ/ds | `SimCruiseRoadNetwork.cpp:788-801` |

Test method notes:
- Use bundled tiles for repeatable tests. All current bundled tiles are flat (Miami Beach, heights -0.12 to 4.96 m). Add a hilly fixture before you trust G2, G4 and G10.
- Log per-step wheel data (contact, load, normal) from the vehicle telemetry for G7.
- For "feel" checks, drive the Miata NB (`data/cars/nb2001_aaron.json`) with the Xbox controller at 50, 90 and 120 km/h across seams and through banked curves.

## 5. Common failure modes

- Seam steps: two tiles build the same seam differently. SimCruise prevents concurrent neighbour builds (`SimCruiseTileManager.cpp`, "Edge neighbours never build at the same time").
- Seam humps: a lift or a runout near every tile edge. The car feels a regular crest or a flat patch every 550 m.
- Adverse crown on rural curves: the outside of the curve falls away at 2 %.
- Faceted normals: the trace returns a face normal. Normals jump at triangle edges. Small on a 2 m mesh; large on a coarse terrain mesh.
- DEM noise in the profile: short pitches and dips that do not exist.
- Wrong surface under the tyre: face index order changes after collision cooking.
- Floating-origin events: one frame where the ground and the car are in different frames.
- Licence drift: a new source added without an entry in `docs/data-sources.md` and `licenses/`.
- Quota drift: a new per-tile query without a cache.

## 7. Worked numbers for SimCruise

Use these numbers as quick checks during a review.

- Tile size at Miami Beach (25.8 deg N): 611.5 m x cos(25.8 deg) = about 550 m.
- Terrain mesh cell: `FineCellTargetM` = 2.2 m (`SimCruiseTileBuilder.cpp:16`). About 250 x 250 cells and 125 000 collision triangles per tile.
- Road station step: 2 m (`SimCruiseRoadSection::StationStepM`). Five columns across the carriageway.
- Ground under a paved surface: 8 cm below its lowest point (`UnderDepthM`).
- Seam pin fade and superelevation run-out: 30 m (`PinSpanM`, `SimCruiseRoadNetwork.cpp:16`).
- Superelevation rule in code: `e = min(0.06, 0.5 V^2 / (127 R))`, V = 90 km/h for motorway and trunk, 60 km/h for primary and secondary, none below (`SimCruiseRoadNetwork.cpp:48-53, 849-868`).
- Example: a secondary road, R = 150 m: V^2/(127 R) = 0.189. Code gives e = min(0.06, 0.094) = 0.06. Side friction demand at 60 km/h is then 0.129. Plausible.
- Example: an unclassified rural road, R = 80 m, driven at 60 km/h: code gives no superelevation. The outside half keeps a -2 % crown. Side friction demand becomes 0.354 + 0.02 = about 0.37 on the outside wheels. The driver feels the road lean out of the curve.
- Seam pin lift on a cross slope s with half width W and 0.5 m edge reach: lift = s (W + 0.5) + 0.02 W + 0.07 m. For s = 10 % and W = 4 m: about 0.60 m. Smoothstep over 30 m has a peak slope of 1.5 x 0.60 / 30 = 3 % grade change. [estimate from the code, not measured]
- 3DEP 1/3 arc-second noise of 0.82 m RMSE over a 10 m posting gives local slopes of +-8 % if not smoothed. The sigma-16 m fit reduces this. [estimate]

## 8. Review routine

1. Read the open findings of the last review.
2. Read the diff since the last review in `Source/SimCruise/Private/World/` and `Private/Vehicle/`.
3. Run checks G1-G10 that the diff touches.
4. For each finding, give file:line, the driving impact, a fix and a test.
5. Mark every number that you did not measure as an estimate.
6. Do not change code. Report to the caller.

## 9. References

- USGS, "What is the vertical accuracy of the seamless 3DEP DEMs?": https://www.usgs.gov/faqs/what-vertical-accuracy-seamless-3dep-dems
- USGS 1/3 arc-second DEM catalogue: https://catalog.data.gov/dataset/1-3rd-arc-second-digital-elevation-models-dems-usgs-national-map-3dep-downloadable-data-co
- OSMF Attribution Guidelines: https://osmfoundation.org/wiki/Attribution
- OSMF Licence and Legal FAQ: https://osmfoundation.org/wiki/Licence_and_Legal_FAQ
- OSM copyright page: https://www.openstreetmap.org/copyright
- Overpass API User's Manual, commons and limits: https://dev.overpass-api.de/overpass-doc/en/preface/commons.html
- AASHTO minimum radius equation (U. Idaho NIATT lab manual): https://www.webpages.uidaho.edu/niatt_labmanual/chapters/geometricdesign/professionalpractice/MinimumRadius.htm
- FHWA, horizontal curve research (FHWA-HRT-17-098): https://www.fhwa.dot.gov/publications/research/safety/17098/004.cfm
- TxDOT Roadway Design Manual, superelevation transition: https://www.txdot.gov/manuals/des/rdw/chapter-9--mobility-corridor-facilities--5r-/9-2-cross-sectional-elements/9-2-10-superelevation-transition.html
- Maine DOT, superelevation transition design guide: https://www.maine.gov/dot/sites/maine.gov.dot/files/inline-files/DG%20-%20Superelevation%20Transition.pdf
- Mucka, "Simulated road profiles according to ISO 8608" (summary of classes): https://link.springer.com/article/10.1007/s40534-017-0122-1
- AASHTO, A Policy on Geometric Design of Highways and Streets (Green Book), 7th ed. 2018. Paid publication; values above marked [unverified] come from it via secondary sources.
- ISO 8608:2016, Mechanical vibration, road surface profiles. Paid standard.
- Epic, Large World Coordinates: https://dev.epicgames.com/documentation/unreal-engine/large-world-coordinates-in-unreal-engine-5 [link not fetched in this session]
- Project files: `docs/data-sources.md`, `Credits.txt`, `licenses/Road Atlas.txt`, `docs/research/domain-ideas.md`.
