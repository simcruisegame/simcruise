# Driving thread handoff (2026-11-13)

## Done (on main)

- Brakes: Miata proportioning valve and 30 % rear split, real full-pedal capacity, threshold-braked 70-0 bench, ISO 7975 brake-in-turn bench (`SimCruise.Vehicle.BrakeInTurn.*`), straight-line split check (BRAKESPLIT).
- Engine braking with closed-throttle pumping; roll centres (NB 45 / 90 mm); Torsen coast bias (NB 1.5).
- Menus: stick and trigger settings, the pad reaches every atlas setting, "Your car" row (Tune car anywhere, Seat & view), Traffic Normal / Light / Off, start hint.
- Views: seat, hood, near and far chase, scenic; look-around on the right stick; slight widening with speed; Seat & view menu saved per car.
- Crash fixes: no tyre contact on near-vertical ground, corner load capped at 10x static (611f955); contact guard on body speed and spin (e10835b, release cherry-pick 977aaed).
- Tune car steering ratio no longer reads 0.0; Mustang eye 6 cm higher.

## Off or unfinished

- Throttle-plate progression (`throttle_progression`) is in the code but 0: with the current trigger curve it spun turning launches. Retune it together with `simcruise.TriggerGamma`.
- Live dash gauges: code dormant (presentation profile `dials`, rig `FDial`, render-target faces); the first capture had misplaced faces and no numerals. NB dial hubs are in the vehicle-physics memory notes.

## Next (owner's priority: driving at high speed, traffic off, highest settings, Tail of the Dragon)

1. Corvette `meridian_z` runs wide at speed: `axle_grip` 1.0 front / 1.5 rear. Rebalance and refit the skidpad, then drive it fast on the Dragon.
2. Owner's Miata has ABS: set `abs_enabled` for `nb2001_aaron`, re-check 70-0 and brake-in-turn; add a brake-trigger curve (the fronts of a non-ABS car lock from about 60 % LT).
3. Physics audit for all three cars (mass, CG, inertia, springs, damping, bounce) with measured drives.
4. Variable-ratio racks (NB 2.6 turns, C8 about 2.5); the visible wheel should follow a tuned ratio.
5. A "keen driver" pace for `scripts\preflight.ps1` as the acceptance drive; the robot's steering gains assume the NB.
6. Later: tyre squeal at the limit, road joints, gauges for all three cars.

## Update (later on 2026-11-13)

- Done: Corvette grip balance (axle_grip 1.0/1.1, TMeasy fit: 70-0 149 ft, skidpad 1.032 g, understeer 3.1 to 0.8 deg/g; Dragon pre-flight at the normal pace stays on the road). ABS on Aaron's Miata and the brake trigger curve (trigger^1.4); refit 70-0 159 ft, skidpad 0.981 g; no wheel locks in the brake-in-turn bench.
- New: `scripts\preflight.ps1 -Keen` (no speed limit, 75 % of skidpad grip, brake at 70 %, 45 m/s cap, 200 m look-ahead, traffic off).
- First keen runs on Tail of the Dragon (1843f2a, report Saved\Preflight\20261113-123949 in lane1): all three fail. Miata spun at 44 mph in 2nd entering a bend and sat in the verge at full throttle; Corvette off the road 17 s (jolt 136 m/s^2, the guard clamped spin once); Mustang stopped after 144 m with jolts. Next: log the robot's line, throttle and yaw on these runs to separate robot limits (corner speed from 5 m path steps, exit throttle, an automatic in 2nd at 6600 rpm) from car behaviour; the robot should lift when the rear slides.
- Not started: recording and replaying an F9 drive with the replay tool.
