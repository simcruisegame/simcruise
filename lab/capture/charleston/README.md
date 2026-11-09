# Registering a street camera to a surveyed skyline

The study fits one horizontal field of view, one yaw/pitch correction, and one east/north offset to a short Charleston KartaView sequence. The numbered photographs look southeast along Lee Street East at its intersection with Leon Sullivan Way. Two connected real-world building footprints identify the surveyed landmark on the left. The selected image interval is recorded in study.json. Camera height was assumed to be 1.65 m.

From the parent capture directory, run the following with NumPy, SciPy, Pillow, pyproj and Shapely installed.

```sh
python fit_roofline.py charleston/study.json charleston/result.json
```

The input contains the top sampled returns from projected 3DEP points within those two mapped footprints and the ten nearby ground returns used by each camera. Points were reduced to a 0.83 m grid for registration. The NPZ stores projected easting, northing, elevation, LAS classification, strip ID, return number and return count. It is an offline research input. Players do not download it.

Training used sequence frames 2858 and 2863. The simple 85° level-camera estimate and fitted camera were evaluated on two separate photographs. On frame 2860 mean vertical position error on the observed building skyline fell from 16.7 to 9.4 pixels. On frame 2861 it fell from 22.1 to 10.5 pixels. Label images are 513 pixels wide. The fitted horizontal FOV was 88.31°. This compares camera registration against the selected skyline. It does not measure the geometry of every facade. The interactive comparison keeps the same pose on both sides when testing a change to the game world.

Pixel labels were prepared with the local DeepLab/LiteRT script in lab/semantic. Individual door/window textures are withheld. A separate sparse OpenCV trial using the initial three views produced no facade feature tracks that met both training and independent-view reprojection thresholds. A flat paint sample on the most consistently identified opposite facade also varied by roughly 46 channel levels in one withheld frame. That evidence did not justify changing the building's visible frontage.

Credits for the four adapted label images go to wolfgang8741, © Grab and KartaView contributors, CC BY-SA 4.0. Changes include resizing and machine semantic labeling. The elevation input is from the 2018–2019 USGS/FEMA West Virginia survey. The two footprint rings derive from OpenStreetMap via OpenFreeMap under ODbL. Additional survey provenance is included in the Charleston lidar authoring materials.

[Frame 2858](https://kartaview.org/details/3485953/2858) · [Frame 2860](https://kartaview.org/details/3485953/2860) · [Frame 2861](https://kartaview.org/details/3485953/2861) · [Frame 2863](https://kartaview.org/details/3485953/2863) · [Photo license](https://creativecommons.org/licenses/by-sa/4.0/) · [OpenStreetMap](https://www.openstreetmap.org/copyright) · [USGS 3DEP](https://www.usgs.gov/3d-elevation-program).
