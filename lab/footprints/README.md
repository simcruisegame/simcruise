# Independent building outlines

> Former name: this study was written when SimCruise was called "Sunday Driver"; the app, paths and commands it mentions under that name are now SimCruise.

The included districts add 19 clearance-tested footprints in Lovelock, Nevada and one in Colby, Kansas. Sixteen have a Microsoft model estimate for total height. Four have unknown height and render with a 3 m display assumption. Eave heights, façade details, roof pitch and building use remain unknown. The runtime preserves those statuses separately. A previously classified-lidar building takes precedence if both methods support the same geometry.

The authoring step is reusable for other zoom-16 U.S. districts. The game reads a small evidence manifest for the two districts included in this release. It rechecks the current Overture outline hash, geometry origin, all known buildings and parts, nearby roads and tile ownership against the current basemap. It appends remaining shapes after the original building indices. It reuses the existing district archive request. The road surface and tire-contact datasets are independent of these additions.

## Reproduce a district

Use the repository's Bun setup and Python 3 with NumPy, SciPy, Shapely 2 and Pillow. The public inputs require no account or payment. Run these commands from the project root. Substitute another zoom-16 X/Y pair to screen a new district.

```sh
ROOT_CACHE="$TMPDIR/sunday-driver-outline-study"
mkdir -p "$ROOT_CACHE/map" "$ROOT_CACHE/public"
bun helper/atlas.mjs tile "$ROOT_CACHE/map" 11200 24767 > "$ROOT_CACHE/driving-tile.json"
bun lab/footprints/screen-inputs.mjs "$ROOT_CACHE/map" 11200 24767 "$ROOT_CACHE/inputs.json"
python3 lab/footprints/screen_outlines.py "$ROOT_CACHE/inputs.json" "$ROOT_CACHE/public" "$ROOT_CACHE/lovelock.json"
python3 lab/footprints/screen_outlines.py --offline "$ROOT_CACHE/inputs.json" "$ROOT_CACHE/public" "$ROOT_CACHE/replay.json"
cmp "$ROOT_CACHE/lovelock.json" "$ROOT_CACHE/replay.json"
```

Colby uses 14370, 24954. The public source cache is an immutable authoring snapshot. A fresh directory requests current vintages. The normal atlas cache manages Overture release selection and base-map downloads. Replacing a district in `helper/corroborated-outlines.json` requires reviewing the resulting counts, source attribution and camera/roof evidence. The authoring command only writes the chosen output file.

The USA Structures request includes only geometry, source, validation method, source image date and height. Its source cap is 350 records or 350 kB. NAIP items are selected from the actual footprint location. A neighboring same-date quad can fill a no-data edge. The two witness images require distinct acquisition years and a source date different from the ORNL image date. Source requests have a nine-second per-read timeout and one-hour throttling cooldown. Worst-case per district is 12 authoring requests and 6.4 MB of response bodies. Source timeouts and truncation halt the run. A complete warm replay uses zero requests. Reuse the source cache for adjacent work and delete obsolete offline snapshots to reclaim disk space.

## Acceptance and controls

The basemap and Overture must leave a simple, aboveground building polygon clear of existing footprints and road buffers. The independent FEMA/ORNL layer must match it uniquely in both directions with overlap-over-union of at least .70, center difference of at most 3 m and area ratio .70–1.43. Features labeled “Unverified” or sourced to a substitute producer cannot pass.

Only previously mapped buildings train the optional district translation. Disjoint existing buildings validate it. A fitted translation requires at least five held-out buildings, five training buildings, median residual at most 1.7 m, translation no greater than 5 m and held-out median IoU gain at least .07. Districts already aligned to the source can use native coordinates after seven training and seven blind controls, median native IoU ≥ .8, lower-quartile IoU ≥ .65, median native center error ≤ 2 m and upper-quartile error ≤ 3 m. Moving the ORNL layer 12 m east must eliminate the candidate matches. Candidate buildings never serve as registration controls.

The optical check tests direction and color contrast along the proposed roof edges and along parallel strips on both sides. At least three distinct edges and 65% of the tested perimeter must pass in each of two separately dated images. Missing imagery and black no-data seams abstain. Two extraction pipelines can use similar source imagery, and an ORNL “Spatial Intersect” label does not specify its validation dataset. The optical checks and the independent acquisition dates matter.

| District | Existing buildings | Training / blind controls | Held-out IoU | Added buildings | Cold authoring requests / body bytes |
| --- | ---: | ---: | --- | ---: | --- |
| Lovelock NV | 34 | 13 / 11 | .668 → .798 after translation | 19 | 4 / 512,441 |
| Colby KS | 21 | 9 / 8 | .864 native, .845 with the rejected fitted translation | 1 | 4 / 500,432 |

The Nevada example matches Microsoft and ORNL at IoU .930, with centers .63 m apart. ORNL dates the source image to November 2022. August 2019 and June 2022 USDA photos show the same roof in the same location. Its 4.3909 m rendering height comes from Microsoft. Autumn 2020 USGS airborne samples showed a raised surface, but the project supplied no class-6 roof returns and the alternative height check had insufficient footprint coverage. The Kansas building has IoU .722 and a 1.28 m center difference. Its USDA photographs were taken in July 2021 and August 2023, and ORNL dates its source image to July 2024. [Nevada flight comparison](evidence/lovelock-two-dates.jpg) and [Kansas flight comparison](evidence/colby-fringe-two-dates.jpg).

For the blind spot audit, 12 Nevada candidate centers and 16 independently randomized Nevada points were mixed before examining the photographs. Ten candidate centers looked like roofs and two were indeterminate. Six were among the final accepted additions, all visibly roofs. The Kansas candidate looked like a roof. Across the independent sets, none of 24 clearly open control points lay in an added structure. The low sample count does not establish national precision. Alamosa, the previously tested Allston and Lewisburg tiles, and multiple Winnemucca neighborhoods produced no accepted addition under the same geometry controls. Both headlining tiles had no street photo available from the ordinary KartaView/Panoramax sampler.

Run the standard Godot entry point with `-- --building-check lovelock` or `-- --building-check colby`. These checks traverse normal navigation, background streaming, roof meshing, wall collisions and four-wheel contact. They also assert that the original 34/21 indices keep the same outlines and that the inferred and measured inventories stay separate. The isolated [Nevada](evidence/lovelock-godot-comparison.jpg) and [Kansas](evidence/colby-fringe-godot-comparison.jpg) Godot comparisons show how geometry changes an oblique view of the aerial image. Neutral walls in those figures show the limits of the available façade evidence.

## Sources

Microsoft ML Buildings via Overture Maps Foundation provide ODbL 1.0 geometry and, where attributed, total-height estimates. ORNL and FEMA Geospatial Response Office provide USA Structures under CC BY 4.0. This method transforms the ORNL reference geometry for registered districts and publishes derived agreement measurements. USDA NAIP photographs are public domain. Each admitted outline retains its ORNL date, STAC image identifiers, photo acquisition dates and Overture geometry digest. [Overture attribution](https://overturemaps.org/attribution/), [Microsoft data](https://github.com/microsoft/USBuildingFootprints), [FEMA published item/terms](https://www.arcgis.com/home/item.html?id=0ec8512ad21e4bb987d7e848d14e7e24), [ORNL production methods](https://pmc.ncbi.nlm.nih.gov/articles/PMC11099081/), [USDA NAIP](https://www.usgs.gov/centers/eros/science/usgs-eros-archive-aerial-photography-national-agriculture-imagery-program-naip).
