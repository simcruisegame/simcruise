# Pre-flight 20261113-083923

Commit `30fe59f`, 12 runs of 60 s, time 2026-11-12T17:00:00Z. Result: **FAIL** (8 passed, 4 failed).

| Road | Car | Result | View | Distance (m) | Mean speed (m/s) | Off road (s) | White sky (%) | Tile pop-in | Car pop-in | Flashes | Road steps (max cm) | Gaps | Jolts (at borders) | Stuck traffic | Video | Sheet | Reasons |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ocean-drive | Miata | PASS | PASS | 502 | 9.35 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (0.1) | 0 | 0 (0) | 0 | [video](ocean-drive-nb2001_aaron.mp4) | [sheet](ocean-drive-nb2001_aaron-sheet.png) |  |
| ocean-drive | Mustang | PASS | PASS | 533 | 10.69 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (0.0) | 0 | 1 (0) | 0 | [video](ocean-drive-mustang65.mp4) | [sheet](ocean-drive-mustang65-sheet.png) |  |
| ocean-drive | Corvette | PASS | PASS | 549 | 9.93 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (0.1) | 0 | 1 (0) | 0 | [video](ocean-drive-meridian_z.mp4) | [sheet](ocean-drive-meridian_z-sheet.png) |  |
| dragon | Miata | PASS | PASS | 531 | 10.93 | 0.26 | 0.0 | 0 | 0 | 2 | 0 (1.0) | 0 | 0 (0) | 0 | [video](dragon-nb2001_aaron.mp4) | [sheet](dragon-nb2001_aaron-sheet.png) |  |
| dragon | Mustang | FAIL | PASS | 543 | 11.16 | 0.03 | 0.0 | 0 | 0 | 0 | 0 (1.5) | 1 | 4 (0) | 0 | [video](dragon-mustang65.mp4) | [sheet](dragon-mustang65-sheet.png) | road-gaps-1 |
| dragon | Corvette | PASS | PASS | 593 | 12.27 | 0.23 | 0.0 | 0 | 0 | 0 | 0 (0.8) | 0 | 2 (0) | 0 | [video](dragon-meridian_z.mp4) | [sheet](dragon-meridian_z-sheet.png) |  |
| nebraska | Miata | PASS | PASS | 488 | 9.98 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (0.0) | 0 | 0 (0) | 0 | [video](nebraska-nb2001_aaron.mp4) | [sheet](nebraska-nb2001_aaron-sheet.png) |  |
| nebraska | Mustang | PASS | PASS | 513 | 10.21 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (0.1) | 0 | 0 (0) | 0 | [video](nebraska-mustang65.mp4) | [sheet](nebraska-mustang65-sheet.png) |  |
| nebraska | Corvette | PASS | PASS | 533 | 10.80 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (0.0) | 0 | 0 (0) | 0 | [video](nebraska-meridian_z.mp4) | [sheet](nebraska-meridian_z-sheet.png) |  |
| big-sur | Miata | FAIL | PASS | 738 | 15.04 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (2.9) | 0 | 3 (1) | 0 | [video](big-sur-nb2001_aaron.mp4) | [sheet](big-sur-nb2001_aaron-sheet.png) | jolts-at-tile-borders-1 |
| big-sur | Mustang | FAIL | PASS | 740 | 14.81 | 0.06 | 0.0 | 0 | 0 | 0 | 0 (3.2) | 0 | 9 (1) | 0 | [video](big-sur-mustang65.mp4) | [sheet](big-sur-mustang65-sheet.png) | jolts-at-tile-borders-1 |
| big-sur | Corvette | FAIL | PASS | 777 | 16.07 | 0.07 | 0.0 | 0 | 0 | 0 | 1 (6.8) | 0 | 4 (0) | 0 | [video](big-sur-meridian_z.mp4) | [sheet](big-sur-meridian_z-sheet.png) | road-steps-1-max-7cm |

Look at every contact sheet and watch the videos of failed runs before a play-test build goes out. Real game bugs go in docs/release/bugs.md with the run, the frame and a screenshot.
