# Godot vehicle controller comparison

> Former name: this study was written when SimCruise was called "Sunday Driver"; the app, paths and commands it mentions under that name are now SimCruise.

These measurements used Godot 4.7.2.stable.official.ed1daf0bf on Apple Silicon, headless, with two isolated upstream projects. The bench uses the supplied vehicle scenes and their default tuning. It adds a level collider in the Road group, lets the car settle for two seconds, applies full throttle, then applies full brake at 30 km/h until ground speed falls below 0.15 m/s. A separate trial holds 25% left steering and full throttle for one second after crossing 30 km/h and then brakes. Fixed seed 4367. Shift uses a manual first-gear launch with a one-second clutch release. GEVP uses its automatic launch.

| Upstream project and tested commit | Preset | Project physics rate | 0–30 km/h | 30 km/h brake distance | One-second steering yaw |
| --- | --- | --- | ---: | ---: | ---: |
| DAShoe1/Godot-Easy-Vehicle-Physics, c392257f54f6ca537dc10bc5badad0c060f18982 | Simcade, 1,746 kg | 120 Hz | 1.625 s | 7.852 m | 38.97° |
| kakoiihito/Shift, 77b7fbd2681dfa3899b900a1d31679e5809c316f | Roadster, 949 kg | 300 Hz | 2.047 s | 8.339 m | 34.93° |

The two repeats of each maneuver produced identical telemetry and recorded four wheel contacts throughout. The steering runs ended at different speeds (46.06 and 36.54 km/h). The measurements establish compatibility and reproducibility for these two presets. An independent physical test with matched tires, loading, steering angle, throttle, speed and surface would be needed to infer which setup represents a particular Miata. GEVP exited cleanly. Shift reported 18 leaked ObjectDB instances and eight resources still in use at exit, even after the test removed the vehicle and waited two frames.

GEVP's README recommends at least 120 Hz and documents keyboard assistance. Its combined-slip brush implementation deliberately retains grip beyond peak and augments braking. The stock Simcade scene enables countersteer and yaw assistance and uses a 569 Nm engine. Upstream publishes an MIT license. The GitHub repository listed no release tags. [Controller README](https://github.com/DAShoe1/Godot-Easy-Vehicle-Physics/blob/c392257f54f6ca537dc10bc5badad0c060f18982/README.md), [wheel force equations](https://github.com/DAShoe1/Godot-Easy-Vehicle-Physics/blob/c392257f54f6ca537dc10bc5badad0c060f18982/addons/gevp/scripts/wheel.gd#L259-L313), [license](https://github.com/DAShoe1/Godot-Easy-Vehicle-Physics/blob/c392257f54f6ca537dc10bc5badad0c060f18982/LICENSE).

Shift's tested October 27, 2026 commit postdates the April 18 release tag v0.3.0. Its single-point wheels use both slip directions with a shared brush force limit. Force saturation uses a constant friction coefficient times normal load, and the tire equations omit camber thrust. Its Roadster preset disables the front and rear anti-roll bars. The project config sets 300 Hz despite the README's 333 Hz description. Upstream publishes an MIT license. [Latest tagged release](https://github.com/kakoiihito/Shift/releases/tag/v0.3.0), [tested tire model](https://github.com/kakoiihito/Shift/blob/77b7fbd2681dfa3899b900a1d31679e5809c316f/Car/Scripts/TireModels/brushtire.gd#L115-L206), [Roadster preset](https://github.com/kakoiihito/Shift/blob/77b7fbd2681dfa3899b900a1d31679e5809c316f/Car/Scenes/Roadster.tres), [project settings](https://github.com/kakoiihito/Shift/blob/77b7fbd2681dfa3899b900a1d31679e5809c316f/project.godot), [license](https://github.com/kakoiihito/Shift/blob/77b7fbd2681dfa3899b900a1d31679e5809c316f/LICENSE).

Sunday Driver already models a 1,043 kg 1999 Sport, axle-specific wheel rates and roll bars, rear Hard S damper-dyno samples, clutch, five-speed gearing, wheel rotational inertia and transient lateral slip. The front damper, differential torque bias, tire grip and relaxation length retain estimated inputs. Keep the car-specific model and use a matched fixed-speed steer-and-release trial or measured tire data to decide on future changes. Godot documents its built-in VehicleBody3D as unsuitable for advanced realism and points to custom rigid-body integration. A third MIT project, dicnunz/vehicle-physics v0.21.0, supplies scripted telemetry scenarios and explicitly documents that its sedan is unvalidated against physical handling data. Its sources were reviewed separately. [Godot VehicleBody3D documentation](https://docs.godotengine.org/en/stable/classes/class_vehiclebody3d.html), [sedan project and scenarios](https://github.com/dicnunz/vehicle-physics/blob/v0.21.0/README.md).

## Reproduce

Check out one of the two tested commits in an isolated directory. Copy the adjacent vehicle_plugin_bench.gd into the upstream project root as bench.gd. The GEVP archive can omit its top-level project.godot, so use a Git checkout or restore that file from the exact revision. Use HZ=120 for GEVP or HZ=300 for Shift and run each maneuver twice.

```sh
godot --headless --editor --path . --import
godot --headless --path . --fixed-fps "$HZ" --script res://bench.gd
godot --headless --path . --fixed-fps "$HZ" --script res://bench.gd -- --turn
```
