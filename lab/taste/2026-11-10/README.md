# Taste test 1: the game against reality (10 November 2026)

This is the first "taste reality" pass. The game is rendered at Ultra, posed like licensed photographs of the real thing, and the differences are ranked by how much each one breaks the illusion. The ranking is a judgement made by looking at the images side by side. It is not a metric: the realism score on the [roadmap](../../../docs/roadmap.md) ("Places that look like the real place", item 7) is still to come. Run the test again after every iteration, with the same commands and poses, so the lists can be ticked off or reordered.

Build under test: `main` at e1313db (Godot 4.7.2, Vulkan, NVIDIA RTX 2070 Super with Max-Q Design, Windows 11), branch `taste/baseline`.

## Method (re-runnable)

1. **Helper.** The captures stream map data through the helper. Build it once in the checkout. The `tools/` folder is git-ignored.

       bun install --frozen-lockfile
       bun build --compile --minify --target=bun-windows-x64 --no-compile-autoload-dotenv --no-compile-autoload-bunfig \
         --no-compile-autoload-tsconfig --no-compile-autoload-package-json helper/atlas.mjs --outfile tools/atlas.exe
       "$GODOT_BIN" --headless --editor --path game --import     # once; run it again if the first run crashes on exit

   On macOS or Linux, drop `--target` and write `tools/atlas`.

