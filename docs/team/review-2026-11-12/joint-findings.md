# Joint team review: 12 November 2026 (main 4de89c5)

All eleven experts (lighting-director joined the same day) reviewed the current game in five pairs. They read the code,
the car data, the tests, the docs and the captures. **Nobody built or ran the
game for this review.** So every item needs its "verify" step before a fix
is merged. Items marked (estimate) or (speculation) are not measured.

Each role's full review is in this folder (`<role>.md`). This file merges
them into one ranked list for the current phase (bugs + driving
experience). Where two or more experts found the same problem, the item
names all of them.

## Ranked findings for the driving release

| # | Sev | Title | Found by | Evidence | Fix | Verify |
|---|---|---|---|---|---|---|
| 1 | P1 | Physics runs in slow motion when a frame takes more than 33 ms. The 28 W laptop runs near 30 fps, so this can happen often. | game-developer, qa | `Config/DefaultEngine.ini:85-90` (async physics, no `MaxPhysicsDeltaTime`); Epic physics settings page | Set `MaxPhysicsDeltaTime=0.1`; check `MaxSubstepDeltaTime` too | `t.MaxFPS 15`: `phys_steps` = `frame_dt*360` within 1 % |
| 2 | P0 (estimate) | The default graphics preset (High) is about 2x over the frame budget at 28 W | performance | `SimCruiseGraphics.cpp:104,133`; 13.7 ms at 90 W (`docs/unreal-architecture.md:780`) x 2.6 | Measure each preset at 28 W; default to the one that fits | `-SimCruiseDrivePerf` at 28 W, 2 runs per preset |
| 3 | P1 | No frame cap, VSync or pacing; fps swings 45-107 | performance | grep of Config/Source; `docs/unreal-architecture.md:778-781` | Cap 60 + VSync, 30 cap option | frame-time std dev < 1 ms |
| 4 | P1 | Miata (no ABS) locks the REAR brakes first; fixed 64 % front split, no proportioning valve | automotive, physicist | `SimCruiseVehicleSim.cpp:812`; `nb2001_lowered.json`; hand calc in automotive review | Rear knee/slope (proportioning valve) or EBD; ask owner if his car has ABS | New brake-in-turn bench (ISO 7975): fronts reach peak first |
| 5 | P1 | No test brakes hard in a corner, so item 4 was never caught | physicist, qa | `VehicleBenches.cpp:177-191,271` | Add ISO 7975 case: 0.5 g circle, full pedal | Fails today, passes after item 4 |
| 6 | P1 | Head motion and g-meter jitter: physics velocity (changes in 360 Hz steps) divided by render frame time. About ±0.13 g fake noise at 60 fps. | game-developer, cinematographer | `SimCruiseCameraModels.cpp:82-94,245`; `SimCruiseVehicleMovementComponent.cpp:608-611` | Divide by physics time between samples (`SimTimeS`, `:119`) | Test with 5/6/7 steps per frame: head < 1 mm, g < 0.02 g noise |
| 7 | P1 | Manual clutch coupling has no stability cap (k·Δt·Σ1/I = 2.3 in 1st, 3.3 in reverse, limit 2) and never locks (161 rpm slip at peak torque) | physicist, automotive | `SimCruiseVehicleSim.cpp:725-739` | Same cap as the automatic clutch, or implicit solve; stick/slip lock | Rear wheels in the air in 1st: torque does not flip sign; tacho = wheel rpm |
| 8 | P1 | Road banking forced to zero within 30 m of every tile edge (~every 550 m) | gis | `SimCruiseRoadNetwork.cpp:879-882,903` | Blend to shared seam values, do not run out to zero | Synthetic curve across a seam: bank within 0.005 |
| 9 | P1 (estimate) | Seam lift can raise the road ~0.6 m on hills: a crest every ~550 m | gis | `SimCruiseRoadNetwork.cpp:812-822,874-878` | Lift only the low edge, longer fade, grade change ≤ 1 % | Hilly fixture: no extra vertical curvature at seams |
| 10 | P1 | Rural tertiary and minor roads get no banking; bends tip outward at 2 % crown | gis | `SimCruiseRoadNetwork.cpp:48-53` | Bank rural roads (max 8 %), keep crown in towns | R = 80 m rural curve banks inward > 3 % |
| 11 | P1 | Tile install holds the game thread 30-90 ms: a hitch you feel in the steering | performance, magician | `docs/unreal-architecture.md:707`; `SimCruiseWorldStreamer.cpp:513-547` | Split install over frames (~4 ms each) | Insights, 5 km drive: no streaming frame > 25 ms |
| 12 | P1 (speculation) | Wheel ground traces run on the physics thread while the game thread changes the world (tile install/remove, origin rebase). A queued reset also keeps its old position across a rebase. | performance, game-developer | `SimCruiseVehicleMovementComponent.cpp:33,262-283,313`; `SimCruiseWorldStreamer.cpp:343-355`; `SimCruiseGameMode.cpp:636-652` | Apply world changes at physics sync points; shift queued resets with the origin | Cross 5 rebases at 130 km/h: 4 wheel contacts every step |
| 13 | P1 | Recover (View) snaps the camera to the old spot, then it swoops to the new one | game-developer | `SimCruisePlayerController.cpp:604-615`; `SimCruiseVehicleMovementComponent.cpp:594-599,210-218` | Snap when the reset counter in telemetry changes | Recover 50 m off road: no fly-across |
| 14 | P1 | Audio rev limiter fixed at 6950 rpm for every car (C8 cut 6600, Mustang 5000 never sound it; Miata stutters early) | sound | `SimCruiseInlineFourSynth.h:25`, `.cpp:76,96` | Pass each car's fuel cut / max rpm into the synth | New test: limiter follows car, all 3 cars |
| 15 | P1 | Brake (LT) and clutch (LB) need the same finger on the pad; a manual shift is refused without the clutch | ux | `SimCruiseInputConfig.cpp:29,44,47` | Automatic clutch dip on the pad | Downshift under braking, 10 of 10 |
| 16 | P1 | HUD toggle on L3 (steering stick click) fires in hard corners, and the choice is saved | ux | `SimCruiseInputConfig.cpp:54` | Move to D-pad Left | Bindings test: nothing on L3 |
| 17 | P1 | Engine braking about half of a real 1.8 L engine (estimate) | automotive | `SimCruiseVehicleSim.cpp:712-714` | More friction + closed-throttle pumping term | Coast-down in 3rd vs owner's phone log |
| 18 | P1 | Tests cannot see frame-rate bugs: benches tick at exactly 1/60 s; the drive robot never drives the C8 or the Mustang | qa | `VehicleBenchWorld.cpp:120-122`; `drive-robot.ps1:23,25` | Physics-time check; 15 fps in the default run; robot drives all 3 cars | `history.csv` has all 3 cars; check fails before item 1 fix |
| 19 | P1 | Glance HUD text ~16 px at 1080p and ~2:1 contrast (1.2:1 when faded); XAG asks 26 px and 4.5:1 | ux | `SimCruiseHUD.cpp:329-340` | Bigger text, panel ≥ 75 % opaque, never fade speed and gear | 1080p capture, measure |
| 20 | P1 | Tree LOD rings all draw from 0 m: three crowns overlap near the car, and LOD0 cuts out hard at 33 m | magician | `SimCruiseTrees.cpp:335-337,472-475` | One ring per distance band, or mesh LODs with dither fade | Screenshot at 10 m; `stat RHI` triangles |
| 21 | P1 | Whole tiles appear in view 551-1102 m ahead with no fade | magician | `SimCruiseTileManager.cpp:46-55`; `SimCruiseWorldStreamer.cpp:547` | Fade in 1-2 s, or request the next row one tile early | Frame-pair diff on Ocean Drive |
| 22 | P1 | Night: 24 street lights jump to the nearest lamps every 0.25 s and switch on/off hard, half of them behind the car | magician | `SimCruiseWorldStreamer.cpp:451-478` | Prefer lamps ahead, hysteresis, 0.5-1 s fade | 60 s night drive: no hard switch in view |
| 23 | P2 | Chase camera yaw is rigid (only position lags), so steering twitches shake the picture | cinematographer | `SimCruiseCameraModels.cpp:194-226` | 0.15-0.3 s critically damped heading spring | Slalom: camera yaw > 2 Hz at least 50 % lower |
| 24 | P2 | Chase camera jumps in front of palms and parked cars | cinematographer | `SimCruisePlayerCameraManager.cpp:122-129` | Sphere sweep, ignore thin objects, ease out | No > 0.5 m distance change per frame |
| 25 | P2 | Cockpit shake is new noise each frame: 2x stronger at 30 fps than 120 fps | cinematographer | `SimCruiseCameraModels.cpp:136-137` | Noise on sim time, or from suspension | Shake RMS within 10 % at 30/60/144 fps |
| 26 | P1 | Night exposure meters lux on a card facing the low beams, not road luminance: road renders ~0.1 cd/m² vs real 0.4-2 cd/m²; 61 % of night pixels at code 0-3 (root cause of the black night in item P2 list) | lighting-director | `SimCruiseVehicle.cpp:214-216`; `SimCruiseDaylightModel.cpp:105-116` | Meter an adaptation luminance (~0.5 cd/m² halogen low beam) | Road probe 0.3-1 cd/m² at 0-32 m; ≤ 25 % pixels at code 0-3 |
| 27 | P1 | Low-beam pattern attenuated twice (8°/32° cone x light function; 122 m radius halves the beam at 90 m) | lighting-director | `SimCruiseVehicle.cpp:155-171` | Inner cone 30°, outer 32°, radius ~250 m | Pattern x cone ≥ 0.9 at ±20°; ≥ 3 lx at (10 m, 3 m right) |
| 28 | P1 | Software Lumen sees no streets (tiles have no distance fields), so shade has no warm bounce and paint reflects only the sky | lighting-director | `DefaultEngine.ini:16-20`; `SimCruiseWorldTile.cpp:159` (wrong comment) | Fix the comment; A/B hardware Lumen at 28 W | Shade within 0.5 EV of photo; cost logged |

