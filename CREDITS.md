# Credits

SimCruise is built on the work of many people and organisations. This file gives credit to each maker whose AI model, data, 3D model, texture, font, library or tool we use, with its licence and a link. We use a work only when its licence lets a free, open-source game use it, and we obey the terms of each licence.

The game code is under the [MIT License](LICENSE). Everything below keeps its own licence. The full licence texts are in [`licenses/`](licenses/). Place-by-place sources (single buildings, surveys, photographs used for one place) are in [Credits.txt](Credits.txt) and [`licenses/Road Atlas.txt`](licenses/Road%20Atlas.txt). How each data layer reaches the game is in [docs/data-sources.md](docs/data-sources.md).

Licences were checked at each maker's own page on 14 November 2026.

## Contents

- [Credit shown in the game](#credit-shown-in-the-game)
- [AI models](#ai-models)
- [Map, elevation and aerial data](#map-elevation-and-aerial-data)
- [Live data while you drive](#live-data-while-you-drive)
- [Cars](#cars)
- [Textures, sky and trees](#textures-sky-and-trees)
- [Fonts](#fonts)
- [Engine, libraries and tools](#engine-libraries-and-tools)
- [Named in our research, not used now](#named-in-our-research-not-used-now)

## Credit shown in the game

Some licences need the credit where the player sees the data, not only in a file. The game shows:

- **On the map and in the route planner:** "© OpenStreetMap contributors" in the corner of each map (`SimCruiseAtlasUI.cpp`, `SimCruiseRoutePlanner.cpp`).
- **On the road atlas page:** one line that names OpenStreetMap (ODbL), OpenFreeMap and OpenMapTiles, Photon, USGS 3DEP and Mapzen, USGS NAIP and The National Map, the WRI and Meta canopy height map (CC BY 4.0), Overture Maps, and "full list in DATA-SOURCES.md".
- **In the install folder:** `LICENSE.txt`, `DATA-SOURCES.md`, `CREDITS.txt` (each car model's credit) and `licenses/` (`scripts/package.ps1`).

| Licence | What it needs in the game | Sources under it |
|---|---|---|
| ODbL 1.0 | "© OpenStreetMap contributors" where the map data is seen, and a way to find the licence | OpenStreetMap (roads, buildings, routes, place facts), Overture Maps, Photon search results, adsb.lol aircraft |
| CC BY 4.0 | The maker's name, the licence and a link, in a manner that fits the medium | WRI and Meta canopy heights, Open-Meteo weather, Esri Community Maps buildings (through Overture), the Sketchfab cars, ESA WorldCover (training only) |
| ODC-BY 1.0 | Credit to the database maker when you use or share it | Satlas model weights (testing) |
| CC BY-SA | Credit and the same licence for adapted works | The place photographs listed in [Credits.txt](Credits.txt), the Detectron2 weights (development tool) |
| US public domain | Nothing required; USGS, USDA and NOAA ask for credit as a courtesy | USGS, USDA, NOAA, NASA, FHWA, US Army Corps of Engineers data |

**Gaps to close** (the code has not changed yet): the road atlas line does not name **Open-Meteo** (CC BY 4.0; Open-Meteo asks for "Weather data by Open-Meteo.com" with a link) or **adsb.lol** (ODbL), and the live aircraft and weather have no credit near them on the screen. One short line on the road atlas page, for example "Weather data by Open-Meteo.com (CC BY 4.0) · aircraft from adsb.lol (ODbL)", closes both.

## AI models

All models run on the player's own computer, or only on our computers when we make the game. No model sends data to a server. The training data of each model is the makers' responsibility; we record what each maker says about it.

| Model | Maker | Licence | What we use it for | Where it is | Training data (as the maker states it) |
|---|---|---|---|---|---|
| [Real-ESRGAN General x4v3](https://github.com/xinntao/Real-ESRGAN) | Xintao Wang, Liangbin Xie, Chao Dong and Ying Shan (Tencent ARC Lab); ONNX export by [Qualcomm AI Hub](https://github.com/qualcomm/ai-hub-models) | [BSD 3-Clause](https://github.com/xinntao/Real-ESRGAN/blob/master/LICENSE) (code and released weights; Qualcomm's card also lists bsd-3-clause) | Adds detail to the aerial photographs the ground is shaded with, once per place, then from a cache | In the repository as `data/ai/real-esrgan-general-x4v3.onnx` for development builds; not in the installer | DIV2K ("academic research purpose only"), Flickr2K and OST, with synthetic damage ([paper](https://arxiv.org/abs/2107.10833)) |
| [Stable Diffusion XL base 1.0](https://huggingface.co/stabilityai/stable-diffusion-xl-base-1.0) | Stability AI | [CreativeML Open RAIL++-M](https://huggingface.co/stabilityai/stable-diffusion-xl-base-1.0/blob/main/LICENSE.md) (use limits in its Attachment A; Stability AI claims no rights in the output) | Made the 8 textures of the fine-detail kit offline (asphalt, concrete, soil, sand, leaf litter; stucco, brick and bark are not used); prompts and seeds in `lab/detail-kit/raw/prompts.json` | Only the textures, in `data/art/detail-kit/`; the model is not in the repository or the game | The model card does not name the training data |
| [Detectron2 Panoptic FPN R50](https://github.com/facebookresearch/detectron2) (semantic head) | Meta AI Research (FAIR); ONNX export by [Qualcomm AI Hub](https://github.com/qualcomm/ai-hub-models) | Code: [Apache 2.0](https://github.com/facebookresearch/detectron2/blob/main/LICENSE). Weights: [CC BY-SA 3.0](https://github.com/facebookresearch/detectron2/blob/main/MODEL_ZOO.md#license) ("All models available for download through this document") | A development tool that finds the walls in place photographs, so that we can measure wall colours | Downloaded into `Saved/AI` by `scripts/ai/fetch_open_models.py`; not in the repository or the game. Only the measured colours are bundled | COCO panoptic (annotations CC BY 4.0; images from Flickr under their own licences) |
| [SatlasPretrain Aerial_SwinB_SI](https://github.com/allenai/satlaspretrain_models) | Allen Institute for AI (Ai2) | Weights: [ODC-BY 1.0](https://github.com/allenai/satlas/blob/main/DataLicense). Code: Apache 2.0 | The whole backbone with our head is the development "satlas_landcover" eye (the teacher); its first two stages (2.1 M of 89.6 M weights) are inside the shipped "aerial_student" eye | The first two stages inside `data/ai/scan-eye-student.onnx`; the whole model only on our computers |
| SimCruise aerial student (`aerial_student` scan eye) | SimCruise contributors | [MIT](LICENSE) for our layers; it contains the first two Satlas stages above (ODC-BY 1.0: credit to Ai2). Contains information from OpenStreetMap, © OpenStreetMap contributors, ODbL (labels) | Reads the aerial photo of each world tile into land-cover probabilities (road, roof, water, tree, grass, crop, bare, built ground ...) and the place's own colours, on the CPU, off by default (`-SimCruiseScanEye=aerial_student`); lower 48 states only | `data/ai/scan-eye-student.onnx` (11 MB) | Trained by us from USGS The National Map aerial photos (public domain) with labels from OpenStreetMap (ODbL), the WRI and Meta canopy height map v2 (CC BY 4.0) and ESA WorldCover 2021 (CC BY 4.0), and the teacher eye's answers on 2,100 more USGS tiles (`lab/scan/`, `docs/research/local-ai-cpu.md`) | SatlasPretrain: USDA NAIP aerial images (public domain), Sentinel-2, Sentinel-1 and Landsat images, labels from OpenStreetMap (ODbL) and new Ai2 labels (ODC-BY) ([dataset notes](https://github.com/allenai/satlas/blob/main/SatlasPretrain.md)) |
| [DeepForest tree crown model](https://github.com/weecology/DeepForest) | Weecology lab, University of Florida (Ben Weinstein and others) | [MIT](https://github.com/weecology/DeepForest/blob/main/LICENSE) (code and the [weights on Hugging Face](https://huggingface.co/weecology/deepforest-tree)) | **Planned, not in the game yet.** Finds single tree crowns in aerial photographs | Not used yet | Millions of tree labels made from lidar by an unsupervised method, then hand labels on RGB airborne images from selected sites ([paper](https://besjournals.onlinelibrary.wiley.com/doi/full/10.1111/2041-210X.13472)) |

The models run with [ONNX Runtime](https://github.com/microsoft/onnxruntime) (Microsoft, [MIT](https://github.com/microsoft/onnxruntime/blob/main/LICENSE)) through Unreal Engine's Neural Network Engine plugin `NNERuntimeORT`. Training and export on our computers use [PyTorch](https://github.com/pytorch/pytorch) (BSD-style), [ONNX](https://github.com/onnx/onnx) (Apache 2.0), [NumPy](https://github.com/numpy/numpy) (BSD 3-Clause) and [rasterio](https://github.com/rasterio/rasterio) (BSD 3-Clause).

An earlier scene model, DDRNet23-Slim trained on Cityscapes, was removed because the Cityscapes data is for non-commercial use only. Its MIT code licence stays in `licenses/DDRNet-MIT.txt` for the record.

## Map, elevation and aerial data

| Data | Maker | Licence | Credit line |
|---|---|---|---|
| Roads, buildings, land use, water, place names, GPS routes, street furniture, guard rails, building facts | [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors | [ODbL 1.0](https://opendatacommons.org/licenses/odbl/1-0/) | © OpenStreetMap contributors |
| Vector tiles of the OpenStreetMap data | [OpenFreeMap](https://openfreemap.org/) with the [OpenMapTiles](https://openmaptiles.org/) schema (MapTiler and contributors) | Data ODbL 1.0; OpenMapTiles code BSD 3-Clause | OpenFreeMap © OpenMapTiles Data from OpenStreetMap |
| Lane counts and speed limits | OpenStreetMap contributors, through Esri's OSM North America replica | ODbL 1.0 | © OpenStreetMap contributors |
| Place search | [Photon](https://photon.komoot.io/) by komoot | Software Apache 2.0; results are OpenStreetMap data, ODbL 1.0 | © OpenStreetMap contributors |
| Building heights, roofs, extra footprints, traffic signals | [Overture Maps Foundation](https://docs.overturemaps.org/attribution/) | Buildings theme: ODbL 1.0 | © OpenStreetMap contributors, Overture Maps Foundation |
| Building footprints inside Overture | [Microsoft Global ML Building Footprints](https://github.com/microsoft/GlobalMLBuildingFootprints) | ODbL 1.0 in Overture (Microsoft's own release is CDLA Permissive 2.0) | Microsoft |
| Building footprints inside Overture | Esri Community Maps contributors | CC BY 4.0 | Esri Community Maps contributors |
| Building use | US Army Corps of Engineers, National Structure Inventory | US public domain | USACE NSI |
| Ground elevation | [USGS 3D Elevation Program](https://www.usgs.gov/3d-elevation-program) (1/3 arc-second and 1 m), Mapzen/Tilezen Terrarium tiles | US public domain; Tilezen [attribution](https://github.com/tilezen/joerd/blob/master/docs/attribution.md) | U.S. Geological Survey; Mapzen |
| Lidar for roofs and road camber | USGS 3DEP lidar (EPT on AWS); Miami-Dade County; NOAA Digital Coast | US public domain; CC0 (Miami-Dade) | U.S. Geological Survey |
| Aerial photographs | [USDA NAIP](https://naip-usdaonline.hub.arcgis.com/) and USGS The National Map imagery; USDA Forest Service (Alaska) | US public domain | USDA Farm Production and Conservation; U.S. Geological Survey |
| Tree canopy heights | [WRI and Meta, High Resolution Canopy Height Maps v2](https://registry.opendata.aws/dataforgood-fb-forestsv2/) | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) | Version 2 High Resolution Canopy Height Maps by WRI and Meta |
| Forest type and shrub cover | USGS/USDA LANDFIRE EVT; MRLC NLCD and RCMAP; USDA ARS Rangeland Analysis Platform | US public domain | LANDFIRE; MRLC; USDA ARS |
| Recent surface colour (optional) | Copernicus Sentinel-2 L2A, through Element 84 Earth Search | Copernicus Sentinel data terms (free, with credit) | Contains modified Copernicus Sentinel data |
| Snow cover | NASA NSIDC DAAC, MODIS MOD10A1 v61, through NASA GIBS | Free, with credit | NASA NSIDC DAAC |
| City street trees | City of Lincoln, Nebraska; City of Cambridge, Massachusetts | City open-data terms; PDDL 1.0 (Cambridge) | City of Lincoln; City of Cambridge |
| Architecture facts | [Wikidata](https://www.wikidata.org/) | CC0 1.0 | Wikidata |
| Place photographs and wall colours | [Wikimedia Commons](https://commons.wikimedia.org/) authors (only CC BY, CC BY-SA, CC0 and public-domain files) | Each file's own licence | Each author, as kept with the photo |
| Street photographs | KartaView and Panoramax contributors | CC BY-SA 4.0 | Each author |
| Land-cover labels to train the aerial eye (training only, never in the game) | [ESA WorldCover 2021 v200](https://esa-worldcover.org/) | CC BY 4.0 | © ESA WorldCover project 2021 / Contains modified Copernicus Sentinel data (2021) processed by ESA WorldCover consortium |

## Live data while you drive

| Data | Maker | Licence | Credit line |
|---|---|---|---|
| Weather, air quality, sea and waves | [Open-Meteo](https://open-meteo.com/) (with Copernicus CAMS for air quality) | Data [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/); the free API is for non-commercial use, which SimCruise is | Weather data by Open-Meteo.com |
| Airport weather | NOAA Aviation Weather Center METAR; NWS observations through Iowa Environmental Mesonet; NOAA GOES | US public domain | NOAA |
| Tides | NOAA CO-OPS water levels | US public domain | NOAA |
| Aircraft | [adsb.lol](https://adsb.lol/) | [ODbL 1.0](https://opendatacommons.org/licenses/odbl/1-0/) | adsb.lol |
| Traffic counts, lanes, road roughness | FHWA Highway Performance Monitoring System; US Census geocoder | US public domain | FHWA; US Census Bureau |

## Cars

All car models are from [Sketchfab](https://sketchfab.com/) under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The game fits, rigs and re-materials them, and hides maker badges. Car names and brands are trademarks of their owners and are not licensed. Details: [`licenses/Traffic models.txt`](licenses/Traffic%20models.txt).

| Car | Model and author |
|---|---|
| Mazda MX-5 Miata (NB), the player's car | ["Mazda MX-5 (NB) convertible HQ interior"](https://sketchfab.com/3d-models/mazda-mx-5-nb-convertible-hq-interior-5d103c567bee4c61ad66f04562049933) by [Nieve5677 (niev)](https://sketchfab.com/niev) |
| 1965 Ford Mustang | ["1965 Ford Mustang Convertible"](https://sketchfab.com/3d-models/1965-ford-mustang-convertible-05f2a294087541ab82f681136d134344) by [Kristian LaGrange](https://sketchfab.com/kristian123) |
| Corvette Stingray (C8) | ["2019 Chevrolet Corvette C8 Stingray"](https://sketchfab.com/3d-models/2019-chevrolet-corvette-c8-stingray-790c40ccff6843eab0b7b4bd18421ff8) by [Hari Prasath R](https://sketchfab.com/Haris3D) |
| Honda Accord | ["2018 Honda Accord"](https://sketchfab.com/3d-models/2018-honda-accord-54c227442c4b42dfbaa25e474ad9d736) by [roflowhale](https://sketchfab.com/roflowhale) |
| Honda CR-V | ["2025 Honda CR-V 1.5 VTEC Turbo CVT"](https://sketchfab.com/3d-models/2025-honda-cr-v-15-vtec-turbo-cvt-91c6673859444597bf006a0ef8570005) by [Palma123 JAAT](https://sketchfab.com/palma123.jaat) |
| Ford F-150 Raptor | ["2018 Ford F-150 Raptor"](https://sketchfab.com/3d-models/2018-ford-f-150-raptor-096aa1ba46984eeeb196b75f4d213a0a) by [Galaxy Car Showroom](https://sketchfab.com/adrianaflak09) |
| Jeep Grand Cherokee SRT8 | ["2013 Jeep Grand Cherokee SRT8"](https://sketchfab.com/3d-models/2013-jeep-grand-cherokee-srt8-74686a75e26146a19337724a7e1d458a) by [Galaxy Car Showroom](https://sketchfab.com/adrianaflak09) |
| Chevrolet Tahoe | ["Chevrolet Tahoe Suv"](https://sketchfab.com/3d-models/chevrolet-tahoe-suv-39b86984238941b0b6a46da6a8c31907) by [maznalamri80](https://sketchfab.com/maznalamri80) |
| Honda Odyssey | ["Honda Odessey"](https://sketchfab.com/3d-models/honda-odessey-175ce6fbbb11469ab068965ce4854554) by [MattDoesBlender](https://sketchfab.com/MattDoesBlender) |
| Tesla Model 3 | ["Tesla M3 model"](https://sketchfab.com/3d-models/tesla-m3-model-3a602469d7874d1397efa67182198705) by [wintrez](https://sketchfab.com/wintrez) |
| Parked cars | ["Generic passenger car pack"](https://sketchfab.com/3d-models/20f9af9b8a404d5cb022ac6fe87f21f5) by Comrade1280 |

The car physics are calibrated against published road tests (Car and Driver, Motor Trend, Edmunds and others), as listed for each car in [docs/cars.md](docs/cars.md). We use only their published numbers.

## Textures, sky and trees

| Asset | Maker | Licence |
|---|---|---|
| Asphalt Floor, Grass Ground, Sandy Gravel 02 (Dario Barresi), Jølcham Oak Bark 01, Pine Tree 01 | [Poly Haven](https://polyhaven.com/) | [CC0](https://polyhaven.com/license) |
| Sky: Kloofendal 48d Partly Cloudy, Kloofendal 43d Clear (Greg Zaal) | [Poly Haven](https://polyhaven.com/) | CC0 |
| Street oak mesh | [Innerscene](https://www.innerscene.com/tools/library/3d-parts/oak-street-tree-detailed-leaves-a273fef9) | CC0 |
| Building facades | ["Free Urban Textures"](https://opengameart.org/content/free-urban-textures-buildings-apartments-shop-fronts) by Scouser, OpenGameArt | CC0 |
| Foliage | [paramecij's vegetation base texture pack](https://opengameart.org/content/paramecijs-vegetation-base-texture-pack), OpenGameArt | CC0 |
| Fine-detail kit | Made with Stable Diffusion XL (see [AI models](#ai-models)) | Ours, made under CreativeML Open RAIL++-M |

## Fonts

| Font | Maker | Licence |
|---|---|---|
| [Overpass](https://github.com/RedHatOfficial/Overpass) (menus and HUD) | Delve Withrington and the Overpass Project Authors, for Red Hat | [SIL OFL 1.1](licenses/Overpass%20font%20(OFL-1.1).txt) |
| Roboto (Unreal Engine's default font) | Christian Robertson, Google | Apache 2.0 |

## Engine, libraries and tools

| Software | Maker | Licence | Use |
|---|---|---|---|
| [Unreal Engine 5.8](https://www.unrealengine.com/) | Epic Games | [Unreal Engine EULA](https://www.unrealengine.com/eula) | Renderer, Chaos physics, audio, input. Made with Unreal® Engine; Unreal® is a trademark or registered trademark of Epic Games, Inc. |
| Unreal plugins: Enhanced Input, GameInput, NVIDIA Reflex, NNERuntimeORT | Epic Games, Microsoft, NVIDIA | Shipped with Unreal Engine, under its EULA and the notices in the engine | Controls, wheel and pad input, low input delay, AI models |
| [Bun](https://bun.com/) | Oven | MIT ([`licenses/bun.txt`](licenses/bun.txt)) | Runs the map helper |
| [@mapbox/martini](https://github.com/mapbox/martini) | Mapbox | ISC | Terrain meshes |
| [@mapbox/vector-tile](https://github.com/mapbox/vector-tile-js), [pbf](https://github.com/mapbox/pbf), point-geometry | Mapbox | BSD 3-Clause; ISC (point-geometry) | Reads the map tiles |
| [earcut](https://github.com/mapbox/earcut) | Mapbox | ISC | Polygon triangles |
| [fast-png](https://github.com/image-js/fast-png), iobuffer | Michaël Zasso (image-js) | MIT | PNG images |
| [geotiff.js](https://github.com/geotiffjs/geotiff.js) | EOX IT Services | MIT | Elevation files |
| [laz-perf](https://github.com/hobuinc/laz-perf) | Hobu | Apache 2.0 | Lidar point clouds |
| [proj4js](https://github.com/proj4js/proj4js), wkt-parser, mgrs | Proj4js authors | MIT | Map projections |
| h5wasm | NIST | NIST statement and HDF5 licence ([`licenses/h5wasm-HDF5.txt`](licenses/h5wasm-HDF5.txt)) | HDF5 files |
| pako, fflate, zstddec, lerc, float16, quick-lru, web-worker, xml-utils, parse-headers, protocol-buffers-schema, resolve-protobuf-schema | Their authors (see [`licenses/`](licenses/)) | MIT, Apache 2.0 (lerc: Esri; web-worker), CC0 (xml-utils) | Helper support code |

Licence texts for all of these are in [`licenses/`](licenses/).

## Named in our research, not used now

These are named in our research notes, but no code in the game or its tools uses them now. When one is used, it moves into the tables above with its credit:

- Köppen-Geiger climate maps by Beck et al. (CC BY 4.0). The game uses rough latitude bands now.
- USDA Forest Service TreeMap.
- ambientCG materials (CC0).

Did we miss you, or credit your work wrongly? Please [open an issue](https://github.com/simcruisegame/simcruise/issues) and we will fix it.
