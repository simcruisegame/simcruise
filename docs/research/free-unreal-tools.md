# Free Unreal tools we can use or build on (12 November 2026)

The owner asked: "Are there free tools for Unreal that we can build on, like car physics? Do we have a plan to make buildings look better?" A research agent checked free, legally usable tools and compared each with the SimCruise code. No money, licences that a free MIT game can use. Items marked **[unconfirmed]** were not checked at the source. GPU costs are estimates, not measured.

## Short answer

- **Car physics:** nothing free is better than our own 360 Hz core with TMeasy tyres and calibrated benches. Use free tools as references to check against, not as replacements.
- **Buildings:** yes, there is a plan. Epic's free **City Sample, updated for UE 5.8**, now builds its buildings with **PCG and shape grammar** (no Houdini). Use it as the template: our streamer gives each footprint, its height and a style tag to a PCG graph that cuts it into floors and bays and fills them with kit meshes.

## Fab content and the public repo

The Fab Standard Licence lets us use Fab and Epic content in a free game, but it forbids redistribution "on a standalone basis" and mentions only private repos. So:

- Do **not** commit Fab or Megascans `.uasset` files or their source textures to the public repo.
- Fab content **can** ship cooked inside packaged builds (GitHub release binaries).
- Contributors claim the free asset on Fab themselves and put it in a git-ignored `Content/ThirdParty/` folder.
- What must be in the repo stays CC0 (Poly Haven, ambientCG) or our own work.
- **[unconfirmed]** Epic has not answered the public-repo question on its forum. https://www.fab.com/eula , https://forums.unrealengine.com/t/github-project-distribution-and-free-fab-assets/2665655

## Car physics, input and sound

| Tool | Licence and status | Against our code | Verdict |
|---|---|---|---|
| Chaos Vehicles (built in) | Unreal EULA | Simple tyre friction, basic clutch, tied to the Chaos tick. Behind our tyres and benches. | Skip |
| Chaos Modular Vehicle | Unreal EULA; work by Psyonix and Epic said to land in 5.8, still experimental **[unconfirmed]** | Modules for engine, clutch, torque converter, axles; network replay. Tyres still game-grade. | Borrow ideas (module layout, replay for multiplayer) |
| Project Chrono, Chrono::Vehicle | BSD-3, maintained | Open TMeasy (basic version), Pacejka 89/2002 (reads `.tir` files), Fiala. Too heavy to embed. | Borrow: check our TMeasy against it with the same inputs; read public `.tir` tyre data. https://api.projectchrono.org/wheeled_tire.html |
| SDL3 (haptics and joysticks) | zlib (MIT-compatible) | We lack wheel force feedback. SDL drives DirectInput wheels. | **Adopt** (to-do item 8) |
| JoystickPlugin (JaydenMaalouf) | licence **[unconfirmed]** | SDL-based wheel input and force feedback for UE | Reference only, unless it is MIT |
| engine-sim (AngeTheGreat) | MIT; quiet since June 2024 | Physical pistons and exhaust waves at 44.1 kHz | Borrow ideas for our engine sound; too heavy to embed **[not measured]** |
| MetaSounds (built in) | Unreal EULA | Mixing, spatial sound and occlusion for free | **Adopt as host**: our C++ sound code as custom MetaSound nodes |
| CARLA 0.10 vehicles | MIT code | Uses Chaos vehicles | Skip the vehicle part |
| Paid physics (Kallisto, FGear, AGX, rFpro) | paid | | Skip (no money) |

## Buildings and cities

