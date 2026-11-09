# Measured highway striping

Two Utah stretches use measured road-paint positions in the ordinary driving scene. US 163 near Monument Valley contributes five contiguous accepted inventory records, MP 12.800–13.298 (798 m). US 89 in Logan Canyon contributes three, MP 461.600–461.900 (481 m). Five and three passed out of eight and five records tested respectively. The acquisition adapter can query the federal inventory across states. The dated striping adapter currently understands Utah. Runtime coverage is limited to the five included driving tiles.

## Evidence

At 37.101444, −109.990814, bearing 233.2°, the HPMS US 163 route lies 4.27 m from the camera and has two through lanes. UDOT reports 523 ft of broken yellow and 1,046 ft of solid white across MP 13.0–13.1 on August 1, 2023, with zero solid yellow. The federal record contains no lane width at this camera. Another HPMS record with a lane width ends about 80 m away. A separate UDOT shoulder survey reports one foot of asphalt on the inventory-direction right, with gravel or soil beyond. The left shoulder remains unknown. The game does not change pavement width, shoulder geometry, road elevation, grip, or collisions in this study. [UDOT paint catalog and terms](https://www.arcgis.com/home/item.html?id=65478779f74d4fcbb6f7dd3411aaaec2) · [UDOT shoulders](https://www.arcgis.com/home/item.html?id=399d7ff388f442f29054ff0092a4c7d4) · [Federal Utah HPMS 2024](https://geo.dot.gov/server/rest/services/Hosted/HPMS_FULL_UT_2024/FeatureServer/0)

The Logan Canyon control at 41.7418193, −111.7723689, aligned with US 89, has 1,055 ft each of solid yellow and solid white coverage and zero broken yellow in its 0.1-mile record on September 27, 2023. Each quantity represents the combined length of two parallel lines. In Moab, inventory records with complex line quantities were excluded. The Florida and Washington profiles demonstrate that missing or unrelated data can leave paint unknown. The raw Skykomish selection pin is about 324 m from the federal US 2 centerline and returned null. The ordinary game later snapped that pin to the mapped road. The independent point on US 2 was tested at 47.71149, −121.3726639.

## Placement and interpretation

The author checks position (9 m), road-axis difference (12°), unambiguous route identity, two-way facility type, and two through lanes. The route identifier must also agree between the two inventories. The Utah inventory distinguishes two simple layouts using route length, aggregate line quantities and passing designation. USDA 0.6 m NAIP photographs from August 22, 2021 on US 163 and August 13, 2021 on US 89 locate two neutral bright edges in alternating training and withheld longitudinal bands. At least three bands in each group must agree. The maximum withheld lateral deviation is 1 m and the median limit is 0.35 m. The source pixels and agency coordinates set the absolute accuracy. The 0.075 m median on the camera segment describes repeatability within a single acquisition. [USDA NAIP image service](https://imagery.geoplatform.gov/iipp/rest/services/NAIP/NAIP_plus/ImageServer) · [FHWA data definitions](https://www.fhwa.dot.gov/policyinformation/hpms/fieldmanual/page05.cfm)

At the US 163 camera, measured stripe separation is 6.59 m and the broken centerline cycle is 12.20 m. Its period and onset fit the first 65% of the segment and must retain contrast in the last 35%. The Logan Canyon control measures 7.43 m between the white edges. The 0.12 m line width, 0.22 m double-line center spacing and one-quarter painted duty fraction are rendering assumptions. Positions between knots follow the surveyed centerline with the measured lateral offsets. The game compares strokes with the current two-way, paved-or-unspecified main road within 5 m and 12°. It draws accepted strokes on the existing road surface. The line overlay is exclusively visual.

The fixed Metal reference camera uses 960 × 720, heading 233.2°, horizontal field of view 69.4° and pitch −2°. In the photo the white edge is visible on 23 of 23 transverse foreground rows, zero in the original game and 23 with the inventory. Median absolute residual is 24 pixels. [Photograph and Metal comparison](../landscapes/utah/striping/road-comparison.webp) · [Image-space readings](camera-check.json)

## Repeat

Python 3.10 or later and Pillow are required for live authoring. Run from this directory. The environment variable SIMCRUISE_ROAD_CACHE can point to a disposable directory. Responses are cached by full URL. The three inventory requests are bounded to 128 records or fewer; the optional USDA query and one 1,000 × 1,000 JPEG are capped at 32 kB and 950 kB. New collections or retired endpoints can change the results of a live retest. The checked-in measurements preserve the accepted source IDs, dates, fitted bands and map axes.

```sh
python3 road_profile.py 37.101444 -109.990814 233.2
python3 survey_paint.py 37.101444 -109.990814 /tmp/us163-measured.json
python3 survey_paint.py 41.7418193 -111.7723689 /tmp/us89-measured.json
python3 bake.py monument-measured.json logan-canyon-measured.json --output /tmp/verified-stripes
godot --headless --path ../../game --script "$PWD/check_road.gd"
```

A geometry rebake recreated all five shipped tile documents. Godot accepted 673 of 702 strokes using the five contemporaneous OpenFreeMap driving-tile centerlines, including all 238 in the US 163 camera tile. Of 29 unrendered strokes, 28 were in the eastern Logan Canyon tile. The gate also preserved default paint on the offset control and rejected a perpendicular alignment. The small road-axis fixture is © OpenStreetMap contributors under ODbL. [OpenStreetMap terms](https://www.openstreetmap.org/copyright)

UDOT publishes these datasets for informational use, requests field verification before project use, and disclaims warranties. This is a simulation of historical records. The comparison photograph is by Awinda, April 30, 2023, CC BY-SA 4.0. The shared comparison crops, resizes and labels it. [Original photograph](https://commons.wikimedia.org/wiki/File:Forrest_Gump_Highway_Monument_Valley.jpg) · [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
