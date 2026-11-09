# SimCruise history: release notes and studies through 1.0.8

This file preserves, nearly word for word, the release notes and study summaries that lived in the project README up to Sunday Driver 1.0.8. The project was renamed to **SimCruise** in November 2026 and the README was rewritten for newcomers; nothing was dropped, it moved here and to [architecture-notes.md](architecture-notes.md).

Read it with the former name in mind:

- "Sunday Driver", "Sunday Driver - Miata Sport.app" and `"Sunday Driver.app/Contents/MacOS/Sunday Driver"` all mean what is now SimCruise and `build/SimCruise.app/Contents/MacOS/SimCruise`.
- The Godot user-data folder was `~/Library/Application Support/Godot/app_userdata/Sunday Driver/`; it is now `.../app_userdata/SimCruise/`.
- Only the relative links were re-pointed (`lab/...` is now `../lab/...`) so they resolve from `docs/`. The text itself is unchanged, including version labels, dates and measurements that may have been superseded since, except that passages about data that is no longer part of the project were removed or shortened.
- Sections are in their original order. For the current state of the project see the [README](../README.md), the [roadmap](roadmap.md) and the [changelog](../CHANGELOG.md).

## Introduction as written for 1.0.8

Sunday Driver is a free, open-source real-world driving simulator. Original game code, shaders, data-processing tools, and tests are MIT-licensed. Map databases, survey data, photographs, models, fonts, and third-party code retain their own licenses. See [LICENSE](../LICENSE) and [the asset and data notices](../licenses/). The macOS build requires no account, paid API key, or subscription.

The demo starts on a bundled starter road. Search for a US place, enter coordinates, or select the Miami and Cambridge examples. Local coverage, photo capture dates and network availability affect what the game can show.
The Mac build includes the engine, local map helper, artwork and starter neighborhood. An internet connection is needed to search for or visit an unbundled place.

## Play
Open Sunday Driver - Miata Sport.app. W / up accelerates; A and D / left and right steer; S / down brakes, then reverses at a standstill. Space is the service brake; H applies the rear parking brake. C changes camera, M toggles synthesized engine sound, R resets to a road, / searches, and F3 shows frame rate, tile and request counts. The five-speed starts with assisted shifting. T (Xbox B) switches to manual. Hold Shift (LB), press E (RB) to select first, apply throttle and release the clutch. E/Q (RB/X) shift up/down through reverse, neutral and five forward gears. If the engine stalls, hold the clutch and press I (right-stick click). Xbox A or keyboard H holds the rear e-brake. Click the minimap, press Tab or Xbox Menu to open the road atlas. Drag to pan, scroll to zoom, click to pin a location, or compare that position and heading in Street View. The Imagery and Weather buttons switch the recent satellite colour and weather layers. Search supports names and latitude, longitude; pick the result to load the nearest mapped road.

## Missing building footprints

Two surveyed buildings fill gaps in the street map. In Miami, 272 classified roof returns support a measured height of 5.91 m above the surrounding ground. In Allston, Massachusetts, 791 roof returns support 4.39 m. The game streams the corresponding Overture outlines and admits each one only if the stored survey still matches its shape and it clears all current roads and buildings. Roof elevation and building presence have survey support. Wall finish and interior use remain unknown. Additional districts can be processed with the [public lidar verification pipeline](../lab/lidar/verified-footprints.md).

## City tree inventories

In Lincoln, Nebraska and Cambridge, Massachusetts, streamed tree canopies can use municipal trunk locations, diameters and recorded species. The helper requests one bounded city tile when a new area comes into view. Uniquely matching height-map crowns move to the recorded stems. Some additional stems are sized from nearby trees of similar diameter. Buildings and pavement can block placements. Other cities use the existing canopy-height model. The [two-camera Lincoln comparison and source notes](../lab/holdout/civic-trees.md) distinguish survey measurements from modeled dimensions.

## Package and checks
Outside the bundled area, the game streams its general US map. Roads and terrain load independently of optional aerial photography.
    
    godot --headless --path game --fixed-fps 60 --script res://checks/RideCheck.gd
    bun lab/cache-check.mjs

The package includes all authored runtime assets and needs no geospatial toolchain to build the game.

Xbox controller: left stick to steer, RT to accelerate, LT to brake and reverse, A for the parking brake, Y for camera, and View to recover. Pair in macOS Bluetooth settings.

Minimap zoom: D-pad down/up, scroll over the minimap, or use its minus/plus buttons.


