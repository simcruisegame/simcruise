# Local AI on the player's CPU (AI team, 14 November 2026)

Goal: local AI that adds to the game with the hardware the player has, without GPU time:
CPU cores the game leaves idle, RAM, the disk cache, and work done ahead of the car or while
the game loads or is paused. Owner: AI thread. Branch `ai/cpu-models`.

## 1. The budget on the reference PC

Measured on BarlowFamilyPC (laptop): Intel Core i9-10885H, 8 cores / 16 threads, 2.4 GHz base,
16 MB L3; 31.7 GB RAM; RTX 2070 Super Max-Q.

| What | Value | How |
|---|---|---|
| Game CPU while it drives the Tail of the Dragon (Ultra, 1280x720, traffic on) | median 2.9, mean 3.3 hardware threads busy, p90 4.5, peak 10.8 (tile builds) | `Process.TotalProcessorTime` of the game, once a second, during `scripts\preflight.ps1 -Roads dragon -Cars nb2001_aaron` (pre-flight PASS, frame p95 17.0 ms, 0 hitches) |
| Idle while driving | about 11-12 of 16 threads | 16 minus the game's use |
| Game RAM | 5.0 GB working set at the end of a 60 s drive | same log |
| Free RAM for AI | about 20 GB on this PC (32 GB minus game, OS and editor) | Windows counters |
| Disk | D: 22 GB free, C: 152 GB free | |

The scan engine contract (`docs/research/scan-engine.md`) gives all eyes together 2 cores,
1.5 GB and 3 s per zoom-16 tile. The measurement shows that the budget is safe: two busy cores
still leave 9-10 threads idle.

## 2. Licence rule

Aaron's rule (14 November 2026): if a model's makers do not say we cannot use it, we can; what it
was trained on is the makers' responsibility. The gate is the publisher's licence for the weights,
for a free open-source game that redistributes them. We still obey every licence term (attribution,
share-alike, use limits). Weights marked non-commercial or research-only stay out. We record the
known training data of each model so the choice is open.

ODbL and trained weights: we treat weights trained with OpenStreetMap labels as a Produced Work
(ODbL section 4.3): it needs the notice "contains information from OpenStreetMap, (c) OpenStreetMap
contributors, ODbL" in the credits and the model card, and no share-alike of the weights. The scan
files that the game writes on the player's PC are a Derivative Database used privately, so ODbL
share-alike does not apply to them; if we ever publish scan files, they go out under ODbL.

## 3. Models

CPU times are measured on this PC with ONNX Runtime on 2 threads unless marked (estimate).

| Model | Weights licence | Known training data | Size | CPU speed | What it gives the game | Status |
|---|---|---|---|---|---|---|
| Satlas Aerial_SwinB_SI (Allen AI) | ODC-BY 1.0 | SatlasPretrain: NAIP (PD), OSM and MS Buildings (ODbL), WorldCover (CC BY 4.0), own annotations (ODC-BY); ImageNet start | 89.6 M params; ONNX 352 MB fp32, 88 MB int8 | 1.40 s per 512 px tile (1 m/px), 0.34 s per 256 px; peak RAM 0.74 GB fp32, 0.50 GB int8 | Backbone of the aerial eye: surface classes per cell | **In use** (teacher; heads trained by us) |
| SimCruise aerial head | MIT (ours) | USGS aerial (PD), OSM (ODbL), Meta/WRI CHM v2 (CC BY 4.0), ESA WorldCover 2021 (CC BY 4.0) | 0.08 M params | under 5 ms | Class probabilities from Satlas features | **In use** |
| SimCruise aerial student (planned) | MIT (ours) | Same labels plus the Satlas eye's soft labels | 2-8 M params (target) | target p95 1.5 s per tile, 2 cores, int8 | The shipped eye (Satlas stays a development teacher) | Next |
| DeepForest tree (weecology) | MIT | NEON 10 cm RGB (CC BY 4.0), lidar-made labels plus hand labels | 32.3 M params | (estimate) 0.3-0.6 s per 400 px window | Tree crown boxes, but trained at 10 cm: weak on 0.6-1 m NAIP | Low priority: CHM v2 crowns already exist |
| Meta/WRI canopy height v2 (map, not a model we run) | CC BY 4.0 (data) | DINOv3 model by Meta | data only | 0 (download) | Canopy height and crowns | Already in the game (helper) |
| SAM 2 tiny / small | Apache-2.0 | SA-1B (research licence), SA-V | 39 / 46 M | (estimate) 1-2 s per image | Masks of objects | Allowed; no clear use yet |
| Florence-2 base | MIT | FLD-5B (Microsoft, not published) | 0.23 B | (estimate) 2-4 s per image | OCR of signs, captions | Candidate for street photos |
| Moondream2 | Apache-2.0 | not published | 1.9 B | (estimate) 5-15 s per question | Read a street photo into facts (storeys, material, colour) | Candidate (place graph, offline per place) |
| SmolVLM 256M / 500M | Apache-2.0 | The Cauldron, Docmatix (mixed licences, listed) | 0.26 / 0.5 B | (estimate) 1-3 s per question | Same as Moondream2, smaller | Candidate |
| SmolLM2 360M / 1.7B Instruct | Apache-2.0 | FineWeb-Edu, DCLM, The Stack, SmolTalk (listed) | 0.36 / 1.7 B | (estimate) 20-60 tokens/s at 360M | Short place cards from map facts | Low priority |
| Kokoro-82M | Apache-2.0 | "permissive / non-copyrighted audio" (self-reported; CC BY parts) | 82 M | (estimate) faster than real time | Spoken GPS directions with street names | Candidate; phonemiser espeak-ng is GPL: keep it out, use a dictionary G2P |
| Piper voice en_US ljspeech | voice: MIT; data LJ Speech (PD) | LJ Speech | 60-114 MB | (estimate) faster than real time | Same; engine needs espeak-ng (GPL) | Candidate |
| Real-ESRGAN General x4v3 | BSD-3-Clause | DIV2K (research), others | 1.2 M | GPU today | Sharper aerial photo | In the game (GPU, idle time) |
| Depth Anything V2 Small | Apache-2.0 (Small only; Base/Large CC BY-NC) | synthetic + pseudo-labelled | 25 M | (estimate) 0.3 s | Depth from street photos | Allowed (Small only); no clear use yet |
| Ultralytics YOLO | AGPL-3.0 | COCO | | | | **Excluded** (copyleft would bind the game) |
| Any weights marked non-commercial / research-only (DINOv3 licence, Depth Anything V2 Base/Large) | | | | | | **Excluded** |

