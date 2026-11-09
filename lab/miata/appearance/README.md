# 1999 U.S. Miata Sports Package

The 1999 U.S. Mazda sales catalog, an original 1,300-mile Emerald Green Sports Package car, and the workshop manual informed the model-year layout. The separately licensed lamp photographs supply image detail. The bonnet, windscreen, roof, interior fabrics, rear spoiler and reflectors still differ visibly from the original. Each rendered pair holds the test camera and the road fixed.

- [U.S. Mazda 1999 brochure, 9999-92-0105-99](https://importarchive.com/brochure/mazdamiata1999_02)
- [1999–2001 Mazda Workshop Manual, instrument cluster in section 09–22](https://www.mellens.net/mazda/Mazda-Miata-1999-2001.pdf)
- [Original Emerald Green 1999 Sports Package car with 1309 miles, photos 34 and 114–123](https://www.pcarmarket.com/results/1999-mazda-mx-5-miata)

## Photographic adaptations

Elise240SX photographed a 1999 silver Leather Package on June 29, 2023. The shared NB1 clear front light was masked inside the gasket, rectified and partially desaturated except for the inboard amber chamber. Its 384 × 102 pixel adaptation lands on curved left and right housings. The upper/lower green bumper, recessed radiator cavity, amber side markers and wheelhouses are original approximate geometry. Kevauto photographed the rear lights on April 28, 2018. The prior game's perspective-adjusted stop-lamp adaptation remains present. The exterior model, photographic adaptations and rendered exterior comparison are shared under CC BY-SA 4.0. The gauge graphics were redrawn procedurally. The manufacturer brochure and seller photos are linked as references only.

- [Elise240SX, source photograph and rights](https://commons.wikimedia.org/wiki/File:1999_Mazda_MX-5_Miata_Leather_Package_in_Highlight_Silver_Metallic,_Front_Right,_06-29-2023.jpg)
- [Kevauto, source photograph and rights](https://commons.wikimedia.org/wiki/File:1999_Mazda_MX-5_Miata_rear_4.28.18.jpg)
- [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)

## Reproduce

From the source root, the first command regenerates the separate driver cabin using NumPy and Pillow. For the exterior, use the prior game/art/miata.glb from the 1.0.3 source package and the full 3486 × 2214 original photograph above. The generator checks both inputs’ hashes before writing the output. SciPy, NumPy and Pillow are required. The completed model and imported texture are included with the game.

    python3 authoring/build_cockpit.py
    python3 authoring/build_nb_front.py ORIGINAL-1.0.3.glb ORIGINAL-FRONT-RIGHT.jpg candidate.glb
    python3 authoring/check_assets.py

The native check opens the normal default Chula route. Run the game with a separately named scratch project/app-data directory and pass an absolute capture directory. The high-resolution viewport captures require Metal or another graphics device. The car is stationary while photographing the matched views. The four wheels then drive, steer and brake through ordinary inputs for 240 physics ticks.

    godot --path game --windowed --resolution 1280x826 -- --car-audit /absolute/qa-directory

The exterior pair compares oblique views. The instrument pair uses an inspection camera with the steering wheel hidden to expose the entire cluster. The in-game driving camera retains the steering wheel. The independent front photograph of the green car suggests an intake width of roughly 60% of the bumper width. Manual image annotations put the control insert at about 53% and the updated opening at about 62%. Camera elevation limits this approximation. The JSON receipt includes the image measurements, file hashes, separate normal-drive runs, the instrument threshold and the packaged test.

The 15-inch rim bead seat, 195 mm nominal tire width, 576 mm unladen outside diameter and 2.266 m wheelbase apply to the driving car. The three rear stop-light meshes stay attached to the chassis around the physical rear wheels. The maximum measured chassis-relative drift was zero through 4.5 rear wheel revolutions. Both runs ended with four contacting tires.

## Factory 15-inch castings

Five spoke openings in a photograph of one original rear wheel supplied relative radial coverage and guided the reconstructed casting. An independent photograph of the opposite rear wheel on the same car checked the result. The vendor's bare casting also shows the rounded spoke returns and four lug recesses. Only numeric shape measurements were transferred. No photograph was wrapped around the rims. The factory silver is represented by an estimated metallic-paint finish.

- [Original car, photographs 72 and 65](https://www.pcarmarket.com/results/1999-mazda-mx-5-miata)
- [Independent casting photograph, 15 × 6 rim, 4 × 100 mm lug spacing and 40 mm offset](https://i1parts.com/products/mazda-mx-5-miatta-1999-2000-15-factory-original-wheel-rim)

At 45%, 52%, 60%, 68% and 75% of flange radius, the game’s angular metal-coverage error against the second photograph averaged 23.11 percentage points before and 3.36 after. The photo ellipse/edge picks and dish perspective contribute uncertainty. The numeric outline script and measurements are beside this file. The material-ID render of the baked wheel matched the procedural version pixel for pixel. Wheel shape was captured in the engine's normal lit view for the gallery.

The wheel is baked once in authoring and shared by the four mounted wheels. It measures 189 KiB and loaded in 1.7 ms in one local test. The 13,200 triangles and four material surfaces include the tire, rim, four fasteners and disc. The caliper remains a separate, nonspinning mesh on each hub. For comparison, building the new geometry procedurally at runtime took around 100 ms.

    godot --headless --path game --script ../authoring/build_sport_wheel.gd
