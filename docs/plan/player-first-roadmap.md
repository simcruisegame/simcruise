# SimCruise: player-first roadmap

Date: 12 November 2026. Status: **proposal, waits for Aaron's yes or changes.**
No thread changes course before Aaron agrees. This plan does not stop the
v2.0.1 release.

## 1. Why this plan

Aaron played v2.0.0 and said: *the field of view was off, it was too bright,
the traffic was bad, and it did not look real.*

Every one of those four points is about what a player sees in the first five
minutes. In the last two days the team did a lot of good engineering: 145 tests,
cars within 2 % of magazine numbers, live weather, tides, aircraft, a knowledge
graph, an AI super-resolution pass on aerial photos. But almost none of that is
visible from the driver's seat in the first five minutes. We measured what is
easy to measure (braking distance, wall albedo) and not what the player feels.

I looked at the v2.0.0 screenshots as a player. What I see:

- **The picture is washed out.** The sky is white, the cabin is flat grey, the
  road is pale, and nothing has deep shadow or rich colour. A real photo of
  Ocean Drive has a blue sky, dark asphalt and strong contrast. This is the
  camera's exposure and tone curve, not the walls. Darker walls alone will not
  fix it.
- **The cockpit view feels like a security camera.** The A-pillar and the roof
  frame fill a third of the screen, the eye feels too high and too far forward,
  and there is no hood line to aim with. Two lenses (32 degrees "true" and 74
  degrees "natural") are the only choices, and neither feels like sitting in the
  car.
- **The world is empty.** On Ocean Drive the parked cars stand still and no car
  drives. On the Nebraska gravel road there is a flat neon-green field, a white
  strip of road, and nothing else: no poles, wires, fences, mailboxes, signs,
  ditches, or other cars.
- **There is no reason to drive.** The game puts you on a random road. You do
  not choose a great road, there is no start or end, and nothing remembers the
  drive.

### Bug list from Aaron's v2.0.0 playtest (12 November 2026)

Aaron's own words, then the known bugs that match them. P0 = a player
notices in the first five minutes.

| # | Sev | What the player sees | Known cause or lead | Owner |
|---|---|---|---|---|
| B1 | P0 | "The view in the car was not correct" (field of view off) | Only two lenses; eye point not set from the real seat; no live sliders; no hood in view | driving thread (now top work) |
| B2 | P0 | "It still did not drive like the real car" | Bench numbers pass, but the feel on a pad was never tested by a person; steering and trigger shaping, rumble and camera motion change the feel as much as the tyres | driving thread (now top work) |
| B3 | P0 | "Could not adjust the vehicle as discussed" | Tuning menu (D-pad Right) never driven by hand in the package (release row N6); not shown in any on-screen hint | driving thread (now top work) |
| B4 | P0 | "Too bright" | Exposure and tone curve wash out the sky and the road; no colour grade; wall albedo was only a small part | world-look owner (5.2) |
| B5 | P0 | "Traffic was bad" | Few cars, spawn 60-260 m and removed at 320 m (cars vanish in view), no oncoming flow on many roads, parked cars appear in view | traffic owner (5.3) |
| B6 | P0 | "Did not look real" | Empty roadside, neon-green fields in November (R7), flat white gravel, tree LOD rings overlap near the car | world-look + roadside owners (5.2, 5.4) |
| B7 | P1 | "A lot of bugs" | Known open: ground through the road at tile seams on short ribbons; chase camera jumps in front of palms; Mustang launch spin; C8 0-60; Model 3 spin; HUD text too small; tiles appear in view without a fade | see the expert joint review; triage into the next playtest build |

Aaron's process note: *put more changes in before you test the game.* See
section 6, "Playtest builds".

## 2. If I played SimCruise, what would I want?

I would want to type "Tail of the Dragon" or "the road to my house", and in
under a minute sit in *my* car, on *that* road, at *this* hour, with the real
weather. The view from the seat feels like my seat. The picture looks like a
dashcam video. There are other cars around me that behave like people. The
engine and the road sound right. I drive for 15 minutes, and at the end I
want to save a photo, see my line on the map, and come back tomorrow to drive
it again in the rain or at night.

## 3. Core fantasy, loop and fun

