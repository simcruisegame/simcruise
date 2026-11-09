# A measured roof for the Clay Center, Charleston, West Virginia

The public lidar survey and two mapped building outlines reproduce the stepped roof and one planetarium dome at 38.34810° N, 81.62939° W. This opt-in authoring path has one accepted work unit. The game uses a packaged profile only when the live footprint rings match. Its two aggregate material colors are specific to this building.

## Reproduce the packaged profile

From this directory, use Python 3.14 and the pinned analysis dependencies. The replay verifies the byte count and SHA-256 for every source object before decoding any measurements. The nineteen LAZ nodes total 4,449,010 bytes. The first run can download the nodes. Subsequent runs work offline.

```sh
python3 -m venv venv
venv/bin/python -m pip install -r requirements.txt
venv/bin/python replay.py charleston/site.json --fetch --cache /tmp/charleston-laz --output /tmp/charleston-roof
cmp /tmp/charleston-roof/lidar-roofs-17907-25198.json ../../../game/data/lidar-roofs-17907-25198.json
godot --headless --path ../../../game --script res://checks/LandmarkRoof.gd
```

The checked profile is 149,226 bytes, SHA-256 `e50307f75832daf37dfe79dc93a0da6e1729f32bcc6a3d48724c8253f9a6ae71`. The Godot regression loads all 15 prior Miami profiles, an unmatched Cambridge tile, the Charleston case and a perturbed footprint.

## Geometry and holdout

