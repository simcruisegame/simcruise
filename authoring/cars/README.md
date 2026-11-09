# Car models

`build_car.py` builds each car's exterior and simple cabin in Blender from a shape file in this folder and writes `game/art/cars/<id>.glb`:

```sh
blender -b -P authoring/cars/build_car.py -- authoring/cars/kestrel_s.json game/art/cars/kestrel_s.glb
blender -b -P authoring/cars/build_car.py -- authoring/cars/kestrel_s.json /tmp/x.glb --render /tmp/kestrel.png [--view side|rear]
```

Tested with Blender 5.2 LTS. One build takes about ten seconds.

**How the body is made.** The skin is lofted from cross-sections along the car. Each section takes its height from the side silhouette (`top`, `belt`, `bottom`) and its half-width from the plan (`plan`), all given as fractions of the length from the rear bumper. Above the beltline it narrows toward the roof (`tumblehome`) between the cabin stations (`cabin`: C-pillar base, C-pillar top, A-pillar top, A-pillar base). One subdivision pass smooths the skin, and wheel arches are cut at the axles. Materials are assigned on that single skin by position: paint, satin black behind the glass and on the pillars, lower trim, and thin dark shut lines for doors, hood and lid.

**Detail without geometry cost.** Glass, lamps, grilles, intakes and cladding are *decals*: an outline drawn in a side, top, front or rear plane, rounded to a fixed corner radius, filled with a fine grid and shrink-wrapped onto the skin 3 mm proud. The skin is then cut away under the glass, so you can see out from the driver's seat and see in from outside. Wheels (tyre with sidewall bulge, spoked rim, hub, disc and caliper), mirrors, handles, plates, extra parts (`parts`) and the seats, dash and steering wheel are separate meshes.

**Game contract.** Origin on the ground midway between the axles, Blender +Y forward (Godot −Z). Node names `wheel_fl/fr/rl/rr` (origin at the hub, spins), `wheel_*_upright` (disc and caliper, steers but does not spin). Material `lamp_rear` becomes the brake lamps and `glass` is drawn tinted and transparent. `game/CarBody.gd` mounts the model on the physics car, and the car file in `game/cars/` names it in `model_scene` and gives the driver's eye point in `driver_eye`.

**Licence.** The script and every model it generates are original MIT work for SimCruise. Proportions come from published dimensions of the reference cars named in `game/cars/*.json`; no manufacturer shapes, logos or badges are traced. The shapes are deliberately generic. They read correctly at driving distance but are not photoreal, and contributions of better models (CC0 or CC BY, with the same node names) are welcome.
