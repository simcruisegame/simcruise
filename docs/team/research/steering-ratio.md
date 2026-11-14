# Steering ratio and wheel speed audit (14 November 2026)

Owner: "the steering wheel does not turn realistically, too fast. Look up the real ratio." automotive-engineer, code reading and sources only; nothing was run.

**Result: the ratios in the car files are right. The wheel looks too fast because the pad moves the steering too fast, and the lock gives too many turns.**

| Car | Real ratio / turns lock to lock | Source | Game (ratio, lock, turns) | Mismatch |
|---|---|---|---|---|
| Miata NB 2001, power steering | 15:1, 2.6 turns, 9.2 m circle | GoAuto, 14 Feb 2001: https://goauto.com.au/amp/mazda/mx-5/convertible/2001-02-14/35945.html | 15.0, 36.6° rack (37.1° mean), 3.09 turns | +19 % turns; 2.6 turns gives a 31.2° mean lock |
| Corvette C8 Stingray | 15.7:1 on centre, variable-ratio rack; 2.5 turns; 11.6 m | Chevrolet preliminary specifications: https://corvetteactioncenter.com/specs/c8-corvette/2020-corvette/press-release-2020-corvette-preliminary-specifications.html ; turns: https://autotrader.co.nz/car-reviews/chevrolet-corvette-stingray-3lt-2022 | 15.7 fixed, 34°, 2.97 turns | +19 %; the mean ratio to lock is about 13.6 |
| 1965 Mustang, manual | 21.7:1, 3.73 turns | Car Life 1965 road test: https://www.tunnelram.net/news-blog/2016/9/13/original-road-test-1965-mustang-4-speed | 21.7, 33°, 3.98 turns | +7 %; 3.73 turns gives a 31.1° lock |

## Why it looks too fast

`SimCruiseDriverShaping.cpp:166-167` (`.h:35-36`): the pad applies 2.75 × the available steer angle per second, so the wheel reaches full stick in 0.36 s and returns in 0.23 s. Miata hand-wheel speed: about 1510°/s at parking speed (2416°/s on return), about 700°/s at 10 m/s, about 290°/s at 25 m/s. NHTSA's steering robots turn at 1000°/s (J-turn) and 720°/s (fishhook): https://www.govinfo.gov/link/fr/68/59254 . At low speed the game is faster than a test robot.

## Fixes

1. P1: cap the pad rate at a hand-wheel speed: about 540°/s apply (estimate) and 720°/s return, divided by the ratio. Done when a stick step from rest to full takes ≥ 1.0 s for the drawn wheel to reach 90 % of lock, and no pad test exceeds 720°/s at the wheel.
2. P1: turns lock to lock match the source: add `turns_lock_to_lock`; a ratio curve for the C8's variable rack. Done when drawn turns are within ±0.05 of the table and the turning circle within ±0.3 m of the spec.
3. P2: check the Miata lock (36.6° gives about 8.1 m against 9.2 m spec; kerb-to-kerb or wall-to-wall not yet checked).
4. Owner (Garage Day): count turns lock to lock, measure road-wheel lock, chalk the turning circle.

Owner thread: Make the driving and traffic feel real (car team).
