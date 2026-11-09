# Surveyed pavement markings

Southbound US 1 at 25.087060, −80.449145 in Key Largo has two straight-ahead arrows, one per through lane. The 1.0.3 development tile also contains nine observed divider dashes and three sections of the right edge stripe totaling 118.4 m. The field measurements are registered in WGS84. The renderer clips the guessed center dashes only between consecutive measured ones on the named one-way carriageway. It draws small terrain-following pieces with the existing road-paint material. [Street and render comparison](../holdout/views/key-largo-road-paint/street-and-renders.webp) · [Stock driver camera](../holdout/views/key-largo-road-paint/cockpit.webp)

## What the map supplies

OpenFreeMap’s November 1, 2026 vector tile gave the selected Key Largo way its trunk, one-way and U.S. highway network attributes. The Boston way had primary, paved and one-way attributes. The original OpenStreetMap ways both specify two lanes and carry no turn-lane tag. The October 21, 2026 Overture transportation archive contained segments, connectors and regulations. Neither sampled tile encoded the painted footprints. Reading the three PMTiles ranges used 72,922 bytes in Florida and 507,451 bytes in Massachusetts. These checks apply to the two sampled archive tiles. [OpenStreetMap Florida way](https://www.openstreetmap.org/way/52069511) · [OpenStreetMap Boston way](https://www.openstreetmap.org/way/86134690) · [OpenFreeMap](https://openfreemap.org/) · [Overture transportation documentation](https://docs.overturemaps.org/guides/transportation/)

## Measurements and rejection

The authoring scripts use the direction and position of a mapped carriageway to bound an aerial search. They look for light, neutral features over a local asphalt background and allow up to 1.05 m of inter-survey translation. Longitudinal observations have to form a repeated dash series or a parallel stripe one lane to its right. A pair of wider contours must share a station and sit a lane apart. The program retains their observed shapes. Edge samples that a vehicle, tree or shadow obscures can leave gaps.

The Florida fit used Monroe County’s 2024 and 2015 orthophotos. A separately registered 2018 FDOT aerial provided the temporal check. Within 0.29 m of its bright road pixels, the nine individual dashes scored 94.8–100%, the right-stripe sections 97.2–99.8%, and the two arrows 96.9% and 100%. The 0.29 m tolerance covers raster blur and registration uncertainty. These percentages measure cross-survey agreement. The 2024 and 2015 county services advertise 0.25 ft and 0.5 ft pixel size, respectively. The working grid is 0.125 m. [Monroe County 2024](https://mcgis4.monroecounty-fl.gov/public/rest/services/Images/Orthos2024/ImageServer) · [Monroe County 2015](https://mcgis4.monroecounty-fl.gov/public/rest/services/Images/Orthos2015/ImageServer) · [FDOT 2018](https://ca.dep.state.fl.us/image/rest/services/FDOT_Yearly_Aerials/Aerial_Imagery_2018/ImageServer)

Two street photographs supplied transverse width checks. At each usable image row, the measured fraction of the divider-to-edge spacing covered by paint gives a width estimate. Against a 3.32 m surveyed lane, the outer Florida photographs gave 0.148 m for the divider and 0.142 m for the edge. The unused middle photograph gave 0.142 m and 0.140 m. Fitted outer-camera values were 59.1° horizontal FOV, 6.23° of yaw relative to the supplied bearing, 9.10° upward pitch and about 1.07 m eye height. These are model estimates. A 2018–2019 USGS/NOAA laser survey provided a fixed roadside pole used only in the optical check. Its sampled shaft returned points over 18.6 vertical metres, reaching 21.1 m above the nearby ground. [USGS project report](https://rockyweb.usgs.gov/vdelivery/Datasets/Staged/Elevation/metadata/FL_TopobathyFLKeysNOAA_2019_D20/FL_TopobathyFLKeysNOAA_Hydroflattened_2019/reports/USGS_Florida_Keys_3DEP_Lidar_Technical_Data_Report_Final_Signed.pdf)

The 2017 middle photograph was excluded from optical fitting. At the original published comparison pose the two arrow centroids averaged 32.6 px of error in an 820 × 615 image. Preserving GPS distance along the road and centering the camera 2.44 m laterally in the observed right lane gave 9.8 px mean arrow error and 13.5 px mean error at the ends of the nearest dash. A separate Metal render using that diagnostic pose gave 11.0 px mean error at the two actual arrow centroids. Simple interpolation between the independent outer GPS corrections gave 50.8 px mean arrow error. Alternative registrations were examined during the study. The issue is significant for a close marking, even in a straight road. The normal player camera continues to follow the simulated car. [Unused middle photograph](https://kartaview.org/details/332471/21) · [First training photograph](https://kartaview.org/details/332471/20) · [Last training photograph](https://kartaview.org/details/332471/22)

The Massachusetts control is Soldiers Field Road in Boston at 42.365183, −71.134311. The archive labels this sample Cambridge. The 2023/2025 measurements yielded ten candidate dashes, six candidate edge segments and no arrows. The independent 2019 survey agreed with only 11.4% of one dash and a median 75.2% across the ten. The same lane-center optical hypothesis missed the closest middle-frame dash by 39.5 px. The shipping data includes only Florida. Authors must evaluate separately sourced acquisitions and a corresponding street observation before adding another district. The packaged game reads no aerial-paint service at runtime. [MassGIS 2019](https://www.mass.gov/info-details/massgis-data-2019-aerial-imagery) · [MassGIS 2023](https://www.mass.gov/info-details/massgis-data-2023-aerial-imagery) · [MassGIS 2025](https://www.mass.gov/info-details/massgis-data-2025-aerial-imagery) · [Boston control photograph](https://kartaview.org/details/1284745/41)

## Reproduction

Use a working copy of this directory and Python with the dependencies from `requirements.txt`. The checked-in numerical observations, widths and lane-registration receipts suffice to rebuild the runtime asset offline.

```sh
python pack.py /tmp/road-paint-tested
cmp /tmp/road-paint-tested/observed-paint-18122-28047.json ../../game/data/observed-paint-18122-28047.json
```

To repeat the image measurements, run the following in the working copy. The first command fetches bounded raster windows and the six source photographs. Florida uses one image export per aerial date. Massachusetts uses small, cached z20 tiles. Fitting uses the two outer street images and existing optically identifiable image coordinates. New sites need independently checked camera and width observations. Each operation writes its outputs into that working copy.

```sh
python acquire.py
python extract.py
python lanes.py
python widths.py
python calibrate.py
python lane_pose.py
python pack.py /tmp/road-paint-tested
```

The recorded rectangular bounds, service URLs, acquisition labels and georeferencing are in the regional directories. The source thumbnails can change if the provider reprocesses them, so acquisition checks their recorded hashes. The laser-pole position, sampled return elevations and fetch receipt are in `key/pole-control.json` and `key/usgs-lidar-requests.json`. The laser sampling used a 112 m square local window. The remaining numerical acceptance checks are in `validation.json`.

## Credits and reuse

Monroe County GIS, the Florida Department of Transportation, and MassGIS provided the public aerial services. Their source raster pixels are excluded from the game and source distribution. FDOT describes its downloadable aerial imagery as generally free public records. The county service supplies an empty copyright field. Contact the issuing agencies to establish image-republication rights if a separate product needs their photographs. OpenStreetMap contributors supplied the candidate road geometry under ODbL via OpenFreeMap. [FDOT imagery FAQ](https://www.fdot.gov/gis/aerialfaq.shtm) · [OpenStreetMap copyright](https://www.openstreetmap.org/copyright)

Florida street images were contributed by **cowdog**, and Boston images by **cowsandmilk**, © Grab and KartaView contributors, CC BY-SA 4.0. The gallery photograph is a resized and cropped adaptation arranged beside original game renders. The photographic comparison is shared under CC BY-SA 4.0. Survey geometry and the rendered cockpit contain no street-image pixels. [KartaView](https://kartaview.org/) · [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
