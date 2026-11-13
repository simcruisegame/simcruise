# Magician review, 2026-11-12

Reviewer: `magician` (perception and illusion).
Scope: what the driver notices while driving: pop-in, LOD swaps, streaming, spawns, light pops, repetition, speed feel.
Inputs: repo at origin/main (2026-11-12), captures in the project files folder (ocean.png, buildings-*.jpg, nb2001_*.png, palms-wind.jpg).
Phase: bugs and driving experience. Proposals for the next phase are in the last section, separate from the findings.

## Findings (ranked)

### 1. P1 | All three tree LOD rings draw from 0 m: near trees show three overlapping crowns
Evidence: `Source/SimCruise/Private/World/SimCruiseTrees.cpp:335-337` sets `SetCullDistances(Near[Level], Far[Level])` with Near = {0, 3300, 9200} cm. `SimCruiseTrees.cpp:472-475` adds every tree to all three level rings.
Unreal HISM start cull distance is the fade start, not a near cull: "Sets the fading start and culling end distances" (Epic API, SetCullDistances). So ring 1 draws from 0 to 92 m and ring 2 from 0 to 600 m. The Godot source (Grove.gd, `visibility_range_begin`) did hide near instances; the port kept the numbers but not the meaning.
What the driver sees: near trees have the full LOD0 crown plus the LOD1 and LOD2 crown shells inside or around it. Masked two-sided foliage is drawn three times (overdraw on the 28 W GPU). Beyond 33 m the LOD0 crown vanishes with a hard cut (no fade material), so the tree changes shape at a fixed distance in plain view.
Fix: give each instance to one ring only, by distance, or use one mesh with real static-mesh LODs (`LODScreenSize`) and dithered LOD transition. Smallest fix: keep the three rings but add per-instance fade in the leaf/bark materials (`PerInstanceFadeAmount`) and set start/end per ring so they cross-fade, and cull near instances of ring 1/2 on the CPU at `Refresh` time around the car.
Verify: stand under a tree; `ShowFlag.InstancedFoliage`/hide rings 1-2 by console; count triangles with `stat RHI` before/after; screenshot diff of the same tree at 10 m with rings 1-2 hidden must be identical after the fix. Unreal Insights: base pass time for foliage.
Confidence: verified in code; engine semantics from Epic docs. Visual effect inferred (not run here).

