# README images

| File | Shot |
|---|---|
| `dragon-cockpit.jpg` | Driver view on the Tail of the Dragon, US 129 at Deals Gap, NC. |
| `cheyenne-capitol.jpg` | Chase view on Capitol Avenue, Cheyenne, WY, toward the State Capitol. |
| `monument-valley.jpg` | Chase view on US 163 at Forrest Gump Point, UT, toward Monument Valley. |
| `i180-viaduct.jpg` | Chase view on the I-180 viaduct over the Cheyenne rail yard (a streamed bridge deck). |
| `road-atlas.jpg` | The road atlas (Tab) around Deals Gap. |
| `simcruise-wordmark.png`, `simcruise-icon-1024.png` | Logo, made by `authoring/logo/make_logo.py`. |
| `unreal-nb1999-front-34-left.jpg`, `unreal-mustang65-front-34-left.jpg`, `unreal-meridian-z-rear-34-left.jpg`, `unreal-nb1999-wheel.jpg`, `unreal-mustang65-steer-driver.jpg` | The Unreal client's car turntable (`SimCruiseTurntableGameMode`, scalability 4, Lumen), 11 November 2026, scaled to 1200 px; see [unreal-building.md](../unreal-building.md). Car models CC BY 4.0 by their Sketchfab authors (`Credits.txt`). |

## Regenerating the screenshots

`game/checks/Screenshots.gd` visits each place, waits for it to stream, hides the HUD and saves a JPEG. The sun is pinned so captures are repeatable:

```sh
godot --path game --resolution 1280x720 -- --screenshots /abs/output/dir --solar-epoch 1792096200
godot --path game --resolution 1280x720 -- --screenshots /abs/output/dir --only=capitol,viaduct --solar-epoch 1792096200
```

Run it windowed, not `--headless`. On Linux without a GPU it works with Mesa's software Vulkan driver at about one frame per second: install `mesa-vulkan-drivers xvfb`, then prefix the command with `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json xvfb-run -a -s "-screen 0 1600x900x24"` and add `--audio-driver Dummy`. `SCREENSHOT_SETTLE=<seconds>` sets how long each place streams before the capture (default 45); `--without-landscape` skips the horizon terrain, which software rendering draws slowly.

The images in this folder were captured that way, with the sun pinned to 15 October 2026, 20:30 UTC. Captures are MIT like the rest of the repository; the scenery carries the data licences listed in `licenses/`, and the map is © OpenStreetMap contributors (ODbL), so keep that credit if you reuse a screenshot elsewhere.
