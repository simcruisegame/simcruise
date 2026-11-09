# How SimCruise works: the diagram

Three files, one model:

| File | What it is |
|---|---|
| `index.html` | The interactive page. Click or tab to any stage or data layer for what it does, the files that implement it, its source, licence and attribution, and its limits; press **Play** to watch tiles stream around the car while the cache fills and trims. Self-contained: inline CSS, SVG and JavaScript, no external requests, no fonts loaded, no tracking. Follows the system light/dark preference (with a manual switch), works at phone width, and is keyboard accessible (Tab, Enter, Esc). |
| `diagram.svg` | The static picture embedded near the top of the main [README](../../README.md). Every stage and layer carries a `<title>` tooltip; it follows the same light/dark media query. |
| `build.py` | Generates both files from one model, so they always show the same stages and labels. Standard library only. |

## Open it locally

Double-click `docs/how-it-works/index.html`, or from the repository root:

```sh
open docs/how-it-works/index.html                            # macOS
python3 -m http.server --directory docs/how-it-works 8000    # then open http://localhost:8000/
```

It works from `file://` as well as over HTTP; nothing is fetched from the network. On a phone the diagram keeps its layout and scrolls sideways inside its frame; the list of stages and layers under it opens the same panels.

## GitHub Pages

Once Pages is enabled for the repository (Settings → Pages → Deploy from a branch → `main`, folder `/docs`), GitHub serves the page at

```
https://simcruisegame.github.io/simcruise/how-it-works/
```

with `diagram.svg` beside it. No build step, Jekyll plugin or external asset is involved. Only `/docs` is published, so on a `*.github.io` host the page's *Back to the README* link points at the repository's README on github.com instead of `../../README.md`.

Until Pages is enabled, the README's links open the files in GitHub's file view, which shows `index.html` as source code rather than running it. Clicking stages and pressing Play needs the page in a browser: from a clone (above) or from Pages.

## Tooltips in the README

GitHub shows README images through its image proxy as plain pictures, so hovering the README shows no `<title>` tooltips. GitHub's file view also shows `diagram.svg` as a plain image, and raw.githubusercontent.com serves it as text, so the tooltips appear only when the SVG is opened on its own in a browser: from a clone, or from Pages once enabled. The interactive page is the intended click-through; the README picture links to it.

## Regenerating

Edit the model in `build.py` (stages, layers, edges, their text, files and licences) and run:

```sh
python3 docs/how-it-works/build.py          # rewrites index.html and diagram.svg
python3 docs/how-it-works/build.py --check  # exits 1 if the committed files are stale
```

The output is deterministic, so a clean `--check` makes a reasonable CI step.

## What it says, and how that was verified

Every statement in the panels was read from the code it names on 8 November 2026 and re-checked after review against branch `preview/round-1` at commit `6f05ae2`: `helper/atlas.mjs`, `helper/atlas-cache.mjs`, `helper/cache-eviction.mjs` and the per-layer helper modules (`helper/road-grade.mjs` for the 65 × 65 height grid, `helper/regional-terrain.mjs` for the horizon mesh, `helper/signal-controls.mjs` for signals and so on); the bundled tiles under `game/data/` (50 with the 65 × 65 grid, 25 legacy tiles with 33 × 33); `game/Drive.gd`, `game/WorldTile.gd`, `game/Powertrain.gd`, `game/NB1999Sport.gd`, `game/InlineFourSound.gd`, `game/RoadNoise.gd`, `game/SurveyDaylight.gd`, `game/Cockpit.gd`, `game/Hud.gd` and `game/project.godot`; `scripts/build-macos.sh`; and the licence texts under `licenses/`. Byte budgets, request spacings, cache sizes and tile counts are the constants in those files; the calibration numbers are the ones `CONTRIBUTING.md` guards. When the code changes, change the model and rebuild: the diagram cites nothing it did not read.

The Play animation is a cartoon of `Drive.stream()`, `Drive.rebase()` and `helper/cache-eviction.mjs` (the 3 × 3 ring, loading ahead, freeing behind, the 96 MiB least-recently-used trim), not a measurement: real tiles vary in size and the cache holds many kinds of file, not a fixed 6.4 MiB per tile.