### 2. P1 | Whole 551 m tiles appear in view without a fade
Evidence: `Source/SimCruise/Public/World/SimCruiseTileManager.h:30` `RingRadius = 1` (3 x 3). `SimCruiseTileManager.cpp:46-55` requests only inside the ring; the 5 s prediction only orders requests. `SimCruiseWorldStreamer.cpp:547` `Tile->Install(...)` shows the tile at once. Tile size at 25.8 deg N: 40 075 km / 65 536 × cos(25.8°) ≈ 551 m (`SimCruiseGeo.cpp:25`, `SimCruiseGeo.h:31`).
What the driver sees: on a long straight (Ocean Drive, Collins Avenue) the far end of the world is 551-1 102 m ahead. A new tile (road, buildings, palms) appears there in one frame. With 20 km visibility the haze hides only about 10 % at 551 m (Koschmieder). The vanishing point of a straight road is where the eye rests, so this is near the gaze.
Fix: fade a new tile in over 1-2 s (a material parameter on the tile's MIDs, as `SetLightsOn` already does with "Lights"), or install a tile only when its nearest edge is outside the camera frustum or beyond a distance where `Haze >= 0.5`. Second: request the next row in the travel direction one tile early (ring 2 forward only).
Verify: pop-in button test on Ocean Drive at 25 mph and 45 mph; frame-pair diff on the drive capture. Goal: no new tile visible in frame without a fade.
Confidence: verified in code; appearance inferred.

### 3. P1 | Street light pool reassigns 24 lights every 0.25 s with no fade
Evidence: `Source/SimCruise/Private/World/SimCruiseWorldStreamer.cpp:451-478`. Lamps within 250 m are sorted by distance to the focus (`:463`), the nearest 24 get lights (`:472-477`), `SetVisibility(bUse)` switches a light on or off at once.
What the driver sees: at night, a pool of light on the road ahead appears in one frame when a lamp enters the nearest 24, and the lamp behind the car still keeps its light. Distance ignores direction, so about half the pool lights the road behind the car. Luminance onset near the gaze is the most visible change type.
Fix: score lamps by distance in front of the camera (weight lamps in the view frustum and ahead of travel), add hysteresis (keep a lamp until it falls below rank 28), and fade intensity in and out over 0.5-1 s instead of `SetVisibility`.
Verify: night drive at 30 mph on Ocean Drive, record 60 s; count frames where a road light pool turns on in view. Goal: zero hard onsets.
Confidence: verified in code; capture buildings-night-edison-cafe.jpg shows the pools.

### 4. P2 | Tree and palm HISMs are cleared and rebuilt on every tile change
Evidence: `SimCruiseTrees.cpp:677-683` calls `Refresh()` when the ready set changes; `Refresh` (`:375-480`) calls `ClearInstances()` on all rings and re-adds all instances of all shown tiles (`:477-485`).
What the driver sees: speculative. Each tile arrival rebuilds every tree instance (thousands on Ocean Drive). Risks: a game-thread hitch, and a frame where HISM clusters are rebuilt (trees may flicker or draw unculled for a frame).
Fix: add and remove only the instances of the tile that changed (one HISM per tile, or instance index ranges per tile).
Verify: Unreal Insights capture across a tile install; game thread spike must stay under 2 ms; visual check for a flicker frame.
Confidence: verified that full rebuild happens; hitch and flicker are speculative.

### 5. P2 | Parked cars spawn with no sight test
Evidence: `SimCruiseWorldStreamer.cpp:364-369` places 3 parked cars per frame across all tiles. `SimCruiseWorldTile.cpp:449-496` spawns them with no frustum or distance test. Traffic has a sight test (`SimCruiseTraffic.cpp:775-776`).
What the driver sees: at the start of a drive, after a recover, or when a near tile installs, parked cars appear one by one along the kerb in view. Captures ocean.png and buildings-ocean-drive-heights.jpg show dense kerbside rows, so the pop is large.
Fix: reuse the traffic sight test; place parked cars first that are out of view or behind the camera; place in-view ones only during a mask (view change, recover fade) or with a fade.
Verify: start a drive on Ocean Drive with the drive capture; frame-pair diff on the first 5 s. Goal: no car appears in view.
Confidence: verified in code.

### 6. P2 | Lit windows are uniform flat white at night
Evidence: `Source/SimCruise/Private/World/SimCruiseWorldResources.cpp:284-285` picks lit rooms by one threshold (`lit = step(r, 0.36)`) and one "Lights" amount. Captures buildings-night-breakwater.jpg and buildings-night-edison-cafe.jpg: every lit window is the same clipped warm white.
What the driver sees: rows of identical white rectangles. The eye reads the pattern as a diagram.
Fix: use the existing random `r` to vary each room's intensity (0.3-1.0) and colour temperature (2700-4000 K, a few cool 5000-6500 K), and keep peak below display white.
Verify: histogram of a night capture: fewer than 1 % of window pixels at 255; blind "photo or game" test against a licensed night photo.
Confidence: verified in code and captures.

### 7. P2 | Night sky is a black void
Evidence: captures buildings-night-*.jpg: sky RGB ≈ 0. `SimCruiseWorldStreamer.cpp:398` sets sky glow to at most 0.06 cd/m², about 7 stops below the night exposure's mid grey (`SimCruiseDaylightModel.cpp:111-116`, band at EV100 ≈ 3.6-6.1 with low beams).
What the driver sees: buildings have no silhouette against the sky. The world ends at the roof line.
Fix: see cinematographer finding 6 (day-for-night lift of the sky floor). This is a look choice, not a physics error.
Verify: night capture: sky mean above the darkest roofs by at least 3 code values.
Confidence: verified in captures; cause inferred.

### 8. P2 | Palm repetition in wide views
Evidence: `SimCruiseTrees.cpp:385-399` one HISM per palm variant; yaw from a hash, scale clamped 0.6-1.5. Capture buildings-ocean-drive-heights.jpg shows the same palm silhouette many times with similar lean.
What the driver sees: from the seat this is mostly peripheral (low risk). In the chase and scenic views and captures the repeat is easy to spot.
Fix: add a small random lean (±5° pitch/roll) and non-uniform scale (crown ±10 %) per instance.
Verify: 5 s repetition test on the aerial capture.
Confidence: inferred from capture.

## Not checked

- Traffic despawn behaviour in view.
- Building and sign LOD inside tiles (`SimCruiseTileDetail.cpp`).
- Shadow pop (virtual shadow map page updates) and Lumen surface cache pop.
- Any test in a real build: all items are from code and captures.

## Next phase proposals (ranked)

These are proposals for the phase after the driving release. Do not start them now.

### N1. Magician's streamer: swaps only when nobody can see (domain-ideas idea 4)
- Goal: no visible pop-in in normal driving.
- Why: "drive your favourite real roads" fails the moment the world visibly assembles itself. Pop-in breaks presence more than low detail does.
- Scope: one "swap scheduler" for tiles, LOD, parked cars, street lights and aerial photos. Rules: out of frustum first; else fade 0.5-2 s; else wait for a mask (view change, recover fade, overpass shadow, a large passing vehicle). No new art.
- Acceptance test: pop-in button test with 10 players, 10 min each, Ocean Drive day and night. Masked or faded swaps detected at less than half the rate of today; zero hard light onsets.
- Risk: fades cost a little GPU (dithered masked materials); waiting for masks can delay detail. Keep a deadline.
- Size: M.

### N2. Honest speed gate (domain-ideas idea 5)
- Goal: the driver judges speed within 5 % without the speedometer.
- Why: "feel exactly like the real car" includes feeling the speed. Simulators show about 10 % error at 1:1 FOV (Diels and Parkes 2010).
- Scope: measure first. Then tune only existing levers: lens FOV, road texture frequency, roadside object density, head vibration, sound level.
- Acceptance test: hold 20/30/50/70 mph with the speedometer hidden, 8 drivers, two lenses. Mean error under 5 %.
- Risk: the laptop's GFOV/FOV ratio (≈ 2.4:1) is outside the published range; results may differ from the paper.
- Size: S to measure, M to tune.

### N3. Felt detail where the eye goes
- Goal: the near road and kerb read as real at a glance.
- Why: drivers look at the lane, the road edge and the car ahead. Detail there sells the whole street (inattentional blindness covers the rest).
- Scope: variation, not resolution: road wear and patch decals, kerb dirt, base-of-wall darkening, window light variation, lean and scale variation of palms.
- Acceptance test: blind "photo or game" test (d') on the same spots against licensed photos (Panoramax CC BY-SA); d' must fall by 30 %.
- Risk: GPU budget; decal count.
- Size: M.

### N4. Gaze-weighted detail budget
- Goal: spend LOD and shadow budget on the road ahead and the bend, less on the periphery.
- Why: the 28 W GPU cannot draw everything; the periphery sees flicker, not detail.
- Scope: a predicted gaze map from road geometry (tangent point, car ahead) to scale LOD distances and shadow detail. No eye tracker needed.
- Acceptance test: GPU time saved ≥ 15 % at equal "photo or game" score.
- Risk: wrong prediction makes the gaze land on low detail.
- Size: L.
