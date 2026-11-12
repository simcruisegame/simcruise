# Performance engineer: study notes

Role: `performance-engineer`. Owner of frame time, frame pacing and hitches on the target PC.
Written 2026-11-12 for SimCruise (Unreal Engine 5.8, C++).
Style: ASD-STE100 Simplified Technical English. Facts marked **[unverified]** were not confirmed from a primary source in this session.

## 1. Mandate

- Keep the drive smooth on the owner's laptop GPU at its 28 W power cap.
- Measure first. Change one thing. Measure again.
- Rank hitches above average fps. A driver feels a 50 ms hitch more than a 10 % lower average.
- Phase rule: the current phase is bugs and driving experience only. Do not propose new features unless the caller asks.

## 2. Target hardware and budget

- GPU: NVIDIA RTX 2070 Super with Max-Q Design, 8 GB, i9-10885H (`docs/unreal-architecture.md:424`).
- The owner runs the GPU at a 28 W power cap (`docs/place-graph.md:87`, `docs/unreal-architecture.md:767`).
- Measured: at the 28 W cap "everything ran about 2.6 times slower" than at about 90 W (`docs/unreal-architecture.md:767`).
- Rule: all published frame-time tables in the repo are at about 90 W. Multiply GPU time by about 2.6 for the target. Re-measure; do not trust the factor for every pass.
- Budget at 60 Hz: 16.6 ms for each of game thread, render thread and GPU. Keep 10-15 % margin: aim for GPU p95 <= 14.5 ms.
- Budget at 30 Hz: 33.3 ms. A stable 30 is better than an unstable 40-55 for driving. [engineering judgement]
- Hitch definition used here: a frame > 2x the target frame time (> 33 ms at 60 Hz). Target: zero streaming hitches per 5 km of driving.

## 3. Key knowledge

### 3.1 Measure

- `stat unit`: Frame, Game, Draw (render thread), RHIT, GPU in ms. The highest one is the bound. [Epic stat commands docs]
- `stat unitgraph`: shows spikes over time. Use it while you drive.
- Unreal Insights (`-trace=default,gpu,frame` or `trace.start`): frame-by-frame timing of every thread. Use it for hitches. [Epic Timing Insights docs]
- `ProfileGPU` (`r.ProfileGPU`): one frame, per pass. Use it for steady GPU cost.
- `stat streaming`, `stat physics`, `stat chaos`: streaming and physics cost.
- Epic: "there is no substitute for profiling directly on your target hardware".
- SimCruise sampler: `FSimCruisePerfSampler` writes `Saved/Profiling/SimCruisePerf.csv` (frame avg/p50/p95/p99, GPU avg/p95, game, render). `-SimCruiseDrivePerf` runs it on the drive.
- Use Development or Test builds, not the editor in PIE, for numbers. Close other Unreal processes (they share the GPU).

### 3.2 Rendering costs (UE 5)

- TSR: renders below output resolution and upscales. Cost scales with output resolution and history size. `r.TSR.History.ScreenPercentage=100` saves cost at 1080p. [Epic TSR docs]
- Lumen software GI and reflections: largest single block in the High frame here (GI 1.9 ms at 90 W).
- Virtual shadow maps: non-Nanite dynamic geometry invalidates pages every frame. Static draw path lets VSM cache pages.
- Volumetric cloud shadow map: cost follows map size and ray steps. SimCruise cut it from 2.1 ms to 0.56 ms (`Config/DefaultScalability.ini`).
- Nanite: car bodies use it. The streamed tiles do not (runtime meshes).
- Mesh distance fields: needed by software Lumen. Runtime tiles have none, so Lumen does not see the street in reflections.

### 3.3 CPU and threads

- Game thread: actor ticks, traffic, streaming installs, UI.
- Render thread and RHI thread: draw submission.
- Physics: SimCruise runs Chaos async at a fixed 360 Hz step (`Config/DefaultEngine.ini:89-90`). The vehicle sim runs in `OnPreSimulate_Internal` on the physics thread.
- Rule: async physics decouples sim rate from frame rate. A long game-thread frame makes the physics thread run many steps to catch up.
- Rule: scene queries from outside the game thread are not supported in UE 5 according to an Epic forum answer ("Doing scene queries in threads outside the game thread ... is not supported yet"). SimCruise traces the ground from the physics thread. Watch for races with tile install, removal and rebase. [Epic forum, not official docs]

### 3.4 Streaming hitches

