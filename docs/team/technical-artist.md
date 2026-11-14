# Technical artist: study notes

## Rules
- Physically based values: base colour is albedo in linear space. Walls ≤ 0.75, concrete 0.25-0.35, asphalt 0.05-0.12, grass 0.08-0.25 (team lighting notes). Metals use metallic = 1 with measured F0.
- Never bake light into albedo textures. Photos must be de-lit before use.
- Moving parts (steering wheel, needles) turn from sim data, never by a fixed speed: wheel angle = mean road-wheel angle x overall steering ratio (`SimCruiseCarPresentationComponent.cpp:44-47`).
- Gauges: static face texture + one rotating needle per dial; no per-frame canvas (team review 2026-11-13).
- Budget on the 28 W GPU: the AI detail layer ≤ 1.5 ms (estimate).

## Tests
- Daylight capture: patch luma / albedo within ±10 % of the plain material.
- Cockpit capture per car: needles match speed and rpm; wheel angle matches the sim.

## Sources
- Epic, Physically Based Materials: https://dev.epicgames.com/documentation/en-us/unreal-engine/physically-based-materials-in-unreal-engine
- B. Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013 course notes.
- T. Akenine-Möller et al., *Real-Time Rendering*, 4th ed., CRC Press, 2018.

## What I now know (onboarding, 2026-11-14)

**How my area works today.**
- `SimCruiseGltfModel.cpp` reads `scene.gltf`/`.glb` at run time. `SimCruiseCarModelRig.cpp` cuts the model into body, 4 wheels, 4 uprights, steering wheel and dials. It does this work in the Godot frame (X right, Y up, Z rear) and converts to Unreal cm at step 10 (`SimCruiseCarModelRig.h:145-147`).
- Steering-wheel search (`SimCruiseCarModelRig.cpp:1418-1670`) tries these in order: a profile override, then a node name (regex `steer|wheel_s|volant|lenkrad...`), then geometry. The geometry search looks for a ring of radius 0.12-0.24 m (`:458-459`) that faces fore/aft (|N.Z| ≥ 0.55) on the driver side. If that fails, it uses a Kasa circle fit to the rim arcs (`RingFromArcs`, `:565`).
- `RingOf` (`:461-520`) sets the ring centre to the **mean of the vertices** and the axis to the smallest covariance eigenvector. It does not fit a circle. The search for an outer rim (`:1557-1562`) changes only `R`. It does not fit the centre or the axis again.
- What turns with the wheel (`:1588-1615`): every connected part that lies wholly inside the cylinder from axial −0.12 m to +0.09 m with radius 1.08 R. If the hub does not turn or fewer than 13/16 rim sectors are covered, the rig also cuts triangles from parts that reach the hub, in a slab 0.12 m deep (0.045 m when the hub turns) with radius 1.02 R (`:1620-1650`).
- Pivot: `PartToVehicle` has X = −N (along the column) and its origin at the ring centre (`:1692-1698`). The component turns the wheel by (FL+FR)/2 × `steering_ratio` about local X (`SimCruiseCarPresentationComponent.cpp:44-47, 57-60, 670-677`).
- Dials: `centre_m` in the JSON is in the **Godot frame** (`SimCruiseCarModelRig.cpp:993-1004`, `dials_note` in `CarPresentation.json:6`). Step 8a removes the model's welded needle by material (`:1301-1329`). The needle meshes attach to the component, not to the wheel (`SimCruiseCarPresentationComponent.cpp:350-363`). They turn with a lag of about 0.12 s (`:417-440`). The canvas draws the dial face once, at init (`:346`).
- NB override (`CarPresentation.json` `steering_wheel`): centre (0.11, −0.351, 0.713) m, axis (−0.829, 0.035, 0.559), radius 0.185. The note says that this centre and axis come from the **0.162 m horn ring**, not from the rim. The Mustang and the C8 have no override, so the geometry search finds their wheels.
- **Cause of "gauges turn with the wheel" (desk calculation, estimate until a capture confirms it):** in the NB frame, the speedometer hub is at axial −0.088 m and radial 0.194 m from the wheel axis (nb2001_lowered). It is at −0.108 m and 0.166 m on nb1999/nb2001. Both points are inside the turn cylinder (−0.12 m, 0.200 m). Thus the model's dial faces, numerals and ticks move into the steering mesh. The needle meshes stay fixed, so the needles also slide off their faces.
- **Cause of "wheel off its axis" (estimate):** the pivot is a vertex mean of the horn ring (NB) or of a single part (geometry path). It is not a circle fit to the rim. Uneven vertex density and asymmetric spokes move the mean away from the column, so the rim orbits as the wheel turns.
- World materials (`SimCruiseWorldResources.cpp`): the HLSL is built in code. Ground (`:169-310`) uses the aerial photo (highlights compressed above 0.16) blended with land-cover class colours: bare 0.15/0.14/0.11, veg 0.052/0.088/0.026, sand 0.50/0.45/0.35, paved 0.27/0.26/0.24, forest litter 0.062/0.052/0.036. Other layers: CC0 grass, sand and asphalt photos (`:961-963`), the detail kit with 8 slices (`:105-111, 978-979`), MODIS `SnowCover` (`:307`) and an 8-shell grass field (blade 8 cm, "St. Augustine", `:255-260`).
- Asphalt `Tint` is 0.085/0.083/0.080 and concrete is 0.34/0.33/0.31 (`:776, 784`). No caller sets either value per road. Unpaved roads use one fixed gravel colour, 0.26/0.23/0.19 (`:326`). Wet asphalt darkens to 0.6 of dry with roughness 0.35, and to 0.04 in puddles (`:339-349`).
- `SimCruiseNeuralDetail.cpp` runs Real-ESRGAN x4v3 on each aerial photo, in 128 px windows. It box-filters the result to 2x and caches it as JPEG q95 (`:68, 87, 251, 293`).