**Core fantasy:** *"It is my car, on my road, right now."* (The expert team
had "It is my car, on a real road". I add "my road" and "right now", because
the choice of road and the live time and weather are what no other game can
give.)

**Core loop (one session, 10 to 30 minutes):**

1. **Choose** a road: a famous drive, a saved favourite, a place you search, or
   "surprise me".
2. **Drive** it, from a real start to a real end, in real time and weather, with
   life around you.
3. **Keep** something: a photo, the drive on the map, your time, a ghost to beat.
4. **Come back**: the same road at night, in the rain, in another car, or the
   next road on your list.

**What makes a session fun:**

- The car responds the way you expect. The benches pass, but after v2.0.0
  Aaron said it still does not drive like the real car, so the feel on the
  pad is first work again (section 5.0).
- The view and the picture make you believe you are there (sections 5.1, 5.2).
- The road is alive: traffic to pass, follow and avoid (section 5.3).
- The road edge tells you speed and place: poles going past, signs, mailboxes
  (section 5.4).
- Small surprises: the real plane overhead, the real fog, a sunset at the real
  minute. We already have these feeds; they only matter once the basics above
  are right.
- You leave with something and a reason to return (section 5.5).

## 4. What big driving and open-world games do (methods, not copies)

These are the general methods that studios use. We use the methods only; we
copy no game.

1. **Camera, controls and car first ("the 3 Cs").** Studios lock how it feels
   to sit, steer and look before they build content. They give several views
   (cockpit, hood, bumper, near and far chase), let the player move the seat
   and the field of view, and widen the view a little with speed so that speed
   is felt. The cockpit eye sits where a real head sits, with a small part of
   the hood in view as an aim point.
2. **The picture is directed, not only computed.** Physically correct light is
   the base, but every big game then has a look: an exposure that keeps the sky
   blue, a film-like tone curve, a colour grade (LUT), ambient occlusion,
   haze in the distance. They tune the look by putting game frames next to
   reference photos and video, all day, every day.
3. **Detail goes where the eye goes.** A driver looks at the road and the first
   30 m on each side. Studios fill that corridor densely (poles, signs, kerbs,
   cracks, grass, fences) and keep the far world cheap. A dense corridor also
   gives the sense of speed.
4. **Traffic is life, and life is an illusion.** Cars spawn out of sight
   (behind the camera, behind buildings, over a crest) and never pop in or out
   in view. Density follows the time of day and the road type. Each car has a
   small "personality" (speed, gap, lane changes, turn signals). Distant cars
   are cheap stand-ins; near cars are full.
5. **Sound is half of the feel.** Engine, tyres, wind and the road surface
   tell the driver as much as the picture. (Our engine sound is good; it stays
   in the plan as the next step after the five below.)
6. **The first five minutes sell the game.** Start fast: no long loading, a
   beautiful road, the best time of day, a clear choice. Studios design and
   playtest the first five minutes more than any other part.
7. **A loop and memories keep players.** Short goals (a drive with an end),
   collections (roads driven), photo mode and replays, ghosts, and the same
   place in a new condition.
8. **Vertical slice, then scale.** Make a few places look and feel finished,
   then turn what you learned into rules for everywhere. Judge every change on
   the same few "hero roads".
9. **Playtest by feel, with fresh eyes, every week.** Numbers guard against
   regressions, but the gate for a feature is "a player notices and likes it".

## 5. The roadmap: the big things, in order

Each item has a "done when" that a player notices, an owner, and a size.
Numbers and tests stay, but as guards, not as the goal.

**Hero roads (every item is judged on these first):**

- **City:** Ocean Drive, Miami Beach (the bundled start).
- **Twisty:** a mountain road (US 129 "Tail of the Dragon", or a road Aaron
  chooses).
- **Rural:** a two-lane country road with farms (the Nebraska gravel road, or a
  paved road near it).
- **Aaron's road:** one road Aaron drives in real life (Aaron to choose).

### 5.0 Drive like the real car, and let me adjust it — FIRST (driving thread, already started)

- Pad feel: steering speed, trigger curves and rumble tuned by playing on the
  hero roads, not only by benches. The car's physics stays; the feel between
  the stick and the tyres is the work.
- The setup menu (suspension, gearing, steering ratio, body roll, as agreed)
  shown on screen ("D-pad Right: setup"), usable with the pad, and it saves.
