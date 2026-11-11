# 7. 'Taste reality' harness: reference-matched captures of cars and exact street spots, numeric scores, receipts in every change (reality-loop)
category verification · impact 5 · 5 d · depends: none

## Rationale
The owner wants every change compared against real references in a feedback loop. Today turntable captures exist but are compared by eye in a hand-written table, and nothing compares a street with a photo of that exact spot. Each realism claim needs a number with a receipt, so that visual work converges instead of drifting.

## Code claims (proposer)
checks/CarTurntable.gd captures 16 Ultra views but grep finds no compare, mask or metric code. The 'What differed' table in lab/miata/appearance/turntable/README.md is hand-written. authoring/cars/build_car.py already supports --camera cam.json for matching a photo. RoadMap.gd has 'Compare Street View'. KartaView and Panoramax photos are already fetched as colour hints (detail-engine.md, CC-BY-SA, kept in the user cache). SurveyDaylight computes the real sun position from time and place.

## Design (proposer)
1) Car references (lab/reality/cars/<id>/refs.json):
- Each Commons, owner or press photo gets a licence, a cam.json pose (focal length, position, target, fitted once by hand or by solving against 6-8 clicked hardpoints), the sun azimuth and elevation, and a segmentation mask (hand-made, or the reality critic in ai-assist-engine with human approval).
- CarTurntable --refs renders each pose at Ultra Forward+ with a matching sun.
- Scores: silhouette IoU, plus mean CIELAB deltaE per region (paint, glass, trim, wheel, tyre), plus edge SSIM. Written to receipt.json.
2) Street references (lab/reality/streets/<slug>/):
- A Panoramax or KartaView image of the exact spot, with its lat/lon/heading and timestamp, sets the in-game camera pose and sun.
- Metrics:
  - luminance ratios road:marking:verge:sky (expected about 0.12 : 0.35-0.5 : 0.11-0.15, from the physicallybased/amper albedo tables)
  - road-edge lateral position against the photo
  - marking width and dash cycle (MUTCD 10/30 ft)
  - deltaE per semantic region
- Photo pixels stay in the user cache. Only scores are committed.
3) Physics references (lab/reality/physics/<id>.json):
- Every published figure with its source: C&D sheets, EPA test-car-list coastdown A/B/C, NHTSA dimensions.
- An auto-generated table comparing CarTrack and the new suite against those figures, refreshed with every physics PR.
4) A random-place sampler picks 10 start-pool or random US points per run, as the owner asked.
5) Every scored metric is appended to lab/reality/history.csv, and CHANGELOG entries quote the before/after delta.
6) An albedo validator debug view samples linear albedo and flags road > 0.15, marking > 0.6 and tyre > 0.05.

## Gate
A lab receipt, not in check.sh, since it needs a GPU. It runs windowed with --quality=ultra. Acceptance:
- Car silhouette IoU at least 0.95 at matched cameras.
- deltaE paint at most 6 against owner photos.
- Street luminance ratios within ±20 % of the photo.
- A metric may not regress on a change unless the CHANGELOG justifies it.
The headless parts (physics table, albedo bounds on the bundled start) join the suite. No calibration impact.

## Files
game/checks/CarTurntable.gd
game/checks/RealityCompare.gd
lab/reality/README.md
lab/reality/cars
lab/reality/streets
lab/reality/physics
authoring/cars/build_car.py
docs/technical-overview.md
CHANGELOG.md

## Skeptic verdict
claims_hold false · already_done false · keep true

