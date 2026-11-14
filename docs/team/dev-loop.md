# The team loop: the release train (owner asks, 2026-11-14)

One loop for every team. Short and light. One **cycle** = one version; all teams build at the same time and test together once. The producer runs the loop. Version files: `docs/team/cycles/<version>.md`.

## How real studios do it (what we take)

- **Short time-boxed cycles with a working build at the end** (Scrum sprints; Keith, *Agile Game Development with Scrum*, 2010: game teams use 2-3 week sprints that end in a playable build). We are faster, so a cycle is one batch, not weeks.
- **Vertical slice**: each batch makes one visible thing complete end to end (data, code, look, test), not a layer that nobody can see (Keith 2010; common studio practice for milestones).
- **Find the fun / truth early**: playtest every build, watch the real output, do not trust numbers alone (Fullerton, *Game Design Workshop*, 4th ed., 2018, ch. 9 "Playtesting").
- **Retrospective after each cycle**: what went well, what went wrong, what we change, and each change has an owner (Derby and Larsen, *Agile Retrospectives*, 2006; Scrum Guide 2020, "Sprint Retrospective": https://scrumguides.org/scrum-guide.html).
- **Definition of done**: one shared list that every item must meet (Scrum Guide 2020).

## The release train (owner, 2026-11-14 03:19Z)

Owner: "all meeting and then going off in their corners and working on stuff, adding it, testing it and then next version ... the whole team is testing their stuff at once." So one cycle = one **version**, and all teams work at the same time.

| Step | Owner | Output before the next step |
|---|---|---|
| 1. Meet | Producer with all team leads | The version file `docs/team/cycles/<version>.md`: **one clear objective per team**, its done-when test, its random US places, and the tools to use (`docs/team/tools-and-skills.md`). Research notes come in here. |
| 2. Build | Each team in its own lane, all at the same time | Changes on the team's branch. **Only fast tests** on its own changes (`scripts\check.ps1 -Tier fast`). No full game test of single changes. |
| 3. Join (cut-off) | Producer, tools engineer | All lanes merge into one release candidate on main. Cut-off rule below. |
| 4. Test once | QA and tools runs it; every team reviews its own area in the same run | One full test of the whole candidate: Ultra, hero roads plus `-Roads random`, the 3 cars, frame review, cockpit lock-to-lock test, controller menu test. One GPU test at a time. A defect list with frames. |
| 5. Fix | Owner teams | Only fixes for faults the test found; then **one** re-test. |
| 6. Release, then retro | Release thread; producer | Tag, README, screenshots, tested installers (release rule). Retro in the version file: what landed, what failed, 1-3 changes with owners. Then step 1 of the next version. |

**Cut-off: the ready check (owner, 2026-11-14 03:19Z).** The coordinator asks each team "are you ready to test?". A team is ready when its objective is merged and passes its fast tests; it sends the coordinator "READY TO TEST" with one line on what it merged. When every team says ready, the one shared test runs. After the test, everyone learns from it (retro). Then each team goes back to its lane: first it fixes what the test found broken, then it goes on to its next idea.

The retro feeds the next meet: every retro change and every open defect goes to the top of the next version before new features.

## Who manages the train (owner, 03:20-03:23Z)

The **coordinator is the manager**: it sets the criteria below, runs the ready check, calls the shared test, sends each team its feedback, and decides when a tested state is worth a release.

Order of one version: **meet → build in corners → ready check → test together → feedback to each team → corners again for fixes → meet for the next version.**

## READY TO TEST (each team, before it says ready)

1. Its objective for the version is merged to main.
2. `scripts\check.ps1 -Tier fast` passes on main with its change.
3. The team examined real frames of its own area against a real reference (photo, manual, real car) one time, not numbers only.
4. No known crash and no known fault that breaks another team's area.
5. The docs for the change are updated in the same merge: the feature or system doc, the changelog entry, and README or CREDITS.md if the change affects them.

## RELEASE STANDARD (the one shared test; all must be true)

1. Full test at Ultra passes on the hero roads and on random US places, with no crash and no stuck game.
2. Frame review finds no fault that a player sees at once (cockpit parts, road steps, flicker, a grey or soft picture worse than the last release).
3. Frame rate and input delay are not worse than the last release.
4. The Xbox controller reaches every menu control.
5. Each car completes its fast Dragon run with no spin.
6. Each new data source and model has its licence recorded and its credit in CREDITS.md.
7. README, CREDITS.md, changelog, current screenshots, team docs and tested installers are updated. The docs update is part of the release, not a later task.

A fault against the release standard is fixed before release, or its change is removed from the candidate. Other findings go to the retro and the next version.

**Release decision.** The coordinator decides when the tested state is worth a release. Then the release goes to main with the docs updated, and the coordinator tells the owner.

## Finish before new

A team does not start a new idea until its current objective meets the criteria (READY TO TEST, then the release standard). Faults from the shared test come first.

## Innovation meeting

Every second release, all teams meet with the innovation team, which brings new ideas. An accepted idea becomes an objective for a later version, never mid-version work.

## Big questions (at each meet and each retro; each team answers in one or two lines)

1. Why does this work matter to a driver in the seat?
2. Does it work anywhere in the US, and what happens where the data is missing?
3. Is there a simpler or bolder way to get the same result?
4. What would a player see that our numbers do not show?
5. What detail did we assume and not check against the real thing (real car, real photo, real manual)?
6. Does it fit the whole game, or does it only make one piece better?

Keep the repo and docs current as the work goes, not only at release.

## Definition of done (every item)

1. The done-when test passes, and fast tests pass.
2. The game ran: frames or video were read by the thread, at the hero roads **and** at random US places.
3. Highest graphics setting; no new frame-time hitch (frame p95 limit 50 ms in pre-flight).
4. Realism claim has a source or is marked (estimate).
5. On main, pushed from the PC; the owner thread reported the result with a frame.

## Test rule (owner, 2026-11-14)

- During build: fast tests only, on your own changes.
- Once per version: one full game test of the joined candidate and the frame review, all teams together.
- One GPU test at a time.

## Hygiene (owner, 2026-11-14)

The D: drive is 100 GB and every lane fills it with test runs, packages and build caches. Clean what you do not need.

- At the end of each release, each lane runs `scripts\clean-lane.ps1` on its own checkout: first without `-Apply` (it only reports), then with `-Apply`. It keeps the newest 3 runs of each run folder (Preflight, Review, ReleaseTest, ViewCheck, view-check pictures, Profiling, Crashes) and removes logs older than 7 days.
- Add `-Package` when your package outputs (Saved\Package, StagedBuilds, Cooked, Shaders) are not the release you test now.
- A lane that is idle for a day: `-Package -Build` (it also removes Intermediate, Binaries and DerivedDataCache), or remove the worktree when all its commits are on origin. Push a backup branch for uncommitted files first.
- Never clean another lane that is in use, and never clean D:\simcruise (Aaron plays from it). The script stops when an editor, game, build or test of the checkout runs.
- Keep the evidence that the current release test uses until the release is out. Put files that must stay in `docs/release/evidence-<version>/`, not only in Saved.
- Temporary files (logs, patches, scratch scripts) go in your session scratchpad or in your lane's Saved folder, not loose in D:\sc.
