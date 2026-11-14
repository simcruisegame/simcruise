# The team loop: research, plan, build, test, retro (owner ask, 2026-11-14)

One loop for every team. Short and light. One **cycle** = one batch of related changes per team (about one day of work, or less). The producer runs the loop. Cycle files: `docs/team/cycles/cycle-<n>.md`.

## How real studios do it (what we take)

- **Short time-boxed cycles with a working build at the end** (Scrum sprints; Keith, *Agile Game Development with Scrum*, 2010: game teams use 2-3 week sprints that end in a playable build). We are faster, so a cycle is one batch, not weeks.
- **Vertical slice**: each batch makes one visible thing complete end to end (data, code, look, test), not a layer that nobody can see (Keith 2010; common studio practice for milestones).
- **Find the fun / truth early**: playtest every build, watch the real output, do not trust numbers alone (Fullerton, *Game Design Workshop*, 4th ed., 2018, ch. 9 "Playtesting").
- **Retrospective after each cycle**: what went well, what went wrong, what we change, and each change has an owner (Derby and Larsen, *Agile Retrospectives*, 2006; Scrum Guide 2020, "Sprint Retrospective": https://scrumguides.org/scrum-guide.html).
- **Definition of done**: one shared list that every item must meet (Scrum Guide 2020).

## The steps

| Step | Owner | Must produce before the next step starts |
|---|---|---|
| 1. Research | The team's lead expert (AI and Innovation teams for new methods) | A short note: what exists (games, papers, data, licences), what we do better, and the risk. Sources cited, guesses marked (estimate). |
| 2. Plan | Producer, with each team lead | The cycle file: a ranked list, max about 5 items per team, each with owner thread, done-when test, the random US places it must pass, and the skills, plugins and Unreal features to use (`docs/team/tools-and-skills.md`). |
| 3. Build | The owner thread | Code on a branch; fast tests after each change (`scripts\check.ps1 -Tier fast`); small commits. |
| 4. Test | QA and tools (qa-playtester, tools-engineer); the owner thread runs it | Once per batch: full game tests and a real-output review. `scripts\preflight.ps1 -Roads ocean-drive,dragon,nebraska,big-sur,random` (3 random US places, each at local noon), the 3 cars, highest settings; read every contact sheet and frame; compare with the last PASS. **One GPU test at a time on the PC** (the editor lock in `scripts\EditorLock.ps1`). Merge to main only after check.ps1 passes. |
| 5. Review and retro | Producer, with all leads | `docs/team/cycles/cycle-<n>.md` "Retro": what landed (commits, frames), what failed and why, what the owner said, 1-3 changes to how we work, each with an owner. Then the expert review per playtest wave (owner's choice, 2026-11-13). |

The retro feeds the next plan: every retro change and every open defect goes to the top of the next cycle's plan before new features.

## Definition of done (every item)

1. The done-when test passes, and fast tests pass.
2. The game ran: frames or video were read by the thread, at the hero roads **and** at random US places.
3. Highest graphics setting; no new frame-time hitch (frame p95 limit 50 ms in pre-flight).
4. Realism claim has a source or is marked (estimate).
5. On main, pushed from the PC; the owner thread reported the result with a frame.

## Test rule (owner, 2026-11-14)

- After each change: fast tests only.
- Once per batch: full game tests and the frame review.
- One GPU test at a time.
