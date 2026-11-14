# SimCruise game team: charter and ownership map (14 November 2026)

## Charter (owner's words, 2026-11-14)

SimCruise is **a realistic driving game that builds the world from real-world data.**

Every role works to these rules:

1. **Every part of the game is realistic.** Cars, roads, light, sound and controls match the real thing. Each realism claim has a real source (manual, standard, paper, measured data) or is marked (estimate).
2. **Be innovative and original, and also research what exists and improve on it.** Study how the best games and the real world do it first; then do it better, in our own way. Never copy another game's content.
3. **Be efficient, detailed and thorough.** Small tested steps; no guess left unmarked; no "works" without proof.
4. **Launch and look.** Before you say a change works, run the game, capture it and look at the images yourself (procedure below).
5. Standing rules: no money; legal APIs and licences only; do not break other threads' work; push from the owner's PC; editor and tests only through the scripts.

## How real studios split the work

Real studios group people by discipline, each with a lead: design, engineering (gameplay, graphics, systems, physics, tools, audio, AI), art (environment, vehicle, technical, UI), audio, QA and production (UMBC roles list; Playground Games credits for Forza Horizon 5 list Design, Production, Software Engineering, Art, Audio, QA, plus a Car Team and car photography and sourcing at Turn 10; Chandler, *The Game Production Handbook*). A racing studio adds a car team that owns real-car data and handling. Our team follows the same split. Each team has one lead expert (a subagent) and one project thread that does the work.

Sources: https://userpages.cs.umbc.edu/olano/class/493-24-2/roles.md.html · https://www.mobygames.com/game/174940/forza-horizon-5/credits/ · https://www.oreilly.com/library/view/the-game-production/9781449688097/

## Teams

| Team | Lead expert | Other experts | Owns | Thread that does the work |
|---|---|---|---|---|
| Production | producer (new) | — | One ranked list, scope, releases, the owner's asks | Build a game team that owns the game |
| Design and gameplay | game-designer (new) | ux-accessibility | The reason to drive, first five minutes, defaults, menus, HUD, controls | Add GPS waypoints, menus and HUD gauges (menus); Make the driving and traffic feel real (controls) |
| Car team (vehicle) | vehicle-dynamics-physicist | automotive-engineer | Real-car data, tyres, suspension, brakes, drivetrain, steering, handling | Make the driving and traffic feel real |
| Engineering | game-developer | performance-engineer | Engine code, threads, physics step, frame time, input delay, crashes | Make the driving and traffic feel real (driving code); Make the picture look like a photo (frame time) |
| Picture (graphics) | lighting-director | cinematographer, technical-artist (new) | Light, exposure, colour, camera and lens, materials, shaders, cockpit art (wheel, gauges) | Make the picture look like a photo |
| World | gis-data-scientist | environment-artist (new), magician | Real-world data to roads, ground, roadside, buildings; no pop-in | Make the picture look like a photo (world look and the detail layer) |
| Audio | sound-designer | — | Engine, tyre, wind, road sound; mix and loudness | Make the driving and traffic feel real (until an audio thread starts) |
| QA and tools | qa-playtester | tools-engineer (new) | Tests, pre-flight, launch and look, replays, builds, installers | Review real game output on every test |

## Current work by team (high level)

| Team | Now | Next goal | Done when |
|---|---|---|---|
| Production | Priorities set (meeting 2026-11-13 18:00) | Keep one list; next release scope | Every item has an owner thread and a test |
| Design and gameplay | GPS, menus, HUD gauges landed | Realistic defaults; quick road choice; first five minutes | New player drives a road of their choice in < 60 s |
| Car team | Dragon spins solved (the test robot was the cause); brakes, ABS, clutch and step-steer tests on main | Cockpit: steering wheel too fast, wheel off its axis, gauges must not turn with the wheel; real turns lock to lock (below); physics audit of all 3 cars | Wheel speed and turns match the sources; wheel turns on its column axis; gauges stay fixed to the dash |
| Engineering | Laptop GPU now at full power (88 W): input delay 53 ms Medium, 56 ms High, 103 ms Ultra; Reflex stays off | Ultra under 100 ms; keep frame time smooth | Latency probe < 100 ms at Ultra |
| Picture | Contact shadows, sharper picture, cockpit gauges for 3 cars | Photo match at High and Ultra | Frame next to a real photo: owner says it matches |
| World | Hill grass on main; AI detail layer on `detail-layer-wip` | Open again (owner 2026-11-14): more detail on buildings, terrain and trees; one detail layer over everything; legal AI models only; more local data so each area feels familiar | Frame next to a real photo of the same place: owner says it looks like there; GPU cost logged |
| Audio | Limiter per car | Sound matched to recordings (later) | — |
| QA and tools | Pre-flight with video and contact sheets; replay steps on the physics thread | Launch and look gaps (poses, night pass, reference diff); tests fail on spin and slow frames | A broken change turns the run red; every thread reads its captures |

## Launch and look (every thread, before "it works")

1. `scripts\watchdog.ps1` (stop stale runs with `-Stop` only after checking the list).
2. `scripts\check.ps1 -Tier fast` (view check on; never `-NoViewCheck`).
3. `scripts\preflight.ps1 -Cars nb2001_aaron,meridian_z,mustang65 -Seconds 60`: video, 4x3 contact sheet, `preflight.json`, `report.md` per road and car in `Saved\Preflight\<time>\`.
4. Driving change: `scripts\replay-compare.ps1 -Tape <tape> -CarB <car>`. Frame-time change: `scripts\framecheck.ps1`. Look change: `scripts\realism.ps1`.
5. Read the logs: PREFLIGHT reasons (spin, past edge, jolts, pop-ins, flash), any `Error:`, `Ensure` or `Fatal`, `Saved\Crashes`.
6. **Open the images with the Read tool**: every `*-sheet.png` and `Saved\ViewCheck\latest\*.png`, and the same files from the last PASS run. List each defect with its image. "Tests pass" is not enough.

Gaps (tools-engineer): fixed pose list per car and road for chase and cockpit (M), a night pass (S), a pinned reference set and automatic diff (M), one sheet with all 3 cars (S), captures from the packaged build (L).

## Steering (owner ask, 2026-11-14)

Real ratios are right in the car files (Miata 15:1, C8 15.7:1 variable, Mustang 21.7:1), but the pad turns the wheel up to about 1500°/s at low speed (faster than NHTSA's 1000°/s test robot) and the lock gives 7-19 % too many turns. Fixes and sources: `docs/team/research/steering-ratio.md`. Owner: car team, "Make the driving and traffic feel real".

## Updates

- 2026-11-14 00:42: world work open again (detail on buildings, terrain, trees; one layer over everything; legal AI; local data); Dragon spins solved; car team on the cockpit; GPU at 88 W, delay 53/56/103 ms (Medium/High/Ultra), Reflex off.
