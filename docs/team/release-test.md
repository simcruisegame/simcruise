# The release test

One full game test of a release candidate, for all the teams at once. The tools thread owns it.

## When

- After each change: `scripts\check.ps1 -Tier fast` only (no game, no GPU). See [dev-loop.md](dev-loop.md).
- At the cut-off, all lanes merge into one candidate (a commit or tag such as `rc-v2.0.3`, or `docs/team/cycles/rc.txt`). Then `scripts\release-test.ps1` tests that commit once. Do not start it on a commit that is not the candidate.
- One GPU test at a time on the PC: the script takes the `Global\SimCruiseGpuTest` lock for each launch (`scripts\EditorLock.ps1 -GpuTest`). Other GPU tests wait. The lock holder is in `%LOCALAPPDATA%\SimCruise\gpu-test-lock.txt`.

## Run it

```powershell
powershell -ExecutionPolicy Bypass -File scripts\release-test.ps1
powershell -ExecutionPolicy Bypass -File scripts\release-test.ps1 -MaxPlaces 2   # a dry run of the script
```

| Argument | Effect |
|---|---|
| `-Seed n` | The draw of random places and the car turn (default: the date, `yyyyMMdd`). The same seed gives the same places and cars. |
| `-RandomCount n` | Random US places from `data/test-places.json` (default 4). |
| `-Seconds n` | Drive time per place (default 60). |
| `-MaxPlaces n` | Stop after n places. Also skips the controller test. |
| `-ShowWindow` | Show the game windows (default: off screen, `-RenderOffScreen`). |

At the start, the script prints the places, the cars and a time estimate (about 5 min per place; 8 places in about 42 min). The limit is 90 min.

## What one launch collects

Each place is one game (`scripts\launch-review.ps1`) at the Ultra preset and the screen's own resolution (1920x1080 on this PC). In the same launch:

- the view check (what the world shows at the start),
- the cockpit steer test at rest (wheel axis, hub, rim, needles with the wheel hidden, lock to lock),
- the robot's drive for `-Seconds` with the pre-flight judge (spin, run-off, slow frames p50/p95, hitches, road steps and gaps, pop-in, stuck traffic, white sky),
- frames from the driver's seat every 5 s and at each event, with close-ups of the wheel, the cluster, the mirrors and the windscreen,
- a 60 s video at 10 fps, the contact sheet, the log, crash data and GPU data,
- the real graphics settings from the engine (the `REVIEW SETTINGS` log line).

Places: the four hero roads (Ocean Drive, the Dragon, Nebraska, Big Sur) and the random US places. The three cars (the 1999 roadster, the Mustang, the Corvette) turn round the hero roads, so each car drives each hero road in three runs. Each random place gets one car. Each random place drives at its own local solar noon.

Then the controller menu test (`scripts\pad-review.ps1`, automation test `SimCruise.UI.PadNavigation`), once. When the script is not there, the UI report says NOT RUN.

## Test cases with their own launch

The world lane makes the game side. The release test starts each case, applies the gates, and writes the results to the team reports. When the game flag is not in the build, the case is NOT RUN with the reason; the rest of the run continues.

| Case | Launch | The game writes | Gates | Teams |
|---|---|---|---|---|
| Standstill | Ocean Drive, `-SimCruiseStandstillTest` (`launch-review.ps1 -Case standstill`) | `standstill\t000.png`, `t005.png`, `t015.png`, `t060.png` (0, 5, 15 and 60 s after the stop), `wall.png` (1 m from a wall), `ground.png` (1 m from the ground) | Edge density and fine-detail energy (`scripts\detail-metrics.py`) rise at each step. The 60 s frame beside the place's reference photo (`side-by-side.png`) when one is in `data\reference\<road>\` (legal source only). | world, picture |
| Speed | Nebraska (straight), then the Dragon, `-SimCruiseSpeedTest` (`-Case speed`) | `speed\speed.json`: `{"runs": [{"road", "kmh", "ai", "lead_m", "lead_s", "holes", "popin_spikes", "frame_median_ms", "frame_p95_ms", "hitches_2x"}]}`, one run per speed (50, 130, 240 km/h), AI workers on and off in the same launch | No holes. No pop-in spikes on the road ahead. On the Dragon, frame p95 with AI on is not more than 0.5 ms above AI off. No extra frames over 2x the median with AI on. Frame p95 not above 50 ms. | world, ai, picture |

## Graphics rule

A launch is valid for sign-off only when the engine really had the top preset (Ultra), all `sg.*` levels at the top, the screen's own resolution, and a screen percentage not below the preset's (75 for Ultra). Else the run is **NOT VALID FOR SIGN-OFF**: the picture report has a P1 finding, the summary says so, and `scripts\review-check.ps1` and `scripts\playtest.ps1` refuse it.

## Output

`Saved\ReleaseTest\<stamp>-<commit>\`

| File | Contents |
|---|---|
| `summary.md` | The verdict, a row per place (car, verdict, drive judge, cockpit, view check, frame p95, minutes, link to review.md), and the verdict of each team. |
| `teams\driving.md` | Physics and the robot judge: spin, run-off, the robot did not move; rear slip and steering numbers. |
| `teams\cockpit.md` | Steering wheel and gauges: the cockpit test and the gauge samples from the drive. |
| `teams\picture.md` | Frame times, hitches, white sky, the real graphics settings. |
| `teams\world.md` | Road steps and gaps, pop-in, stuck traffic, the start place, the load time, the view check. |
| `teams\ai.md` | The detail engine's AI workers: frame cost with AI on against off, detail lead at speed. |
| `teams\ui.md` | The controller menu test. |
| `teams\release.md` | Crashes, hangs, engine errors, a drive that did not happen. |
| `<place>\` | One launch: `review\` frames and JSON, `cockpit\`, `video.mp4`, `sheet.png`, `game.log`, `crash\`, `review.md`. |

Each team report has the same four sections:

1. **Verdict**: PASS, FAIL or NOT RUN.
2. **Findings**: a table; each row has the severity, place, car, time, evidence path and what is wrong.
3. **Frames to look at**: the frames the team must open.
4. **Numbers vs limits**: a table of measure, value, limit and result.

Severity:

| Level | Meaning |
|---|---|
| P0 | Stops the release: a crash, a hang, a drive that did not happen, a start at the wrong place. |
| P1 | A player sees it at once: a spin, a run-off, slow frames, a wrong wheel or gauge, NOT VALID FOR SIGN-OFF. |
| P2 | A player can see it: road steps, pop-in, stuck traffic, gauges off the true value. |

## The frame review

The script's checks are not the review. A person or the thread must open the frames and the video of each place and fill its `review.md` (each item PASS or FAIL with a note). Then:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\review-check.ps1 "Saved\ReleaseTest\<stamp>-<commit>"
```

It fails when a `review.md` has an empty item or a FAIL.
