# README images

| File | Shot |
|---|---|
| `simcruise-wordmark.png`, `simcruise-icon-1024.png` | Logo, made by `authoring/logo/make_logo.py`. |
| `unreal-nb1999-front-34-left.jpg`, `unreal-mustang65-front-34-left.jpg`, `unreal-meridian-z-rear-34-left.jpg`, `unreal-nb1999-wheel.jpg`, `unreal-mustang65-steer-driver.jpg` | The Unreal client's car turntable (`SimCruiseTurntableGameMode`, scalability 4, Lumen), 11 November 2026, scaled to 1200 px; see [unreal-building.md](../unreal-building.md). Car models CC BY 4.0 by their Sketchfab authors (`Credits.txt`). |

## Regenerating the screenshots

The in-game screenshots come from the Unreal client: `-SimCruiseDriveCapture` and `-SimCruiseUiShots`, at the highest settings (see [Drive captures](../unreal-building.md#drive-captures)). The car shots come from the turntable (`SimCruiseTurntableGameMode`). The Godot 1.x screenshots and their capture script are kept at the git tag `godot-final`.

Captures are MIT like the rest of the repository; the scenery carries the data licences listed in `licenses/`, and the map is © OpenStreetMap contributors (ODbL), so keep that credit if you reuse a screenshot elsewhere.
