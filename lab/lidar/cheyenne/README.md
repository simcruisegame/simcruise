# Cheyenne Capitol survey

An exact OpenStreetMap footprint match enables a measured 3D roof in tile 13686,24537. The former mapped extrusion was 5 m high. The new model separates the ordinary roof, drum, gilded dome, lantern and finial. This is one prepared landmark. Other tiles retain their existing coverage.

## Geometry

Five USGS requests totaling 336,236 bytes identified and retrieved the public-domain June 17–30, 2021 survey leaf. A local plane fitted to 2,376 classified ground returns supplies the building datum. The roof uses unclassified returns inside the map polygon, partitioned into training and withheld spatial cells. The state's independent hands-on survey gives the eight-sided drum, sixteen-sided shell and eight-sided lantern, and the Legislature reports a grade-to-tip height of 146 feet. The finial is an envelope. Columns, openings and ornament do not have individually measured solid geometry. The script stores factual dimensions in metres and does not include the architects' drawings. [USGS point cloud](https://usgs-lidar-public.s3.amazonaws.com/WY_Laramie_1_2021/ept.json) · [State dome investigation, sheet K.10](https://www.wyomingcapitolsquare.com/s/2014-Level-I-II-Vol-3-pgs-1-32.pdf) · [Wyoming Legislature](https://wyoleg.gov/LSOResearch/2006/06rm049.pdf) · [OSM way 112916176](https://www.openstreetmap.org/way/112916176)

## Photographic evidence

The source controls, camera matrices, original image URLs and SHA-256 checksums are in registration.json. Hand-reviewed donor masks exclude sky, vehicles, banners, vegetation and street furniture. The reusable baker also checks donor depth, visible sides and local agreement in two independent photographs. A section of facade is eligible only when at least two photographs reproduce each of its bounding control stations within 1.25 m. At the two eastern interior controls the Library of Congress image misses by 2.38 m and 3.52 m. The entire eastern correction is withheld. The east wing still shows the renderer's prior illustrative openings in the documented stone finish. No neighboring window bay is repeated onto an unknown wall. Source coverage remains incomplete even within the admitted west and central spans.

The validation photo supplies six ground-level camera anchors and the date of the sun position. Its surface colors and upper silhouette do not enter training. The registered camera is about 3.4 m from the phone GPS fix. The source bearing has an approximately 18° compass discrepancy. Both game images share the same lens, eye height, pose and simulated sun. Ordinary held-out roof returns have a median error of 0.09 m and a 90th-percentile error of 0.64 m. Tower radial error is 0.38 m and 0.94 m. Those returns belong to the original survey strip. The separately photographed skyline is an independent witness.

## Rebuild and drive

The offline scripts require Python with NumPy, SciPy, Pillow, Shapely and laspy with LAZ support. Obtain the one leaf from survey.json and the three photographs from registration.json, saving them under their listed filenames. The checksums protect against silent source changes. The map ring is normalized to its zoom-16 tile and retains the OpenStreetMap ODbL attribution.

```sh
python geometry.py /path/to/10-404-344-13.laz /tmp/capitol-geometry.json
python bake.py /path/to/photos /tmp/capitol-output --geometry /tmp/capitol-geometry.json
godot --path ../../../game -- --capitol-drive-proof /tmp/capitol-approach.jpg
```

geometry-check.json and source-check.json are the generator receipts. The photo atlas and game comparisons are CC BY-SA 4.0 adaptations. The depth map and packet stay local during play. The game falls back if the precise footprint changes or a texture is missing. [Bradlyons, date unknown](https://commons.wikimedia.org/wiki/File:Wyoming_State_Capitol.jpg) · [Gates Frontiers Fund Wyoming Collection, Carol M. Highsmith Archive, Library of Congress, Prints and Photographs Division, July 24, 2015](https://www.loc.gov/item/2017884408/) · [GRALISTAIR, February 29, 2020](https://commons.wikimedia.org/wiki/File:Cheyenne_WY_Capitol_Building.jpg) · [Dough4872, October 25, 2026, independent comparison](https://commons.wikimedia.org/wiki/File:Wyoming_State_Capitol_from_24th_Street_and_Capitol_Avenue.jpeg) · [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)

The final Metal check drove 42.26 m north on Capitol Avenue at a 13.27 m/s peak with four tire contacts. Fixed central-facade luminance error fell from 72.14 to 68.55 on a 0–255 scale. Pattern correlation rose from 0.024 to 0.282. See runtime-check.json for exact asset hashes. To reproduce the optical and solar comparison, run the general reference check with latitude 41.13975559, longitude −104.81982824, bearing 333.72646°, horizontal field of view 72.24724°, pitch-down −9.94941°, roll 1.15464° and --reference-epoch 1792960080.
