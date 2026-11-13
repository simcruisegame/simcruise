# New ways to add world detail (13 November 2026)

Aaron asked for new and original methods to add detail to the world efficiently: textures, meshing, depth from grid or street imagery, and other new ideas. This note lists 15 methods, ranks them by the realism they add for each millisecond of GPU time on the 28 W laptop GPU, and reports two prototypes that we built and measured.

Read with: [detail-engine.md](detail-engine.md) (the Godot-era texture plan), [real-world-detail.md](real-world-detail.md), [domain-ideas.md](domain-ideas.md), [../team/research/lighting-tech.md](../team/research/lighting-tech.md) and the player-first roadmap ([../plan/player-first-roadmap.md](../plan/player-first-roadmap.md)).

## The rules that shape the list

- **The GPU has no time to spare.** The High frame is about 15 ms already. A method that runs once per place on the CPU, and leaves only a texture fetch per pixel, wins over any method that runs per frame.
- **Every model and every dataset must have an open licence for a free game.** v2.0.1 ships no neural weights, because Real-ESRGAN (DIV2K) and DDRNet (Cityscapes) have research-only training data. Most depth networks have the same problem: Depth Anything V2 Small has Apache-2.0 weights, but its teacher used Virtual KITTI 2 (CC BY-NC-SA) and SA-1B (research licence). So we prefer **measurement over learning**: US public-domain lidar (USGS 3DEP, NOAA), public-domain aerial photographs (USGS, NAIP) and classic geometry, which need no weights at all.
- **Our gap is data, not shaders.** The world already has a facade grammar (eyebrows, awnings, parapets), procedural ground detail, lidar roofs and low plants. What it does not have is the real shape of the ground near the road and a ground photograph without someone else's sun in it.
- **Do not duplicate other threads.** Exposure, tone, colour grade, seasonal fields and asphalt wear belong to the world-look thread (5.2). Poles, wires, mailboxes and signs belong to the roadside work (5.4). Photo parsing belongs to the live-data thread.

## The methods, ranked

Realism gain: 1 (small) to 5 (large), our estimate of what a driver sees on the hero roads. GPU cost: estimated for High at 1600 x 900 on the RTX 2070 Super Max-Q; "0" means the method only changes data that the shader already reads. Rank = gain / (GPU ms + 0.05).

| # | Method | New? | Gain | GPU ms | Rank | Status |
|---|---|---|---|---|---|---|
| 1 | **Unbake the sun**: find the sun shadows printed in the aerial photograph from lidar heights, fit the sun direction to the photograph's own shadows, lift each shadow by the local lit/shade ratio of its surface class | ours | 3 | 0 | 60 | **prototype built, in game** |
| 2 | **Corridor relief**: 0.5 m lidar ground in a 40 m band along each road (cuts, ditches, benches, kerbs, berms) as a height and normal texture, plus a mesh refine near the car | ours (as a game method) | 4 | 0.05-0.1 | 40-57 | **prototype measured** (Tail of the Dragon) |
| 3 | **Lidar intensity road map**: the same lidar gives a 0.5 m picture of paint, worn lines, patches, crack seal and gravel shoulders | ours | 3 | 0 (same texture as 2) | 60 | seen in prototype 2 |
| 4 | **Detail spectrum matched to photographs**: set each surface class's detail amplitude so its band-pass energy equals that of licensed photographs of the same road (the realism score's own "detail" measure) | ours | 2 | 0 | 40 | proposed (world-look thread) |
| 5 | **Texture by example from the place**: cut a clean 64 px patch of the local wall, pavement or verge from a photograph and quilt it on the CPU into a tileable 512 px swatch (image quilting, Efros and Freeman 2001; no weights) | known method, new use | 3 | 0 | 60 | proposed |
| 6 | **Shade-divided photo paint**: project a licensed street photograph onto the facade it shows, then divide by the game's own render of that scene in white albedo at the photograph's time and sun, which leaves albedo only | ours | 4 where photos exist | 0.05 | 40 | proposed; Panoramax CC BY-SA, so runtime cache only |
| 7 | **Heights from aerial shadows**: where there is no lidar, measure each shadow's length in the photograph and the fitted sun elevation to get wall, hedge and tree heights | known in remote sensing, new for games | 2 | 0 | 40 | proposed (fallback for 1) |
| 8 | **Sky occlusion from lidar**: bake the sky-view factor of the lidar surface into the same ground texture, so a lane between hedges or a cut is darker in shade, as it is in a photograph | known, new use | 2 | 0 (same texture) | 40 | proposed (goes with 1 and 2) |
| 9 | **Sequence stereo**: classic multi-view stereo on consecutive street photographs (COLMAP, BSD-3, no learned weights) to get facade relief depth and the shape of hedges and walls | known method, new use | 3 | 0-0.2 | 12-60 | proposed; slow on CPU, sparse coverage |
| 10 | **Facade relief layers**: eyebrows, fins and cornices extruded as real geometry so they cast real shadows (already built by the facade grammar; extend to more styles) | built | 3 | 0.1-0.3 | 9-20 | buildings thread |
| 11 | **Lidar roadside skin**: a low mesh of hedges, walls and fences from classified lidar, coloured from the aerial | known (3D tiles) | 2 | 0.2-0.5 | 4-8 | not now: "melted" look near the car |
| 12 | **Photo impostor cards** for far roadside (cut-outs from licensed photographs) | known | 2 | 0.05 | 20 | not now: SA licence, coverage |
| 13 | **Monocular depth network** on street photographs | known | 3 | 0 (CPU) | n/a | blocked: no depth network with fully open training data found |
| 14 | **Gaussian splats** of hero spots from street imagery | known | 4 locally | 2-5 | 1 | not now: too costly at 28 W, sparse capture, SA licence |
| 15 | **AI texture generation** (FLUX.2 klein 4B, MatFuse) for missing material classes, offline only | known | 2 | 0 | 40 | detail-engine.md slice 5; human review of each output |

