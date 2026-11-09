# Bartlesville shop roofs and grade

At Southeast Washington Boulevard, an exact outline match enables two USGS-derived roof profiles. A separate 196-node correction uses the same elevation grid for the foreground mesh and the Miata’s wheel contacts. The patch requires the original tile ID, resolution and elevations to match. The outermost grid rows and the road center remain outside this correction. Other districts continue through the ordinary streaming pipeline.

The federal project OK_Statewide_1_D22 was acquired between December 14, 2022 and January 18, 2023. Its 257,925-byte Entwine LAZ leaf includes ASPRS class 2 ground and class 1 unclassified structures. Its class 1 points acquire an architectural interpretation through coherent elevated surfaces inside independently mapped OpenStreetMap outlines, spatially withheld returns, two open-parking controls and separate street photographs. Two translated control polygons contained 3 and 21 elevated returns. The restaurant contained 882. Federal laser elevations are expressed relative to fitted local class 2 ground. [USGS project](https://usgs-lidar-public.s3.amazonaws.com/OK_Statewide_1_D22/ept.json) · [USGS project catalog](https://index.nationalmap.gov/arcgis/rest/services/3DEPElevationIndex/MapServer/24) · [OpenStreetMap attribution and ODbL](https://www.openstreetmap.org/copyright)

| Result | Evidence |
| --- | --- |
| Restaurant roof deck | 5.31 m median above local ground. 97.3% of its eligible core cells support the dominant plane. |
| Raised entrance roof | A 7.09 × 7.21 m four-sided hip rises to 8.97 m. Twenty withheld roof returns gave 0.044 m median and 0.087 m 90th-percentile vertical residual. Aerial returns extend 2.63 m beyond the existing footprint at the entrance roof. The original ground-level collision outline remains intact. |
| Adjacent shop | 4.78 m median roof plane with 100% eligible cell coverage. |
| Forecourt grade | 702 withheld class 2 samples. Median and P90 ground error fell from 2.35 and 2.80 m to 0.04 and 0.43 m. Twenty-two samples on the nearby road constrain the 0.97 m local vertical-datum offset. |
| Default normal game | Cross-season matched roofline error fell from 22 px median / 28.6 px P90 to 3 px / 5 px across 75 columns at 960 × 720. The ordinary player controls drove 42.10 m on Southeast Washington Boulevard with a minimum of four wheel contacts. |

These measurements describe the two specific stores. The arches, storefront glazing, parked vehicles and freestanding sign are still visually different in the default game. The renderer uses the mean observed roof and upper-wall colors from the separate, credited street-appearance study. The individual image-space controls and corrected survey appearance remain subject to survey, camera and registration uncertainty.

## Rebuild

Install Python with NumPy, SciPy, Shapely and laspy with its LAZ backend. The local authoring command needs one small checked file on first use. Repeating the command reuses that file. The game reads its prepared 41,672-byte data packet from the local project without making any additional provider calls while driving.

```sh
python -m pip install numpy scipy shapely 'laspy[lazrs]'
python lab/lidar/bartlesville/generate.py /tmp/bartlesville-result --fetch
```

An existing file can be supplied with `--leaf /path/to/11-1298-1358-1021.laz`. Compare the two resulting JSON files against `game/data/`. `source.json` includes the download checksum, exact project scope and source photograph metadata. `base-tile.json` contains the necessary road lines, grid and outlines from OpenStreetMap through OpenFreeMap plus base terrain elevations. The game-code license does not replace the ODbL on the map extract. [OpenFreeMap](https://openfreemap.org/) · [AWS open terrain tiles](https://registry.opendata.aws/terrain-tiles/)

## Registered wall experiment

An ordinary shopping street was selected from a limited US Panoramax catalog search before checking source-photo registration. The first result lacked repeat street views. The next district had June 7, 2024 and February 12, 2026 photography by Baloo Uriza under CC BY-SA 4.0. Six small views totaled 1,350,790 bytes and a separate 2,215,491-byte source image supported the offline study. The two seasons include a real storefront, entrance and a separate roadside pylon. It took manual control identification and an occlusion mask to register this site. Ordinary two-view visual matching alone could project the storefront onto a false wall 24 m into its parking lot.

The federal survey independently locates a 4.8 m-wide sign in front of the shops. Reprojection against the physically separate sign and roof corners distinguished the true wall from the translated controls. Withheld cross-season storefront features agreed on the real wall within 0.52–0.60 m median. Translating the wall by 12 m increased this to 1.74–1.75 m, and a 24 m translation increased it to 3.13–3.59 m. No storefront pixels entered those camera fits.

The experimental mask covers 79.4 m² of the mapped front. The fixed façade-window-band luminance-pattern correlation against the separate February photograph was 0.011 for the revised geometry with generic windows and 0.071 with the partial photographic layer. The original geometry scored 0.111 in that same band. The default package includes only the roofs, grade and observed solid roof colors. The gallery contains a separate rendering of the unshipped wall. Field and experimental images were reprojected, cropped, masked and/or exposure corrected. [June 2024 source](https://panoramax.xyz/#focus=pic&pic=facd96a4-bae2-4230-9793-8a3a60a851be) · [February 2026 field reference](https://panoramax.xyz/#focus=pic&pic=37d05f23-c4c9-428d-90b0-0a47b4e6be13) · [Creative Commons BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