The Road Atlas update streams WRI/Meta canopy heights, Copernicus Sentinel-2 true colour and scene classification, and optional NOAA METAR observations. Recent satellite imagery defaults on when visiting unprepared roads. The road atlas shows the accepted capture date. Elsewhere in the contiguous US, a small local logistic regression combines available aerial pixels with mapped road centrelines to refine shoulders. The classifier was trained and evaluated on geographically separate tiles. Individual tree species and building facades remain inferred. Cameras, road geometry and ground photography can have different survey dates.

Run the compiled Mac application with `-- --us-check` to test manual shifting and live drives in Vermont, Alaska and Hawaii. `-- --field-check` exercises the bundled start and the expanded map with the network helper disabled.

## Hiding the HUD

F2 or a press of the Xbox left stick hides the driving HUD so the instruments and mirrors fill the view. The full map remains available on Tab or the Xbox Menu button.

## Nationwide preview

The first launch uses automatic gear changes. Press T on the keyboard or B on Xbox while driving, or open the map and select Transmission. Manual mode operates the five-speed with the clutch (keyboard Shift or Xbox LB), upshift (E or RB) and downshift (Q or X). The game remembers transmission and street-photo choices on this Mac. Automatic shifting is an assist on the simulated five-speed.

On streamed US roads the helper can query one nearby KartaView sequence in the background. A cold lookup uses one catalog request and up to three processed thumbnails. Three consistent daylight views can adjust the pavement and green verge colors near that short capture track. Shaded, obstructed, dissimilar or missing views retain the existing materials. Check Street photos on the map for the result and Photo credit for the capture. The lookup runs when entering a different tile, has a request interval and a bounded disk cache, and can be disabled. These colors describe the capture conditions and can predate today's road.

The general terrain path also fits paved-road cross slopes to shared elevation samples. It preserves measured terrain near mapped bridges, tunnels and clipped vector boundaries. This is an approximate roadbed without a surveyed bank or a reconstructed bridge deck.

## Miata Sport test build

The active district and the district ahead of the car can receive a second building pass after roads and terrain load. The helper reads local ranges from Overture's versioned building archive and checks shapes and recorded heights against a read-only North America OpenStreetMap replica. It applies a precise OSM height when the original way revision and numeric tag agree. For the common five-metre map default, a property-specific USGS lidar or Esri Community Maps value can replace it when the corresponding OSM record has no reported height or levels. The visual meshes and collision shapes change together.

A separate USACE National Structure Inventory lookup can supply a modeled use for a generic footprint. A point from the inventory and the matching generic OSM building must each fall unambiguously inside that footprint. A residential use suppresses inferred storefronts along large streets. Window placement, untagged wall finishes and unsurveyed roof shapes remain approximate. The two building layers share a cached OSM lookup and can succeed independently. The base road remains drivable when a source is slow or unavailable. A warm district uses its local cache. The 96 MiB shared source cache can evict older districts, so revisiting one can require another download. Release dates refer to publication and are distinct from survey dates. Source coverage and latency vary.

The South Miami Avenue sample includes 15 precomputed roof meshes based on Miami-Dade's 2021 airborne lidar flight. The lab tool fits classified roof returns relative to nearby classified ground and clips the result to each footprint. These measurements cover the bundled tile. Elsewhere the game can stream verified building heights and georeferenced roof photography where available. The runtime does not yet reconstruct arbitrary roof shapes from nationwide point clouds. Sources and adaptation credits are in `licenses/Road Atlas.txt`.

