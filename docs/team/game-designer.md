# Game designer: study notes

## Rules
- The fantasy (owner): "my car, on a real road, right now". Fun comes from realism, not from arcade rules.
- The first five minutes decide: start in the car on a good road, with a clear way to choose another.
- Defaults must be realistic: true car data, real steering ratio, real brake feel. Assists (ABS, automatic clutch) only where the real car has them, or as clearly named options.
- Game feel = input, response, context, polish (Swink). Input delay and camera are design problems as much as code problems.
- Drivers look at the tangent point in curves and at the road edge for speed (Land & Lee 1994); design the view and the HUD for that.

## Tests
- A new player picks a road and drives in under 60 s.
- Five-minute drive per car by the owner: each car feels like the real one.

## Sources
- J. Schell, *The Art of Game Design: A Book of Lenses*, 3rd ed., CRC Press, 2019.
- T. Fullerton, *Game Design Workshop*, 4th ed., CRC Press, 2018.
- S. Swink, *Game Feel*, Morgan Kaufmann, 2008.
- M. F. Land, D. N. Lee, "Where we look when we steer", *Nature* 369:742-744, 1994.

## What I now know (onboarding, 2026-11-14)

**How my area works today.**
- `ASimCruiseGameMode::BeginWorldDrive` (Source/SimCruise/Private/Gameplay/SimCruiseGameMode.cpp:158) picks the start in this order: `-SimCruiseStart=lat,lon` ("chosen"), then a seeded random start-pool road ("random", the default because `bRandomStart = true`, SimCruiseGameMode.h:102), then the bundled Ocean Drive place. It writes `START <mode> seed=<n> <title>` to the log (line 203).
- The start pool is data/start/start-pool.json: 36 hand-picked scenic roads. `ParseStartPool` (SimCruisePlaces.cpp:68) marks each entry `bLoose`. `Sequence` (line 112) returns 1 + `Retries` (3) places, so a failed road falls through to 3 more.
- Car placement: `TryPlacePendingPlayers` (GameMode.cpp:533) waits for the 3x3 ring of tiles. The player sees "Building X · n of 9 tiles" every 2 s. The place is given up when 4 or more tiles fail, or 1 tile fails and 90 s pass. After 180 s the car is placed on whatever has loaded.
- Road snap: `FindRoadPoseFacing` (World/SimCruiseWorldStreamer.cpp:152). A pool road must be the named street within 125 m, else any road within 30 m. A chosen or searched point takes a road within 900 m (`StartSearchRadiusM`). Only `service` ways are rejected, so `track` is a valid start. Points within 15 m of a junction are skipped. With no road, the car "starts on the ground" (GameMode.cpp:594).
- After a failed chain, `TravelFailed` (line 394) goes back to Miami with "No random road loaded · starting in bundled Miami Beach".
- Search: `SSimCruisePlaceSearch::Submit` (SimCruiseAtlasUI.cpp:403) runs `ParseQuery` ("lat, lon", "surprise", three aliases, SimCruisePlaces.cpp:169-189), else the helper `search` command (helper/atlas.mjs:240): Photon `limit=6&lang=en`, no bbox or location bias, then keeps only `countrycode=US`. Timeout 25 s.
- Road atlas: 11 zoom steps (AtlasUI.cpp:501), about 1000 km across at the far end, so the player can pin any US spot. `DriveToPin` (line 665) titles the place with the raw "lat, lon"; no reverse geocode.
- "Surprise me" (`SurpriseMe`, GameMode.cpp:376) draws from the same 36 pool roads, not a random US point.
- Famous roads (SimCruiseFamousRoads.cpp:33): 4 roads with a start, end and length (Ocean Drive, Dragon, Big Sur, Monument Valley).
- GPS: helper/route.mjs A* over OpenFreeMap z14 tiles; free-flow speeds by class (line 16), no `track` class; a leg is refused above 144 tiles (`MAX_TILES`, line 352). Off route at more than 30 m for 3 s; arrived within 25 m (SimCruiseRoute.h:112-115).
- Defaults: car `nb2001_lowered` (Config/DefaultGame.ini:11) or the last car driven; start time 10:00 local solar time (Environment/SimCruiseEnvironment.cpp:204).

**Does it work at a random US place?**
- General: the "chosen" start, the atlas pin, Photon search and the 900 m snap work at any lat/lon. The us-* test places use this path. (estimate)
- Hard-wired: the default start and "Surprise me" reach only 36 curated roads; on failure the player goes back to Miami. Search presets are 9 lab places (SimCruisePlaces.cpp:49-59). Only 4 famous roads have an end point.
- us-casselton-nd: many section lines are OSM `track`; a start can land on one, but the GPS cannot route on `track`. (estimate)
- us-welch-wv, us-hazard-ky: a pin more than 900 m from a road puts the car "on the ground" in the forest with no warning. (estimate)
- us-bronx-ny, us-doraville-ga: the router drops tunnels (route.mjs:47), which can cut routes. (estimate)
- us-anchorage-ak, us-hilo-hi: search and pin work; the title is "61.21810, -149.90030", not "Anchorage". (estimate)

**Main gaps against "realistic everywhere".**
1. P1: "Surprise me" and the default start are a 36-road list, not the US (GameMode.cpp:185-196, 376). Add a "random ordinary US road" mode that samples a populated point and snaps with the 900 m rule; keep the pool as "Scenic surprise".
2. P1: a random place gives no reason to drive: no place name, no destination (GameMode.cpp:606, AtlasUI.cpp:662). Reverse-geocode with Photon `/reverse`; offer the next town or a landmark to the GPS.
3. P1: search can come back empty for common names (helper/atlas.mjs:240). Add a US `bbox` and a `lat`/`lon` bias at the car (Photon supports both).
4. P2: no `track` class and a 144-tile leg cap in helper/route.mjs:16, 19.
5. P2: "starts on the ground" with no message (GameMode.cpp:594). Retry wider, then tell the player.
6. P2: lab presets are shown to players (SimCruisePlaces.cpp:49).

**My first three actions.**
1. Spec "random ordinary US road" (sampling, snap, fallback chain, message). Done when `scripts\preflight.ps1 -Roads random -RandomCount 3` starts on a public road at 3 random places and I have read the sheets.
2. First five minutes on `scripts\preflight.ps1 -Roads us-casselton-nd,us-welch-wv,us-bronx-ny -Seconds 60`: time to car placed, start message, road kind under the car. Done when each defect has its image in my report.
3. Photon query fix (bbox plus bias) in helper/atlas.mjs. Done when "Main Street", "Hilo" and "Welch, WV" each return a US result in a helper test.

**Docs that are wrong or old.**
- docs/controls.md says the client starts in Ocean Drive's lane; the code starts on a random pool road by default (GameMode.h:102).
- docs/controls.md and README.md:28 say "Surprise me" is "a random US road"; it is one of 36 pool roads (GameMode.cpp:382).
- docs/plan/player-first-roadmap.md 5.5 names a start screen (Famous drives, My roads, Search, Surprise me); none exists in the code.