Other P2 items (details in the role reviews): Torsen bias 2.0 should be ~2.5
for 2001 and coast bias is missing; C8 front tyre size wrong (uses rear
305/30R20) and old tyre model; lock angle vs steering ratio disagree (3.05 vs
2.6 turns); understeer gate 0-8 deg/g too wide, no step-steer test; atlas
close resets steering mid-corner; gearbox label stale in the atlas; car
button cycles 10 cars alphabetically; no pause on pad disconnect; no volume
sliders or mix bus; no full-mix loudness test; engine hard-clips at ±0.94;
shift light colour-only; trigger rumble documented but never runs; atlas
settings may not be reachable by pad; perf sampler hides hitches (no
`frame_max`); traffic vanishes in view at 320 m; surface grip depends on
cooked face order (needs a test); motion blur left at defaults; parked cars
spawn in view.

## What the team recommends for the release thread

Send items 1, 3, 4-7, 8-10, 12-16 and 18 to "Ship the driving-experience
release". They are the ones a driver feels. Item 2 needs a 28 W measurement
first. Items 26-27 (night light) also go to the release thread; item 28 needs a 28 W measurement. Items 19-25 are polish: fix them if the release has time.

## Questions for the owner

1. Does your NB have ABS? (Item 4 depends on it.)
2. Can you do a coast-down in 3rd and a lock-to-lock count on your car? (Items 17 and the steering ratio.)
