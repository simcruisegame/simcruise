# USDA low-vegetation observation preview

> Former name: this study was written when SimCruise was called "Sunday Driver"; the app, paths and commands it mentions under that name are now SimCruise.

The map’s **USDA ground cover** button remembers the player’s choice. The command-line option `--spectral-ground` also enables a late surface-material update for the currently occupied streamed tile. Driving, terrain collision, mapped roads and canopy placement work independently of this preview. The ordinary game starts with the feature disabled.

The native helper selects a single 0.3–1 m NAIP raster that encloses the whole game tile. It checks the USDA acquisition date in the original raster identifier, a four-band 8-bit catalog item description, complete image opacity, and usable channel contrast. It requests the raw NIR, red and green bands as one 256 × 256 PNG locked to that raster, without an enhancement or stretch function. Only a positive red/NIR vegetation index from a low-canopy pixel can strengthen the existing grass material. The independent 512 × 512 canopy-height tile has to be present locally. Water, mapped pedestrian paving and motor-road paint retain their existing priority. [Federal GeoPlatform NAIP service](https://imagery.geoplatform.gov/iipp/rest/services/NAIP/NAIP_plus/ImageServer) · [USGS NAIP data description](https://www.usgs.gov/centers/eros/science/usgs-eros-archive-aerial-photography-national-agriculture-imagery-program-naip)

Acquisitions more than five years old, acquisitions outside May–October, incomplete coverage, HRO identifiers, missing bands and saturated or degenerate imagery are skipped. The game suspends the layer during live weather and dated cold-season street-photo replays. The catalog is limited to CONUS. Canopy height and spectral thresholding estimate low vegetation and can confuse bushes, persistent shadows or low irrigated crops with turf. Those checks do not identify a grass species, reconstruct a footpath or establish current surface conditions.

## Validation

| Measure | Bethel, Maine | Lewisburg, West Virginia |
| --- | ---: | ---: |
| Original NAIP acquisition | 2023-09-27, 0.3 m | 2022-10-21, 0.6 m |
| Live cold requests and payload | 3 / 143,839 bytes | 3 / 184,840 bytes |
| Warm requests and payload | 0 / 0 bytes | 0 / 0 bytes |
| NDVI correlation with independently decoded original 4-band TIFF | 0.950 | 0.870 |
| Mean absolute NDVI difference | 0.0205 | 0.0549 |

An isolated Metal frame pair used the same loaded terrain, structures, trees, camera, time and light. For Bethel, three building corners from the photograph and mapped outline placed the camera 5.76 m east and 1.13 m south of the original phone GPS, at bearing 24.76°. A fourth shaded corner, excluded from the solve, projected at x=164 in the observed x=149–165 interval at 640-pixel width. The camera had no recorded height, pitch, roll or accuracy. Ground pixels were excluded from this registration. At the uncorrected geotag the early TIFF mask aligned poorly with the photographed driveway, so the raw-pose color comparison cannot resolve material accuracy.

On the published USDA-service runtime pair, mean absolute RGB channel error over 60,790 photographically green ground pixels fell from 49.33 to 47.35 on a 0–255 scale. The near-lawn subset fell from 54.08 to 51.41. Mean error remained 25.92 on the photographed paved entrance (7,401 pixels) and 19.44 on the independent West Virginia road (76,216 pixels). No pixel in either paved mask changed by three channel levels. The Bethel material remains considerably browner than the June photograph. [Bethel photograph by User:Nickline4, CC BY-SA 3.0](https://commons.wikimedia.org/wiki/File:Gould_Academy_Davidson_Hall.JPG) · [Lewisburg photograph by Antony-22, CC BY-SA 4.0](https://commons.wikimedia.org/wiki/File:Lewisburg,_West_Virginia_post_office_2016.jpg)

Three requests per cold tile share the host pacing and a 200 KiB budget. Catalog and image failures leave the current ground intact. Warm masks are cached for thirty days. A temporary failure suppresses retries for that tile for fifteen minutes and for other optional spectral jobs for one minute. Intermittent GeoPlatform timeouts occurred during these checks. Each update handles only the currently occupied tile. Alaskan, Hawaiian, tile-edge and multi-season generalization remain untested.

Run `node --test lab/test-spectral-ground.mjs` with the existing dependencies, or call `atlas ground-cover CACHE X Y` after the standard canopy source has populated the corresponding tile. The gameplay switch follows Godot’s application-argument delimiter, for example `SundayDriver -- --spectral-ground`.

## Repeat the paired capture

Use a GPU build with its normal atlas helper. Create the destination folder, then run the executable separately for each camera. The evidence script takes a path to the original 1,280 px Commons photograph and a matching output prefix. The paired run freezes simulation and streaming, changes only the local low-vegetation shader input and writes lossless off/on/off-repeat frames. The scorer rejects a pair if its repeated control differs at even one pixel.

```sh
"$GAME" --windowed --resolution 1024x765 -- --reference 44.4079898759 -70.7932608322 24.7587678253 61.041895 /tmp/proof/bethel.png 0 0 --complete-reference --spectral-ground --ground-cover-ab
"$GAME" --windowed --resolution 1024x768 -- --reference 37.8008611111 -80.4441 64.599945 61.655874 /tmp/proof/lewisburg.png 0 0 --complete-reference --spectral-ground --ground-cover-ab
python3 lab/holdout/score_spectral.py bethel /path/to/bethel-reference.jpg /tmp/proof/bethel
python3 lab/holdout/score_spectral.py lewisburg /path/to/lewisburg-reference.jpg /tmp/proof/lewisburg
```
