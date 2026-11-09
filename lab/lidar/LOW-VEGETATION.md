# Low roadside vegetation

The offline authoring tool places compact plant volumes where independent ground, canopy and spectral measurements agree. A tile without accepted cells gets no additional plants. Each instance uses the existing Poly Haven shrub scan scaled to a conservative height and horizontal reach. This represents a small patch of low vegetation. The data leave species, hedge continuity, trunks, fences and walls undetermined.

## Inputs and coordinates

The points NPZ stores a numeric matrix named `p` with local projected ground-metre X/Y, metric elevation, integer ASPRS class and optionally the flight-strip point source ID. Use a local metre projection such as UTM. Ground is class 2. Low and medium vegetation are classes 3 and 4, and class 5 screens for high overstory. The map GeoJSON uses WGS84, with `layer` set to `transportation`, `building` or `water`. Transportation features carry their OpenMapTiles `class` and optional `subclass`. Additional footprint GeoJSON polygons are supported.

Supply registered, radiometrically comparable four-band orthophotography. The RGB image and separate NIR image must have identical pixel dimensions, cover the entire z16 tile, and use the same numeric range without separate contrast adjustments. For the Miami and Chula tests we requested 800 × 800 PNG32 images with band indices 0,1,2 and 3,3,3 from the same 2023 USDA NAIP ImageServer, in EPSG:3857. An ImageServer identify at each tile center verified the source quad and date. The output is a standard JSON tile containing UV positions running east and south, above-ground heights and radii in metres, and sampled RGB values. [USDA NAIP 2023 ImageServer](https://imagery.geoplatform.gov/iipp/rest/services/NAIP/NAIP2023_CONUS/ImageServer), [OpenFreeMap data attribution](https://openfreemap.org/quick_start/).

```sh
python low_vegetation.py --points classified.npz --crs EPSG:6346 \
  --rgb tile-rgb.png --nir tile-nir.png --map map.geojson --footprints buildings.geojson \
  --tile 18166 27914 --output low-vegetation-18166-27914.json \
  --aerial-id m_2508023_nw_17_030_20230111_20230530 \
  --point-source https://noaa-nos-coastal-lidar-pds.s3.amazonaws.com/entwine/geoid18/10338/ept.json \
  --point-date 2021-04-07/2021-06-10
```

The offline requirements are NumPy, SciPy, Shapely 2.1 or later, pyproj and Pillow. Prep LAS/LAZ as needed. For each accepted 1.35 m cell, the gate requires five eligible training returns across three smaller cells, nearby classified ground, a mapped roadside location outside buffered roads, buildings and water, and nine green/NIR checks out to 0.99 m. This last check excludes most unmapped pavement and driveway openings, including gaps between cells. An independent flight strip must support compatible heights when available. With a four-column point source the fallback is a disjoint subset of 0.45 m XY cells. Radii reflect the extent actually sampled and never exceed 0.68 m. The sampled elevations describe detected low returns. Ground referencing, overstory occlusion, position accuracy, unrecorded pavement and dates can limit interpretation.

## Miami and Chula

South Miami Avenue used nine depth-10 source blocks and nine depth-11 blocks. The nine additional blocks totaled less than 3 MB compressed. A separate flight strip, number 74, supplied the vegetation height holdout. Twelve of 4,751 cells passed with 0.10 m mean absolute height difference and 0.21 m at the 90th percentile. Miami's January 11, 2023 NAIP quad has native 0.30 m sampling. The lidar was flown in 2021. [Miami-Dade lidar source](https://noaa-nos-coastal-lidar-pds.s3.amazonaws.com/entwine/geoid18/10338/ept.json), [validation receipt and point-block hashes](low-vegetation-validation.json).

The optical-camera comparison used KartaView frame 11591982/217, independently of fitting. At the approximate camera pose, three cells projected onto 349 semantic-mask pixels with 190 labeled vegetation and 159 uncertain. A frozen 1024 × 766 Metal A/B changed 161 rendered pixels by more than 3/255 in any channel. Of these, 59 corresponded to confident vegetation and 102 were uncertain. No changed pixel corresponded to labeled road, sidewalk or building. On those 59 pixels the mean absolute RGB error fell from 65.31 to 57.24. The observed improvements cover a few plants along the left property edge. The street photograph shows much longer hedges. Camera, date and lighting differences limit the comparison. [Held-out street photograph](https://kartaview.org/details/11591982/217).

At Chula Road, the independently verified October 13, 2023 NAIP quad has 0.60 m native sampling. Of 478 low-height cells in the cached Virginia 256 × 256 lidar canopy-maximum raster, 32 also had a fully vegetation-positive infrared neighborhood. The raster lacks point classifications and independent flight-strip observations. It yielded zero additional placed plants.
