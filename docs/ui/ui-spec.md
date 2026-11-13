# SimCruise UI spec (2.0.2)

Every menu row is a US green guide sign, easy to read from a sofa on a TV. The GPS course is magenta (the aviation GPS convention), so it is never confused with road paint.

## Tokens

| Token | Value | Use |
|---|---|---|
| Guide | `#0A5C3A` | rows, turn sign |
| Guide deep | `#06402A` | selected tab text, "Then" part of the turn sign |
| Retro white | `#F3F6F0` | text, focus border, slider fill |
| Asphalt glass | `#161B1D` at 82 % | cards, rows over the world |
| Course | `#E23A9C` | route line, chevrons, Start route button |
| Amber | `#F4B230` | stop numbers, gear, Stock tick, small badges ("2 stops") |
| Mist | `#A9B6AF` | second text |

Pad glyph circles: A `#5DB447`, B `#D8423A`, X `#3F7FD6`, Y `#E8B923`. LB, RB, LT, RT are small white rounded rectangles.

## Type

Overpass 400/600/700/800/900 (SIL OFL 1.1), tabular numbers. Sizes at 1080p: tabs 26 bold; row title 34 extra-bold; row note 21; card title 44; body 23; HUD speed 132 black; turn distance 56.

UI text uses simple English, sentence case, no all caps.

## Pause menu (1080p)

- Blurred, darkened world behind (dark gradient from left to right).
- Top row at y 56, x 96: [LB] tabs Drive | Your car | Seat & view | Settings [RB]. The selected tab is a white pill with guide-deep text.
- Top right: place name (bold 26) and "Miami Beach, Florida, 6:42 PM, 81 F" (22, mist).
- Left column x 96, y 190, width 720: guide-sign rows 14 px apart, radius 12, padding 20 x 28, white line icon 46 px, title, note under it, value on the right (for example "Traffic: Normal").
- Focused row: 4 px white inner border, moved 18 px right, white pointer.
- Right card at x right-96, width 760: "Your route", title "Ocean Drive to Collins Park", one-line summary (distance, minutes, stops), small map with the magenta route.
- Bottom hints at y bottom-52: (A) Choose (B) Back to the drive [LB][RB] Change page.

## Your car page

- Rows of sliders, width 1060. Each row = name 330 px | track | value 200 px, unit in smaller mist text.
- Group headings "Suspension", "Steering and power".
- Amber Stock tick with the word "Stock" under it.
- Focused row is guide green with a white inner border.
- Right card 600 wide: setting name, one plain sentence of what it does, 2 lines more, and the real numbers (Front 31 N/mm, Rear 23 N/mm).
- Hints: (Left/Right) Change (Y) Back to stock (X) Try it now (B) Done.

## Seat & view page

- The world is NOT blurred (it keeps running). Dark gradient only on the left 45 %.
- Lens row = segmented control Natural | True FOV | Wide.
- Sliders: Forward/back cm, Height cm, Head motion %, Screen size in.
- Small card at the bottom left that tells the difference between Natural and True FOV.

## Route planner

- Full-screen map, cross at the centre, moved with the left stick.
- Left card 640 wide: title "Plan a route", one help line, list (You are here / 1 Lummus Park 0.9 mi 4 min / 2 Collins Park 2.4 mi 9 min, end of route), magenta "Start the route" row with (Y).
- Hints: (A) Add a stop here (X) Remove stop (Y) Start the route [LT][RT] Zoom (B) Back.

## HUD

- Bottom left: speed 132 px with amber gear and "mph", 360 px rev bar with a red zone at the end.
- Top centre, only with a route: green sign, arrow pictogram cell | "0.3 mi" + "Right onto 5th Street" | darker cell "Then <arrow> Collins Ave".
- Road: magenta chevrons 5 to 60 m ahead in the driven lane, fading, and a magenta arrow board with a white border at the turn corner.
- Bottom right: round 330 px map that turns with the car, magenta route, amber stops; under it "2.1 mi" and "to Lummus Park, 6:51 PM".
- Arrival: "You are at <stop>" for 4 s.
- Distances in mi, feet under 0.1 mi.
