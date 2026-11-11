# Unreal client architecture

How the Unreal client (`SimCruise.uproject`, module `Source/SimCruise`) is put together, one section per area. The Godot client under `game/` is the behavioural reference while the port is under way; each section names the Godot files it replaces.

## Car presentation (`Source/SimCruise/{Public,Private}/Presentation/`, `Content/Vehicles/`)

The visible car: the downloaded Sketchfab model of each car, fitted to the car's data, cut into moving parts and drawn with physically based materials. It replaces `game/ModelRig.gd`, `CarBody.gd` and the material parts of `CarFinish.gd`. Physics never reads anything from here.

### Pipeline

1. **glTF in at runtime** (`SimCruiseGltfModel`). A small, strict glTF 2.0 reader loads `scene.gltf` + `scene.bin` (or `.glb`) with every node transform applied, in glTF's own space. Unreal's Interchange glTF importer was not used: it is an editor import that writes `.uasset`s, and the rig needs the raw triangles anyway. The glTF stays the only source of truth, so nothing binary has to be merged.
2. **Rig** (`SimCruiseCarModelRig`), a port of `ModelRig.gd` plus the `cars/rig-infra` steering-wheel search, working in the Godot vehicle frame so it can be compared line by line:
   - upright check; wheels by node name (`Front Left Wheel`, `wheel_fl` …) or by the four ground-contact patches, refined from the tyre surfaces (new) so the hub sits at the tyre's true centre and the model's real tyre radius is measured;
   - front/back from the named wheels, else `game/art/traffic/manifest.json`'s `flip` when the model has an entry there, then the rear lamps overrule it (new: a model whose red lenses sit at the front end is turned round; the NB has no manifest entry and is placed by its lamps alone);
   - uniform scale to the car's wheelbase, then (new) a lateral fit so the model's mean track equals the car's (applied for misses of 1.5–10 %);
   - static ride: hubs at `mount_height_m - static_length_m`;
   - wheel cut by **connected parts** wholly inside a tyre cylinder (a suspension arm that reaches outside stays on the body); off-axis parts inboard of the wheel's centre plane, or parts with the caliper role, become the non-spinning **upright** (calipers steer and travel but do not spin);
   - badges: by name (`badge|logo|emblem|brand|invisible|unimportant`), by profile box, and (new, general) by shape and place — a small, thin, detached part on the centreline of the nose or tail, or a small part of another material on a wheel's axle at its outer face (centre-cap logo). Lettering off the centreline (the NB's `MX-5` and `mazda` on the tail panel, the Mustang's `FORD`) needs a profile box;
   - role areas (profile boxes, new): parts wholly inside a box take another role through a copy of their author material, for models that draw several surfaces with one texture atlas (the Mustang's tail-lamp lens segments, so its brake lamps light);
   - reversing lamps (new, general): white lenses (`lamp_front`) behind the axles are reversing or plate lamps and become unlit clear glass, so they do not light with the head lamps;
   - steering wheel: by name, else by geometry (a flat closed ring of 0.24–0.48 m across in the driver's half of the cabin — the side of the car data's driver eye, so right-hand-drive cars work too — facing fore/aft), else (new) by fitting circles to arcs of a rim modelled in pieces; when only the rim was found, the spokes and hub welded to the column are cut out in the wheel's disc;
   - output in Unreal axes: X forward, Y right, Z up, centimetres, origin midway between the axles at the physics body origin. The axis change keeps glTF's counter-clockwise front faces as Unreal front faces (checked with two-sided materials, whose facing sign turns the shading normal).
3. **Meshes** (`SimCruiseCarAssets`): one transient `UStaticMesh` per part (body, 4 wheels, 4 uprights, steering wheel), glass and lenses in a separate non-Nanite mesh. Editor builds run the full mesh build with **Nanite for parts over 20 000 triangles** (the 1.1 M-triangle NB body), MikkTSpace tangents and the author's normals; the result is cached in the DDC by content hash, so later starts take seconds. Cooked games take the fast runtime build (no Nanite). Built cars are cached per process and shared between instances.
4. **Materials** (`SimCruiseCarMaterialLibrary`): three master materials generated from C++ — a two-sided clear-coat surface, its masked twin, and thin-translucent glass — each around HLSL custom nodes. Roles (`SimCruiseCarMaterialRoles`: paint, glass, chrome, wheel alloy, tyre, brake disc, caliper, grained plastic, interior plastic, cloth, leather, carpet, lamps, gauges …) are guessed from author material names and overridden per car. Surface detail is procedural in each part's own space, so it moves with the part and needs no UVs:
   - paint: a cellular field of 0.55 mm flakes tilts the **base-coat normal under a smooth clear coat** (`r.ClearCoatNormal=True`), orange peel tilts the coat; flakes fade into base roughness below a pixel;
   - pebble grain on plastics, leather grain, cloth weave, brushed metal, lathe marks and rusty edges on brake discs, rubber grain and road dust on tyres, procedural asphalt for the stage;
   - glass is thin translucent with coloured transmittance (faint green laminated glass, red/amber lenses) and Fresnel reflection; lamp lenses glow in nits (red channel: tail 300, brake 6000) in a near-pure linear red, since signal red lies outside the sRGB gamut and any green or blue tone-maps a bright lens to salmon.
   The editor builds the masters in memory on first use, so a code change is never hidden by a stale asset; `SimCruise.SaveCarMaterials` writes them under `/Game/Vehicles/Materials` for cooking.
5. **Component** (`USimCruiseCarPresentationComponent`, actor `ASimCruiseCarPresentationActor`).

### Interface for the vehicle pawn

```cpp
// Implement on the pawn or movement component:
class ISimCruiseWheelStateProvider { virtual bool GetCarVisualState(FSimCruiseCarVisualState& Out) const = 0; };

USimCruiseCarPresentationComponent* Look = CreateDefaultSubobject<USimCruiseCarPresentationComponent>(TEXT("Look"));
Look->SetupAttachment(Body);              // identity: the physics body origin, Unreal axes
Look->InitializeForCar(TEXT("nb1999"));    // blocking on a car's first build
Look->SetWheelStateProvider(this);         // polled every frame in TG_PostPhysics
```

`FSimCruiseCarVisualState` per wheel (FL, FR, RL, RR): `SteerAngleRad` (+ left), `CamberRad` (+ top outboard), `SpinAngleRad` (+ rolling forward; keep it wrapped to one turn, since a float accumulated over a long drive loses the precision a spinning wheel needs), `SuspensionTravelCm` (+ bump); plus `bBrake` and `bLamps`. The component polls the provider on the game thread in `TG_PostPhysics`: a provider fed from the physics thread must hand over a copy of the last completed step (no reads of live physics-thread state). Part components get unique names, so one actor can carry several cars and a car can be re-initialised. The steering wheel turns by the mean front steer angle times the car's overall `steering_ratio` (anticlockwise for a left turn as the driver sees it). `GetStaticHubLocation`, `GetDriverEye` and `GetSteeringWheelRestTransform` give the cockpit camera and HUD what they need. The sign conventions are pinned by `SimCruise.Presentation.Component.Conventions`.

### Data: `Content/Vehicles/CarPresentation.json`

Per car id: `model` (glTF path), `credit`, `steering_ratio` with its source, `paint` (name, sRGB base, metallic, base roughness, flake, clear-coat roughness, orange peel, source), `roles` (author material → role), `tints` (role → colour), `badges` (brand notes, or `{note, material?, box}` in the Unreal vehicle frame, metres), `role_areas` (`{note, role, material?, box}`), optional `flip`, `fit_track`, `auto_badges`, `hide_pattern` and a `steering_wheel` override (`node`, or `centre`/`axis`/`radius`, or `none`).

### Turntable

`SimCruiseTurntableGameMode` on `/Engine/Maps/Entry` (or the `SimCruise.Turntable [ids|all] [dir]` console command in any world) lights an outdoor stage — physical sun 42° up and 215° from the nose as in the Godot taste test, sky atmosphere, volumetric clouds, real-time sky light, procedural asphalt — sets scalability 4 with Lumen GI and reflections, and photographs each car from eight angles plus the driver's seat, a wheel close-up, a steered front three-quarter, the steered cockpit and a braking rear three-quarter. Shots and `report.txt` (rig log, build time, mean frame time) go to `Saved/Screenshots/Turntable`. See `docs/unreal-building.md` for the command line.

### What it looks like (turntable, 11 November 2026)

![1999 roadster, front three-quarter](images/unreal-nb1999-front-34-left.jpg)
![1965 Mustang, front three-quarter](images/unreal-mustang65-front-34-left.jpg)
![Corvette C8, rear three-quarter](images/unreal-meridian-z-rear-34-left.jpg)
![NB wheel: tyre, alloy, disc and caliper](images/unreal-nb1999-wheel.jpg)
![Mustang cockpit with the wheel turned for 8 degrees of road-wheel lock](images/unreal-mustang65-steer-driver.jpg)

### Tests

`SimCruise.Presentation.*`: glTF loading of all three models; the rig for each car (wheelbase, hubs, track and tyre within tolerance of the car data, wheels centred on hubs, steering wheel ahead of and below the eye, rear-lamp surfaces present and behind the axles, white head-lamp lenses ahead of them, calipers on uprights for the C8, badge removal); role guessing; component conventions; building and posing the C8 in a world.

### Known gaps (review, 11 November 2026)

- Lit lamps glow evenly over the whole lens: there is no reflector or bulb structure in the glow, so the NB's brake lamps read as a flat red oval at close range.
- Shots dominated by shade (the wheel close-up, the cockpit) come out strongly blue: the shade is lit by the blue sky and Unreal has no automatic white balance. A real phone photo of the same scene would be white-balanced. Lighting and camera belong to the rendering stream.
- The C8's rear quarters show jagged reflection patches where the horizon reflects off the curved paint; the model's normals are the likely cause, and this has not been checked further.
- The Mustang's steering wheel is 0.26 m across in the model, well short of the real wheel; its tail-lamp lenses are dark when unlit in shade.
- The cooked-game path (fast mesh build, saved masters, runtime textures) has been compiled but never run.
