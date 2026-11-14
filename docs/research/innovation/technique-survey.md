# Survey: current game techniques in each domain (14 November 2026)

Innovation team. For each domain: the best current methods (with the studio or paper), what SimCruise has today, and what we take. This survey feeds [hyper-detail-proposals.md](hyper-detail-proposals.md). The Flight Simulator study is in [flight-simulator-study.md](flight-simulator-study.md). We do not repeat what the team already wrote in `research/domain-ideas.md` (driving senses, audio, camera, perception), `team/docs/research/lighting-tech.md` (light), and `world-detail/world-detail-methods.md` (lidar and aerial methods); we link to them.

Source marks: **[V]** read today. **[S]** headline or search summary seen today. **[K]** a well-known talk or paper, cited from the team's knowledge, not re-read today (check before you quote a number from it).

## 1. World generation and streaming

| Method | Who and where | What it does | Take for SimCruise |
|---|---|---|---|
| Markers per data layer, parent fallback, bandwidth-driven LoD | Asobo, MSFS, GDC 2022 [V] | Each layer is a state machine; draw the best available | **Adopt** in the scan engine (see the study, section 8) |
| GPU run-time procedural placement | Guerrilla, Horizon Zero Dawn, GDC 2017, Jaap van Muijden [S] (https://www.guerrilla-games.com/read/gpu-based-procedural-placement-in-horizon-zero-dawn) | Artists write density rules in a graph over world data maps; the GPU places plants, rocks, sounds, effects and wildlife around the player at run time | **Adopt the idea** (density from data maps), but run on CPU workers into the scan map, because our GPU is the bottleneck |
| Houdini world tools | Ubisoft Montreal, Far Cry 5, Étienne Carrier, GDC 2018 and HIVE 2018 [S] (https://80.lv/articles/houdini-procedural-world-generation-of-far-cry-5/) | Biomes, terrain texturing, freshwater networks and cliff rocks generated over ~100 km² | **Borrow** "cliff rocks where slope and erosion say", "water network drives wetness"; we compute from lidar, not from hand-painted terrain |
| Ghost Recon Wildlands terrain tools | Ubisoft Paris, GDC 2017 [K] | Large terrain from erosion, roads carved by tools, splat by rules | Borrow road carving with cut and fill |
| PCG framework, shape grammar | Epic, UE 5.7 production-ready [S] (https://www.tweaktown.com/news/107858/) | Graph-based placement and building grammar; City Sample 5.8 is built with it | Use offline or on workers; team rule: no run-time PCG until a profile says yes |
| World Partition, HLOD, level instances | Epic UE5 [K] | Cell streaming and far proxies | We stream our own tiles; HLOD idea for far tiles |
| Real places at 1:1 from data | KT Racing, Test Drive Unlimited Solar Crown, 1:1 Hong Kong Island [S] (https://gtplanet.net/tdusc-hong-kong-island-reveal-20230414) ; Ivory Tower, The Crew (scaled US) [K] ; Playground, Forza Horizon 5 (11 biomes; skies from 48,000 8K captures, 75 TB) [S] | Hand-finished worlds, partly from map data | They hand-finish one region. We cannot: our rule is generative everywhere. We take their density targets (what a finished roadside contains) |
| Open procedural nature | Princeton, Infinigen (CVPR 2023, BSD-3 code) [K] | Terrain, rocks, plants, creatures made only from maths, no training data | **Adopt offline**: make rock and plant meshes with a clean licence (ours to ship) |

**What we have:** tile streamer, atlas helper, place graph, 32 m road context records, scan engine contract v1. **Gap:** per-layer fallback and plausibility checks; placement rules that read physical processes.

## 2. Terrain

| Method | Who | Take |
|---|---|---|
| Procedural shader splatting by slope, height and masks | DICE, "Terrain Rendering in Frostbite using Procedural Shader Splatting", Andersson, SIGGRAPH 2007 [K] | We do this already; add soil and rock inputs (proposal 1) |
| Adaptive virtual textures | Ubisoft, "Adaptive Virtual Texture Rendering in Far Cry 4", Ka Chen, GDC 2015 [K] ; UE Runtime Virtual Texture [K] | Bake the near corridor (road edge, decals, wear) into a virtual texture so many layers cost one fetch |
| Terrain in Far Cry 5 | Ubisoft, GDC 2018 "Terrain Rendering in Far Cry 5" [S] (https://gdcvault.com/play/1025480/Terrain-Rendering-in-Far-Cry) | Borrow cliff and road-cut handling |
| Displacement near the camera | MSFS 2024 "3D ground" [V]; UE Nanite tessellation (still has landscape bugs in 5.8, for example UE-367789 displacement direction with Z scale) [S] (https://issues.unrealengine.com/issue/UE-367789) | Displace only a 30 m corridor; first test Nanite tessellation on our own tile meshes (not Landscape); fallback a CPU-refined mesh |
| Distance-field edits merged into the DEM | MSFS terraforming [V] | Road cross-sections as vector edits merged at load (proposal 2) |
| Lidar corridor relief | Ours, measured on the Dragon: 16.9 % ground >35° in lidar vs 1.6 % on 10 m (world-detail-methods.md) | Already the plan; extend with the engineering cross-section |

## 3. Vegetation

| Method | Who | Take |
|---|---|---|
| Procedural grass blades, clumps, wind | Sucker Punch, "Procedural Grass in Ghost of Tsushima", Eric Wohllaib, GDC 2021 [S] (https://gdcvault.com/play/1027214/) | Our "grass field" already does blades in the shader; take clumping by Voronoi cell and wind as a moving noise |
| Impostors, one vertex per tree | MSFS [V] | Far trees |
| Nanite foliage (voxel far LoD, assemblies), Procedural Vegetation Editor | Epic, UE 5.7 [S] (https://digitalproduction.com/2025/11/12/unreal-engine-5-7-foliage-pcg-and-in-editor-ai/) | Test on the laptop GPU before use; tree generation offline is useful now |
| Space colonisation tree growth | Runions, Lane, Prusinkiewicz, "Modeling Trees with a Space Colonization Algorithm", 2007 [K] | **Core of proposal 4**: grow each tree into its measured lidar envelope |
| Inverse procedural trees, tree from point cloud | Stava et al. 2014; Livny et al., SIGGRAPH Asia 2010 [K] | Method support for proposal 4 |
| Tree crown detection | DeepForest (Weecology; MIT code; trained on NEON data, which NEON publishes openly) [S] | Already planned as an eye; check the weights' training data record |
| SpeedTree | paid | Skip |

**What we have:** two tree meshes plus palms, blob look, flicker fixed, Meta CHM positions. Aaron: "trees first". **Gap:** the shape of each tree; understory; forest edge.

## 4. Buildings and facades

| Method | Who | Take |
|---|---|---|
| CGA shape grammar | Müller, Wonka et al., "Procedural Modeling of Buildings", SIGGRAPH 2006; "Instant Architecture", SIGGRAPH 2003 [K] | We have a facade grammar; feed it real structure facts (proposal 5) |
| City Sample PCG building subgraph | Epic, UE 5.8 [K, from free-unreal-tools.md] | Template for bays and kits |
| Wave function collapse for local consistency | Townscaper, Oskar Stålberg [K] | For porches, steps and attached garages that must fit together |
| ML roof and height from photographs | Blackshark for MSFS [V] | We use lidar for heights and NSI for stories instead |
| Interior mapping (fake rooms behind windows) | van Dongen 2008 [K]; common in UE | Cheap; already in our plan |
| National structure inventories | USACE National Structure Inventory (NSI): construction type, stories, foundation type and height, occupancy, median year built per block [V] (https://gee-community-catalog.org/projects/nsi/) ; FEMA/ORNL USA Structures: footprints and occupancy, CC BY 4.0 [V] (https://gee-community-catalog.org/projects/usa_structures/) | **New input for facades**; no game uses these |
| Measured drawings of US vernacular buildings | Library of Congress HABS/HAER (Historic American Buildings Survey), mostly public domain [K] | Proportions per region and era for the grammar |

## 5. Roads and roadside (the domain games rarely write about)

| Method | Who | Take |
|---|---|---|
| Laser-scanned tracks (mm accuracy) | iRacing, rFactor 2, Assetto Corsa, Gran Turismo [K] | Not possible for every road; lidar QL2 (about 2-8 points/m²) is our "scan" for the ground beside the road |
| Procedural roads on terrain with cut and fill | Galin et al., "Procedural Generation of Roads", CGF 2010 [K] | Grade and cut faces |
| Road design standards | AASHTO "Green Book" and Roadside Design Guide; state DOT standard drawings; FHWA; MUTCD (public domain) [K] | **Proposal 2**: build every cross-section from the standards |
| National road statistics | FHWA HPMS: shoulder type and width, lane width, median, year of last improvement are Sample Panel only; surface type, cracking %, rutting, faulting full extent for NHS roads; IRI for some systems [V] (https://www.fhwa.dot.gov/policyinformation/hpms/fieldmanual/page02.cfm) | Learn distributions from the sample, apply everywhere |
| Lidar intensity for paint and patches | Ours (world-detail-methods.md, method 3) | Keep |

## 6. Materials and micro-detail

| Method | Who | Take |
|---|---|---|
| Stochastic and hex tiling (no repeats) | Heitz and Neyret, HPG 2018 [K]; Mikkelsen, JCGT 2022 [K] | In use (grass field) |
| Detail maps chosen per pixel | MSFS [V] | In use (detail layer) |
| Neural material compression and neural appearance | NVIDIA, "Random-Access Neural Compression of Material Textures" 2023; Zeltner et al., "Real-Time Neural Appearance Models", 2024 [K] | Watch; needs tensor cores and drivers; not for the laptop now |
| PBR material datasets | MatSynth: 4,069 materials, CC0 (ambientCG, Poly Haven, CGBookCase, ShareTextures, TextureCan) plus a small CC-BY set [V] (https://huggingface.co/datasets/gvecchio/MatSynth) | **Train our own small generator on the CC0 part only** (proposal 9) |
| Clean-data image models | CommonCanvas (Creative Commons images) [S] (https://huggingface.co/papers/2310.16825); Public Diffusion (Spawning; public domain and CC0 only; open weights planned for 2025, check status) [V] | Candidates to replace SDXL for the detail kit |
| Realism cues | Rademacher 2001; Fan et al. 2018; 1/f spectrum (world-detail-methods.md) | Measure the spectrum in the road-level self-check |

**Legal flag (Aaron's rule: licence of weights AND training data).** The current plan lists SDXL (trained on LAION web images, whose copyright status is disputed) and Real-ESRGAN (DIV2K training set, research use). SAM and SAM 2 weights are Apache-2.0, but their SA-1B images have a research licence. These do not pass the rule as written. Proposal 9 removes the need.

## 7. Lighting

Covered in `team/docs/research/lighting-tech.md` (Lumen with tile distance fields or hardware ray tracing, VSM cost, albedo caps, GT7-style colour-volume tone mapping, HDR10). Add only:

- MSFS shadows: cascades near, ray-marched terrain height map far, screen-space ray-marched contact shadows for small detail [V]. The far terrain-shadow height map is cheap and fits our big valleys (Dragon, Big Sur).
- Sky-view factor baked from lidar (world-detail-methods.md, method 8) is the cheapest "ambient occlusion" for the corridor; put it in the scan map (proposal 3 adds the channel).

## 8. Vehicle physics and tyres

| Method | Who | Take |
|---|---|---|
| Magic Formula, brush, TMeasy, FTire | Pacejka, "Tire and Vehicle Dynamics"; Rill (TMeasy); Gipser (FTire) [K] | We use TMeasy; keep, check against Project Chrono (free-unreal-tools.md) |
| Many contact points per tyre | Turn 10, Forza Motorsport 2023: "48x" more tyre detail [S] (https://gamingbolt.com/forza-motorsports-48x-improved-tire-physics-showcased-in-new-comparison-clip) | Our road cross-section and scan heights should be sampled by more than one point per tyre, so a rumble strip, a shoulder drop or a gravel edge is felt |
| Soft-body node-beam vehicles | BeamNG [S] (https://beamng.com/game/about/physics) | Not for us (cost); borrow "the road edge is physical" |
| Rolling road state (rubber, wet line) | rFactor 2 "Real Road" [K] | Our road surface record can carry wet and dry lines from live weather |

**Link to hyper detail:** the same scan data that draws the shoulder, gravel and ruts must feed the tyre (MSFS reads its ground texture back to the CPU for collision [V]). See proposal 2.

## 9. Cockpit and car art

| Method | Who | Take |
|---|---|---|
| Manufacturer CAD and photo reference, months per car | Polyphony, Turn 10 [K] | We have Blender MCP and Aaron's own NB; measure the real car (domain-ideas idea 3, "Garage Day") |
| Rigged parts checked against service data | all sims [K] | Current cockpit bugs (wheel axis, gauges) are owned by the driving thread |
| Interior light from the world | GT7, Forza [K] | Cabin light flips (Aaron's review) belong to the picture thread |

Nothing here changes the proposals; the cockpit needs correct rigs before detail.

## 10. Audio

See `research/domain-ideas.md` (engine models, tyre noise from FHWA OBSI, Burnout whooshes, guard rail flutter). Add: the road cross-section (proposal 2) gives the audio a real event list: rumble strip (about 30 cm spacing, a tone near speed / spacing), shoulder gravel spray, expansion joints, bridge decks, cattle grids. Same record, three senses.

## 11. UI and UX

Xbox Accessibility Guidelines; Forza and GT assists; "first five minutes" (player-first roadmap). For hyper detail: a **photo mode with a "real photo" split view** of the same place (licensed photo where one exists) makes the realism visible to the player and gives us playtest data. Low cost.

## 12. Performance

| Method | Who | Take |
|---|---|---|
| GPU-driven culling and draws | Haar and Aaltonen, "GPU-Driven Rendering Pipelines", SIGGRAPH 2015 (Assassin's Creed Unity) [K]; Wihlidal, GDC 2016 [K] | For scatter and trees: instance buffers, one draw per kind per tile (as MSFS) |
| Nanite and VSM | Karis, "Nanite: A Deep Dive", SIGGRAPH 2021 [K] | Our tiles are runtime meshes; measure Nanite for corridor meshes only |
| Detail where the driver looks | Land and Lee 1994; team realtime-context.md item 7 | Budget by distance band (proposal 8) |
| Change blindness for LoD swaps | team domain-ideas idea 4 | Keep |

Budget reminder (team): GPU ≤ 1.5 ms for the whole detail layer; eyes ≤ 2 cores, ≤ 1.5 GB, ≤ 3 s per tile; game thread ≤ 1 ms per frame for uploads.

## 13. Tools and testing

| Method | Who | Take |
|---|---|---|
| Automated testing at scale | Rare, "Automated Testing at Scale in Sea of Thieves", GDC 2019 [K] | We have pre-flight and random places |
| RL agents for coverage | EA SEED, Gordillo et al. 2021 [K] | Not needed; our drive robot covers roads |
| Perceptual image difference | NVIDIA FLIP, Andersson et al. 2020, BSD-3 [K] | **Use in the road-level self-check** (proposal 7) |
| Top-down self-check against the aerial | Our scan engine v1 | Keep; add the road-level check |

## 14. AI for content: what is legal for a free game

| Model or data | Weights | Training data | Verdict |
|---|---|---|---|
| DeepForest | MIT | NEON airborne data (open) | OK after we record the exact release |
| Our own model trained on MatSynth CC0 subset | ours | CC0 | **OK by construction** |
| Infinigen | BSD-3 (no learned weights) | none | OK |
| SDXL | Open RAIL++-M | LAION (web-scraped) | Fails the training-data rule; replace |
| Real-ESRGAN | BSD-3 | DIV2K (research only) | Fails; replace with classic methods or our own CC0-trained model |
| SAM / SAM 2 | Apache-2.0 | SA-1B (research licence) | Fails the rule as written; use classical segmentation or lidar |
| Moondream2, Florence-2 | Apache-2.0, MIT | Not fully published | Unknown; do not ship until the training data is documented |
| Public Diffusion | open weights planned (check) | Public domain and CC0 (PD12M) | Best candidate if released |

## Sources (new in this note; the study has the MSFS list)

- Guerrilla Games, "GPU-based procedural placement in Horizon Zero Dawn": https://www.guerrilla-games.com/read/gpu-based-procedural-placement-in-horizon-zero-dawn [S]
- 80.lv, "Houdini: Procedural World Generation of Far Cry 5": https://80.lv/articles/houdini-procedural-world-generation-of-far-cry-5/ [V]
- GDC Vault, "Terrain Rendering in Far Cry 5": https://gdcvault.com/play/1025480/Terrain-Rendering-in-Far-Cry [S]
- GDC Vault, "Procedural Grass in Ghost of Tsushima": https://gdcvault.com/play/1027214/ [S]
- Digital Production, "Unreal Engine 5.7: foliage, PCG and in-editor AI": https://digitalproduction.com/2025/11/12/unreal-engine-5-7-foliage-pcg-and-in-editor-ai/ [S]
- TweakTown, "Unreal Engine 5.7 preview ... production-ready PCG": https://www.tweaktown.com/news/107858/unreal-engine-5-7-preview-now-out-with-production-ready-procedural-content-generation-framework/index.html [S]
- Unreal issue UE-367789 (Nanite tessellation direction on landscapes, still in 5.8): https://issues.unrealengine.com/issue/UE-367789 [S]
- GTPlanet, "Test Drive Unlimited Solar Crown's 1:1 Hong Kong Island": https://gtplanet.net/tdusc-hong-kong-island-reveal-20230414 [S]
- TweakTown, "Forza Horizon 5's skies: 48,000 8K image captures worth 75TB": https://www.tweaktown.com/news/80373/forza-horizon-5s-skies-48-000-8k-image-captures-worth-75tb-of-data/index.html [S]
- GamingBolt, "Forza Motorsport's 48x improved tire physics": https://gamingbolt.com/forza-motorsports-48x-improved-tire-physics-showcased-in-new-comparison-clip [S]
- BeamNG physics: https://beamng.com/game/about/physics [S]
- FHWA HPMS Field Manual, data items and extent: https://www.fhwa.dot.gov/policyinformation/hpms/fieldmanual/page02.cfm [V]
- USACE NSI on the GEE community catalog: https://gee-community-catalog.org/projects/nsi/ [V]
- USA Structures on the GEE community catalog: https://gee-community-catalog.org/projects/usa_structures/ [V]
- USGS SGMC geologic map of the conterminous US: https://www.usgs.gov/data/state-geologic-map-compilation-sgmc-geodatabase-conterminous-united-states [S]
- MatSynth dataset: https://huggingface.co/datasets/gvecchio/MatSynth [V]
- CommonCanvas paper: https://huggingface.co/papers/2310.16825 [S]
- Spawning, "A fireside chat with the creators of Public Diffusion": https://spawning.substack.com/p/a-fireside-chat-with-the-creators [V]
- Mapillary licence (images CC BY-SA): https://help.mapillary.com/hc/en-us/articles/115001770409-CC-BY-SA-license-for-open-data [V]