- Blind A/B in the game (see idea I3): two setups on the same corner, the
  player picks which one is more like the real car.

**Done when:** Aaron opens the setup menu without help, changes the steering
ratio and the springs, feels the change on the next corner, and says the
Miata drives like his car.
**Owner:** "Make the driving and traffic feel real". **Size:** M.

### 5.1 The view from the seat (camera and field of view) — FIRST

The biggest complaint, and the fastest fix.

- Eye point from real seat data for each car, a small part of the hood or
  bonnet in view, mirrors that you can use.
- Four views: cockpit, hood, near chase, far chase. The chase camera follows
  with a soft lag and does not snap through palms.
- Seat and view settings in the pause menu with a live picture: field of view,
  seat forward/back, seat up/down. Saved per car.
- A small speed effect: the view widens a few degrees and the camera shakes a
  little with road texture at speed.
- First start: a 10-second "choose your view" screen with three views to try
  on the road.

**Done when:** Aaron drives the Miata on the hero roads and says the cockpit
"feels like my seat" without opening a menu, or sets it once in under 30
seconds and never again. At 100 km/h the speed feels right.
**Owner:** the driving thread now owns the in-car view with 5.0 (the cars
thread made the lens and can help), with the cinematographer and
ux-accessibility experts. **Size:** S to M.

### 5.2 A picture that looks like a photo (exposure, tone, colour, haze) — FIRST, in parallel

Stop fixing brightness one material at a time. Fix the camera.

- Exposure that keeps a blue sky blue and puts mid-grey where a phone camera
  puts it; a film-like tone curve; one colour grade (LUT) made by matching game
  frames to licensed real photos of the hero roads at the same time and
  weather.
- Dark, slightly glossy asphalt with wear in the wheel paths; deep contact
  shadows (ambient occlusion) under cars and kerbs; distance haze.
- Fields and verges that follow the season and region (no neon green in
  November).
- A side-by-side tool: game frame next to a real photo of the same place, the
  same sun angle. This is the main test.

**Done when:** for each hero road, a game frame and a real photo sit side by
side, and Aaron says the brightness and colour match. No sky clips to white in
day frames. The realism score goes up, but Aaron's eye is the gate.
**Owner:** "Make buildings look like the real places" becomes the "world look"
owner, with the lighting director and the cinematographer. Night exposure stays
with the cars thread. **Size:** M.

### 5.3 Living roads (traffic) — SECOND

- Traffic that is never empty where real roads are busy, and never full where
  they are quiet: density by road type and time of day (and later the live
  traffic feeds).
- No car appears or vanishes in view: spawn behind the player, behind
  buildings or over crests; keep cars until they are out of sight; cheap far
  cars instead of a 320 m cut-off.
- Oncoming traffic, cars that pull out of parking spaces, turn signals and
  brake lights, a mix of vehicles that fits the region (pickups in Nebraska).
- Drivers with small differences: some faster, some slower, some change lanes.
  Cars react to the player (yield, brake), and never deadlock.

**Done when:** on Ocean Drive at 17:00 you always have cars ahead, behind and
coming the other way; in a 30-minute drive across the hero roads no car pops
in or out in view and no junction locks. Aaron says "traffic feels real".
**Owner:** "Make the driving and traffic feel real" (the physics work is
mostly done, so traffic becomes its main job), with the magician and
performance-engineer experts. **Size:** M to L.

### 5.4 The roadside: the 30 m that the driver sees — SECOND, in parallel

- Rules that place what is really at the edge of a road: utility poles and
  wires, mailboxes and driveways on rural roads, fences, ditches and culverts,
  speed-limit and curve signs (from OpenStreetMap where mapped, from rules where
  not), guard rails, verge grass of mixed height, cracks and patches in old
  asphalt, gravel with dust.
- The place knowledge graph chooses these by region, road type and era
  (its first real job for the driver).
- Far fields and hills stay cheap; the money goes to the corridor.

**Done when:** a 2-minute drive on the rural hero road passes poles, wires,
mailboxes, signs and fences at real spacing, and a still frame no longer
looks like a test level. Speed feels higher at the same speed because
things pass the edge of the view.
**Owner:** "Design a place knowledge graph for details" (reopen) for the
rules, and the world-look owner for the art. **Size:** M to L.