The same authoring settings accepted 0 of 57 mapped footprints within 156 m of 42.3736° N, 71.1097° W in Cambridge, Massachusetts. Of those candidates, 36 failed the five-fold spatial validation or flat-roof comparison, and the other 21 lacked sufficient classified coverage or locally consistent ground. The test used the USGS 3DEP MA_CentralEastern_1_2021 survey collected March 20–April 24, 2021. Cambridge keeps its ordinary mapped buildings. [USGS point-cloud metadata](https://s3-us-west-2.amazonaws.com/usgs-lidar-public/MA_CentralEastern_1_2021/ept.json)

At Lee Street East and Leon Sullivan Way in Charleston, West Virginia, two USGS/FEMA lidar flight lines support a single Clay Center roof profile and its 13.07 m-radius planetarium dome. On two withheld southeast-facing street photographs, the paired game captures reduced mean left-side skyline error from 43.5 to 24.9 pixels and from 52.8 to 19.6 pixels. The roof still extends into some photographed sky. The authoring study includes the original views, survey dates and repeatable point-cloud checks. [Charleston landmark study](../lab/lidar/landmark/README.md)

Twelve small low-vegetation cells in the South Miami Avenue tile have supporting classified airborne returns, local ground measurements, an independent flight strip and near-infrared USDA aerial imagery. The model leaves gaps between cells. In the matched street-camera view these cells changed 161 of 784,384 rendered pixels. The lab includes the authoring command, exact sources, comparison and limits. [Low vegetation authoring](../lab/lidar/LOW-VEGETATION.md).

The wheel solver now builds lateral tire forces over rolling distance. The 0.35 m relaxation length and on-throttle 2:1 differential bias cap are provisional calibration values. The rear damper interpolates a published comparative Hard S Bilstein shock-dynamometer curve at a 0.77 shaft-to-wheel motion ratio. The front damper remains an estimate. The physical car's exact tire construction, damper condition and bias ratio have not been measured.

The source/data diagram generator is lab/build_diagram.py. When releasing changes to providers, the graphics pipeline, calibration or controls, regenerate the same diagram with the release source ZIP and output directory. On macOS it uses Python and Pillow.

    python3 lab/build_diagram.py --source "Sunday Driver - Miata Sport Source.zip" --out .

## Georeferenced shape and appearance

The streaming building worker now uses roof and wall tags carried in the same Overture PMTiles range responses as building heights. It checks properties against a unique overlapping base footprint, the recorded OSM way and its license before passing a finish to Godot. A matching observed finish in local survey data takes priority. The current renderer supports attributed flat or gabled roofs, material classes, tagged colors and supported ridge orientation. Unsupported geometry keeps its existing form. A live test used two mapped gables in Honolulu and a mapped glass exterior in Anchorage. No additional HTTP requests were needed for these attributes in the warmed test. [Overture building schema](https://docs.overturemaps.org/schema/reference/buildings/building/)

The source includes the CPU-only camera registration study in lab/capture/charleston. Two withheld street photographs tested how closely a lidar-derived landmark skyline could be registered to a passing camera. Scripts, reduced survey points, attributed image labels and results are included. The runtime needs no reconstruction model to display the resulting geometry.

The 96 MiB source cache favors recently used entries. Returned aerial images and thumbnails have a short handoff window so that another worker can finish loading them. A burst of recently used files can temporarily exceed the budget. The collector leaves unrelated files in the cache directory alone.

Key Largo’s measured US 1 pavement markings, independent Boston control, and driver-camera comparison are documented in the [road-paint study](../lab/road-paint/README.md).

The [independent footprint study](../lab/footprints/README.md) documents how to screen another U.S. district against two aerial surveys and independently mapped polygons. The included districts add 19 outlines in Lovelock, Nevada and one in Colby, Kansas. Source-modeled and unknown heights have separate provenance.

### Regional terrain

Open **Find a place → Monument Valley · US 163, UT** to drive toward the mesas. The regional terrain worker downloads small elevation tiles and aerial images around the active drive, simplifies them with Martini and hands Godot a reusable mesh. The foreground driving surface and tires continue updating during the download. Each region extends 45 km from its center along both map axes. The horizon can take several seconds to arrive on a first visit. Streetside detail, aerial dates and seasonal appearance vary with source coverage. See [the landscape measurements](../lab/landscapes/README.md) and the source diagram.

The Wyoming State Capitol in Cheyenne has a measured roof and independently corroborated photographic surfaces on parts of its south frontage. The registration check withholds the eastern correction. The nearby street, vegetation and portions of the facade still use ordinary streamed scenery. [Cheyenne source data and validation](../lab/lidar/cheyenne/README.md)


### Current Mac demo

Choose **Find a place → Monument Valley · US 163, UT** to compare the live 3D roadside banks, western shrub stands and surveyed road markings. **Capitol Avenue · Cheyenne, WY** starts a drive toward the measured dome and photographed frontage. Both use normal player controls. Photos, source dates and camera alignment limits are documented in the offline comparison gallery.

The project includes free open-source Godot, Jolt, Martini and GeoTIFF.js. Federal vegetation, elevation, lane and satellite data have different coverage and dates across the US. See [rangeland and soil validation](../lab/rangelands/README.md) and [stock Miata lock-up regression](../lab/vehicle/locked-braking/README.md).

## 1.0.5 landscape and street comparisons

Recent terrain can stream NASA snow observations followed by Copernicus classifications and calibrated reflectance on mountain ridges in the driver's view. Ridge selection, transferred bytes, photographic measurements and limitations are documented in [the Wyoming comparison](../lab/landscapes/wyoming/seasonal-ridges/README.md). The [shrub survey comparison](../lab/landscapes/utah/shrub-density/projected-cover.json) measures the area covered by the rendered geometry. A comparison photograph can predate its local aerial imagery by several years. The new [Medford shoulder](../lab/ma-road-section/README.md) and [Bartlesville roofs](../lab/lidar/bartlesville/README.md) have location-specific surveys. Other neighborhoods stream their available data.

The runtime uses Godot, Jolt, GeoTIFF.js, proj4js and Martini with no API keys for these layers. PDAL's spatially bounded EPT reader can help author verified roadside geometry from public lidar. OpenSfM can estimate the pose of licensed overlapping street photographs in an offline reconstruction. Cesium Native supplies streaming and level-of-detail selection for providers that publish 3D Tiles. These three research options are independent of the shipped native game. [PDAL](https://pdal.io/en/stable/stages/readers.ept.html) · [OpenSfM](https://github.com/OpenSfM/OpenSfM) · [Cesium Native](https://cesium.com/learn/cesium-native/ref-doc/)

## 1.0.6 public laser surveys

Start the game and enter `40.808017, -96.675328` to try the Nebraska street. As you drive, measured roof shapes can replace the base building model after the first scenery loads. This works by location, including unbundled streets. The November 8 study found qualified shapes in seven of twelve patches. The source dates and the houses that failed to qualify are part of the [roof-stream study](../lab/lidar/roof-stream/README.md). Its downloads are anonymous and cost the local player no API fees.

The photographed Snake River area uses 2,453 surveyed tree crowns and reconstructed riverbanks at the overlook. The [airborne forest study](../lab/airborne-forest/README.md) contains the procedure, original sources, matched view and geographic limits.


Xbox countersteering now releases the previous steering angle at the same rate as centering the stick. In the US 89 simulation at 24.2 mph, 200 milliseconds after a right correction the car had begun turning right. The previous controller response was still rotating left at 13.78 degrees per second. The [test report](../lab/vehicle/pad-corrections/README.md) includes mirrored cases, mapped Xbox axis inputs and the limits of the comparison.

Precisely dated street photographs can supply archived local sky and visibility. The game checks nearby airport observations against NOAA’s satellite cloud mask and retains the ordinary sky when the sources disagree. The study includes matched Nebraska, Oklahoma, Wyoming and Minnesota photographs, cold and warm helper costs, Alaska and Hawaii coverage, and the camera and weather limits. [Archived sky study](../lab/sky-archive/README.md)

Ground returns from separately identified USGS flight strips can supply measured road crown and camber on a later tile visit. Lincoln, Lewisburg and Anchorage include the mapped road and crossing controls required to use this option. The complete Miata driving tests, dates, limits, cache costs and instructions for adding other districts are in the [road-survey study](../lab/road-crossfall/README.md).

### Street height, road camber, observed façades and sky

The normal driving mesh now reads small blocks of the free USGS 1/3-arc-second ground product. A 14 KB index covers the national product directory. Revisiting the same area can reuse both the TIFF metadata and compressed elevation blocks on disk. Downtown Anchorage’s K Street became drivable when this replaced a spurious grade. The source checks include Alaska, Guam and Puerto Rico, a four-product join, and a mixed-source tile boundary. [Ground measurements and replay](../lab/terrain-source/README.md)

The roof survey can reuse its laser returns to resolve road camber in three districts with verified roadway and crossing records. The resulting triangles carry the road material and support the tires. Two observed wall patches in Iowa City and Minneapolis use photographs registered from different locations. Historical views can combine a dated source photograph with station reports and the satellite cloud mask. [Road camber](../lab/road-crossfall/README.md) · [Observed walls](../lab/holdout/ordinary-street-walls/README.md) · [Cloud reconstruction](../lab/sky-archive/README.md)

At 601 West Fifth in Anchorage, the optional game argument `--source-era 2015-05-22` shows the pre-renovation ribbon windows measured in four public photographs. The historic finish is date- and footprint-gated. [Historical cladding and reproduction](../lab/lidar/anchorage/cladding/README.md)

Mapped single-lane one-way roads can use explicit OSM lane counts to correct inferred center stripes. Q Street NW in Washington adds four independently registered painted marks and a measured left-side parking envelope. The normal macOS camera pairs and Portland four-wheel driving check are included in the [lane-marking study](../lab/road-layout/README.md).

Engine sound follows the Miata's simulated RPM, throttle, clutch load, shift interruption and stall. Press M or use the sound control to hear it. [Audio preview, measurements and reference recordings](../lab/vehicle/engine-sound/README.md)
