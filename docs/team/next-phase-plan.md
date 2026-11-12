# Next release phase: team proposal (draft 1, 12 November 2026)

Status: **draft, not agreed.** The team builds this plan in the hourly
meetings. The owner discusses it when the driving release (v2.0.0) ships.
No work on it starts before the owner agrees.

Inputs: the joint review (`review-2026-11-12/joint-findings.md`), each
role's "Next phase proposals" (`review-2026-11-12/<role>.md`), and
`docs/research/domain-ideas.md`.

## Proposed goal

**"It is my car, on a real road."** The three cars (Miata NB, Corvette C8,
1965 Mustang) feel, stop, sound and look right at every frame rate the
28 W laptop can give, on real roads that have real banking, hills and
texture. Each claim has a measured test.

## Ranked themes

| Rank | Theme | What it contains | Size | Acceptance test | Main risk | Proposed owner |
|---|---|---|---|---|---|---|
| 1 | Same car at every frame rate | Physics time budget, controls time-stamped and interpolated on the physics thread, camera maths on physics time, measured 28 W preset per machine, frame cap and pacing | M | Bench with uneven frame times passes for all 3 cars; 2 runs per preset meet budget; frame-time std dev < 1 ms | Async physics edge cases in UE 5.8 | Miata physics thread + performance-engineer |
| 2 | Brakes and drivetrain like the real car | Brake system model (proportioning valve / EBD, ABS per car, pad heat), stick/slip clutch, engine braking with pumping loss, correct diff per year | M | Non-ABS Miata locks fronts first; 70-0 within ±5 ft of tests; coast-down within 10 % of owner log | Few public numbers for the C8 and Mustang | automotive-engineer + physicist |
| 3 | Garage Day: measure the owner's own Miata | Corner weights, alignment, bump gap, steering lock, 60-0, coast-downs, diff type, engine sound recording | S team / M owner | Estimates in `nb2001_aaron.json` replaced by measured values with dates | Owner time; equipment (borrow, do not buy) | automotive-engineer + owner |
| 4 | Feel the road | Wheel mass and tyre spring, ISO 8608 road texture anchored to world position (fills existing roughness inputs), AASHTO banking, grade and vertical curves continuous across tile seams, hilly test fixture | M-L | 10-15 Hz wheel hop on bump bench; profile spectrum matches road class; no roll/pitch event at seams | Higher physics cost on 28 W | physicist + gis-data-scientist |
| 5 | Handling test suite with real targets | ISO 4138, 7401, 7975, 3888-1 with a band per car; nightly driving regression (3 cars at 60 and 15 fps, physics-time check, diff vs last release); 2-hour soak | S-M | Suite runs nightly in `check.ps1`; release diff report | Bands need real data | qa-playtester + physicist |
| 6 | Camera feel and honest speed | Smoothed chase camera, swept collision, shake from suspension, lens chosen by measurement for the 15.6 in screen, speed-judgement test with speedometer hidden | M | < 5 % speed error with speedometer hidden; 5 of 6 drivers prefer it blind | Personal taste differs | cinematographer + magician |
| 7 | The magician's streamer | One scheduler hides every swap (tiles, LOD, parked cars, lights) out of view, faded, or behind a masking event; tile install split over frames | M | Pop-in button test: seen swaps < half of today; no streaming frame > 25 ms at 130 km/h | Complexity in streamer | magician + performance-engineer |
| 8 | Sound that matches recordings | Recorded reference set (owner's Miata; licensed clips only for others), offline match test, mix bus with volume controls, R128 loudness check | M | First 6 engine orders within 3 dB median at 6 points; owner blind A/B 4 of 6; -24±2 LUFS | Licensing of recordings | sound-designer |
| 7b | Light like a film master ("Dolby style") | Display contract (SDR BT.709 reference), optional HDR10/PQ output with 203-nit paper white and calibration screen, Lumen that sees the street (hardware Lumen or distance-field proxies), night light in real units, photo-match regression | M-L | HDR display: grey card 20-30 nits, white stucco 180-220 nits, paint glints ≥ 600 nits; shade within 0.5 EV of licensed photo; ≤ +1.5 ms at 28 W | Laptop panel may be SDR only; Lumen cost | lighting-director + cinematographer |
| 9 | Pad controls for everyone | Automatic clutch option, controls page with remapping, sensitivity and dead zones with live readout (XAG 107), readable HUD | S-M | 10/10 stall-free downshifts under braking; XAG 101/102 sizes and contrast | Menu work in Slate/UMG | ux-accessibility |

## Out of scope for this phase (proposal)

New feeds, satellite imagery, street photos and new knowledge-graph
features stay on hold, as in phase 1, unless the owner moves them in.

## Open questions for the owner

1. Is the goal right, or does the next phase also bring back world
   features (imagery, photos, live feeds)?
2. Can you spend one afternoon on Garage Day (theme 3)?
3. Does your NB have ABS?
4. Default lens: true field of view (about 32° on the laptop, most honest speed) or a wider "natural" view (about 2.4x, more awareness)? Theme 6 tests both.

## Change log

- 2026-11-12: draft 1 from the first team review.
- 2026-11-12 16:53: lens default question added (true FOV landed in ff35887).
- 2026-11-12 14:07: tuning menu (4abe190) becomes the base for theme 3; ride-height and clutch P1s sent to release.
- 2026-11-12 12:52: items 1, 8, 10 fixed on main; theme 7 builds on the new step installer (47a9edf).
- 2026-11-12: lighting-director joined (owner request); theme 7b added.
