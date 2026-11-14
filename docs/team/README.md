# The SimCruise expert team

The team is a set of expert subagents. Any thread or session that works in
this repository can call them. Each expert has two files:

- `.claude/agents/<role>.md` is the subagent. It tells Claude Code who the
  expert is, what to read first, what to check, and how to report.
- `docs/team/<role>.md` holds the expert's study notes: rules, numbers,
  test methods and a list of real sources (papers, books, standards, Epic
  docs). The subagent reads this file before it starts.

## The team's goal

The owner set the goal on 2026-11-12: **plan the next release phase.**
The team builds a proposed plan across the hourly meetings in
`docs/team/next-phase-plan.md`: goals, scope, ranked features, owners,
acceptance tests and risks. The plan is ready to present when the current
driving release ships. The owner discusses it then. The team does not
start next-phase work before the owner agrees.

Reviews of the current game are input to that plan. Current driving bugs
that the team finds go to the release thread through the coordinator.

## Charter and ownership

The team charter and the map of teams, leads, owner threads and current work are in `docs/team/ownership.md`. Read it first.

## The members

| Role (subagent name) | Call it when you need |
|---|---|
| `game-developer` | Unreal 5.8 C++ review: tick order, physics substeps, GC, `WITH_EDITOR`, packaging, tests. |
| `qa-playtester` | Test plans, repro steps, regression and soak tests, bug severity, controller-only play. |
| `vehicle-dynamics-physicist` | Tyre model, load transfer, understeer, yaw, stability of the integrator, ISO/SAE test manoeuvres. |
| `automotive-engineer` | Real-car numbers for the Miata NB, Corvette C8 and 1965 Mustang: mass, springs, gearing, torque, brakes, diffs, steering. |
| `magician` | Perception: what the driver notices, how to hide pop-in and LOD swaps, how a cheap world feels detailed. |
| `cinematographer` | Cameras, lenses, FOV, head motion, exposure, tone map, colour, light, replay shots. |
| `lighting-director` | Light in physical units (lux, cd, cd/m²), night and street lighting, auto exposure chain, tone range, SDR/HDR output (PQ, paper white, "Dolby style"), Lumen GI/reflection correctness. |
| `sound-designer` | Engine, tyre, road and wind sound; interior and exterior mix; MetaSounds; loudness. |
| `ux-accessibility` | Gamepad steering curves, dead zones, haptics, HUD, menus, comfort and accessibility guidelines. |
| `gis-data-scientist` | Real-world data: OSM, elevation, projections, road geometry, licences and rate limits. |
| `performance-engineer` | Frame time and hitches on the 28 W GPU, scalability, streaming, game-thread cost. |
| `producer` | One ranked list, scope, owner threads, releases. |
| `game-designer` | The reason to drive, first five minutes, realistic defaults, menus and HUD from the player's view. |
| `technical-artist` | Materials, shaders, PBR values, car and cockpit rigs (wheel, gauges), LODs, texture budgets. |
| `environment-artist` | Roadside, ground, vegetation and buildings against real photos and road standards. |
| `tools-engineer` | check, pre-flight, view check, replay, packaging, installers; tests that fail when they should. |

## How to call the team

From any Claude Code session in this repository (a lane worktree on the PC
or a cloud clone), use the Agent tool with the role as `subagent_type`:

```
Agent(subagent_type: "vehicle-dynamics-physicist",
      prompt: "Review my change to SimCruiseTireModel.cpp (diff below). Check the slip definition and the relaxation length.")
```

Rules for callers:

1. Give the expert the goal, the files or the diff, and what you already know.
2. Call only the experts that the change touches. Two or three is normal.
3. For a large question, call several experts in one message. They run at
   the same time. Then merge their findings into one ranked list.
4. The experts review and advise. They do not change files unless you ask.
5. The experts follow the current phase (see project memory). In phase 1
   that is bugs and the driving experience only.

Each expert returns ranked findings in one format:
`severity (P0/P1/P2) | title | evidence (file:line or test) | fix | how to verify`.

## Team review (all members)

To get a full team review, call all ten experts in one message with the same
question, then write one joint ranked list. The first review is in
`docs/team/review-2026-11-12/` and the joint list is
`docs/team/review-2026-11-12/joint-findings.md`.

## Review per playtest wave

Since 2026-11-13 (owner's choice) the team reviews once per playtest wave, from the pre-flight videos and contact sheets, not every hour. The old hourly steps below still describe a review:

1. Read what changed on `origin/main` since the last meeting
   (`git log <last>..origin/main`).
2. Call the experts whose areas the new commits touch.
3. Write short minutes to `docs/team/meetings/<YYYY-MM-DD-HH>.md`:
   what changed, findings, decisions, owners.
4. Update `docs/team/next-phase-plan.md` with what the meeting learned.
5. Send new driving bugs to the release thread ("Ship the
   driving-experience release") through the project coordinator.

If nothing changed on main, the minutes say so in one line.

## How to keep the team trained

When an expert learns a new verified fact (a measured number, a new paper,
a test result from the game), add it to `docs/team/<role>.md` with its
source. Keep each notes file short enough to read in one pass.