| Tool | Licence and status | Against our code | Verdict |
|---|---|---|---|
| City Sample, 5.8 update | Fab Standard Licence, free | Two levels built only in PCG, a building subgraph, shape grammar for buildings and roads, rooftop kits. The full level is too heavy for 28 W; the kit and graphs are the useful part. https://www.unrealengine.com/learning/city-sample-gets-a-major-update-with-pcg-and-unreal-mcp-workflows | **Adopt the kit and graph patterns**, kept out of the public repo |
| PCG framework and shape grammar | built in; PCG production-ready in 5.7; runtime use of shape grammar **[unconfirmed]** | Can split our footprints into floors and bays and fill them with instanced meshes | **Adopt** (to-do items 16, 17, 40). https://dev.epicgames.com/documentation/en-us/unreal-engine/using-shape-grammar-with-pcg-in-unreal-engine |
| Poly Haven, ambientCG | CC0 | Brick, stucco, concrete, glass, asphalt | **Adopt** (in the repo) |
| Megascans on Fab | free claiming ended in 2024; many items now paid | | Skip for the repo; free items in cooked builds only |
| Cesium for Unreal + Google Photorealistic 3D Tiles | plugin Apache-2.0 **[unconfirmed]**; Google forbids caching beyond headers, offline use and derived 3D objects; each player needs a key; Cesium ion free tier is non-commercial | Blobby at street level; against our keyless rule | Skip as the main path; at most an optional "bring your own key" compare mode |
| Blosm (Blender OSM) | GPL; free version has roof shapes, no facade textures | | Borrow ideas: roof shapes, building parts (do not link the code) |
| StreetMap plugin | MIT, community UE5 forks | Our streamer already does more | Skip |
| OSM2World | licence **[unconfirmed]** | How OSM tags (`building:material`, `roof:shape`, `building:colour`) become looks | Borrow ideas |
| CityEngine, Houdini tools | paid | | Skip |

Our own shader work to add on top: window interior mapping (fake rooms behind the glass, cheap in the material) and lit windows at night.

### The building plan in steps

1. The streamer passes footprint, height or floor count, use and a style tag (Art Deco, ranch, brick row, etc.) from OSM, Overture and the place knowledge graph into a PCG graph.
2. Shape grammar cuts the mass into floors and bays (City Sample 5.8 building subgraph as the pattern).
3. Bays are filled with kit meshes: windows, cornices, setbacks, doors, awnings. CC0 materials with the grime floor (see [domain-ideas.md](domain-ideas.md), idea 7).
4. Instanced meshes and HLOD keep it inside the 28 W budget (to-do item 9).
5. Test with the realism score against licensed Panoramax photos of the same spot (to-do item 18).

The buildings thread owns this choice.

## Other things to borrow

- **Mass Traffic (City Sample):** cheap CPU traffic for thousands of cars, but it needs ZoneGraph lanes and is fragile to move into another project. Borrow, or adopt if we generate ZoneGraph lanes from our OSM roads (to-do items 3, 12). https://forums.unrealengine.com/t/migrating-citysample-s-traffic-system-into-a-new-project/2588482
- **CARLA (MIT):** Traffic Manager and junction logic as references for our traffic AI (items 3, 12). https://carla.org/2024/12/19/release-0.10.0
- **SUMO (EPL-2.0 [unconfirmed]):** city-scale traffic as a separate process over TraCI, optional.
- **AirSim:** archived. Skip.
- **UE 5.8 rendering:** test **Lumen Lite** (probe lighting for low-end hardware) first for the 28 W GPU; test MegaLights for street lights at night; compare the Procedural Vegetation Editor with our trees; keep clouds at low quality. https://gamefromscratch.com/unreal-engine-5-8-released/
- **NVIDIA DLSS plugin:** free, NVIDIA's own licence **[terms unconfirmed]**; Super Resolution works on the RTX 2070, Frame Generation does not. Optional download, not in the repo; TSR stays the default.

## Recommendation

1. Buildings: a PCG shape-grammar facade pipeline fed by our streamer, with City Sample 5.8 as the template and CC0 materials in the repo.
2. Keep our physics core; check our TMeasy against Chrono's with the same inputs.
3. Add SDL3 for wheels and force feedback.
4. Host our sound code in MetaSounds.
5. Test Lumen Lite and DLSS Super Resolution on the 28 W laptop.
