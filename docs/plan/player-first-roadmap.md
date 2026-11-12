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

- The car responds the way you expect (this is mostly done: the physics is
  good, Aaron said "car drives good").
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

## 5. The roadmap: five big things, in order

Each item has a "done when" that a player notices, an owner, and a size.
Numbers and tests stay, but as guards, not as the goal.

**Hero roads (every item is judged on these first):**

- **City:** Ocean Drive, Miami Beach (the bundled start).
- **Twisty:** a mountain road (US 129 "Tail of the Dragon", or a road Aaron
  chooses).
- **Rural:** a two-lane country road with farms (the Nebraska gravel road, or a
  paved road near it).
- **Aaron's road:** one road Aaron drives in real life (Aaron to choose).

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
**Owner:** "Make every traffic car drivable" (it owns the lens and eye point),
with the cinematographer and ux-accessibility experts. **Size:** S to M.

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
| A | after v2.0.1 freeze | 5.1 view, 5.2 picture (parallel) | v2.1 "It looks right" |
| B | when A is in Aaron's hands | 5.3 traffic, 5.4 roadside (parallel) | v2.2 "It feels alive" |
| C | with B, or after | 5.5 reason to drive | v2.3 "Your road, right now" |

Each wave ends with a **five-minute player test**: a 60-second video on each
hero road, put next to real photos or licensed dashcam video of the same
place, plus Aaron's own drive. The "Add screenshot checks to play tests"
thread builds this capture as one script, and the release gate adds it.

## 7. What to stop or pause

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
  suites, HDR10 output). The car already drives well. Keep the tests as guards
  and fix regressions only.
- **Hourly expert meetings that review every commit.** Change them to one
  review per wave that judges the five-minute player test videos and says what
  a player would notice next.
- **More cars.** Only the Miata, the Mustang and the Corvette, as Aaron said.

Keep: the v2.0.1 release, the check and release gates, the "no money, legal
APIs only" rules, and the push from the PC.

## 8. Decisions for Aaron

1. Is the core fantasy right: *"It is my car, on my road, right now"*?
2. Is the order right: view and picture first, then traffic and roadside,
   then famous drives?
3. Which road do you drive in real life, for the fourth hero road?
4. OK to pause new live feeds, aerial AI, and more physics work until wave B?