- Creating render resources, physics bodies and actors on the game thread costs ms per object.
- Collision cooking can be async (`bUseAsyncCooking`). The copy of mesh data into the cooker still runs on the game thread.
- Time-slice work: spread one big install over several frames with a ms budget per frame.
- SimCruise measured tile installs at 30-90 ms on the game thread (`docs/unreal-architecture.md:707`).

### 3.5 Frame pacing

- An uncapped frame rate on a fixed 60 Hz panel without VRR gives judder or tearing.
- Cap with `t.MaxFPS` or `UGameUserSettings::SetFrameRateLimit`, and use VSync or VRR. A cap also saves power, and power is the limit at 28 W.
- Even pacing matters more than the average for camera motion at speed.

### 3.6 Scalability

- Unreal scalability groups: `sg.ResolutionQuality`, `sg.ViewDistanceQuality`, `sg.ShadowQuality`, `sg.GlobalIlluminationQuality`, `sg.ReflectionQuality`, `sg.PostProcessQuality`, `sg.TextureQuality`, `sg.EffectsQuality`, `sg.FoliageQuality`, `sg.ShadingQuality`. Overrides go in `DefaultScalability.ini` per level (`[Group@N]`).
- SimCruise presets Low/Medium/High/Ultra map to levels 0-3, with 50/58/67/75 % screen percentage and TSR (`Private/Environment/SimCruiseGraphics.cpp:60-90`).
- Auto-pick uses the adapter name. It can not see a power cap (`SimCruiseGraphics.cpp:91-150`).
- Dynamic resolution (`r.DynamicRes.OperationMode`) can hold a GPU budget by changing screen percentage. Not enabled in SimCruise. [check before enabling: interaction with TSR history]

## 4. How to judge SimCruise (checks)

| # | Check | Threshold | Method | Where |
|---|---|---|---|---|
| P1 | Default preset on target | GPU p95 <= 14.5 ms at 28 W | `-SimCruiseDrivePerf` at 28 W, per preset, seat and chase views | `SimCruiseGraphics.cpp:91-150` |
| P2 | Streaming hitches | 0 frames > 33 ms per 5 km at 90 km/h | Insights trace on a drive across 8+ tiles | `SimCruiseWorldStreamer.cpp:482-555`, `SimCruiseWorldTile.cpp:150` |
| P3 | Frame pacing | frame time std dev < 1 ms at a stable cap | `stat unitgraph`, CSV | `Config/*.ini` (no cap today) |
| P4 | Game thread | <= 8 ms with 20 traffic cars | `stat unit`, Insights | `SimCruiseTraffic.cpp:828-921` |
| P5 | Physics thread | vehicle step <= 0.3 ms at 360 Hz | `StepSecondsTotal` telemetry, `stat chaos` | `SimCruiseVehicleMovementComponent.cpp:286-296` |
| P6 | Rebase event | no hitch > 33 ms, no wheel contact loss | Drive across 2 km, log | `SimCruiseWorldStreamer.cpp:343-355` |
| P7 | Sampler quality | reports max frame and hitch count | Read CSV | `SimCruisePerfSampler.cpp:52-108` |
| P8 | VRAM | < 7 GB on 8 GB card after 30 min | `stat rhi`, `memreport` | streaming, aerials |
| P9 | Docs match config | tables equal ini values | Read | `docs/unreal-architecture.md:415-433`, `DefaultScalability.ini` |

Method rules:
- Run each measurement twice. Discard a run that differs from its twin by > 10 %.
- Record the power state: on mains or battery, the cap, the driver version.
- Measure at the Ocean Drive start and on a long drive. A parked camera hides streaming cost.
- Put each result in the CSV and in `docs/unreal-architecture.md` with the date.

## 5. Common failure modes

- Tuning on 90 W and shipping to 28 W.
- Average fps reported, hitches not reported.
- A one-per-frame install that is itself 30-90 ms.
- Actor spawns in bursts (traffic, parked cars, palms) on the same frame as a tile install.
- Uncapped frame rate that heats the GPU, then the clock drops, then fps swings.
- Physics-thread work that reads game-thread state while the game thread changes it.
- A preset table in docs that no longer matches the ini.
- Editor or PIE numbers used as game numbers.

## 7. Worked numbers for SimCruise

Use these numbers as quick checks during a review. All "at 28 W" values are estimates: 90 W measurement x 2.6.

