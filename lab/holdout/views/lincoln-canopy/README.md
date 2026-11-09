# Surveyed curb trees at J Street, Lincoln

The camera is at 40.808016667, −96.675327778 on August 19, 2025, 13:40:57 UTC, heading 91°, 74° horizontal field of view, zero pitch and roll, height 1.65 m. Its reported horizontal accuracy is 5 m. The right-curb linden T028691 stands 14.2 m east and 10.9 m south of this location, 95.5 m before the eastern tile boundary. The city data records active status and 21 inches DBH. Its public record was edited in March 2024. The date of the diameter measurement is unknown. [Street photo by slinky309](https://panoramax.xyz/#focus=pic&pic=202ef7e6-ea03-4311-a626-b9f3b8405a52) · [City of Lincoln Parks tree records](https://gis.lincoln.ne.gov/public/rest/services/Parks/Trees/MapServer/0)

The nearest canopy-model peak was 7.66 m from the trunk, with an estimated height of 11 m and estimated crown width 10.8 m. Its old urban street mask gave the peak a pavement score of 0.753 and the actual trunk a score of 0.008. Nine raw canopy-model samples along the segment read 8, 10, 10, 10, 11, 11, 11, 11 and 11 metres. A reciprocal, spatially unique match now anchors that existing crown to the surveyed trunk. The rule requires a mature stem, sufficient crown width, continuous raster support, a 9.5 m absolute maximum and clearance from competing stems and peaks. The city-tree filter continues to exclude retired records. Two nearby one-inch stems remain untouched. Existing, independently sampled 2016–17 winter USGS returns support airborne canopy at the linden, reaching 16.6 m at the 97th percentile within 4 m. Those returns only corroborate the research and are never retrieved by this feature. The displayed height comes from the same canopy model that was already loaded.

A separate streaming-order case caused the provisional road-width buffer to discard valid crowns. The native fixture retained 54 inventoried trees before aerial road edges were available versus 114 with the same edges present before the canopy. Upon arrival of the final aerial mask, Godot now rechecks any provisional placements once, including its traffic-lane/wall guards, and invalidates the visible tree batch. The warm matched photograph already had the mask. Its image comparison isolates the corrected linden match. The photographed crown is broader and catches the sunlight differently.

## Checks

The camera, sun, road, car, map and paused simulation were held fixed in the 960 × 720 Metal comparison. The initial frame, control and repeated control were pixel-identical. Of 691,200 pixels, 72,198 changed with the extended match. The lower 249,600 pixels and selected 60,000-pixel road surface patch were identical. At image scale 400 × 300 the upper-right silhouette was measured along rows 18, 33, 48, 63, 78 and 93, searching from the center rightward for fifteen of twenty nearby pixels darker than the luma threshold. Mean absolute errors in frame-width percentage points follow.

| Luma threshold (0–255) | Previous | Surveyed |
| --- | ---: | ---: |
| 130 | 33.58 | 8.71 |
| 145 | 36.00 | 11.00 |
| 160 | 36.62 | 11.58 |

In this Lincoln tile, three of 376 records changed, including the linden and two other unambiguous municipal trees. The number of peer-sized trees stayed at 39. A second city test in Cambridge, Massachusetts paired one more active elm (city inventory 7222) to a 19 m crown over a continuous 7.56 m canopy. Of 435 peaks, prior matches remained and all 88 local peer estimates were retained. The Cambridge check compares source evidence for the pairing. It has no registered photographic evaluation. Both tests reuse the already downloaded 512 × 512 canopy grid, and make zero additional requests. The nine sampled canopy heights, stem diameters and models for both cities are in second-city.json.

Run the small-source negative controls with `bun lab/check_civic_trees.mjs`. Run the deterministic Godot arrival-order and lane-exclusion test with `godot --headless --path game --script res://checks/CrownArrivalOrder.gd`. The comparison panel is CC BY-SA 4.0, credited in manifest.json. See licenses/Road Atlas.txt for the city and canopy source licenses.
