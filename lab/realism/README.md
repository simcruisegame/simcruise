# Realism score

Roadmap item 7: render the exact spot of a licensed street photograph and measure how close the game comes, so a visual change gets a number like a physics change does. It is the measured successor of the [taste test](../taste/2026-11-10/README.md), which ranked the same differences by eye.

## Run it

With the editor closed (Windows, from the checkout):

    powershell -ExecutionPolicy Bypass -File scripts\realism.ps1

It builds the editor target (skip with `-NoBuild`), fetches each pose's photograph into `Saved\Realism\photos` (with Bun and `lab/taste/scripts/panoramax-near.mjs`; the photographs are never committed), opens the game once per pose under the PC-wide editor lock with `-SimCruiseDriveCapture -SimCruiseRealismPose=...` at the photograph's time and size, with moving traffic off, and scores the captures. It prints one line per pose and appends them to `Saved\Realism\history.csv`, labelled with the commit:

    REALISM lummus: 44.6 / 100  layout 31.6  tone 34.9  colour 50.1  structure 51.5  detail 62.8  sky 41.9

That line is the measured baseline for commit d1ae119 (12 November 2026, Ultra, 1600 x 720, the bundled Miami Beach world, moving traffic off).

Score two pictures by hand: `python lab/realism/score.py GAME.png PHOTO.jpg` (add `--json` for one JSON line). The checks: `python lab/realism/test_score.py`. The scorer needs Python 3 with NumPy and Pillow.

## What the number means

Both pictures are cut to the pose's aspect and scaled to 640 px wide. Six parts, each 0 to 100 (100 = the photograph itself), are combined into a weighted total:

| Part | Weight | Measures |
|---|---|---|
| layout | 25 | where the colours are: mean CIELAB Delta E of a 16 x 8 grid of cell means |
| structure | 25 | where the edges run and which way: gradient-orientation histograms per cell |
| tone | 15 | the brightness distribution: earth mover's distance of the L* histograms |
| detail | 15 | fine texture: band-pass energy at 1, 3 and 9 px |
| colour | 10 | colourfulness (Hasler and Suesstrunk) |
| sky | 10 | mean colour of the top fifth of the frame |

The score is a tracker, not an absolute grade. People, parked cars, clouds and the season in a real photograph never match, so compare the same pose across builds. A change of 2 or more points on a pose is worth looking at; the parts say what moved. Look at the pictures too: a number can rise for the wrong reason.

## Poses

[`poses.json`](poses.json) lists them. To add one, find a licensed frame with its position, compass heading and time (`bun lab/taste/scripts/panoramax-near.mjs search LAT LON 0.002`), take the field of view from the camera model, and add an entry; poses outside the bundled Miami Beach tiles set `"world": "helper"` and need the atlas helper (`tools\atlas.exe`). Record the photograph's author and licence; commit only the link.

| Pose | Photograph | Licence |
|---|---|---|
| lummus | [Panoramax 4b6d216b](https://panoramax.xyz/#focus=pic&pic=4b6d216b-ae1c-48a8-b2ae-5d9558334f18), MatthiasMatthias | CC BY-SA 4.0 |
