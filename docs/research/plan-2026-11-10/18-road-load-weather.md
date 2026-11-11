# 18. Road load and conditions: per-wheel rolling resistance f0+f2v², aero lift and altitude density, wet grip from live weather (road-load-weather)
category physics · impact 3 · 3 d · depends: tyre-mf-combined-core, road-edge-authority

## Rationale
Coast-down and lift-off deceleration govern everyday driving feel. Today rolling resistance is one constant central force, aero is drag only at sea-level density, and METAR rain only washes the paint, so a wet road still grips like a dry one. The EPA publishes measured coast-down coefficients that can be checked against.

## Code claims (proposer)
Powertrain.gd:476-477 apply drag ½·1.225·CdA·v² at the COM and 0.012·m·g central rolling resistance. There is no lift field (grep). helper/conditions.mjs:30 derives the wet flag, used only by CarWear.gd:67. WorldTile.wheel_grip (WorldTile.gd:612-) and Powertrain have no wet term. Drive.gd:41 live_weather=false by default.

## Design (proposer)
1) Rolling resistance per wheel = Fz·(f0 + f2·v²) as a wheel-axis torque opposing omega, inside the substep wheel torque. f0 about 0.009 and f2 about 6.5e-6 s²/m², or the tyre QSY1-style value from the tyre block.
2) lift_area_m2 [front, rear], signed, applied at the axles. An aero_centre_height for the drag pitch moment. ρ = 1.225·(1 - 2.2558e-5·h)^4.2559 from map elevation. The same density ratio derates combustion torque.
3) Surface water w ∈ [0,1] from METAR precipitation intensity, decaying with a 15 min dry time constant, plus a manual Dry/Wet/Snow choice.
- surface_grip ×= mix(1, wet_mult(class), w), with asphalt 0.70, gravel 0.85, snow 0.30.
- Hydroplaning loss above Horne 10.35·sqrt(psi) mph, scaled by depth.
- The same w drives a Lagarde wet road shader: diffuse ×= lerp(1, f, w), gloss up, puddles from profile low points.
4) A Coastdown check per car: 75 to 15 mph in neutral, fitting A + Bv + Cv². Sourced target_road_load_abc from the EPA test car list (C8; NB if listed).

## Gate
Coastdown: total force at 30/50/70 mph within ±10 % of EPA A/B/C where sourced. WetTrack variant: 70-0 at 1.35-1.6× dry and skidpad at 0.65-0.75× dry. Dry CarTrack bands bit-identical when w = 0 and lift = 0 (verified first), and re-fit only if rolling resistance moves 0-60 by more than 0.02 s. TractionControlCheck and AbsReverse also pass when wet. Road shader frame cost within +0.2 ms at Ultra.

## Files
game/Powertrain.gd
game/CarSpec.gd
game/WorldTile.gd
game/terrain.gdshader
helper/conditions.mjs
game/checks/Coastdown.gd
game/checks/WetTrack.gd
docs/cars.md

## Skeptic verdict
claims_hold true · already_done false · keep false

### Wrong claims
- Not literally '0.012·m·g'. Powertrain.gd:477 uses spec.rolling_resistance, which defaults to .012 in CarSpec.gd:78. Only nb1999.json sets it, also to 0.012, so the claim holds in effect. The force is also clamped to min(v·m/dt, …), so at very low speed it acts as a stopping force, not a plain constant.
- live_weather=false is at Drive.gd:42, not line 41 (off by one).
- 'Powertrain has no wet term' is true, but the proposed place for the multiplier is wrong. Grip enters Powertrain only through vehicle.surface_grip[i] (Drive.gd:672 -> Powertrain.gd:345). RoadNoise.gd:22/128 and CarWear.gd:64 read that same value as a surface CLASS (1.0 paved, .76 gravel, .65 off-road). Multiplying wheel_grip or surface_grip by a wet factor would change road noise and dirt build-up as well as grip.
- WorldTile.surface_grip is not the only grip path. wheel_grip returns measured_section.surface() and the unpaved .76 before or around it, so 'surface_grip ×= mix(...)' (design 3) would miss measured sections.

