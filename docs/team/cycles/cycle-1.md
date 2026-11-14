# Cycle 1 (started 2026-11-14 01:50Z)

Goal: make the drive look and feel real at any US place, starting with what the owner sees in the cockpit and on a hill road.

## Plan

| # | Item | Owner thread | Done when |
|---|---|---|---|
| 1 | Cockpit: wheel turns on its column axis; gauges stay fixed to the dash; wheel speed and turns lock to lock match the sources | Make the driving and traffic feel real | Cockpit capture lock to lock for the 3 cars: only the wheel turns, rim stays on its circle (3 mm), gauges fixed; turns within 5 % of the source |
| 2 | Scan engine with terrain and trees: road step fix, fallback ground for failed tiles, load ahead by speed, terrain shape and surface (no lime green), real trees from aerial crowns | Make buildings and terrain detailed and familiar | Before/after frame at the owner's hill-road spot; no fall-through test; 130 km/h test; `-Roads random` sheets read |
| 3 | Picture faults: photo match at High and Ultra | Make the picture look like a photo | Frame next to a real photo of the same place, with luma and colour error noted |
| 4 | Local AI on the CPU: legal models for each scan channel | Use local AI without the GPU (Add local AI that uses only CPU) | Each model: licence, CPU ms per tile, error at 3+ random US places |
| 5 | Innovation research: hyper-detail methods for the scan engine | Research and invent hyper-detail methods | Each method: experiment, metric, pass line; scientist and mathematician reviewed |
| 6 | Team: the loop, scientist and mathematician first reviews of the scan engine | Build a game team that owns the game | `docs/team/dev-loop.md` on main; reviews sent to the world thread |

Open defects carried in (from `docs/team/meetings/2026-11-14-onboarding.md`): tile with no ground, one-tile reach at speed, climate boxes, untagged gravel, no random-place release gate.

## Test

Full game test and frame review once this batch lands (see `docs/team/dev-loop.md` step 4).

## Retro

(Written when the batch lands.)
