# Review 2026-11-12: performance-engineer

Scope: Environment/SimCruiseGraphics.cpp, SimCruisePerfSampler.cpp, Config/*.ini scalability, World/* streaming and traffic, the physics-thread vehicle step.
Method: code and config reading, the measured numbers in `docs/unreal-architecture.md`. No build or run. Values "at 28 W" are estimates: the 90 W measurement x 2.6, the factor the repo measured on 11 November (`docs/unreal-architecture.md:767`).

Checks that passed:
- Tiles draw through the static draw path so virtual shadow pages cache (`SimCruiseWorldTile.cpp:160-188`).
- Cloud shadow and sky-occlusion budgets cut 1.5 ms at High (`Config/DefaultScalability.ini`).
- Ultra uses a 100 % TSR history (`DefaultScalability.ini`, `[AntiAliasingQuality@3]`).
- Tile builds run on worker threads; the game thread only wraps prepared render data.
- Neural detail runs on the CPU by default so the capped GPU keeps its watts (`Config/DefaultEngine.ini:92-96`).
- Physics runs async at a fixed 360 Hz (`Config/DefaultEngine.ini:85-90`).

## Findings (ranked)

### P-01 P0 The default preset is about 2x over the frame budget on the 28 W target [estimate]
Evidence: auto-pick sees "max-q" and steps the RTX 2070 tier down to High (`Private/Environment/SimCruiseGraphics.cpp:104, 133`). High in the driver's seat measured 13.7 ms GPU at about 90 W (`docs/unreal-architecture.md:780`); x 2.6 at 28 W gives about 35.6 ms (about 28 fps). Medium gives about 26.5 ms. The adapter name can not show the power cap.
Impact on driving: the owner's default drive runs at about 28 fps with GPU-bound frame times that swing with the scene. Steering and camera motion feel late and uneven.
Proposed fix: (1) measure every preset at the 28 W cap on the drive (seat and chase). (2) Change the laptop auto-pick to the preset that meets 16.6 ms at 28 W, probably Low, or Medium at a 30 fps cap. (3) Record the result in `docs/unreal-architecture.md`. Do not rely on the 2.6 factor for the final choice.
How to verify: `-SimCruiseDrivePerf` at 28 W, per preset, 2 runs each. Pass: the default preset has GPU p95 <= 14.5 ms (60 Hz) or frame p99 <= 33 ms with a 30 fps cap.

### P-02 P1 Each tile install holds the game thread 30-90 ms
Evidence: `docs/unreal-architecture.md:707` ("installs stay 30-90 ms"). One install per frame (`SimCruiseWorldStreamer.cpp:513-547`); `ASimCruiseWorldTile::Install` creates the static mesh, the collision mesh sections, palms, signs and lights in one call (`SimCruiseWorldTile.cpp:150-300`). Parked cars then spawn 3 per frame (`SimCruiseWorldStreamer.cpp` Tick).
Impact on driving: at 90 km/h the car crosses a 550 m tile in about 22 s and a new ring row of 3 tiles streams in. Expect three hitches of 2-6 frames every 22 s. Hitches in a corner disturb steering.
Proposed fix: time-slice the install with a per-frame budget (for example 4 ms): frame 1 static mesh, then one collision section per frame (terrain last, after the road), then palms and props, then lights. Keep the tile's collision-ready gate (`IsCollisionReady`) so the car never drives onto a tile without road collision.
How to verify: Unreal Insights trace of a 5 km drive at 90 km/h over 8+ new tiles. Pass: no game-thread frame > 25 ms from streaming; zero frames > 33 ms.

### P-03 P1 No frame-rate cap, no VSync policy, no frame pacing
Evidence: no `t.MaxFPS`, `r.VSync`, `bUseVSync`, `FrameRateLimit` or `bSmoothFrameRate` in `Config/` or `Source/` (grep). Measured rates vary from 45 to 107 fps by view and preset (`docs/unreal-architecture.md:778-781`).
Impact on driving: on a 60 Hz laptop panel, an uncapped 70-90 fps gives tearing or judder. An uncapped GPU also burns the 28 W budget and heats, so clocks and fps swing more.
Proposed fix: set a cap that matches the display (60) and VSync on by default, with a 30 fps cap option for Low at 28 W. Put it in the game user settings so the player can change it.
How to verify: `stat unitgraph` and the sampler CSV over 60 s of driving. Pass: frame-time standard deviation < 1 ms at the cap; no tearing on video.

### P-04 P1 Physics-thread ground traces race with game-thread world changes [speculation, needs a test]
Evidence: the vehicle step runs in `OnPreSimulate_Internal` (`SimCruiseVehicleMovementComponent.cpp:313`) and calls `World->LineTraceSingleByChannel` (`:262-283`). Meanwhile the game thread installs and destroys tiles (`SimCruiseWorldStreamer.cpp:334-341, 547`), moves 20 traffic cars with collision boxes every frame (`SimCruiseTraffic.cpp:672`) and rebases (`:343-355`). An Epic forum answer says scene queries outside the game thread are "not supported yet".
Impact on driving: rare wrong wheel contact (a jolt), or a rare crash in the query structure. Hard to reproduce; worst near streaming and rebase.
Proposed fix: measure first. Options in order: (a) do not change the scene while physics steps that use it are in flight (apply tile installs, removals and rebases at a physics sync point); (b) trace only the tile components the wheel can reach with `LineTraceComponent` on a snapshot list owned by the physics state; (c) rebase rarely (see GIS finding G-05).
How to verify: a stress test: drive in circles over a tile edge while the streamer evicts and re-installs neighbours and rebases every 10 s, with `-stompmalloc` in a Development build. Pass: no crash, 4 wheel contacts every step.

### P-05 P2 The perf sampler hides hitches
Evidence: `SimCruisePerfSampler.cpp:52-63, 99-108`: it reports average, p50, p95, p99. No maximum frame, no count of frames over budget, no hitch timestamps. The first sample may include the frame that started the window.
Impact on driving: a 10 s window with three 60 ms hitches has p99 near 60 ms but p95 normal; a window with one hitch can pass. Decisions use averages.
Proposed fix: add `frame_max`, `frames_over_33ms`, `frames_over_2x_median` to the log line and CSV. Skip the first frame.
How to verify: inject a 100 ms sleep in a test window; the CSV shows `frame_max >= 100` and one frame over budget.

### P-06 P2 Traffic count is fixed at 20 for every preset
Evidence: `simcruise.Traffic.Cars` default 20 (`SimCruiseTraffic.cpp:25`). The Godot client scaled 6/12/20/32 by tier (`docs/unreal-migration-ledger.md:335`). Each car is a presentation actor with Nanite parts and Lumen cards; the tick has several O(N^2) loops (`SimCruiseTraffic.cpp:375, 433, 498, 540, 738, 785`).
Impact on driving: cost the target can not afford at Low. Cost is unmeasured.
Proposed fix: measure game and GPU time at 0, 10, 20 cars. Then set the count per preset (for example 8/12/20/28).
How to verify: `stat unit` and `ProfileGPU` with each count at the start; pass when the chosen Low count keeps the frame within budget.

### P-07 P2 Traffic cars vanish in view at 320 m
Evidence: removal at `RemoveM = 320 m` with no visibility check (`SimCruiseTraffic.cpp:33, 876`). Spawning does check sight (`:776-777`).
Impact on driving: on long straight roads (causeways, Collins Avenue) the car ahead disappears. It breaks immersion.
Proposed fix: remove only when out of the camera's view or beyond the fog/draw distance; keep the 320 m rule for cars behind.
How to verify: test with a fixed camera on a straight road: count removals with the car in the view frustum. Pass: zero.

### P-08 P2 Docs disagree with the ini
Evidence: `docs/unreal-architecture.md:419-422` gives High cloud shadow 384/24; `Config/DefaultScalability.ini` `[ShadowQuality@2]` sets 192/12. The Ultra drive frame time "has not been measured" (`docs/unreal-architecture.md` Known gaps) but the drive table now has it (45 fps).
Impact on driving: none directly. Wrong data for the next tuning pass.
Proposed fix: update the table and the gap list.
How to verify: read both.

## Next phase proposals

### N-01 28 W frame budget and pacing contract (rank 1, size M)
Goal: every preset has a measured, documented budget at 28 W, and the default meets it with a capped, paced frame rate.
Why for the vision: "the ultimate driver's experience" on the owner's machine needs smooth motion before more detail.
Scope: automated `-SimCruiseDrivePerf` route (start, a 5 km drive, seat and chase), run at 28 W; per-preset tables; cap and VSync policy; optional dynamic resolution with TSR inside a GPU budget; auto-pick that uses a short first-run measurement.
Acceptance test: CSV from two runs per preset at 28 W; default preset frame p99 <= 33 ms at a 30 cap or GPU p95 <= 14.5 ms at a 60 cap; frame-time std dev < 1 ms.
Risk: dynamic resolution and TSR can shimmer; measure image quality too.

### N-02 Hitch-free streaming (rank 2, size M)
Goal: no streaming hitch over 25 ms while driving at 130 km/h.
Why for the vision: real roads anywhere means continuous streaming; a hitch in a corner hurts driving more than lower detail.
Scope: time-sliced install (P-02), spread parked-car and traffic spawns, apply scene changes at physics sync points (P-04), a look-ahead that requests tiles in the direction of travel early enough at 130 km/h.
Acceptance test: Insights trace of a 10 km drive at 130 km/h on bundled and helper tiles: zero frames > 33 ms, max game thread 25 ms.
Risk: more frames between "tile arrives" and "collision ready"; the gate must hold the car's road.

### N-03 Physics-thread safety for the ground query (rank 3, size S-M)
Goal: wheel traces never read a scene that the game thread is changing.
Why for the vision: the tyres are the car's only contact with the real road.
Scope: snapshot of tile collision components for the physics thread, or scene changes at sync points; a stress test.
Acceptance test: stress test of P-04 passes 30 min without crash or contact loss.
Risk: engine internals; follow Epic guidance for async physics.

### N-04 Per-preset world density (rank 4, size S)
Goal: traffic, parked cars, palms and lights scale with the preset.
Why for the vision: keep the world alive at Low without breaking the budget.
Scope: a table of counts per preset, measured.
Acceptance test: Low at 28 W meets N-01 with traffic on.
Risk: a sparse world at Low; tune with the owner.

## Sources

- Epic, performance profiling introduction: https://dev.epicgames.com/documentation/unreal-engine/introduction-to-performance-profiling-and-configuration-in-unreal-engine
- Epic, stat commands: https://dev.epicgames.com/documentation/unreal-engine/stat-commands-in-unreal-engine
- Epic, Timing Insights: https://dev.epicgames.com/documentation/unreal-engine/timing-insights-in-unreal-engine-5
- Epic, Temporal Super Resolution: https://dev.epicgames.com/documentation/unreal-engine/temporal-super-resolution-in-unreal-engine
- Epic forum on scene queries in the async physics tick: https://forums.unrealengine.com/t/uprimitivecomponent-linetracecomponent-in-async-physics-tick/2349680