### Design problems
- Does not answer the user's question. The user asked about the AI engine discussed earlier for traffic, extras and detail generation. This proposal covers coast-down, aero and wet-grip physics, so it does not belong in this answer.
- The gate 'Dry CarTrack bands bit-identical when w=0 and lift=0' cannot hold as designed. Step 1 replaces the central rolling-resistance force with a per-wheel torque and adds an f2·v² term no matter what w and lift are. Bit-identity only holds behind a flag, or with f2=0 and the old central path kept.
- Moving rolling resistance into the substep wheel torque loses the current min(v·m/dt) standstill clamp. The torque must be clamped so it never reverses omega. Otherwise low-speed creep, stopping, KeyboardTap, LaunchCheck and the automatic's reverse-at-rest logic (AbsReverse) change. These gates are not listed.
- Coast-down against EPA A/B/C is not like-for-like. EPA target coefficients include driveline spin losses in neutral, and Powertrain has no wheel-side driveline or bearing drag. Fitting f0/f2 to EPA folds those losses into the tyre. The f0+f2v² form also has no B (linear) term, while EPA B is often large or negative. For the EV (volta), regen_torque_nm applies whenever throttle<.01 (Powertrain.gd:518), so a 'neutral' coast-down must turn regen off.
- Altitude density needs an absolute elevation. WorldTile heights come from DEM tiles, and Drive.gd:674 mentions rebasing, but nothing shows world Y is metres above sea level. This needs checking before ρ(h). Derating combustion torque by the density ratio also changes 0-60 at any CarTrack site that is not at sea level.
- The hydroplaning model is unsound for METAR input. Horne 10.35·sqrt(psi) is for standing water deeper than the tread. METAR intensity says nothing about water depth, and CarSpec has no tyre-pressure field. As specified it would cut grip on damp roads at highway speed, which goes against 'drive like the real car'.
- The METAR wet regex in conditions.mjs counts SN/SG/PL/GR as 'wet'. Snow needs its own class, and RegionalSnow.gd/SummitSnow.gd already exist for visual snow cover and should be the source.
- The road shader scope is too narrow. Roads and decks are also drawn by BridgeDeck, LaneMarkings, MeasuredGranite, HistoricRibbon and others, so terrain.gdshader alone gives patchy wet roads. The Mobile renderer also has no SSR, which limits wet gloss. The +0.2 ms Ultra budget needs measuring.
- depends_on lists 'tyre-mf-combined-core' and 'road-edge-authority'. These are other proposals, not existing code, so the work is blocked behind them. The effort of 3 days is optimistic for physics + weather + shader + 2 new checks + re-fitting 7 cars.
- EPA target coefficients exist for the C8 (meridian_z / Corvette per docs/cars.md). They are unlikely to exist for the 1999 NB or the 1965 Mustang, so the Coastdown gate covers only some cars.

### Corrected design (USE THIS where it differs)
Leave this out of the answer to the AI-engine question; it belongs in the physics backlog. If kept there, do it in phases. (a) Add spec fields rolling_f2 (default 0) and lift_area_m2 (default [0,0]). Keep the central rolling path and its standstill clamp when f2=0, so CarTrack stays bit-identical, and verify that first. Only then move to a per-wheel torque clamped so it cannot reverse omega, and re-run CarTrack, LiftOff, KeyboardTap, LaunchCheck and AbsReverse. (b) Coastdown check with regen off and an explicit driveline-loss term (wheel-side drag torque, f(omega)), so EPA A/B/C are matched physically and not folded into the tyre. Gate only cars with sourced EPA rows (the C8). (c) Wet grip as a separate Powertrain factor, wet_grip[i], multiplied in at Powertrain.gd:345. Never write it into surface_grip, because RoadNoise and CarWear classify surfaces from that value. Water state w comes from METAR rain/drizzle only, with snow split out and tied to RegionalSnow. Use a plain mu scale (asphalt ~0.7–0.8), and no Horne hydroplaning until a tyre-pressure field and a water-depth model exist. (d) Altitude density only after confirming absolute elevation is available. (e) Wet road look applied to every road and deck material, and measured at Ultra on the Mobile renderer.
