# Magician: study notes

Role: perception and illusion expert for SimCruise.
Question this role answers: what does the driver notice, and what can we hide?
Style: ASD-STE100 Simplified Technical English. Short sentences. Active voice.
Status of facts: each fact has a source. "(unverified)" marks a fact that we did not check in the primary source.

## 1. Core idea

A driver looks at a small part of the screen.
The driver sees detail only near the gaze point.
The driver does not see most changes that happen during a masking event.
Use these limits. Spend detail where the eye goes. Make changes where the eye does not go, or when the eye cannot see.
Never fake a hazard. Never hide a thing that the driver needs to drive safely.

## 2. Rules and numbers

### 2.1 Change blindness

- Rule: people miss large changes when a brief interruption hides the motion signal of the change.
  Source: Simons and Rensink 2005 [R1]. Rensink, O'Regan, Clark 1997 flicker paradigm (unverified in primary).
- Masking events in driving: a blink, a saccade, a camera cut, a view change, a flash (overpass shadow, tunnel exit), a large object that crosses the view (truck, pole, palm trunk), an exposure jump.
- Rule: a change without a mask makes a local motion transient. The transient pulls attention to the change. This is "pop-in".
- Rule: a slow change (fade over more than about 1 s) also escapes notice, when no motion transient exists. (Gradual change blindness, Simons, Franconeri, Reimer 2000, unverified in primary.)
- Number for SimCruise: fade any swap over 0.5 s to 2 s. Do not fade faster than 0.25 s. A 0.25 s fade still makes a transient.

### 2.2 Saccadic suppression

- Rule: vision is strongly reduced during a saccade and for about 50 ms around it. (Ross, Morrone, Goldberg, Burr 2001, Trends in Neurosciences 24:113, unverified in primary.)
- Saccades happen about 3 times per second. Drivers saccade to the road edge, to mirrors and to signs.
- Without eye tracking we cannot time a swap to a saccade. Use other masks.

### 2.3 Inattentional blindness

- Rule: people miss an unexpected object in plain view when attention is on a task. (Simons and Chabris 1999, "Gorillas in our midst", Perception 28:1059, unverified in primary.)
- Driving is an attention task. The driver attends to the lane ahead, the car ahead, and the bend.
- Use: low-value detail (building backs, far parked cars, side streets) can be cheaper. The driver does not attend to it.
- Limit: inattentional blindness fails for motion onset and luminance onset. A sudden light or a sudden object grabs attention (attentional capture). Pop-in is an onset. So it is noticed.

### 2.4 Misdirection

- Rule from magicians: "a big move covers a small move". Attention goes to the larger, newer, brighter or moving thing. [R3, R4]
- Rule: the secret move happens before the audience thinks the trick starts. [R3]
- Rule: repetition gets noticed. A repeated method is found out. (Teller, cited in docs/research/domain-ideas.md F23.)
- Use: do swaps during a natural event that already has attention (a corner, a car that passes, a light change).
- Use: never repeat an identical object in a regular pattern near the gaze. Vary scale, yaw, tint.

### 2.5 Peripheral vision

