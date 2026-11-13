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