### Wrong claims
- 'Nothing compares a street with a photo of that exact spot' is false. game/checks/ReferencePose.gd (--reference LAT LON HEADING FOV OUT.jpg [PITCH ROLL], --reference-id for a Panoramax/KartaView frame, --reference-epoch which calls game.apply_daylight with the photo's capture time) already renders the exact spot with the real sun and logs sun azimuth/elevation, frame id and distance. game/checks/MiamiReferenceCapture.gd does the same for KartaView 11591982/217.
- Numeric scores against street photos already exist in several places. lab/holdout/build_comparisons.py quotes per-patch RGB mean-channel error before/after (Charleston 32.7->18.0, Key Largo 67.3->10.3, Miami 106->31.3). lab/holdout/score_spectral.py does region-masked RGB MAE for A/B runs, with a repeat-frame determinism guard. lab/holdout/bethel-facade/score.py scores registered captures with Sobel edge terms. lab/daylight/README.md and validation.json give sky MAE before/after at four dated cameras. The gap is that they are separate per-site scripts, not that no comparison exists.
- Segmentation masks exist. lab/semantic/segment.py runs DeepLabV3 (ADE20K) on KartaView frames and exports road/sidewalk/grass/tree/structure keep-masks. The proposal instead depends on a 'reality critic in ai-assist-engine', which does not exist in the repo and is not declared in depends_on.
- Marking width and lateral registration against photos is already measured. lab/road-paint (widths.py, lane_pose.py, camera-fit.json, validation.json) gives a 0.148/0.142 m width from photos, pixel errors for arrows and dashes, and fitted camera FOV/yaw/pitch/eye height. MUTCD dash rules are checked headlessly in game/checks/LaneMarkingCheck.gd.
- A random-place sampler exists. lab/sample_street_views.py picks seeded random urban and rural districts 'before inspecting their imagery' and keeps the coverage misses. StartPoolCheck.gd covers the start pool.
- Physics references with sources are already per car. Each game/cars/*.json has target_0_60_s/target_70_0_ft/target_skidpad_g plus a 'calibration' citation (Car and Driver) and sometimes 'calibration_open'. CarTrack.gd already prints a per-car measured-vs-published table with bands. Only the EPA coastdown and NHTSA dimension rows would be new.
- 'build_car.py --camera cam.json supports matching a photo' is true only in Blender, and only for the five authored designs (bison_xl, kestrel_s, meridian_z, tora_rs, volta). It does not cover nb1999 (miata.glb) or mustang65, and it is not the game renderer, so it says nothing about the in-game CarFinish/CarBody result. No cam.json for any car is checked in.
- CarTurntable is not 16 fixed views. It has 13 VIEWS constants plus the cockpit view, with three to four more under --lamps. Its studio sun is already settable with --sun=elevation,azimuth, so the 'matching sun' half is partly there.
- 'Ultra Forward+' is not the shipped path. project.godot sets rendering_method="mobile", and the existing turntable receipt is a Mobile renderer run. Scores should be taken on the shipped renderer, with Forward+ optional.

### Design problems
- About two thirds of the street and physics scope duplicates existing tools: ReferencePose, the holdout scorers, lab/semantic, lab/road-paint, lab/daylight, sample_street_views.py, CarTrack's table. Building new parallel lab/reality/streets and physics trees would fork the evidence instead of joining it.
- The only real gap is numeric scoring on the car side. CarTurntable writes JPEGs and lab/miata/appearance/turntable/README.md's 'What differed' table is judged by eye. That part is worth doing.
- The street luminance targets are physically incoherent. 'road:marking:verge:sky about 0.12 : 0.35-0.5 : 0.11-0.15' mixes surface albedos with sky radiance, which has no albedo. Ratios taken from sRGB JPEGs with unknown tone curves and exposure are not linear. The existing daylight README already notes that photo exposure and processing are uncertain. Any ratio has to be computed on linearised values for co-lit Lambertian regions (road vs marking vs verge), never against sky.
- deltaE <= 6 for paint against owner or Commons photos is not meaningful without exposure and white-balance normalisation. Normalise first, e.g. fit a per-image gain on a neutral region such as tyre sidewall or concrete, or compare only hue/chroma. Otherwise Godot's tonemapper versus the camera's JPEG pipeline dominates the score.
- Silhouette IoU >= 0.95 is the wrong gate. At car scale IoU barely notices the known defects (headlamp shape, hardtop glass, bumper). The current Miata model may fail it simply because of the documented 'Remaining: needs new hardtop/shell'. Pose error from hand-clicked PnP also caps reachable IoU. Use boundary/chamfer distance in pixels per view, plus per-feature landmark reprojection error (lamp corners, wheel centres, A-pillar base), and treat IoU as secondary.
- Converting cam.json between Blender (Z-up) and Godot (car frame, -Z forward, Y-up) is unspecified. CarTurntable's views are hard-coded consts, so it needs a pose-file input that uses its existing camera code.
- GPU scores in a committed history.csv will differ by machine and driver (the receipt pins an RTX 2070 Max-Q). The 'may not regress unless CHANGELOG justifies it' rule has no enforcement and will raise false alarms across machines. Record renderer/GPU per row and compare only like with like. score_spectral.py's repeat-frame identity check is the right pattern to reuse.
- Many random US points have no street-photo coverage (lab/footprints/README notes this). A per-run sampler of 10 points needs the existing keep-the-misses accounting, not a fixed photo-scored count.
- The headless 'albedo bounds on bundled start' can only check material/shader parameters, not rendered albedo. On the Mobile renderer, an 'albedo debug view' has to use VIEWPORT_DEBUG_DRAW_UNSHADED, and custom shaders (Stochastic, wear masks) must output ALBEDO honestly for it to mean anything.
- An effort of 5 days for car PnP fitting, per-region masks for 7 cars, a street harness, a physics table and history tooling is underestimated. The scoped-down version below is closer to 2-3 days.
- Relevance: the user asked about the AI engine for traffic, extras and detail generation. This proposal is a verification loop, useful as that engine's acceptance gate but not an answer to the question. Its dependency on the 'reality critic' should be dropped or made explicit.
- Gates: it is read-only tooling with no physics change, so CarTrack/LiftOff are unaffected. That claim holds.

### Corrected design (USE THIS where it differs)
Scope it to the real gap and index what exists.
(1) CarTurntable gets --poses=FILE: a JSON list of {name, eye, target, vfov, sun:[elev,az], resolution} in the car frame, reusing its existing camera and studio-sun code. Each photo pose is fitted once with OpenCV solvePnP against 6-8 hand-clicked landmarks on the car's known dimensions, stored in lab/<car>/appearance/refs.json with the licence. Run it on the shipped Mobile renderer at --quality=ultra; Forward+ is optional.
(2) A Python scorer, lab/appearance/score_car.py, in the style of the existing holdout scripts (numpy/PIL/scipy). It gets the photo mask from lab/semantic/segment.py's ADE20K 'car' class plus a manual fix-up, and the render mask from a transparent-background pass. It reports landmark reprojection error in px, boundary chamfer distance in px, and IoU as secondary. Paint and trim colour is compared after a per-image gain fitted on a neutral patch, using CIEDE2000 on linearised values. Add a repeat-render identity check before scoring. The output replaces the hand-written 'What differed' Status column with numbers in receipt.json.
(3) Streets: no new harness. Write lab/reality/README.md as an index of ReferencePose + holdout/score_spectral.py + bethel score.py + road-paint + daylight. Add a small common wrapper that writes their existing outputs into one JSON per site with renderer/GPU/commit. Luminance comparisons only between co-lit Lambertian regions (road/marking/verge) on linearised pixels, with no sky ratio.
(4) Physics: extend each car JSON's existing calibration block with an EPA coastdown A/B/C and NHTSA dimensions where available, and have CarTrack print those rows too. This stays headless, needs no new suite and has no calibration change.
(5) History: per-machine rows (renderer, GPU, commit) in lab/reality/history.csv. The CHANGELOG quotes before/after only from same-machine pairs. Treat it as an advisory receipt, not a hard gate.
Effort: about 2-3 days. No CarTrack/LiftOff impact.