- Rule: acuity falls fast with eccentricity. Pattern recognition in the periphery is limited by crowding, not by acuity alone. [R5]
- Rule: the periphery is very sensitive to flicker and motion. It is weak at detail. (Patney et al. 2016, cited in domain-ideas F26.)
- Number: crowding zone is about half the eccentricity (Bouma's law, b ≈ 0.5). [R5]
- Use: a LOD swap or texture swap in the periphery is seen as flicker, not as a change of detail. Fade it.
- Use: blur in the periphery reads as "wrong". Noise or grain reads as "detail". (domain-ideas F26, Patney 2016.)

### 2.6 Speed perception and vection

- Rule: drivers in simulators underestimate speed. They drive about 10 % too fast with a 1:1 geometric FOV. [R6]
- Rule: a wider geometric FOV than the display FOV reduces the error. Diels and Parkes found an optimum GFOV/FOV ratio of about 1.22:1. [R6]
- Rule: speed cues come from optic flow in the lower and side field: road texture, lane marks, poles, trees close to the road. Edge rate (objects per second) matters more than distance.
- Rule: a flat, low-frequency road texture removes the flow cue. Then speed feels slow.
- Rule: sound, vibration and camera shake add to perceived speed. They do not replace optic flow.
- Number for SimCruise: on the 15.6 inch laptop at 60 cm the screen subtends about 18.4 deg vertical. The "natural" lens is 45 deg vertical. So the ratio is about 2.4:1. This is far outside the range that Diels and Parkes tested (0.83 to 1.33). Treat the speed effect at 2.4:1 as unknown. Test it.

### 2.7 Hiding streaming and LOD

- Rule: a swap is safe when (a) it is outside the gaze area, and (b) it has no transient, or (c) a mask hides it.
- Order of preference:
  1. Swap beyond the visible distance (fog, haze, terrain, buildings occlude).
  2. Swap behind the camera or behind an occluder.
  3. Swap with a dithered cross-fade over 0.5 s to 2 s.
  4. Swap during a mask (camera cut, view change, overpass, tunnel, big passing object).
  5. Last: swap in view without a fade. This is pop-in. Treat it as a bug.
- Rule: light changes are more visible than shape changes. A lamp that turns on, a shadow that appears, or an exposure jump pops more than a mesh.
- Rule: aerial perspective hides far change only when the haze is strong. With 20 km visibility the extinction at 550 m is about 10 %. That does not hide a pop.
  Compute: extinction coefficient = 3.912 / visibility (Koschmieder). Transmittance = exp(-k d).

### 2.8 A cheap world that feels detailed

- Rule: the eye builds a detailed world from a few correct cues. Get the cues right where the eye looks: road surface, lane marks, kerbs, the car ahead, the near roadside objects.
- Rule: contact shadows and ambient occlusion at the base of objects make them "sit" on the ground. Floating objects read as fake.
- Rule: variation beats resolution. Different window lights, different colour temperatures, dirt at the base of walls.
- Rule: perfect regularity reads as CG. Real streets have irregular spacing and small damage.
- Rule: at night, lit windows that are all the same flat white read as fake. Real rooms vary in brightness and colour (2700 K to 6500 K).

## 3. How to judge SimCruise

### 3.1 Files to read first

- `Source/SimCruise/Private/World/SimCruiseWorldStreamer.cpp`: tile install (`TickStreaming`), street light pool (`UpdateNightLighting`), parked cars budget (`Tick`).
- `Source/SimCruise/Private/World/SimCruiseTileManager.cpp`: request ring, priority, eviction.
- `Source/SimCruise/Public/World/SimCruiseTileManager.h`: `RingRadius` (1 = 3 x 3 tiles).
- `Source/SimCruise/Private/World/SimCruiseTrees.cpp`: tree LOD rings, cull distances, `Refresh`.
- `Source/SimCruise/Private/World/SimCruiseTraffic.cpp`: `Spawn` sight test.
- `Source/SimCruise/Private/World/SimCruiseWorldTile.cpp`: `Install`, `PlaceParkedCars`, `SetLightsOn`.
- `Source/SimCruise/Private/Gameplay/SimCruisePlayerCameraManager.cpp`: camera snaps (they are masks, and they can also be pops).
- `docs/research/domain-ideas.md`: sections 6 and 7, idea 4 "the magician's streamer", idea 5 "honest speed gate".
- `docs/unreal-architecture.md`: streaming, light and capture notes.

### 3.2 Facts about the current world (2026-11-12)

- Tile size is 40 075 km / 65 536 × cos(lat). That is about 551 m at Miami Beach (25.8 deg N).
- The ring is 3 x 3 tiles. The far edge of the loaded world is 551 m to 1 102 m ahead.
- The streamer installs at most one tile per frame.
- Trees use three HISM rings per species with cull distances 33 m, 92 m and 600 m.
- 24 spotlights move to the 24 lamps nearest the car, every 0.25 s.

### 3.3 Checks

For each check: state pass or fail, give the evidence, give the fix.

1. Pop-in of tiles. Does a tile appear in view? At what distance? With a fade? Fail if it appears in view without a fade closer than the haze hides (transmittance > 0.5).
2. Pop-in of LOD. Do LOD rings overlap or leave gaps? Is there a dithered fade? Remember: in Unreal, HISM `InstanceStartCullDistance` is the fade start. It does not hide near instances. [R7]
3. Light pops. Do lights, shadows or exposure jump? Fail if a light visibly turns on or off near the gaze without a fade.
4. Spawn in sight. Do traffic cars, parked cars or props appear in the camera frustum? Traffic has a sight test (`SimCruiseTraffic.cpp` `Spawn`). Check every other spawner.
5. Repetition. Count identical objects in a regular pattern in a capture. Fail if a viewer can spot the repeat in 5 s.
6. Speed cue. Is there optic flow at the road edge? Do the road and the marks have mid and high spatial frequency? Use the "speedometer hidden" test.
7. Camera-induced pops. Does the camera jump (collision, snap, view change)? A jump is a mask for a swap, but an unplanned jump is itself a pop.
8. Night read. Does the scene read at night? Do lights vary? Do silhouettes read against the sky?

### 3.4 Test methods

- Pop-in button test (domain-ideas idea 4): players press a button when they see something appear. Record time and screen position. Goal: masked swaps detected at less than half the rate of unmasked swaps.
- Frame-pair diff: capture two frames 1/60 s apart while driving straight at constant speed. Subtract them after a motion-compensated warp. Large local differences that are not motion show pops. (Method proposal, not a standard.)
- Speed production test (Diels and Parkes protocol [R6]): hide the speedometer. Ask the driver to hold 20, 30, 50 and 70 mph. Measure the mean error. Goal under 5 %.
- Repetition test: show a capture for 5 s. Ask "did you see the same object twice?" Count hits.
- Night read test: show night captures beside licensed night photos of the same street. Ask "which is the photo?" Score d'.
- Use `-SimCruiseDriveCapture` (docs/unreal-architecture.md) to get repeatable captures.
- Use `SimCruise.Env.Time` to step into night and dusk.

### 3.5 Automation tests to look at

- `SimCruise.Gameplay.Camera.*` in `Source/SimCruise/Private/Gameplay/Tests/SimCruiseGameplayTests.cpp`.
- `Source/SimCruise/Private/Tests/WorldDetailTests.cpp`.
- `Source/SimCruise/Private/World/Tests/` (tile manager and streamer tests).
- No test today checks LOD overlap, fades, or spawn-in-sight for parked cars.

## 4. Common failure modes

1. LOD ranges ported from Godot `visibility_range_begin` to Unreal HISM start-cull. Godot hides near instances. Unreal only starts a fade. Result: all LODs draw near the camera.
2. Pool reassignment without a fade. A pooled light, sound or decal jumps to a new owner. The eye sees the jump.
3. Install at a fixed distance in clear air. The world edge "grows" in plain view.
4. Spawn without a frustum test. Cars or props appear on screen.
5. Identical instances at regular spacing. The eye finds the pattern.
6. Flat textures on the road. Speed feels slow. The world feels empty.
7. Night scenes with a black sky and uniform white windows. They read as a diagram, not a street.
8. Swap during a camera cut that the player did not cause. The player feels cheated if they notice.
9. Fixing a pop by making it slower but still visible (a 0.2 s fade). Make it 0.5 s or more, or hide it.

## 5. Severity guide

- P0: a change hides or fakes something the driver needs (a car, a kerb, a road edge, a hazard).
- P1: a visible pop or swap near the gaze in normal driving, every drive.
- P2: a visible pop in rare views, in the periphery, or only at night; a missed chance to add felt detail.

## 6. References

- [R1] Simons, D. J., Rensink, R. A. (2005). Change blindness: past, present, and future. Trends in Cognitive Sciences 9(1):16-20. https://www.cs.ubc.ca/~rensink/flicker/abs.05.0a.html (PDF: https://www.cs.ubc.ca/~rensink/publications/download/S&R-TICS-05a.pdf). Checked abstract.
- [R2] Simons, D. J., Chabris, C. F. (1999). Gorillas in our midst. Perception 28:1059-1074. (Not fetched. Standard citation.)
- [R3] Macknik, S. L., Martinez-Conde, S. et al. (2008). Attention and awareness in stage magic. Nature Reviews Neuroscience 9:871-879. Copy linked in docs/research/domain-ideas.md F23. Book: Sleights of Mind (2010), Henry Holt. (Book not read here.)
- [R4] Kuhn, G. (2019). Experiencing the Impossible: The Science of Magic. MIT Press. (Not read here. Standard text.)
- [R5] Strasburger, H., Rentschler, I., Jüttner, M. (2011). Peripheral vision and pattern recognition: a review. Journal of Vision 11(5):13. https://publications.aston.ac.uk/id/eprint/23648/1/jov_11_5_13.pdf (open access). Bouma factor from this review (unverified page).
- [R6] Diels, C., Parkes, A. M. (2010). Geometric field of view manipulations affect perceived speed in driving simulators. Advances in Transportation Studies XXII:53-64. https://pureportal.coventry.ac.uk/en/publications/geometric-field-of-view-manipulations-affect-perceived-speed-in-d/ . Checked abstract: 10 % speed overproduction at 1:1; optimum GFOV/FOV 1.22:1.
- [R7] Epic Games. UInstancedStaticMeshComponent::SetCullDistances: "Sets the fading start and culling end distances for this component." https://docs.unrealengine.com/4.26/en-US/API/Runtime/Engine/Components/UInstancedStaticMeshComponent/SetCullDistances/index.html . Checked. Same API in UE 5 (unverified for 5.8 text).
- [R8] Ross, J., Morrone, M. C., Goldberg, M. E., Burr, D. C. (2001). Changes in visual perception at the time of saccades. Trends in Neurosciences 24(2):113-121. (Not fetched.)
- [R9] Patney, A. et al. (2016). Towards foveated rendering for gaze-tracked VR. https://research.nvidia.com/publication/2016-12_towards-foveated-rendering-gaze-tracked-virtual-reality (link from domain-ideas F26).
- [R10] Koschmieder visibility: k = 3.912 / V. Used in `SimCruiseDaylightModel.cpp` `ExtinctionPerKmForVisibility`.

Licences: all sources are cited, not copied. Papers are read under their publishers' terms. No figures or text are reproduced.

## What I now know (onboarding, 2026-11-14)

Role: magician (what the driver notices: pop-in, swaps, repetition). This is desk study only. Nothing was run. "(estimate)" marks a judgment that has no run behind it.

**How my area works today.**
- Ring: `FSimCruiseTileManager::TakeRequests` (`Private/World/SimCruiseTileManager.cpp:40-79`) requests the 3 x 3 tiles around the car's **current** tile (`RingRadius = 1`, `Public/World/SimCruiseTileManager.h:30`). The 5 s prediction only sorts tiles inside that ring (score 0.4 now + 0.6 predicted, :54). It never adds a tile ahead. `MaxInFlight` is 2 (`SimCruiseWorldStreamer.h:81`), and two edge neighbours never build together (:62-67). The tile straight ahead builds alone, then its two side tiles build.
- Tile size is 611.5 m x cos(lat). It is not a fixed 551 m. Examples: Hilo 576 m, Rawlins 456 m, Casselton 418 m, Seattle 412 m, Anchorage 294 m. Thus the world ahead can end at 1 tile.
- Install: `TickStreaming` (`SimCruiseWorldStreamer.cpp:483-579`) installs at most 1 tile per frame, with a 6 ms step budget (:523). `ASimCruiseWorldTile::Install` (`SimCruiseWorldTile.cpp:154-473`) registers the static mesh at once. There is no fade and no dither. Tiles are evicted beyond ring + 1 (`TileManager.cpp:81-96`).
- Far land: `ASimCruiseRegionalTerrain` (`SimCruiseRegionalTerrain.cpp`) draws a 45 km mesh. The mesh is hidden within **a fixed 550 m** of the camera (`LandscapeMask`, :76-78). It is sunk 65 m near the camera and 0 m at 3 km (`LandscapeOffset`, :68-73). The whole mesh is replaced in one frame when the car enters a new 8-tile region (`Install`, :384-385; `Tick`, :489-502).
- Trees: `ASimCruiseTrees::BuildMeshes` (`SimCruiseTrees.cpp:374-432`) uses 2 species (leaf, pine) x 3 HISM rings. The cull distances are `{0,3300,9200}` to `{3300,9200,60000}` cm (:425-427). Every tree goes into all 3 rings (:563-566). No material uses `PerInstanceFadeAmount` (grep: none in `Source/`). The result: all levels draw near the camera, and each level has a hard cut at its far distance. Shrubs cut at 250 m (:445). Palms cut at 600 m (:484).
- Tree refresh: `Tick` (:780-787) calls `Refresh` each time a tile's canopy arrives or the origin moves. `Refresh` clears every ring and rebuilds it synchronously (`BuildTreeIfOutdated(false,true)`, :575-584). Canopy data (CHMv2, `helper/canopy.mjs`) arrives after the tile, 2 fetches at a time (:671). Thus trees come in later, on ground that is already visible.
- Tree species: `ConiferShare` (:292-304) uses 5 lat/lon boxes. A tint parameter exists only on sage (:437). The leaf tint is fixed (:228). The yaw and the XY/Z scale come from a hash (:558-561).
- Traffic: `ASimCruiseTraffic::Spawn` (`SimCruiseTraffic.cpp:745-812`) spawns cars 60-260 m away (:33). The sight test (:776) blocks a spawn only when the car is inside a cone of dot > 0.2 **and** closer than 160 m. Removal at 320 m (:898) has no view test. The car count is a fixed 20 at every place (`CVarTrafficCars`, :25, :938).
- Parked cars: `PlaceParkedCars` (`SimCruiseWorldTile.cpp:542-602`) places 3 per frame (`Streamer.cpp:365-370`). It has no frustum test. The draw distance is 150 m (docs).
- Street lights: `UpdateNightLighting` (`Streamer.cpp:373-481`) runs every 0.25 s. It moves the 24 pooled spot lights (1800 cd, 3000 K, no shadow) to the 24 nearest lamps within 250 m. A lamp turns on or off at the pool boundary in one step. Lamps stand every 33 m on curbed roads only (`SimCruiseTileDetail.cpp:1269-1288`).
- Pre-flight: the pop-in check (`SimCruisePreflight.cpp:773-830`) counts tiles and cars that appear within **150 m** (`PopInRangeM`, `SimCruisePreflight.h:96`). It allows 2 car pops (`MaxCarPopIns`). It does not see trees, shrubs, the far land, lights, or cars that vanish.

**Does it work at a random US place?**
- General: tiles, CHMv2 trees, shrub cover and the 45 km landscape use national or global data. Spawn rules, LOD numbers and the light pool use no regional rule.
- Fixed to one latitude: the landscape mask (550 m) and the doc number "far edge 551-1102 m" are true only near Miami (25.8 N). Gap with no drawn ground between the tile edge and 550 m: Rawlins 456-550 m, Casselton 418-550 m, Anchorage 294-550 m. On flat ground (Casselton, Delta UT) the sunk landscape hides this gap (estimate). In hills (Welch, Hazard, Jasper, Seattle) the driver sees a step: ridges past the ring are 55-65 m too low, and they jump up when the row installs (estimate).
- Highway at a data-poor place: Rawlins I-80 at 80 mph (35.8 m/s) crosses a 456 m tile in 12.7 s. A cold-cache tile with helper fetch, build and the neural aerial (1.4-5 s, `unreal-architecture.md:513`) can take more than that. Then the road ahead is missing (estimate). With a warm cache (about 3 s per tile from "9 tiles in 13-16 s", `unreal-architecture.md:637`), the next row pops in at about 350 m. That is in clear view, because 20 km visibility gives a transmittance of about 0.92 at 400 m.
- Hard-wired: building style is `MiamiBeachDeco` only in one box. Every other place is `Generic` (`SimCruiseTileDetail.cpp:219-228`). Per-tile place files exist only for Miami (`data/knowledge/places`, 24 files). The place graph has no region for Rawlins, Delta or Anchorage (`place-graph.json` regions). Palms grow only in South Florida and Hawaii (`:230-237`). Edinburg TX has palms in reality but gets none.
- Repetition where data is thin: every broadleaf tree in the US is one mesh, stretched. In Casselton the shelterbelts are rows of the same tree with the same colour (estimate: you can find it in under 5 s). On a section road, 20 cars crowd the 1-2 ribbons within 260 m, 15 m apart, made from 7 models (estimate).
- Speed cue at rural places: there are no roadside verticals. No poles, delineators, fences or snow fences exist in `SimCruiseStreetFurniture.cpp` (hydrants and lamps only on curbed roads). On Rawlins I-80 and Casselton gravel, the only edge flow is the road texture (the detail layer fades out by 40 m) and lane dashes on major roads. Speed feels slow (estimate).

**Main gaps against "realistic everywhere".**
1. P0: the forward reach is only 1 tile (294-576 m, set by latitude) and it does not depend on speed. A cold tile at highway speed can leave the road ahead missing. Files: `SimCruiseTileManager.cpp:46-56`, `SimCruiseWorldStreamer.h:78-81` (estimate, needs a run).
2. P1: the landscape mask is a fixed 550 m, but the tile edge moves with latitude. This leaves a gap or step, and a 60 m ground jump on install in hilly places. File: `SimCruiseRegionalTerrain.cpp:68-78`.
3. P1: trees, shrubs and palms pop. There is no fade, all 3 LODs overlap near the camera, there are hard cuts at 33/92/600 m and at 250 m for shrubs, a whole tile's trees appear after the ground, and every refresh is a synchronous rebuild (hitch risk). File: `SimCruiseTrees.cpp:425-447, 563-584, 780-787`.
4. P1: traffic spawns in plain view at 160-260 m and vanishes in view at 320 m. This shows on every long straight (Rawlins, Delta, Casselton). File: `SimCruiseTraffic.cpp:776, 898`.
5. P1: repetition and wrong density at data-poor places: 1 broadleaf mesh with a fixed tint, 20 cars everywhere, and Generic buildings. Files: `SimCruiseTrees.cpp:228, 374`, `SimCruiseTraffic.cpp:25`, `SimCruiseTileDetail.cpp:219`.
6. P2: the pre-flight cannot see the pops above, because its range is 150 m and it checks only tiles and cars. File: `SimCruisePreflight.h:96`. Night: the street-light pool boundary turns lights on and off in one step (`Streamer.cpp:469-480`).

**My first three actions.**
1. Measure the forward reach. Log the distance from the camera to the world edge each second, and the tile build time, in `SimCruisePreflight.cpp`. Raise `PopInRangeM` for tiles to 1000 m. Done when: `scripts\preflight.ps1 -Roads us-rawlins-wy,us-casselton-nd,us-anchorage-ak -Seconds 120` reports the minimum reach and the p95 pop distance on a cold cache. Then propose the fix: request the 3 x 3 around the predicted tile as well, or make `RingRadius` grow with speed. Pass: the reach never goes under 300 m at 80 mph, and no tile pops closer than the haze 50 % distance.
2. Tie the landscape mask and sink to the tile size. Pass `TileSizeM` into `LandscapeMask` and `LandscapeOffset` (`SimCruiseRegionalTerrain.cpp:68-78`): mask radius = 0.95 x tile, sink ending near 1 tile. Done when: the view-check frames at `us-welch-wv`, `us-seattle-wa` and `us-anchorage-ak` show no gap or step at the ring edge, and `-Roads random` shows 0 sky pixels below the horizon.
3. Remove the tree and traffic pops at no GPU cost. Put each tree in exactly 1 ring by distance at `Refresh`, and add `PerInstanceFadeAmount` dither to the leaf and bark materials (0.5-2 s or about 20 m of fade). Add a per-instance hue and brightness jitter of about 10 %. In `Spawn`, block any spawn inside the frustum at every distance, and remove a car at 320 m only when it is out of view. Done when: `scripts\preflight.ps1 -Roads random,us-jasper-ar,us-rawlins-wy` shows 0 car pops in view within 400 m, a frame-pair diff finds no tree pop over 20 px, and frame p95 does not rise.

**Docs that are wrong or old.**
- `docs/team/magician.md` 3.2 and `docs/team/how-the-game-works.md` say tiles are "about 550 m" and the far edge is "551-1102 m". That is true only at 25.8 N. It is 418 m at 47 N and 294 m at 61 N.
- `SimCruiseRegionalTerrain.cpp:76` says "the driving tiles are the ground there" (within 550 m). This is false north of about 26 N.
- `SimCruiseTrees.cpp:424` says "Grove.gd: 33 m, 92 m, 485 m", but `Far[2]` is 60000 cm (600 m). The same comment implies near culls, which Unreal HISM does not do. The P1 overlap in the last review (`docs/team/review-2026-11-12/magician.md` item 1) is still open.
- `docs/unreal-architecture.md:759` says cars spawn "out of the camera's view". The code hides only cars closer than 160 m (`SimCruiseTraffic.cpp:776`).

Sources: Epic, `SetCullDistances`, where the start distance is a fade start (https://docs.unrealengine.com/4.26/en-US/API/Runtime/Engine/Components/UInstancedStaticMeshComponent/SetCullDistances/index.html). Koschmieder k = 3.912/V, as used in `SimCruiseDaylightModel.cpp:127`. Simons and Rensink 2005: a change without a mask is seen (https://www.cs.ubc.ca/~rensink/flicker/abs.05.0a.html).