The USGS 3DEP/FEMA work unit WV_FEMAR3_Southcentral_B3_2018 was acquired between November 17, 2018 and April 16, 2019 at Quality Level 2. Within the clipped area, the nodes contain 239,635 returns from flight strips 10010 and 10011. The work unit labels ground but provides no building class for this site. Ground around the footprint supports a robust local reference elevation of 182.993 m. The two strip estimates differ by 0.09 m. Heights in the profile are centimetres above that reference, placed on the game’s terrain datum. [USGS collection metadata](https://prd-tnm.s3.amazonaws.com/index.html?prefix=StagedProducts/Elevation/metadata/WV_FEMAR3_Southcentral_2018_D19/WV_FEMAR3_Southcentral_B3_2018) · [Public EPT](https://s3-us-west-2.amazonaws.com/usgs-lidar-public/WV_FEMAR3_Southcentral_B3_2018/ept.json) · [Verified node manifest](charleston/point-tiles.json)

The 1.6 m grid is clipped to the outside ring and any holes. At each vertex both flight strips must have multiple nearby first returns agreeing in elevation. The two strips corroborate 93.07% of vertices, covering 89.13% of the footprint area with all three triangle vertices corroborated. Other vertices use the existing 5 m map height. The edge line and vertical wall faces derive from the mapped footprint and roof envelope. Doorways, glazing and individual wall surfaces lack survey support in this reconstruction.

A round footprint inside the larger building supplied the candidate region for a sphere fit. One flight strip contributed 842 inliers and the other supplied 926 confirmation inliers. The fitted radius is 13.07 m. Only 96 vertices within the observed sectors and already within 0.62 m of the measured grid were adjusted. The architect independently identifies the planetarium dome. [ZMM Architects & Engineers](https://zmm.com/projects/clay-center)

One seventh of 0.48 m spatial cells were withheld before both fits. The subsequent tests include every unclassified first return inside the footprint’s 0.8 m inset. Nearby points can come from the same flight and scan. Sharp changes in roof height account for much of the remaining error. [Point-by-point evaluation summary](charleston/independent-geometry.json)

| Reserved points | Number | Original 5 m roof, MAE | New geometry, MAE | New geometry, 90th percentile |
| --- | ---: | ---: | ---: | ---: |
| All first returns | 4,925 | 13.630 m | 1.463 m | 4.454 m |
| Locally coherent surface cells | 2,108 | 14.740 m | 0.264 m | 0.531 m |
| Central half-radius of dome | 83 | — | 0.060 m | 0.106 m |

## Street-camera comparison

Only two outer sequence frames set the aggregate colors. The authoring camera alignment used their skyline silhouettes. The ADE20K building masks are eroded and z-buffered against the measured geometry before sampling. Regular roof and steep faces had repeated medians of [76, 64, 61] and [78, 73, 70] in eight-bit sRGB. The dome color differed across views and keeps the game's roof material. These local appearance observations include the lighting in the photograph. They do not measure intrinsic reflectance. [Camera inputs](charleston/site.json) · [Color observations](charleston/palette-qa.json)

The capture car traveled southeast on Lee Street East through its intersection with Leon Sullivan Way. Frame 2860 is 2.02 m from the mapped Lee Street East centerline, heading 136.44°. Frames 2860 and 2861 were withheld from color sampling and camera registration. The paired GPU renders used the same recorded position and heading, 85° horizontal field of view, 1.65 m camera height and zero pitch and roll. The source-camera calibration, fitted only during photo authoring, does not change those settings. Each scene was also rendered with the original flat building at the same pose.

| Withheld frame | Skyline mean absolute error | Photographed building RGB error | Photographed sky RGB error |
| --- | ---: | ---: | ---: |
| 2860 | 43.5 → 24.9 px | 91.60 → 63.82 | 18.74 → 26.37 |
| 2861 | 52.8 → 19.6 px | 91.26 → 74.89 | 19.15 → 27.29 |

To repeat the image score with the original photograph and full-resolution paired captures, run the following command. On a staged checkout the standard Godot reference entry point accepts the recorded latitude, longitude and bearing followed by the horizontal field of view, output JPEG, pitch and roll. Capture the control with the Charleston roof JSON omitted, restore the profile, and repeat with identical renderer settings and map cache.

```sh
godot --path ../../../game --resolution 1024x768 -- --reference 38.348449 -81.630658 136.44 85 /tmp/updated.jpg 0 0 --complete-reference
venv/bin/python score_view.py original-photo.jpg charleston/2860-labels.png /tmp/control.jpg /tmp/updated.jpg
```

The sky silhouette comparison covers 254 left-side columns at 640 × 480. RGB errors are mean absolute 8-bit channel differences in a separate fixed region at 513 × 385. The measured building still projects into some photographed sky, and frame 2860’s 90th-percentile skyline error rose from 58 to 66 pixels. The gallery preserves the full-scene comparisons alongside these results. [Matched photographs and renders](../../holdout/views/charleston-clay-center/manifest.json) · [All four skyline results](charleston/confirmation-scores.json) · [Photometric results](charleston/photometric-qa.json)

## Reuse and credits

A different site needs a pinned EPT node manifest with a metric vertical unit and bounds in its source horizontal CRS, a local projected metric CRS, the map footprints, their original default height, and an interior anchor. The optional color stage expects two calibrated views with ADE building masks, including recorded headings and camera centers. Unusual surfaces, vegetation inside a footprint, survey changes, uncertain local ground and poor optical registration can defeat this approach. Regional reconstruction requires independent per-site validation before adding any profile.

USGS 3DEP / FEMA supplied the public lidar. OpenFreeMap and © OpenStreetMap contributors supplied the two adapted footprint polygons from vector tile z14/4476/6299. IDs in the fixture identify records in that source tile. [USGS copyright](https://www.usgs.gov/information-policies-and-instructions/copyrights-and-credits) · [OpenFreeMap](https://openfreemap.org/) · [OpenStreetMap attribution and ODbL](https://www.openstreetmap.org/copyright)

Training and comparison photographs are by wolfgang8741, © Grab and KartaView contributors, shared under CC BY-SA 4.0. Training PNGs are resized derivatives and the included masks are segmentation derivatives. Comparison WebPs resize the respective original and combine it with two engine renders. These photographic adaptations are offered under the same license. The source camera did not report a usable capture date. [Training frame 2858](https://kartaview.org/details/3485953/2858) · [Training frame 2863](https://kartaview.org/details/3485953/2863) · [Withheld frame 2860](https://kartaview.org/details/3485953/2860) · [Withheld frame 2861](https://kartaview.org/details/3485953/2861) · [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)

The cached ADE20K masks were generated with the DeepLabV3 MobileNetV2 TFLite model. Only derived label rasters and the model digest are bundled. [Model publisher and license](https://www.kaggle.com/models/spsayakpaul/deeplabv3-mobilenetv2/TfLite/ade20k/2)
