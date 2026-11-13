# Handoff: world look and picture (2026-11-13)

## Done (on main)
- 7e417ad: sharpening after the TSR upscale (`FSimCruiseGraphics::SharpenFor`), motion blur at the real frame rate (TargetFPS 0, amount 0.3, max 2 %), VSync on by default (`simcruise.VSync`), pre-flight road-gap probe reads neighbour tiles (the US 129 / CA-1 "gaps" were probe faults), `scripts/framecheck.ps1` (frame times at a set speed; use `-Exe` with a packaged build, the editor is 2-3x slower).
- Earlier: building albedo caps, daylight exposure -0.3 EV, seam pinning for uncontracted road ends, staged tile installs, signs, Deco forms, realism score (`scripts/realism.ps1`).

## Measured
- Owner's laptop GPU (RTX 2070 Super Max-Q) was held at 28 W (855 of 2100 MHz): about 42 fps on Medium in a packaged build. Lenovo Performance mode (Fn+Q / Vantage) with the charger in gives it full power.
- Realism: Lummus 46.0, Dragon 45.5, Bixby 34.5.

## Next
1. Re-run the latency probe with VSync on (expect +8-16 ms; pass is under 60 ms).
2. Frame-rate guard: dynamic resolution so a weak or power-limited GPU holds 60 fps.
3. P6: cabin exposure flicker on Tail of the Dragon (Miata, 10-11 s and 45-46 s).
4. High-speed frame check needs a highway start: the robot at 130 km/h leaves Princeton Road (gravel).
5. Pre-flight now shows road steps up to 24 cm on Dragon (highest surface across tiles); check if real.
6. Small occlusion pass only (contact shadows, Lumen AO); the owner's focus is now the driving experience.
