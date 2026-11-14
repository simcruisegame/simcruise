# Producer: study notes

## Rules
- One ranked list for the whole game. Each item has one owner thread, a size (S/M/L) and a done-when test a player would notice.
- Scope is cut, not stretched: when a release is late, drop the lowest items, never the tests.
- Each release ships an updated README, current screenshots and tested installers (owner rule, 2026-11-12).
- Owner's priority (2026-11-13): a driver's game first; high-speed feel, traffic off, Tail of the Dragon as acceptance; world work = one AI detail layer, then stop.
- Work moves in big tested playtest waves; one expert review per wave (owner, 2026-11-13).

## Method
- Milestones with a playable build at each one; a short "what changed, what to test" note for the owner.
- Track risk early: unknowns (no data, no measurement) get a spike task before the real task.

## Sources
- H. M. Chandler, *The Game Production Handbook*, 3rd ed., Jones & Bartlett (O'Reilly listing: https://www.oreilly.com/library/view/the-game-production/9781449688097/).
- C. Keith, *Agile Game Development with Scrum*, Addison-Wesley, 2010.
- UMBC game development roles (producer, QA, leads): https://userpages.cs.umbc.edu/olano/class/493-24-2/roles.md.html

## What I now know (onboarding, 2026-11-14)

**How my area works today.**
- The code tree has 10 folders under `Source/SimCruise/Private/`. The largest are Gameplay (36 files, 14,351 lines) and World (22 files, 11,668 lines). Next are Presentation (6,573), Tests (4,225), Vehicle (4,060), Live (3,015), Knowledge (2,993), Environment (2,410), Audio (460) and Atlas (343). The largest single files are `Presentation/SimCruiseCarModelRig.cpp` (1,803 lines), `World/SimCruiseTileDetail.cpp` (1,522) and `World/SimCruiseRoadNetwork.cpp` (1,488). The map helper has 52 `helper/*.mjs` modules.
- Start: `SimCruiseGameMode.cpp:180` uses `-SimCruiseStart=lat,lon` ("chosen"). If that is not given, `WantsRandomStart` (`:69`) draws from `data/start/start-pool.json` (`:185`). Checks, captures and the self-test start on the bundled Ocean Drive tiles (`:72-73`).
- `data/start/start-pool.json` has 36 roads. All of them are famous or scenic drives (Dragon, Big Sur, Beartooth, Hana Highway ...). None of them is an ordinary place.
- `data/test-places.json` has 18 ordinary places, from `us-ankeny-ia` to `us-hilo-hi`.
- `scripts/preflight.ps1:40`: by default it drives only the 4 hero roads. `-Roads random` draws 3 test places (`:95`), with the date as the seed. Every run uses `-SimCruiseNoLive -SimCruiseNoNeural` and a fixed `-SimCruiseUtc=2026-11-12T17:00:00Z` (`:43`, `:121`).
- `SimCruisePreflight.cpp:76` `Judge()` fails a run on these items: distance, off road, fall-back to the bundled place, view check, white sky, tile or car pop-in, flashes, road steps or gaps, border jolts, stuck traffic, spin, p95 frame time and hitches. It has no check for blank ground, a missing aerial, an empty roadside or missing buildings.
- `scripts/playtest.ps1:54-55` lets a build go to the owner when any PASS row for the commit is in `preflight-index.csv`. It does not check which roads, cars or how many seconds that run used.
- `scripts/release.ps1` packages the game, checks the zip (no `.pdb`, `Saved` or `.onnx`; `atlas.exe` present, `:47-50`), builds the installer and runs `-SimCruiseShellSelfTest` (`:58`). That self-test starts on the bundled Miami tiles. The script does not read the pre-flight index.
- `docs/release/RELEASE-CHECKLIST.md` section 1 gates on `check.ps1`, `drive-robot.ps1` and the 2.0.0 driving checklist. It has no pre-flight row and no random-place row.
- The ranked list is the meeting of 2026-11-13 18:00 (7 items, Dragon acceptance). On 2026-11-14 the owner opened world work again (`ownership.md` Updates). That cancels meeting item 7 "then stop".

**Does it work at a random US place?**
- General: start-by-coordinate, the tile stream, the snap to the nearest road, the pre-flight measurements and the aerial sources for the lower 48, Hawaii (`hi-naip`) and Alaska (`ak-usfs`) in `helper/regional-aerial.mjs:13-14`.
- Hard-wired to one place: `StyleForPlace` (`SimCruiseTileDetail.cpp:219-227`) gives Art Deco only inside a Miami Beach lat/lon box. `IsCoconutPalmRegion` (`:230-236`) covers only South Florida and Hawaii. The place graph has local data only for the bundled Miami tiles (`data/knowledge/places/18178-18181_*`) and 4 `areas` tiles.
- The place graph has 15 regions, all lat/lon boxes (`place-graph.json`). I mapped the places against them. Rawlins WY, Delta UT and Anchorage AK fall in no box. In the pool, Going-to-the-Sun, Beartooth, Colfax/Denver, Telluride and Seward Highway also fall in no box. With no box, `RegionFacts` (`SimCruisePlaceGraph.cpp:276-281`) uses a latitude band. That gives `climate:humid_continental` to the high desert of WY and UT, which is wrong (Köppen BSk/BWk) (estimate). Welch WV and Hazard KY get `region:midwest`.
- Test time: 17:00Z gives about 12:00 in the Bronx, but the sun is at -6.8° in Anchorage (twilight) and at 6.4° in Hilo (estimate, low-precision formula per https://aa.usno.navy.mil/faq/sun_approx). A random-place pass in AK or HI is not a fair daylight test, and its white-sky and flash results will mislead.
- Poor map data (Welch WV hollow, Casselton ND gravel): `Judge()` cannot see an empty or blank world. A bare place can PASS (estimate).

**Main gaps against "realistic everywhere".**
1. P0 | No release gate checks random places | `scripts/playtest.ps1:54`, `scripts/release.ps1`, `RELEASE-CHECKLIST.md` §1 | Require a PASS on the 4 hero roads plus `-Roads random` (3 places, 3 cars) for the commit, and check the roads column of the index row.
2. P1 | The pre-flight cannot see a thin world | `SimCruisePreflight.cpp:76` | Add counts per run: buildings in view, roadside objects per km, aerial present, ground-texture fall-back. Fail when a place with mapped buildings shows none.
3. P1 | Region facts have holes and errors | `data/knowledge/place-graph.json` regions; `SimCruisePlaceGraph.cpp:276` | Replace the boxes with a US-wide climate and ecoregion source, for example the Köppen rasters (Beck et al. 2018, https://www.nature.com/articles/sdata2018214) and the EPA Level III ecoregions (https://www.epa.gov/eco-research/ecoregions). Owner: World (gis-data-scientist).
4. P1 | Building style and palms are place boxes, not rules | `SimCruiseTileDetail.cpp:219-236` | Choose them from graph facts (climate, era, use). Keep Miami as a test case only.
5. P2 | The pre-flight time is fixed in UTC | `preflight.ps1:43` | Use local solar noon per place, plus a separate night pass.
6. P2 | The detail layer and live data are never in a pre-flight | `preflight.ps1:121` (`-SimCruiseNoNeural -SimCruiseNoLive`) | Add a pass with them on before world-detail work merges.

Areas with no owner or no done-when test (`ownership.md` team table):
- Audio has no done-when ("—").
- Traffic (roadmap 5.3) is out of the release, and no thread owns it now.
- Roadside 5.4 and "reason to drive" 5.5 name the threads "place knowledge graph" and "Restore go-anywhere". Neither thread is in the table.
- No team owns "works at any random US place" as a gate. QA and tools should own it.
- Start-pool content has no owner.

**What each team needs for a "works at any random US place" release:**
- World: correct region facts US-wide, plus a fall-back look for poor data.
- Car team: no change, if roads have grade and banking.
- Picture: local time of day.
- Design: the pool, or "Surprise me", includes ordinary roads.
- QA and tools: a random-place gate with thin-world checks.
- Production: the release checklist row.

**My first three actions.**
1. Put a random-place row into `docs/release/RELEASE-CHECKLIST.md` §1. Ask tools-engineer to make `scripts/playtest.ps1` refuse a commit with no PASS that has the 4 hero roads plus `random` and 3 cars. Done when: `scripts\preflight.ps1 -Roads ocean-drive -Seconds 20` alone gives REFUSED, and `scripts\preflight.ps1 -Roads ocean-drive,dragon,nebraska,big-sur,random` passing gives a build.
2. Run one desk-to-run spike on the worst-judged places with `scripts\preflight.ps1 -Roads us-welch-wv,us-rawlins-wy,us-anchorage-ak,us-bronx-ny -Cars nb2001_aaron -Seconds 60`. Read every `*-sheet.png` and log each defect in `docs/release/bugs.md` with its frame. Done when each of the 4 places has a row that names the defect or says it is clean, and each defect has an owner.
3. Rebuild the ranked list for the next release with "random US place" as the release goal. Give every item from gaps 1-6 one owner thread and a done-when test. Fill in the Audio done-when and an owner for traffic. Done when the `ownership.md` team table has no "—" and no thread that is missing from the table.

**Docs that are wrong or old.**
- `docs/team/ownership.md:13` points to `docs/team/test-places.md`. That file does not exist; the places are in `data/test-places.json`.
- `ownership.md` "Launch and look" step 3 runs `preflight.ps1` without `-Roads random`, so it tests only the hero roads. That breaks rule 5 in the same file.
- `docs/team/producer.md` and `docs/team/meetings/2026-11-13-18.md` item 7 say "world work stops". The owner reopened world work on 2026-11-14. `docs/team/next-phase-plan.md` is still "draft, not agreed", and `docs/roadmap.md` is the Godot-era plan (it says so on line 3).
- `docs/team/how-the-game-works.md` says the helper has "about 80 modules". `helper/` has 52 `.mjs` files.