The top of the table is the same idea three times: **measure the real ground once on the CPU, keep it in one texture, and pay one texture fetch**. Methods 1, 2, 3 and 8 come from the same lidar download and fit in one per-tile "ground truth" texture (RGBA: height offset, two normal components, intensity or sky view). That is the recommendation.

## Prototype 1: Unbake the sun

**Problem.** The ground in the game is coloured by an aerial photograph. That photograph has its own sun shadows: dark, blue-tinted bands north of every building and tree. The game then draws its own shadows from its own sun. So a street has two sets of shadows in different directions, and the photograph's shadows stay when the game's sun is low, on the other side, or set. The ground shader already hides part of this with a rule ("blue and violet casts are tower shadows"), but it cannot find a shadow that is not blue.

**Method** (`lab/unbake-sun/`):

1. Read the lidar points of the tile and a 77 m margin (NOAA Miami-Dade 2021, Entwine point tiles, public domain; about 8 points per m2).
2. Make a surface height grid (highest return) and a ground height grid (class 2) on the photograph's own pixels.
3. **Fit the sun to the photograph.** For each sun azimuth and elevation, cast shadows from the height grid and measure how much darker the shadowed ground is than the lit ground. The best sun is the one whose shadows best explain the dark pixels. No date or time metadata is necessary. On the Miami Beach photograph the fit gives azimuth 170 degrees and elevation 44 degrees: a late-morning winter sun, which agrees with the shadows by eye.
4. Cast the shadows with that sun. For each surface class (ground, vegetation, water) and each colour channel, compare the shaded and the lit pixels near each shadow (a 12-pixel Gaussian window) and multiply the shaded pixels by that local ratio (limited to 1-4). Shadow edges are softened by 0.7 px.
5. Paste the result back into the photograph. The game reads `data/tiles/aerial/unbaked/<key>` when it exists.

**Measured** (z16 tile 18180_27907, Ocean Drive and Lummus Park):

| | Shaded / lit luminance, ground | Shaded / lit luminance, vegetation |
|---|---|---|
| Photograph | 0.51 | 0.70 |
| Unbaked | 0.70 | 0.96 |

1.0 is a photograph with no sun shadows. Ground does not reach 1.0 because the darkest tower shadows are deeper than the 4x limit and because shade that comes from sky occlusion stays (that is correct; method 8 will model it).

**Cost.** GPU: 0 ms (the same texture, the same fetch). CPU: about 3 s per z16 tile with a known sun, plus about 45 s to download the lidar; the sun fit (about 100 s) runs once per photograph. All inputs are public domain, so the result can be committed and shipped.

**In the game.** `SimCruiseTileData.cpp`: `BundledAerialPath` prefers the unbaked photograph. `-SimCruiseBakedAerial` turns it off for A/B tests. The 4 bundled Miami Beach aerial tiles are done; see the results section for the realism score.