## 4. Uses, ranked

Gain for the player, CPU cost, and what happens when the model is absent.

| # | Use | Gain for the player | CPU cost | Fallback |
|---|---|---|---|---|
| 1 | **Aerial eye: surface class per cell** (road, roof, water, tree, grass, crop, shrub, bare, built ground, wetland, snow) for the scan engine | The ground matches the photo where OSM has no polygon: driveways, parking lots, dirt lots, yards, fields, desert, unmapped buildings. Works at any US place. | 1.4 s per tile (teacher), target 1.5 s p95 for the student, on its own low-priority thread; once per tile, then the disk cache | Rules: OSM, NLCD, WorldCover, Köppen (the scan engine's rule layer) |
| 2 | **Self-check of each built tile against its photo** | Wrong guesses (a lawn that is really gravel, a missing building) are found and corrected before the car arrives | about 0.1 s per tile on top of use 1 (compare the eye's classes with the generated cells) | No check |
| 3 | **Material and colour per place** from the de-lit photo and the CC0 library (colour transfer first, a small CC0-trained material model later) | Asphalt, dirt, grass and roof colours of this place, not a generic one | 0.1-0.5 s per material, once per place | The fixed detail kit |
| 4 | **Street photos to facts** (Florence-2 OCR, Moondream2 or SmolVLM) at load or pause | Storeys, wall material, shop names on the right buildings | 2-15 s per photo, once per place, offline-first | OSM tags and rules |
| 5 | **Spoken directions** (Kokoro or Piper) | Natural GPS voice with real street names | faster than real time, only on a route | Text on the HUD |
| 6 | **Tree crowns** (DeepForest) | Single trees in towns where CHM is coarse | 0.5-1 s per tile | CHM v2 crowns (in the game) |
| 7 | Traffic and driver behaviour from a small learned policy | Small; rules (IDM, HCM gaps) already work | small | Rules |

## 5. The first prototype: the aerial eye

- Training data (`lab/scan/scan_data.py`): 357 zoom-16 tiles at 512 px from the 18 test places and
  30 random places in the lower 48; labels from OSM (buildings, roads paved and unpaved, water),
  CHM v2 (trees at 3 m and higher), WorldCover (the rest), with a 2 px ignore band at mapped edges.
- Teacher (`lab/scan/satlas_eye.py`): Satlas backbone and FPN, frozen; our 0.08 M-parameter head on
  the 1/4-resolution features, trained on full-resolution tiles and on tiles degraded to the game's
  own zoom-14 photo (2.4 m/px). Held-out places: Gilbert AZ, Seattle WA, Jasper AR, Selma AL,
  Hilo HI and three random places.
- In the game (`Live/SimCruiseAerialEye`): the world streamer queues each tile with its photo; one
  thread at below-normal priority cuts the tile out of the zoom-14 photo, runs the ONNX model with
  NNERuntimeORTCpu, writes 12 channels of 256 x 256 (the scan contract names) to
  `Saved/Scan/eyes/satlas-v1/16/<x>/<y>.eye`, and keeps the last 64 tiles in memory. A second drive
  reads the disk cache and runs no inference. `-SimCruiseNoEye` turns it off; without the model file
  it is off.

Results: section 6.

## 6. Results

(filled in as measured)