| Preset | Seat view at about 90 W (GPU) | Estimate at 28 W (GPU) | Meets 16.6 ms? |
|---|---|---|---|
| Medium (58 %) | 10.2 ms | about 26.5 ms | no |
| High (67 %, auto-pick default) | 13.7 ms | about 35.6 ms | no (about 28 fps) |
| Ultra (75 %) | 21.5 ms | about 56 ms | no |
| Low (50 %) | not measured on the drive (lab: 3.2 ms) | lab estimate about 8.3 ms | probably, measure |

Source of the 90 W values: `docs/unreal-architecture.md:775-782` and `:426-429`.

- High frame breakdown at about 90 W: Lumen GI 1.9 ms, VSM 2.1 ms (1.0 ms non-Nanite tile meshes), TSR 1.7 ms, clouds 0.85 ms, cloud shadow 0.56 ms, base pass 0.5 ms (`docs/unreal-architecture.md:783`).
- Tile install on the game thread: 30-90 ms each (`docs/unreal-architecture.md:707`). At 90 km/h the car crosses a 550 m tile in 22 s. A new ring row is 3 tiles. Expect 3 hitches of 2-6 frames every 22 s.
- Physics: 360 Hz fixed step. At 60 fps that is 6 steps per frame. A 90 ms hitch asks for about 32 steps.
- Vehicle ground rays: 4 wheels x 3 rays x 360 Hz = 4 320 traces per second.
- Traffic: 20 cars by default (`SimCruiseTraffic.cpp:25`), each a presentation actor with Nanite parts and an engine voice within 40 m.
- Parked cars: about 800 on the four start tiles, spawned 3 per frame (`docs/unreal-architecture.md:763`, `SimCruiseWorldStreamer.cpp` Tick).

## 8. Review routine

1. Read the open findings of the last review.
2. Read the diff since the last review in `Environment/`, `World/`, `Config/`.
3. Ask for, or read, the newest `SimCruisePerf.csv` rows. Note the power state of each row.
4. Run checks P1-P9 that the diff touches.
5. For each finding, give file:line, the driving impact, a fix and a measurement with a pass threshold.
6. Do not change code. Report to the caller.

## 8a. What "smooth" means for a driver

- The eye tracks the road edge and the horizon. A repeated or skipped frame shows as a jerk in the road edge.
- A hitch in a corner disturbs steering. The driver reacts to a picture that is 33-100 ms late.
- Input latency adds to this. A capped, paced 60 fps with low latency is the goal. A stable 30 fps is the fallback on Low at 28 W.
- Audio must not stall in a hitch. Check the engine sound across a tile install.

## 9. References

- Epic, Introduction to performance profiling and configuration: https://dev.epicgames.com/documentation/unreal-engine/introduction-to-performance-profiling-and-configuration-in-unreal-engine
- Epic, Stat commands: https://dev.epicgames.com/documentation/unreal-engine/stat-commands-in-unreal-engine
- Epic, Timing Insights: https://dev.epicgames.com/documentation/unreal-engine/timing-insights-in-unreal-engine-5
- Epic, Guidelines for optimizing rendering for real time: https://dev.epicgames.com/documentation/unreal-engine/guidelines-for-optimizing-rendering-for-real-time-in-unreal-engine
- Epic, Temporal Super Resolution: https://dev.epicgames.com/documentation/unreal-engine/temporal-super-resolution-in-unreal-engine
- Epic, TSR FAQ: https://dev.epicgames.com/documentation/unreal-engine/temporal-super-resolution-frequently-asked-questions-for-unreal-engine
- Epic, Anti-aliasing and upscaling: https://dev.epicgames.com/documentation/unreal-engine/anti-aliasing-and-upscaling-in-unreal-engine
- Epic, Physics settings (async tick, fixed step): https://dev.epicgames.com/documentation/unreal-engine/physics-settings-in-the-unreal-engine-project-settings
- Epic forum, line traces in the async physics tick are not supported outside the game thread: https://forums.unrealengine.com/t/uprimitivecomponent-linetracecomponent-in-async-physics-tick/2349680
- Epic forum, broadphase types and streaming performance: https://forums.unrealengine.com/t/broadphase-types-and-streaming-performance/2690235
- Unreal Fest and GDC talks on Lumen, Nanite and VSM performance (Epic YouTube channel). [not fetched in this session]
- Project files: `docs/unreal-architecture.md` ("Light, sky and rendering", "Frame time on the drive", "Tiles draw as static meshes"), `Config/DefaultScalability.ini`, `Config/DefaultEngine.ini`.
