# Car models

`build_car.py` builds a car's exterior and cabin in Blender from a design file in `designs/` and writes `data/art/cars/<id>.glb`:

```sh
blender -b -P authoring/cars/build_car.py -- authoring/cars/designs/<id>.py data/art/cars/<id>.glb
# preview (Eevee; --fast uses Workbench, --debug-mats colours trim/glass/satin):
blender -b -P authoring/cars/build_car.py -- authoring/cars/designs/<id>.py - --render /tmp/k.png --view three-quarter
# match a reference photograph (camera JSON: location, target, focal_mm, resolution):
blender -b -P authoring/cars/build_car.py -- authoring/cars/designs/<id>.py - --camera cam.json --render /tmp/m.png --fast
```

Tested with Blender 5.2 LTS. A build takes about 20 seconds; an Eevee preview on a software renderer takes a few minutes. After rebuilding, run `godot --headless --path game --import`, then check in game with `--car-showcase OUTDIR --car=<id>` (chase, driver and scenic frames while an autopilot drives).

**No car in the game uses a generated model today.** The five generated bodies (Kestrel S, Tora RS, Volta, Meridian Z and Bison XL) were removed in November 2026: their proportions were accurate, but they did not look like the real cars. Their design files and reference photographs went with them and remain in the git history. The tooling here is kept for future cars; a generated model should ship only if it holds up beside photographs of the real car. The Corvette and the 1965 Mustang wear downloaded Sketchfab models instead (`sketchfab.mjs`; see [docs/cars.md](../../docs/cars.md)).

**Body (`surface.py`).** The skin is interpolated (centripetal Catmull-Rom, both directions) through a control net. Fourteen *levels* run the length of the car (keel, floor, rocker, sill, lower, waist, shoulder, belt, glass_lo, glass_hi, rail, roof_edge, roof_mid, crown); *rows* tie them together across the body and may lean, so a row can run up the A-pillar and across the windscreen. The first and last rows are the tail and nose tips on the centre plane, so the skin closes without caps. Points or levels flagged as creases break tangent continuity (hood edge, shoulder line). Design lines were measured from side photographs with known wheelbase and wheel size, then checked by rendering from a matched camera and overlaying the silhouette on the photo.

**Regions.** Paint, glass, black trim and underbody are rectangles in net coordinates (`design_tools.sedan_regions`), so window and pillar edges are exact grid lines; glass is recessed with a dark wall. Wheel arches are cut afterwards, keeping the skin's own shading around them.

**Features (`features.py`, `skinmap.py`, `paint.py`).** Lamps, grilles, intakes, cladding, plates and shut lines are patches laid on the skin: outlines given in a side/front/rear view, as rough 3D points snapped to the skin, or directly in net coordinates; they follow the skin around corners. Lamp internals and grille meshes are textures painted with NumPy; brake-lamp light guides carry an emission mask.

**Parts and cabin (`parts.py`, `cabin.py`, `wheels.py`).** Mirrors, handles, wings, exhaust tips, hood scoop, antenna, wipers, the pickup bed and bumpers; a cabin packaged around the driver's eye (dash with cluster and screen textures, console, seats, rear-view mirror, steering wheel); tyres with tread grooves, spoke styles, brake discs and calipers.

**Game contract.** The Unreal client fits a glTF car at load (`Presentation/SimCruiseCarModelRig`): nodes `wheel_fl/fr/rl/rr` (origin at the hub) are found by name, else the four wheels by their ground contacts; a node named like `steering_wheel` turns at the profile's ratio. Material names map to roles (`glass`, `lamp_rear`, `mirror`, `tire`, paint and so on; see `SimCruiseCarMaterialRoles`), and a car's entry in `Content/Vehicles/CarPresentation.json` names the model, overrides roles and gives the paint. The Godot-era contract (Godot axes, `Drive.gd`, `CarBody.gd`) is at the `godot-final` tag.

**Known limits.** Interior materials must stay plain: giving them an emission term made one shader variant stall compilation on the software renderer. The one pickup built with it (the retired Bison XL) had its dash sitting high in the driver view.

**Licence.** The scripts and every model they generate are original MIT work for SimCruise. Photographs were used only as measurement references (sources named in each design file); no photograph, logo or badge is copied into a model, and the cars keep fictional names.