2. **Captures.** These need a window, because the captures read the viewport, and they always run at `--quality=ultra`:

       GODOT_BIN=/path/to/Godot_v4.7.2_console sh lab/taste/scripts/capture.sh /abs/out            # Mobile (shipping) renderer
       GODOT_BIN=/path/to/Godot_v4.7.2_console sh lab/taste/scripts/capture.sh /abs/out-fp forward_plus

   [`scripts/capture.sh`](../scripts/capture.sh) runs:
   - **Car:** `checks/CarTurntable.gd` (`--car-turntable DIR --quality=ultra --lamps`, 1600×1000). It produces 13 studio views of the 1999 roadster with the body-colour hardtop, the driver view and 4 dusk shots. The lenses and sun come from the script (`VIEWS`, sun at 42° elevation, 215° from the nose).
   - **Place:** `checks/ReferencePose.gd` (`--reference LAT LON HEADING HFOV OUT.jpg 0 0 --reference-epoch UNIX --complete-reference`). The camera sits 1.65 m above the ground, the player car is hidden and the sun is set for the given time. The poses:

     | Name | Lat, lon | Heading | Horizontal FOV | Time (UTC) | Why |
     |---|---|---|---|---|---|
     | `lummus` | 25.7770425, −80.1309641 | 7° | 70.8° | 2025-04-24 14:34:52 (1745505292) | Exact pose of Panoramax [4b6d216b](https://panoramax.xyz/#focus=pic&pic=4b6d216b-ae1c-48a8-b2ae-5d9558334f18). Position, compass heading and time come from the frame. The field of view is the Pixel 8 main camera (6.9 mm). Rendered at 1600×720 to match the frame's 2.22:1 aspect. |
     | `oceandrive-north` | 25.77800, −80.13118 | 12° | 80° | same | On Ocean Drive at 8th Street, at driver height, under the same sun. No licensed street-level frame exists on the roadway itself: Panoramax has only that walker's beachwalk set here, and Mapillary needs an API token. |
     | `telluride` | 37.9374275, −107.8118513 | 108° | 80° | 2025-07-15 19:00 (1752606000) | Random start-pool entry 25, plus `--landscape-reference` |
     | `oatman` | 35.0441967, −114.3678383 | 250° | 80° | same | Random start-pool entry 28, plus `--landscape-reference` |

     The random entries were picked with the LCG `s = (s*1103515245+12345) mod 2^31` from seed 20261110 over `game/data/start-pool.json` (36 entries). Use a new seed each iteration so new places are tested, and keep these two as fixed controls.
   - Without `--landscape-reference`, Telluride was captured before the regional terrain arrived and had no mountains at all (see place item 9). Without `--reference-epoch`, the sun follows the clock and sat 7° high.

3. **References.** [`scripts/commons.mjs`](../scripts/commons.mjs) (`search`, `cat`, `info FILE WIDTH DIR`) and [`scripts/panoramax-near.mjs`](../scripts/panoramax-near.mjs) (`search LAT LON HALF_DEG`, `get ID DIR`) look up and fetch reference photos into a scratch folder for viewing. Commons rate-limits quickly, so wait a few seconds between files. Only links are committed here.

4. **Compression.** `godot --headless --script lab/taste/scripts/shrink.gd -- OUT_DIR WIDTH QUALITY files…` resizes with Lanczos and writes JPEG. Car views go to 1200 px at quality 0.8. Place views go to 1280 px at quality 0.82. `--sheet OUT COLS CELL_W QUALITY files…` makes the Forward+ contact sheet. This folder holds about 1.8 MB.

## Game captures

- [car/](car): `front`, `front_34_left`, `front_34_right`, `side_left`, `side_right`, `rear_34_left`, `rear_34_right`, `rear`, `top`, `headlight`, `taillight`, `wheel`, `interior`, `cockpit`, `lamps_front`, `lamps_rear`, `lamps_brake`, `lamps_street`, all with the Mobile renderer. [car/forward-plus-sheet.jpg](car/forward-plus-sheet.jpg) shows the same 18 views with `--rendering-method forward_plus`.
- [place/](place): `lummus-mobile`, `lummus-fplus` (the Panoramax pose), `oceandrive-north`, `telluride`, `oatman`.

Forward+ and Mobile look almost the same in every view, so the renderer is not the bottleneck. Forward+ adds contact darkening under the car (SSAO) and slightly crisper wheel shading. It also draws a black band across the top of the sky in `rear_34_left`, which is a Forward+ artefact the Mobile run does not have.

## References (links only, nothing committed)

| Photo | Author | Licence | Used for |
|---|---|---|---|
| [1999 Mazda MX-5 Miata front 4.28.18](https://commons.wikimedia.org/wiki/File:1999_Mazda_MX-5_Miata_front_4.28.18.jpg) | Kevauto | CC BY-SA 4.0 | Front 3/4, green hardtop car, headlamp, cowl, mirror (its chrome multi-spoke wheels are not the 1999 factory design) |
| [1999 Mazda MX-5 Miata rear 4.28.18](https://commons.wikimedia.org/wiki/File:1999_Mazda_MX-5_Miata_rear_4.28.18.jpg) | Kevauto | CC BY-SA 4.0 | Rear 3/4, hardtop rear glass and sail, tail lamps, high-mount stop lamp, bumper |
| [1999 MX-5 Miata Leather Package, Front Right, 06-29-2023](https://commons.wikimedia.org/wiki/File:1999_Mazda_MX-5_Miata_Leather_Package_in_Highlight_Silver_Metallic,_Front_Right,_06-29-2023.jpg) | Elise240SX | CC BY-SA 4.0 | The 1999 15-inch 5-spoke alloy (as on the Sport), nose, soft top |
| [1999 MX-5 Miata Base, Rear Left, 08-06-2022](https://commons.wikimedia.org/wiki/File:1999_Mazda_MX-5_Miata_Base_in_Highlight_Silver_Metallic,_Rear_Left,_08-06-2022.jpg) | Elise240SX | CC BY-SA 4.0 | Rear, side sills, soft-top frame (14-inch wheel) |
| ['99-'00 Mazda MX-5 Miata (Hudson)](https://commons.wikimedia.org/wiki/File:%2799-%2700_Mazda_MX-5_Miata_(Hudson).JPG) | Bull-Doser | Public domain | 15-inch alloy face, headlamp, front intake |
| [Mazda-miata-1999-blue-side](https://commons.wikimedia.org/wiki/File:Mazda-miata-1999-blue-side.jpg) | Tysto | Public domain | Side profile, windscreen rake, belt line |
| [1999 Mazda MX-5 Miata](https://commons.wikimedia.org/wiki/File:1999_Mazda_MX-5_Miata.jpg) | DestinationFearFan | CC BY-SA 4.0 | Green paint in sun, tan soft top, headlamp (aftermarket wheels) |
| [Mazda MX-5 - left tail-light](https://commons.wikimedia.org/wiki/File:Mazda_MX-5_-_left_tail-light.jpg) | Mafmafmaf (en.wikipedia) | CC BY 2.5 | Tail-lamp optics (a 2001+ lamp: clear centre, where the 1999 lamp is amber) |
| [2002 Mazda MX5 1.8 Sport Black Interior](https://commons.wikimedia.org/wiki/File:2002_Mazda_MX5_1.8_Sport_Black_Interior.jpg) | Pahazzard | CC BY-SA 3.0 | Wheel, shifter boot, carpet, door card (2001+ dash and gauges, so only partly comparable) |
| [10am-dash](https://commons.wikimedia.org/wiki/File:10am-dash.jpg) | Mafmafmaf (en.wikipedia) | CC BY-SA 3.0 | 1999 dash. **Not viewed:** Commons refused the download (rate limit) |
| [Panoramax 4b6d216b…](https://panoramax.xyz/#focus=pic&pic=4b6d216b-ae1c-48a8-b2ae-5d9558334f18) | MatthiasMatthias | CC BY-SA 4.0 | Lummus Park beachwalk and Ocean Drive frontage, 2025-04-24 |
| [Panoramax 9dba016e…](https://panoramax.xyz/#focus=pic&pic=9dba016e-cb74-4ed4-849e-f11b0d40308d), [1c209e4d…](https://panoramax.xyz/#focus=pic&pic=1c209e4d-a71f-4593-b48a-a722fd9fbd5e) | MatthiasMatthias | CC BY-SA 4.0 | Dune path and beach at 14th Street (sand, sea grape, lifeguard towers) |
| [Telluride's main street](https://commons.wikimedia.org/wiki/File:Telluride%27s_main_street.jpg) | Woody Hibbard | CC BY 2.0 | Colorado Avenue looking east to Ajax Peak |
| [Along Old Rt. 66, Sitgreaves Pass, Oatman, AZ 2005](https://commons.wikimedia.org/wiki/File:Along_Old_Rt._66,_Sitgreaves_Pass,_Oatman,_AZ_2005_(6595401139).jpg) | inkknife_2000 | CC BY-SA 2.0 | Oatman Road edge, shoulder, desert ground, volcanic ridges |

**Original-equipment tyre.** The 1999 Sport Package fitted 195/50VR15 tyres on 15-inch alloys ([Autoblog road test](https://www.autoblog.com/buy/1999-Mazda-MX_5+Miata-Base__2dr_Convertible/review/); [tiresize.com](https://tiresize.com/tires/Mazda/Miata/1999/Sport-Pkg/)). The Auto Channel's 1999 specification lists the base car on 185/60HR14 Yokohama Advan A-460 and the 10th Anniversary Edition, which includes the Sport package contents, on 195/50VR15 Michelin Pilot SX ([theautochannel.com](https://www.theautochannel.com/vehicles/new/reviews/1999/russ9917.html)). No source found names the Sport Package's tyre brand, so the Michelin Pilot SX is the likely fitment but **unconfirmed**. The game's sidewall reads "195/50 R15 82V" with no brand, which is right for the size.

## The car, ranked (most realism-breaking first)

1. **Panel surfaces ripple.** Long reflections break into steps and waves across the hood, front fenders, doors, the rear fender crown and above all the rear bumper and valance (`rear`, `taillight`, `wheel`, `side_left`). The jagged outline of the wheel-arch lip shows in the reflection. In every reference photo the reflections run unbroken across each panel. *Shape and normals.* This is the coarse body shell, which is still listed as remaining in the [turntable notes](../../miata/appearance/turntable/README.md).
2. **The greenhouse is too tall and upright.** In `side_left` the windscreen frame rises at about 50° from horizontal. Tysto's side photo measures about 30–35°. The hardtop roof sits about 20 % higher above the belt line than on Kevauto's hardtop car (roof-to-belt over belt-to-ground: 0.59 in the game's side view, 0.49 in the photo's rear 3/4; a rough pixel measure across two different views), so the car reads as a coupé with a box on it. The hardtop's sail is a thick body-colour triangle with a narrow rear glass, where the real car has a large wrap-around rear window and a thin sail (`rear_34_left` against Kevauto rear). The A-pillars are still round tubes.
3. **Wrong wheel design.** The game wheel has five thin, swept (twisted) spokes. The 1999 15-inch alloy (Elise240SX Leather Package, Bull-Doser Hudson) has five broad, straight, flat-faced spokes with trapezoid windows and a flat centre cap. The finish and the 4-lug pattern are right. The disc reads as a flat grey plate with no vented edge or hat, and the caliper does not read. Rotor and caliper sizes were not checked against a source this round.
4. **The nose and lamps are the wrong shape.** The headlamp is a thin slit (`headlight`). The real lamp is a tall oval of about 150 mm sitting in the fender front, with the hood shut line running over its top. The game's front body sides are almost vertical, with a visible flat panel and a light seam at each front corner (`front`), where the real car has smooth tumblehome and round fender crowns. The front intake and number-plate area sit about right, but the nose is blunter and higher. *Shape.*
5. **The cabin seen from outside is a stand-in.** Through the glass (`interior`, `front`, `rear`) the seats are grey blobs with no seams or headrest shape. The steering wheel is a white three-spoke "Y", where the driver view has the four-spoke wheel. The shifter is a stick with a ball and no boot. *Shape and material.*
6. **Paint at dusk and paint colour.** At dusk (`lamps_front`, `lamps_street`) the whole body is a black silhouette with no sky or street reflections. Real paint at dusk still reflects the sky. The low-beam pool on the road is a soft blob with no cut-off line. In daylight the green is darker and bluer than both green 1999 cars photographed (Kevauto in overcast light, DestinationFearFan in sun), so it reads almost black in shade. *Colour and lighting.*
7. **The interior from the seat is flat.** In `cockpit` the gauge faces are right for the 1999 car: black faces, white numerals and a 7000 rpm redline. The dash, binnacle and door tops are a uniform matte grey with no grain or soft-touch sheen. The vents are good. The Sport cloth seats, carpet and door cards could not be compared, because no licensed 1999 cloth-interior photograph was found (the 2002 Sport photo is leather and the 2001+ dash). *Material: roughness, grain.*
8. **Mirrors.** The heads are egg-shaped and sit on thin stalks, floating off the door (`front`, `interior`). The real NB mirror is a triangular housing on the door's front corner, with a body-colour cap that flows out of the sail (Kevauto front, DestinationFearFan). *Shape.*
9. **Wheel arches and tyres.** A thick black liner lip shows in every 3/4 view. From the front the tyres look narrow and inset. The sidewall lettering is crisp, but the tyre is one flat black with no shoulder or tread-block relief (`wheel`). *Normal detail.*
10. **Tail lamps.** The layout is right for 1999: an oval with an amber circle, and the red pill-shaped high-mount stop lamp on the boot lid (Kevauto rear). The lens is too smooth and bulges proud of the body, with none of the inner reflector optics the lamp photograph shows. *Material.*
11. **Hardtop and soft top.** Only the hardtop car exists, so a soft top (tan or black cloth, as in three of the reference photos) cannot be compared.

## The place, ranked (most realism-breaking first)

Main comparison: `place/lummus-mobile.jpg` against Panoramax 4b6d216b, at the same position, heading, field of view and sun (applied epoch 1745505292; sun azimuth 101°, elevation 49°).

1. **Wrong trees.** Lummus Park and Ocean Drive are lined with tall, leaning coconut palms. The game plants round broadleaf street oaks along the whole beachwalk and street. Nobody who knows Miami Beach would accept it. Tree positions come from the canopy data, but the species is always the oak mesh (`Grove.gd`). *Shape.*
2. **Wrong buildings.** The frame shows Ocean Drive's pastel 3–4 storey Art Deco hotels: white stucco with pink and teal trim, eyebrow ledges, neon names ("Boulevard Hotel", "Starlite Hotel"), and yellow café umbrellas at street level. The game shows generic red-brick fronts with arched windows and a nine-storey brick-and-glass office slab (`oceandrive-north`, `lummus`). *Material, colour, era.*
3. **Road and path edges are not straight, and dirt bleeds onto them.** This is the owner complaint, and it shows at all three places.
   - *Lummus:* the beachwalk is drawn with wavy, lobed edges and a dark maroon blurred fringe about 0.5 m wide, which bleeds into the grass and the sand. The real path is concrete with dead-straight edges, a dashed centre line, a coral-rock seawall on the beach side and a grass berm on the other.
   - *Ocean Drive (`oceandrive-north`):* the asphalt boundary is blobby. Lobes of asphalt spill into the plaza on the left, there is no kerb line, and the right edge is a wavy light-grey strip. There are no markings at all.
   - *Telluride:* the asphalt runs right up to the building fronts with no kerbs or sidewalks. A ground mound bulges above the road surface on the left, and a dark smudge band runs along the right. The reference has kerbs painted red and yellow, sidewalks, and parked cars on both sides.
   - *Oatman:* terrain and asphalt use the same dark-grey grain, so the right-hand road edge disappears completely. The reference has a crisp asphalt edge against a pale caliche and gravel shoulder.
4. **Ground cover has the wrong colour and texture.** The beach sand is a flat grey-white plane with no ripples, footprints or warm tint. The Oatman desert is dark grey, where the real ground is tan gravel under purple-brown volcanic crags. The Telluride slopes are faceted grey-white rock and snow, where in July they are conifer-green slopes over grey cliffs. The lime-green flat plant tufts at Oatman are much brighter than the real olive and golden brittlebush and creosote. *Colour, normal detail, shape.*
5. **No extras and no street furniture.** The frame shows lamp posts with cameras, benches, blue litter bins, volleyball nets, cyclists and walkers. Telluride's street has fire hydrants, planters, flags, bins, a shuttle bus and people. The game has none of these at either place.
6. **Traffic.** At Oatman, six cars stand in a tight queue on a remote mountain pass, where the real road carries a car every few minutes. In every capture the traffic paint is saturated primary blue or red. The real US fleet is mostly white, grey, black and silver. Telluride has no parked cars, while the real street is parked solid. On Ocean Drive a full-size pickup came straight at the camera, which stood on the centre line, so this may be a legal oncoming car.
7. **Light and sky.** The sun's direction matches the photograph. The game's sky is paler and less saturated, and the whole frame has less contrast and colour than the photograph's hard tropical light. The haze is acceptable. With the clock-driven sun (first Oatman run) the low sun blew out half the sky.
8. **Terrain shape.** The grass berm between the beachwalk and Ocean Drive is flat in the game. The cone-shaped hill at Oatman is smooth, where the real ridges are jagged and craggy.
9. **Horizon pop-in.** Without `--landscape-reference`, Telluride's first capture had a flat horizon. The 13,000 ft peaks arrived later. A player starting there sees mountains appear after the first seconds.

## What this means for the detail engine and AI assist

The plan in [docs/research/detail-engine.md](../../../docs/research/detail-engine.md) keeps generative AI out of the frame loop. This list says where help, AI or otherwise, would buy the most realism:

- **Place character** (place items 1, 2, 5): choosing palms over oaks, Art Deco stucco over brick, and furniture and pedestrians fit for a beachfront is a per-place *classification* job, not texture generation. It is the same kind of job as the existing `--traffic-llm` persona mix, so it can run once per tile, off the render thread, and be cached.
- **Traffic** (place item 6): density from road class and remoteness, a US paint-colour mix and parked cars all come from data and rules first. A model could only help pick the mix.
- **Road edges** (place item 3) need geometry and shader fixes: kerb lines, a separate shoulder material and a sharp asphalt mask. No model can hide a wavy edge.
- **The car** (car items 1–4) needs a new body shell, greenhouse and wheel model. These are modelling work, not generation.