### 5.5 A reason to drive (the first five minutes and the loop) — THIRD

- A start screen: **Famous drives** (a short list of great US roads, each
  with a start, an end and a length), **My roads** (saved favourites), **Search**,
  **Surprise me**.
- On the road: "right now at this place it is 16:20 and foggy" (the live feeds
  finally visible as a feature).
- At the end of a drive: the route on the map, time and average speed, photo
  mode shots, and a ghost of your best run for next time.
- Photo mode: free camera, pause, the same grade as the game.

**Done when:** from start-up, Aaron is on a famous road in three presses and
under one minute of loading, drives it to the end, and wants to try it again
at night or in another car.
**Owner:** "Restore go-anywhere and old Godot features" (reopen; it owns the
atlas and search). **Size:** M.

### After these five

6. Engine and road sound against recordings (sound-designer; V8 voices,
   cabin vs outside).
7. The far world: hills, tree lines and towns to the horizon, without pop-in.
8. The rest of the expert team's physics plan (brakes, C8 launch, ISO
   suites) as regression guards.

## 6. Order and waves

| Wave | Start | Items | Release |
|---|---|---|---|
| 0 | now | Finish v2.0.1 as planned (do not stop it) | v2.0.1 |
| A | now (driving thread) and after v2.0.1 freeze | 5.0 drive and adjust, 5.1 view, 5.2 picture (parallel) | v2.1 "It looks and drives right" |
| B | when A is in Aaron's hands | 5.3 traffic, 5.4 roadside (parallel) | v2.2 "It feels alive" |
| C | with B, or after | 5.5 reason to drive | v2.3 "Your road, right now" |

Each wave ends with a **five-minute player test**: a 60-second video on each
hero road, put next to real photos or licensed dashcam video of the same
place, plus Aaron's own drive. The "Add screenshot checks to play tests"
thread builds this capture as one script, and the release gate adds it.

### Playtest builds: big, tested batches (Aaron's rule)

Aaron asked for more changes before each test. From now on:

1. **No small builds for Aaron.** A playtest build goes to Aaron only at the
   end of a wave, with a large batch: every item of the wave, and every P0 and
   P1 bug from the last playtest fixed or listed as still open.
2. **We play it first.** Before Aaron gets it, the build passes a
   "pre-flight": the drive robot drives every hero road with each of the 3
   cars, records a 60-second video per road, and a check looks for white sky,
   empty roads, cars or tiles that pop in view, ground through the road, and
   stuck traffic (see idea I6). A Claude reviewer watches the videos and
   writes the list of what a player would still notice. Any P0 stops the build.
3. **Each build comes with a one-page "What you should notice" list**, with
   before/after pictures for each of Aaron's notes, so that Aaron can check
   each point in a 15-minute drive.
4. Engineers still merge small changes to main all day (the check stays);
   only the build that Aaron plays is batched.

## 7. Original ideas, backed by game design research

Each idea uses a method from the research below and adds something only
SimCruise can do.

- **I1. Auto colour grade from reality.** For each hero road, take licensed
  photos at known times; the game renders the same view at the same sun, and
  an optimiser on the CPU tunes exposure, tone curve and colour grade until the
  histograms and colours match. The "directed picture" of big games, but
  directed by the real place. (Method: art direction against references.)
- **I2. Your seat from one phone photo.** Aaron sits in his Miata and takes one
  photo from eye height. The game finds the dash, wheel and A-pillar in the
  photo and solves the eye point and field of view from them. The in-game view
  then matches his real view. (Method: sim-racing FOV setting, made automatic.)
- **I3. Blind A/B handling.** On any corner, press a button: the game replays
  the corner with setup A and setup B, unnamed, and the player picks the one
  that feels more like the real car. Over a few picks the game learns the
  player's car. (Method: playtest loops and "game feel" tuning, put inside the
  game.)
- **I4. Real density, out of sight.** A pool of traffic cars fills each road
  to a target density by road type (the method of open-city racing games),
  with the target from public hourly traffic-count patterns (FHWA, free and
  legal) for this hour and day. Cars spawn and leave only out of view.
- **I5. Speed you can feel from the road edge.** Utility poles, posts and
  markings at their real spacing give optic flow: things rush past the edge of
  the view. This gives the sense of speed without a fake wide lens. (Method:
  "feeling of speed" is objects passing near the camera.)