**Does it work at a random US place?**
- General: the aerial photo leads the ground colour. CONUS uses a lat/lon box. Hawaii uses NAIP 2021 and Alaska uses USFS imagery (`helper/regional-aerial.mjs:12-27`), so Hilo and Anchorage both get photos. MODIS snow, the live wetness and the detail kit apply everywhere.
- Art Deco: a lat/lon box sets it (`SimCruiseTileDetail.cpp:219-227`), or the graph `recipe:deco`. So it does not spread outside Miami Beach. The Bronx Grand Concourse (real Art Deco) depends on the graph only (estimate).
- Miami constants that are applied everywhere: sand albedo 0.50 (quartz) leads 70 % on the sand class (`:186, 206`). This gives a light beach at Hilo, where the beaches are black basalt sand (estimate). The water colour is a fixed tropical teal 0.025/0.15/0.17 (`:301`), which is wrong for Stonington ME, Detroit and Selma (estimate). The grass blades are St. Augustine turf at Casselton ND and Rawlins WY.
- `GrassDry` comes only from `-SimCruiseGrassDry=` (`SimCruiseWorldTile.cpp:379-383`, `SimCruiseRegionalTerrain.cpp:398-402`). So November lawns are green-growing everywhere: Casselton, Delta UT, Anchorage.
- There is one asphalt albedo and no concrete roads. The Doraville GA I-85 slabs, Detroit Woodward and the Rawlins I-80 sections all show as dark asphalt (estimate). Casselton gravel is one brown colour, with no data about the local aggregate.
- The car rig is per model, not per place. The cockpit bugs affect all 3 player cars at every place.

**Main gaps against "realistic everywhere".**
1. P0 | Gauges turn with the wheel | `SimCruiseCarModelRig.cpp:1595-1650` (cylinder −0.12 m, 1.08 R) | Exclude every part inside a profile dial disc, and limit the turn slab to the rim's own depth.
2. P0 | Wheel off its axis | `SimCruiseCarModelRig.cpp:461-520, 1557-1562`; NB override from the horn ring | Fit the rim again: least-squares circle for the centre and the plane normal at the final R. Then update the override.
3. P1 | No per-road surface: asphalt and concrete tint are constants, gravel is one colour | `SimCruiseWorldResources.cpp:326, 776, 784` | Use OSM `surface` + HPMS Surface_Type (HPMS Field Manual). Age asphalt from about 0.05 (new) to 0.10-0.18 (aged), per the EPA Heat Island Compendium "Cool Pavements" chapter (check the exact figures).
4. P1 | Miami-tuned sand and water colours, and season-blind grass | `SimCruiseWorldResources.cpp:186, 255, 301`; `SimCruiseWorldTile.cpp:381` | Get the sand and water colours from the de-lit aerial photo, or from place-graph soil and water facts. Get `GrassDry` from the place-graph climate and the date.
5. P2 | No test checks the cockpit cut | `Presentation/Tests/SimCruisePresentationRigTests.cpp:86-93, 200-215` check only the positions | Add the tests in action 1.

**My first three actions.**
1. Keep the dials out of the turning set (`SimCruiseCarModelRig.cpp` step 9). Add a test to `SimCruisePresentationRigTests.cpp`: no steering-mesh vertex within 1.2 × dial radius of any dial centre, for nb2001_lowered and mustang65. Done when: the test passes; and in `scripts\preflight.ps1 -Roads random -Cars nb2001_lowered,meridian_z,mustang65 -Seconds 60`, the cockpit frames show the dials fixed to the dash at full lock. I read the images myself.
2. Fit the rim centre and axis again with a circle fit, and remove the horn-ring centre from the NB override. Add a test: after a 90° turn, the rim vertices stay within 3 mm of the rest circle. Done when: the test passes, and a turntable steered-cockpit capture shows the rim turning about its own centre.
3. Make the world surfaces general: a road `Tint` from the surface data, a sand and water colour from the place, and `GrassDry` from the climate. The work is in `SimCruiseWorldResources.cpp`, `SimCruiseWorldTile.cpp` and `SimCruiseRoadNetwork.cpp:497`. Done when: `scripts\preflight.ps1 -Roads us-hilo-hi,us-doraville-ga,us-casselton-nd,random` daylight frames match Wikimedia photos of the same places, with patch luma within ±10 %, and `scripts\realism.ps1` logs the GPU cost.

**Docs that are wrong or old.**
- `CarPresentation.json:2` `_comment` says that all points use the Unreal vehicle frame. But `dials.centre_m` uses the Godot frame (`dials_note`, `SimCruiseCarModelRig.cpp:993-1001`). Also, `docs/unreal-architecture.md:354-356` does not list `dials` at all.
- The `dials_note` in `CarPresentation.json:6, 596, 704` says that the model's white needles "stay at rest under them". But step 8a removes them (`hide_model_needle: "white"`, `SimCruiseCarModelRig.cpp:1301-1329`, commit 3909ef3).
- `docs/unreal-architecture.md:225-238` describes the ground and asphalt as Poly Haven photos only. It does not mention the detail kit, the grass field, MODIS snow, the wet road or `GrassDry`, which are in `SimCruiseWorldResources.cpp`.
- `docs/cars.md:94` says that the Godot `CarBody.gd` mounts the model and turns the steering wheel. The Unreal client does this in `Presentation/`.

Nothing was run: Unreal cannot run in this clone, and no file was changed. The geometry numbers above come from the profile coordinates. Confirm them with `-SimCruiseRigDebug` and a cockpit capture before you treat them as fact.
