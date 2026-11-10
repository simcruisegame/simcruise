# 1999 NB Miata turntable against photographs

`game/checks/CarTurntable.gd` photographs the player's car exactly as `Drive.gd` builds it (the same `CarFinish.gd` pass), alone on a neutral concrete pad under the game's daylight sky and a studio sun, at Ultra quality (4x MSAA, 4096 shadow atlas). It saves eight exterior angles and a top view, close-ups of a headlamp, a tail lamp, a front wheel and the cabin through the windscreen, the in-game driver view, and with `--lamps` three dusk shots on the pad plus one on the bundled street. Long lenses (11.5° to 16° vertical) keep perspective close to the reference photographs.

    godot --path game --windowed --resolution 1600x1000 -- --car-turntable /absolute/out --quality=ultra --lamps

The model in the game is a 1999 car with the body-colour factory hardtop. Kevauto's green 1999 hardtop car on Commons is therefore the main reference; Elise240SX's silver 1999 Leather Package and Tysto's side photograph cover the rest.

- [compare-exterior.jpg](compare-exterior.jpg): reference, before, after at front 3/4 left, rear 3/4 left, front 3/4 right and side.
- [compare-details.jpg](compare-details.jpg): headlamp, tail lamp, wheel, cabin through the screen, driver view.
- [turntable-before.jpg](turntable-before.jpg), [turntable-after.jpg](turntable-after.jpg): all sixteen views. "Before" is commit 3f66d3e's `CarFinish.gd`, `SportWheel.gd` and `Cockpit.gd` under the same turntable.
- [receipt.json](receipt.json): every photograph looked at, with author, licence and where it is used, and the `CAR FINISH` build report.

## What differed, view by view

| View | Photograph | Game before | Status |
|---|---|---|---|
| Front 3/4, rear 3/4, top | Smooth domed hardtop and glass, continuous reflections | Hardtop and all glass were loose flat quads (122 in the paint, 51 in the glass), each reflecting the sky as its own tile: a chequered roof and rear window | Fixed: shared normals |
| Front 3/4, interior | Slim, flat, body-colour A-pillars and header with black seals | Round light grey tubes about twice the width | Fixed: body colour, half radius; still round |
| Interior, front | Black dash, wheel rim, bulkhead (Sport) | Blue-grey "trim" dash top, wheel rim and bulkhead seen through the glass; seats washed out behind smoked (58 % opaque) glass | Fixed: black trim, 34 % tinted glass |
| Side, wheel | Black underseal and wheel-arch liners, nothing below the sills | Light grey underbody strip below the sills and grey panels behind the wheels | Fixed: satin black underbody, black liners |
| Front 3/4 | Black plastic cowl grille between hood and screen | Paint | Fixed |
| Front 3/4 right | Mirror on the front corner of the door, about 190 × 120 mm glass | Small mirror a third of the way down the door | Fixed: moved forward 0.24 m, larger |
| Wheel | Factory silver, mid grey | Near-white | Fixed: darker metallic silver under clear |
| Driver view | Green hood; hardtop header above the screen | Hood rendered as chrome (cockpit paint too metallic); open sky above (the cabin model is open-topped) | Fixed: hood paint; hardtop lining on the cockpit layer |
| Dusk | Headlamps light the road; tail lamps glow, stop lamps brighter | No head lamps; tail lamps dark unless braking | Fixed: L key, lens glow, two low-beam spot lights, amber parking lamps, tail glow 0.42 under the stop lamps' 1.72 |
| Side | Wheelbase 2265 mm, length 3945 mm, tyre 576 mm | 2.266 m, 3.94 m (body and bumper), 0.576 m; ground clearance under paint 171 mm, arch gaps about 40–60 mm, as photographed | Unchanged (physics fixed) |
| Headlamp | Large oval lens, about 150 mm tall, on the fender front | Same lens texture on a thinner slit sitting on the nose | Remaining: needs new lamp and nose geometry |
| Rear | Smooth bumper with one crease below the plate | Wavy reflections from the low-poly lower bumper | Remaining: needs a new bumper mesh |
| Rear 3/4 | Large wrap-around hardtop rear glass, thin sail | Narrow rear glass, large body-colour C-pillar | Remaining: needs a new hardtop |
| All | Long, tight hood and fender surfaces | Visible ripples in hood and fender reflections (coarse shell) | Remaining: needs a new body shell |
| Front 3/4 | Body-colour hood with a centre dome and pronounced fender crowns | Flatter | Remaining |
| Interior | NB dash, door cards, Sport cloth seats | Exterior shows a coarse stand-in cabin; the driver view has the detailed cockpit | Remaining |

The A-pillars are still round tubes, the paint is a little darker than Kevauto's car in overcast light, and the low-beam pattern is a plain cone, not a cut-off beam. The hardtop lining only exists from the driver's seat; the scenic and chase cameras see the exterior hardtop as before.

## Credits

The comparison sheets are adaptations shared under CC BY-SA 4.0 (they contain the photographs below, scaled and cropped, beside renders of the CC BY-SA 4.0 exterior model).

- Kevauto, [1999 Mazda MX-5 Miata front 4.28.18](https://commons.wikimedia.org/wiki/File:1999_Mazda_MX-5_Miata_front_4.28.18.jpg) and [rear 4.28.18](https://commons.wikimedia.org/wiki/File:1999_Mazda_MX-5_Miata_rear_4.28.18.jpg), CC BY-SA 4.0
- Elise240SX, [1999 Mazda MX-5 Miata Leather Package in Highlight Silver Metallic, Front Right, 06-29-2023](https://commons.wikimedia.org/wiki/File:1999_Mazda_MX-5_Miata_Leather_Package_in_Highlight_Silver_Metallic,_Front_Right,_06-29-2023.jpg), CC BY-SA 4.0
- Tysto, [Mazda-miata-1999-blue-side](https://commons.wikimedia.org/wiki/File:Mazda-miata-1999-blue-side.jpg), public domain
- Pahazzard, [2002 Mazda MX5 1.8 Sport Black Interior](https://commons.wikimedia.org/wiki/File:2002_Mazda_MX5_1.8_Sport_Black_Interior.jpg), CC BY-SA 3.0
- [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/), [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/)
