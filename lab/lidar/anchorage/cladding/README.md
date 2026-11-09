# Historic cladding at 601 West Fifth Avenue

> Former name: this study was written when SimCruise was called "Sunday Driver"; the app, paths and commands it mentions under that name are now SimCruise.

Start an ordinary drive with the game argument --source-era 2015-05-22 to enable this recorded material at 601 West Fifth in Anchorage. The optional date applies to the historic exterior only. It uses no runtime downloads. The documented date span is July 20, 2007 through May 22, 2015. An explicit reference-epoch must fall on the same Anchorage-local day. Automatic nearby street-photo dates and a historical daylight setting alone leave the contemporary material selected. The measured 2015 polygons and 39.2 m eave must both match before five surveyed street-facing wall sections can change. The walls share mesh vertices with their original podium and cap. All existing roof and vehicle collision geometry remains intact.

Three licensed photographs by independent authors were rectified on clearly visible upper walls. Measured glazing fractions were 58.6%, 61.1% and 62.5%. The independent Warren LeMay 2015 photo gave 61.7%. A 2009 close view from the north measured a roughly 3.98 m floor pitch and 1.63 m cap against two independently mapped north-wing widths. Color, roughness and thin mullions approximate the old material. The shader shades and reflects the current environment and contains no photograph. Only the most readily identified upper façade is represented. The recessed ground-floor entry and the neighboring office are generic.

The project builder reports completing a structural-glass recladding and 40,000-square-foot expansion of the southeastern floor plates on August 15, 2023. Both the historic footprint and material have changed. This dated model cannot document the modern shape. A bounded search of nearby open street imagery found bike-rack close-ups from 2024 and KartaView street surveys from 2022.

Download or reuse the five documented source images and remeasure their surfaces with NumPy, SciPy and Pillow.

    python3 measure.py /path/to/image-cache

The cache folder contains public images under their original licenses. sources.json lists creator, source page, rights, date, resolution and the four wall-space correspondence pixels used per photograph. The 2015 view was excluded from all material fitting. The original source pin was displaced; the previously registered camera fits the white office at 620 West Fourth, independent of the foreground cladding. measurements.json contains the recovered values. The game accepts the numerical material parameters. The comparison includes Warren LeMay's CC0 photograph.

The headless acceptance test covers opt-in, calendar validity, both interval limits, local-midnight conversion in summer and winter, mismatching reference dates, duplicate options, changed footprints and adjacent roofs.

    Godot --headless --path game --script res://checks/HistoricAnchorageCheck.gd

To view the frozen dated camera using the ordinary packaged game:

    "Sunday Driver" -- --reference 61.217450586 -149.893369057 290.928953 55.35 /tmp/anchorage-2015.jpg -10.465512 1.045498 --reference-epoch 1432331510 --source-era 2015-05-22

The three independently launched, native 960 × 720 Metal checks produced 0, 1 and 0 historic material surfaces for ordinary, historic and conflicting-date settings. Four wheels were grounded in each run. Between ordinary and conflicting-date frames, no channel varied by more than two levels out of 255. In the 36,464-pixel common upper wall region, the dated model reduced RGB MAE from 56.83 to 45.44/255 and increased vertical contrast profile correlation from .060 to .334. The registered foreground silhouette is still several dozen pixels from the image. Dynamic illumination and neighboring buildings differ from the photograph.

Sources include the individual links in sources.json, the project builder at https://www.neeserinc.com/601-west-5th-avenue/ and the original municipal airborne survey at https://www.fisheries.noaa.gov/inport/item/50281 .