- **I6. A playtester robot with eyes.** The drive robot records the hero
  roads, and a small local vision model flags pop-in, white sky, empty roads
  and seams before a person plays. (Method: playtest early and often; the
  machine finds the cheap problems so the person finds the deep ones.)
- **I7. Drive postcards.** Each finished drive becomes a card: the route on
  the map, the best photo (picked by the local AI), the real weather and time,
  the car. A collection of "roads I have driven" is the long-term loop.
  (Method: memories and collections keep players coming back.)

### Research used

- Steve Swink, *Game Feel*: real-time control, simulated space and polish
  together make feel; summary at
  [lizengland.com](https://lizengland.com/blog/review-game-feel-by-steve-swink/).
- Christer Ericson, playtest and design guidelines for a racing game: test
  with outside players early and often, the player always feels in control,
  never interrupt play, landmarks and light show direction
  ([realtimecollisiondetection.net](https://realtimecollisiondetection.net/blog/?p=10)).
- "AI Madness: Using AI to Bring Open-City Racing to Life": a shared car pool,
  target density per road type, a random speed offset per car, simulation
  bubble and cheap far cars
  ([gamedeveloper.com](https://www.gamedeveloper.com/programming/ai-madness-using-ai-to-bring-open-city-racing-to-life)).
- GDC talks on living worlds: "Living City in Mafia 2"
  ([GDC Vault](https://gdcvault.com/play/1013728/contactUs)), "Populating the
  World With an..." ([GDC Vault](https://gdcvault.com/play/1029282/Populating-the-World-With-an)),
  "Free Range AI" ([GDC Vault](https://gdcvault.com/play/1020110/Free-Range-AI-Creating-Compelling)).
- Feeling of speed: FOV alone does not sell speed; objects rushing past,
  camera distance and responsive control do
  ([critpoints.net](https://critpoints.net/2016/11/24/feeling-of-speed/)).
- How a team made speed feel fast in Unreal
  ([unrealengine.com](https://www.unrealengine.com/developer-interviews/how-formula-fusion-nailed-the-feeling-of-fast)).
- Sim-racing field of view practice
  ([fanatec.com](https://www.fanatec.com/eu/en/explorer/games/gaming-tips/optimizing-fov-for-sim-racing-getting-the-view-right/)).
- Playtesting for very small teams, GDC 2026
  ([GDC Vault](https://gdcvault.com/play/1035679/Playtesting-Process-for-Ultra-Small));
  playtesting God of War ([GDC Vault](https://gdcvault.com/play/1025978/Playtesting-God-of-War)).

## 8. What to stop or pause

Pause until wave B is done (unless Aaron moves something back in):

- **New live feeds** (more data sources, aircraft details, tides). Keep what
  works. The live-data thread helps 5.2 (weather sets the look) and 5.3 (time
  and live traffic set the density) instead.
- **AI super-resolution of aerial photos.** It sharpens the ground as seen
  from above, which a driver almost never sees. Point the local AI at the
  roadside (5.4) instead, for example to choose props from licensed street
  photos.
- **Knowledge-graph features that the driver does not see.** Its next job is
  the roadside (5.4).
- **Tuning brightness one material at a time** (wall albedo, trim caps, sign
  ink). Fold it into 5.2.
- **More physics fidelity work** (C8 0-60, Garage Day, ISO 3888 and 4138
  suites, HDR10 output). The work on the car is now the feel on the pad and
  the setup menu (5.0), not more bench accuracy. Keep the tests as guards.
- **Hourly expert meetings that review every commit.** Change them to one
  review per wave that judges the five-minute player test videos and says what
  a player would notice next.
- **More cars.** Only the Miata, the Mustang and the Corvette, as Aaron said.

Keep: the v2.0.1 release, the check and release gates, the "no money, legal
APIs only" rules, and the push from the PC.

## 9. Decisions for Aaron

1. Is the core fantasy right: *"It is my car, on my road, right now"*?
2. Is the order right: view and picture first, then traffic and roadside,
   then famous drives?
3. Which road do you drive in real life, for the fourth hero road?
4. OK to pause new live feeds, aerial AI, and more physics work until wave B?
