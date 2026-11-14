# World detail v2: plan (14 November 2026)

Owner: Aaron. Approved by the coordinator. Branch: `world-detail-v2`.
Background: [realtime-context.md](../team/research/realtime-context.md),
[world-detail-methods.md](world-detail-methods.md).

## Rules

1. Measure first. Use lidar and OpenStreetMap (OSM) before you use a model.
2. AI only reads photographs into facts. AI does not draw the world.
3. Do not run AI on the game thread. Do not run AI on the GPU while the car drives.
4. Every source is free and needs no key or account (CONTRIBUTING.md).
   Record the source and the licence in `docs/data-sources.md` and `licenses/`
   in the same commit.
5. Each step has a test. Each step has frames from the Dragon and Ocean Drive
   at the highest graphics setting, and fps before and after.

## Models we can ship

| Model | Licence | Use |
|---|---|---|
| SDXL base 1.0 | OpenRAIL++-M | Detail kit textures, offline only |
| Real-ESRGAN x4v3 | BSD-3-Clause | Upscale (in use) |
| Moondream2 | Apache-2.0 | On the CPU: read street photos into facts |
| Florence-2 base | MIT | OCR and captions |
| SAM 2 small | Apache-2.0 | Masks |

Do not use: Depth Anything V2, Marigold, Depth Pro, SegFormer, or any model
trained on Cityscapes or Mapillary Vistas. Their weights or data are not
for commercial use.

## Build order

| # | Step | What it gives |
|---|---|---|
| 1 | One context record per 32 m of road | Surface, smoothness, edge, kerb, sidewalk, lit, barriers, land use left and right, Köppen class, season, species weights, a stable seed. All generators read it. |
| 2 | Familiar things | Street name signs in the local style; shop names on their buildings (text and brand colour, no logos); lamps, poles, signals and guard rails at their OSM positions |
| 3 | One detail layer over everything | A context map per tile and detail-kit taps within 40 m. Target 0.5 ms or less. |
| 4 | Facades from OSM tags | `building:levels` gives window rows; shop ground floor; colour, material, roof, `start_date` |
| 5 | Terrain | Rock and road cuts from 0.5 m USGS 3DEP lidar; a slope rule elsewhere |
| 6 | Tree species | OSM, city tree inventories, GBIF |
| 7 | Street photos | Panoramax (CC BY-SA, no token) into Moondream2, then facts in the place graph |

## Coverage and fallback

The world is made as you drive, anywhere in the US. Every method reads its data per
tile on a worker thread before the car arrives (4 s or more ahead), keeps it in a disk
cache, and has a fallback where the data is missing. Check each licence before use and
record it in `docs/data-sources.md`.

| Method | Data (coverage, licence) | Fallback |
|---|---|---|
| Context record | OSM roads (all US, ODbL); road-edge tags are thin on rural roads | Rules by road class, curb/edge, land use and climate |
| Climate | Köppen-Geiger 1 km (global, CC BY 4.0); NLCD 30 m (CONUS, public domain) | Latitude band |
| Buildings | OSM; Microsoft US Building Footprints, Overture buildings (nearly all US, ODbL) | Height and style rules |
| Shops and landmarks | OSM; Overture Places (CDLA-Permissive-2.0); Wikidata (CC0) | No sign |
| Trees | USFS TreeMap species (30 m CONUS, public domain); LANDFIRE canopy height (public domain); city inventories; OSM | Species mix from Köppen and NLCD |
| Terrain | USGS 3DEP lidar (most of CONUS); 10 m DEM (everywhere) | Slope rule for rock and road cuts |
| Aerial | NAIP, the existing tiles | Land-cover colour |
| Street photos | Panoramax (thin) | Rules |

Test every item on random US places, not only the hero roads: a Mojave desert road,
a Colorado mountain pass, the Kansas plains, a Maine forest, the Oregon coast, an Atlanta
suburb, a small Iowa town, downtown Chicago, northern Minnesota, south Texas. Look at
real frames at the highest setting before and after each item.

## Familiar things: sources by rank

1. OSM street names.
2. OSM shops, with the Name Suggestion Index (BSD-3-Clause) for brand colours.
3. OSM lanes and markings, with MUTCD (public domain).
4. OSM street lamps, poles, signals and guard rails.
5. OSM building tags, then street photos.
6. Wikidata (CC0) landmarks.
7. OSM, city inventories and GBIF trees.
8. Lidar intensity for road markings (US).

## Work rules

- Commit and push the branch after every step. Push to `main` only when
  `scripts\check.ps1` (full tier) passes. Do not force-push.
- Before you change a file that another lane changed in the last 24 hours,
  keep your change small and additive.
- Do not run the game or the editor while another lane runs a pre-flight or
  packages. Open the editor only with `scripts\editor.ps1`. Do not use Live Coding.

## Status for v2.0.3 (14 November 2026)

In (release cut `world-detail-v2-release`; same frame times as main at a true 75 %:
Dragon p95 16.7 ms, Ocean Drive p95 16.8 ms):
- Context record per 32 m of road (OSM edge tags, else rules), read by palms, furniture, grass.
- Junction road steps fixed (per-column fade); a road-step check per tile and in the tests.
- Tiles load ahead by speed on the route (2 x load + 4 s, one slot kept for the route).
- Fallback ground slabs under tiles in range that have no collision.
- Köppen-Geiger 1991-2020 global grid (0.05 degree); season and grass dryness from it.
- Rural cross-section: foreslope, ditch, backslope cut or fill slope (terrain mesh only).
- Albedo and saturation limits on lawns and meadows (ground and landscape).
- Roof colour from the aerial photograph inside each footprint; OSM guard rails.
- Scan core (SCN1 v3, rules-only scan, disk cache, class-map view) and the eye fusion;
  opt-in with `-SimCruiseScan` / `-SimCruiseScanView` / `-SimCruiseScanEye=<name>`, off by default.

Moves to v2.0.4 (on `world-detail-v2`; they cost frame time in the first measurements and
need a profile at a true 75 % before they ship):
- Seamless ground patterns (1664 m anchor, integer hash in every lattice).
- Steep ground: soil and rock, triplanar, mottling.
- TINT in the ground material.
- Tree detail levels with near limits and a dithered cross-fade.
- SSURGO soil colour and USGS SGMC rock type; HPMS and lidar fit of the cross-section.
- A 1 s cross-fade for late tiles; facades (FACD), roof parts, MATL/RSTR/GENR readers.
- Trees grown to lidar or CHM crowns, impostors, TREE chunk.
- Prediction queue, speed rule, standstill ladder, the scan governor and two passes.