**Next.** Run it in the atlas helper for every streamed tile with lidar (the helper already reads 3DEP Entwine for roofs and road grade), with the sun fitted once per photograph and cached. Use NAIP (0.3-0.6 m) near the road instead of the 2.4 m photograph, which makes the shadows sharper and the fit better.

## Prototype 2: Corridor relief from lidar ("depth from a grid")

**Problem.** The game's terrain comes from the USGS 1/3 arc-second grid, about one height every 10 m. A mountain road is a 7 m bench cut into the slope, with a ditch on the inside and a fill slope on the outside. At 10 m none of that exists, so the road is laid on a smooth slope and the cut faces, ditches and rock walls that a driver sees beside the car are missing.

**Measured** on Tail of the Dragon (US 129, Blount County lidar 2015, public domain, 4.4 points per m2 in a 400 m square at the start point):

| | 10 m grid (as the game) | Lidar ground at 0.5 m |
|---|---|---|
| Ground steeper than 35 degrees (cuts, banks, rock) | 1.6 % | 16.9 % |
| Height difference, RMS | | 0.71 m (95th percentile 1.25 m, maximum 10.1 m) |

The 10x difference in steep ground is the cut and fill along the road: exactly the surfaces that fill the side windows on a mountain road. The lidar intensity of the same points shows the edge lines and the road's surface, which is method 3 for free.

![10 m terrain, lidar ground, lidar intensity](images/world-detail/relief-dragon.png)

**Proposed in the game.** For each tile, a 0.5 m texture over the 40 m corridor of each road: R = lidar ground minus the 10 m terrain (metres), GB = normal, A = intensity. The terrain mesh near the car is refined to 1 m in that corridor (CPU, at tile build), and the ground shader takes its normal and its paint from the texture. One fetch per pixel. The road ribbon and its grading stay as they are (the road-engineering code owns the carriageway); the texture changes only the ground outside the ribbon. This needs the road owner's agreement before it goes in.

## Results in the game

Realism A/B on the PC (`scripts\realism.ps1`, ultra, traffic off, the build at commit `66e9129`):

| Aerial | REALISM line |
|---|---|
| Unbaked (default) | `REALISM lummus: 45.7 / 100  layout 31.8  tone 38.4  colour 50.3  structure 55.4  detail 67.2  sky 30.7` |
| Baked (`-SimCruiseBakedAerial`) | `REALISM lummus: 45.8 / 100  layout 32.1  tone 38.2  colour 50.0  structure 55.3  detail 67.0  sky 31.4` |

Nothing moved more than 0.7 on any part, within the run-to-run noise: from the lummus pose the near ground is the park and promenade materials, and the aerial photograph shows only in the far ground, so this score cannot see the change. The unbaked photographs stay on by default (the rule was to turn them off only if the total dropped by 1.0 or more). The change shows from above and at a distance, where the photograph is the ground (see the image in Prototype 1): in the drive capture's top-down shot at the Ocean Drive start the two frames differ by 4.2 / 255 per pixel, almost all of it the lifted shadows beside the road.

![Game, unbaked aerial | photograph](images/world-detail/ab-lummus-unbaked.jpg)

![Game, baked aerial | photograph](images/world-detail/ab-lummus-baked.jpg)

Right half of each image: Panoramax [4b6d216b](https://panoramax.xyz/#focus=pic&pic=4b6d216b-ae1c-48a8-b2ae-5d9558334f18) by MatthiasMatthias, CC BY-SA 4.0, resized.

## Sources

- USGS 3DEP lidar point clouds in Entwine format on AWS (`usgs-lidar-public`), US public domain.
- NOAA Office for Coastal Management, Miami-Dade 2021 lidar (Entwine, `noaa-nos-coastal-lidar-pds`), public domain.
- USGS The National Map, USGSImageryOnly (the bundled aerial photographs), public domain.
- Shadow detection and removal in aerial photographs with lidar: Zhan et al., "Shadow detection and removal for high resolution city aerial photo" (ISPRS XXXV congress, comm. 3, paper 378); true orthophoto with lidar DSM (ISPRS XX congress archives).
- Efros and Freeman, "Image quilting for texture synthesis and transfer", SIGGRAPH 2001.
- Depth Anything V2 model card (Hugging Face, depth-anything/Depth-Anything-V2-Small) for the training data that blocks it.
- Gaussian splats in Unreal Engine 5: NanoGaussianSplats (Digital Production, March 2026); EVolSplat (arXiv 2503.20168).
