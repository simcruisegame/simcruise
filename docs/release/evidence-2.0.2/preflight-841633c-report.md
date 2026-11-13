# Pre-flight 20261113-001655

Commit `841633c`, 12 runs of 60 s, time 2026-11-12T17:00:00Z. Result: **FAIL** (9 passed, 3 failed).

| Road | Car | Result | View | Distance (m) | Mean speed (m/s) | Off road (s) | White sky (%) | Tile pop-in | Car pop-in | Flashes | Road steps (max cm) | Gaps | Jolts (at borders) | Stuck traffic | Video | Sheet | Reasons |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ocean-drive | Miata | PASS | PASS | 523 | 9.77 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (0.1) | 0 | 1 (0) | 0 | [video](ocean-drive-nb2001_aaron.mp4) | [sheet](ocean-drive-nb2001_aaron-sheet.png) |  |
| ocean-drive | Mustang | PASS | PASS | 504 | 10.03 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (0.1) | 0 | 0 (0) | 0 | [video](ocean-drive-mustang65.mp4) | [sheet](ocean-drive-mustang65-sheet.png) |  |
| ocean-drive | Corvette | PASS | PASS | 562 | 10.01 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (0.2) | 0 | 1 (0) | 0 | [video](ocean-drive-meridian_z.mp4) | [sheet](ocean-drive-meridian_z-sheet.png) |  |
| dragon | Miata | FAIL | PASS | 532 | 10.84 | 0.20 | 0.0 | 0 | 0 | 4 | 0 (0.9) | 0 | 0 (0) | 0 | [video](dragon-nb2001_aaron.mp4) | [sheet](dragon-nb2001_aaron-sheet.png) | flash-frames-4 |
| dragon | Mustang | FAIL | PASS | 538 | 11.08 | 0.02 | 0.0 | 0 | 0 | 0 | 0 (1.1) | 1 | 4 (0) | 0 | [video](dragon-mustang65.mp4) | [sheet](dragon-mustang65-sheet.png) | road-gaps-1 |
| dragon | Corvette | PASS | PASS | 608 | 12.58 | 0.29 | 0.0 | 0 | 0 | 0 | 0 (1.1) | 0 | 1 (0) | 0 | [video](dragon-meridian_z.mp4) | [sheet](dragon-meridian_z-sheet.png) |  |
| nebraska | Miata | PASS | PASS | 513 | 10.58 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (0.0) | 0 | 0 (0) | 0 | [video](nebraska-nb2001_aaron.mp4) | [sheet](nebraska-nb2001_aaron-sheet.png) |  |
| nebraska | Mustang | PASS | PASS | 521 | 10.60 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (0.1) | 0 | 0 (0) | 0 | [video](nebraska-mustang65.mp4) | [sheet](nebraska-mustang65-sheet.png) |  |
| nebraska | Corvette | PASS | PASS | 536 | 10.97 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (0.0) | 0 | 0 (0) | 0 | [video](nebraska-meridian_z.mp4) | [sheet](nebraska-meridian_z-sheet.png) |  |
| big-sur | Miata | PASS | PASS | 760 | 15.49 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (3.2) | 0 | 5 (0) | 0 | [video](big-sur-nb2001_aaron.mp4) | [sheet](big-sur-nb2001_aaron-sheet.png) |  |
| big-sur | Mustang | FAIL | PASS | 761 | 15.40 | 0.09 | 0.0 | 0 | 0 | 0 | 0 (2.4) | 1 | 9 (1) | 0 | [video](big-sur-mustang65.mp4) | [sheet](big-sur-mustang65-sheet.png) | road-gaps-1,jolts-at-tile-borders-1 |
| big-sur | Corvette | PASS | PASS | 780 | 15.98 | 0.00 | 0.0 | 0 | 0 | 0 | 0 (4.8) | 0 | 5 (0) | 0 | [video](big-sur-meridian_z.mp4) | [sheet](big-sur-meridian_z-sheet.png) |  |

Look at every contact sheet and watch the videos of failed runs before a play-test build goes out. Real game bugs go in docs/release/bugs.md with the run, the frame and a screenshot.
