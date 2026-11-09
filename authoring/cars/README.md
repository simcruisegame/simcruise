# Car models

`build_car.py` builds each car's exterior and cabin in Blender from a design file in `designs/` and writes `game/art/cars/<id>.glb`:

```sh
blender -b -P authoring/cars/build_car.py -- authoring/cars/designs/kestrel_s.py game/art/cars/kestrel_s.glb
# preview (Eevee; --fast uses Workbench, --debug-mats colours trim/glass/satin):
blender -b -P authoring/cars/build_car.py -- authoring/cars/designs/kestrel_s.py - --render /tmp/k.png --view three-quarter
# match a reference photograph (camera JSON: location, target, focal_mm, resolution):
blender -b -P authoring/cars/build_car.py -- authoring/cars/designs/volta.py - --camera cam.json --render /tmp/m.png --fast
```

Tested with Blender 5.2 LTS. A build takes about 20 seconds; an Eevee preview on a software renderer takes a few minutes. After rebuilding, run `godot --headless --path game --import`, then check in game with `--car-showcase OUTDIR --car=<id>` (chase, driver and scenic frames while an autopilot drives).

| Car | Reference | Design |
|---|---|---|
| Kestrel S | 2022 Honda Civic Si sedan | `designs/kestrel_s.py` |
| Tora RS | 2022 Subaru WRX | `designs/tora_rs.py` |
| Volta | Tesla Model 3 RWD (2024) | `designs/volta.py` |
| Meridian Z | 2020 Chevrolet Corvette Stingray Z51 | `designs/meridian_z.py` |
| Bison XL | 2021 Ford F-150 SuperCrew 5.5 ft | `designs/bison_xl.py` |

**Body (`surface.py`).** The skin is interpolated (centripetal Catmull-Rom, both directions) through a control net. Fourteen *levels* run the length of the car (keel, floor, rocker, sill, lower, waist, shoulder, belt, glass_lo, glass_hi, rail, roof_edge, roof_mid, crown); *rows* tie them together across the body and may lean, so a row can run up the A-pillar and across the windscreen. The first and last rows are the tail and nose tips on the centre plane, so the skin closes without caps. Points or levels flagged as creases break tangent continuity (hood edge, shoulder line). Design lines were measured from side photographs with known wheelbase and wheel size, then checked by rendering from a matched camera and overlaying the silhouette on the photo.

**Regions.** Paint, glass, black trim and underbody are rectangles in net coordinates (`design_tools.sedan_regions`), so window and pillar edges are exact grid lines; glass is recessed with a dark wall. Wheel arches are cut afterwards, keeping the skin's own shading around them.

**Features (`features.py`, `skinmap.py`, `paint.py`).** Lamps, grilles, intakes, cladding, plates and shut lines are patches laid on the skin: outlines given in a side/front/rear view, as rough 3D points snapped to the skin, or directly in net coordinates; they follow the skin around corners. Lamp internals and grille meshes are textures painted with NumPy; brake-lamp light guides carry an emission mask.

**Parts and cabin (`parts.py`, `cabin.py`, `wheels.py`).** Mirrors, handles, wings, exhaust tips, hood scoop, antenna, wipers, the pickup bed and bumpers; a cabin packaged around the driver's eye (dash with cluster and screen textures, console, seats, rear-view mirror, steering wheel); tyres with tread grooves, spoke styles, brake discs and calipers.

**Game contract.** Origin on the ground midway between the axles, Blender +Y forward (Godot −Z). Nodes `wheel_fl/fr/rl/rr` (origin at the hub, spins), `wheel_*_upright` (disc and caliper, steers), `steering_wheel` (origin at the hub, local Godot Z along the column; `Drive.gd` turns it at a 14.5:1 ratio). Material `lamp_rear` becomes the brake lamps and `glass` is drawn tinted and transparent by `game/CarBody.gd`. Godot imports the models with embedded textures (`gltf/embedded_image_handling=3`).

**Known limits.** Interior materials must stay plain: giving them an emission term made one shader variant stall compilation on the software renderer. The pickup's dash sits high in the driver view.

**Licence.** The scripts and every model they generate are original MIT work for SimCruise. Photographs were used only as measurement references (sources named in each design file); no photograph, logo or badge is copied into a model, and the cars keep fictional names.
