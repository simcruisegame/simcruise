# SimCruise 2.0.0 release bugs

Bugs found by the release gate (`docs/release/driving-checklist.md`). Each bug has a short repro, the owner (the thread that owns the area) and a status.

Owners: **release** (this gate; fixed on `release/v2.0.0`), **physics** (lane1), **cars** (lane2), **buildings** (lane3), **live** (lane-live), **place** (lane-place).

| # | Bug | Repro | Owner | Status |
|---|---|---|---|---|
| R1 | Corvette C8 0-60 mph is slower than published | `scripts\check.ps1 -Filter SimCruise.Vehicle.CarTrack.meridian_z`: 3.22 s with a 1 ft rollout (coordinator figure, 4de89c5) against Car and Driver's 2.8 s | physics / cars | open: waits for the C8 on the TMeasy tyre |
| R2 | Shipping compile warning C4701 "potentially uninitialized local variable 'Best'" | `Build.bat SimCruise Win64 Shipping`: `Source/SimCruise/Private/Knowledge/SimCruisePlacePhotos.cpp(150)` | release | fixed: `Best` starts at zero (Kept has at least 200 colours, so it is always set) |
| R3 | Model 3 oversteers on the TMeasy tyre | `SimCruise.Vehicle.LiftOff.model3`: understeer gradient -0.14 deg/g (band 0 to 8), SPIN at 0.7 g (peak beta 45 deg). Came with 4de89c5 | cars | fixed on main d5eefb2 (front-biased anti-roll bars, +0.21 deg/g). Known issue for 2.0.0: a spin remains in the 0.7 g trail-brake case (no stability control model) |

## Code review (12 items, verified 2026-11-12 against main 391cffc)

| # | Bug | Verdict and evidence | Owner | Status |
|---|---|---|---|---|
| CR1 | Physics in slow motion when a frame takes more than 33 ms | CONFIRMED: `Config/DefaultEngine.ini` sets no `MaxPhysicsDeltaTime`, so the engine default 1/30 s applies; `PhysLevel.cpp` passes it to `SetUpForFrame`, which clamps the frame's physics time. Below 30 fps the car runs slower than the clock. | physics | open (routed) |
| CR2 | No frame cap, VSync or frame pacing; default preset maybe too heavy for a 28 W GPU | CONFIRMED (no `t.MaxFPS`, `r.VSync` or frame-rate limit in the config). Fix in this release: see row G of the checklist. | release | see row G |
| CR3 | Non-ABS Miata can lock its rear brakes first | CONFIRMED in code: `SimCruiseVehicleSim.cpp` uses fixed `FrontBrakeTorqueNm` / `RearBrakeTorqueNm`, no load-dependent proportioning. The rear-lock-first effect is not measured yet (needs a brake-in-turn bench, ISO 7975). | physics | open (routed) |
| CR4 | Head motion and g-meter jitter | CONFIRMED and FIXED: they differentiated the velocity over the frame time while the 360 Hz physics gives 5, 6 or 7 new steps per 60 fps frame (about 17 % jitter). Now over the physics time (`FSimCruiseDriveTelemetry::SimTimeS`); new test `SimCruise.Gameplay.Camera.PhysicsTimeNoJitter`. | release | fixed |
| CR5 | Manual clutch has no stability cap and never locks | CONFIRMED: the manual branch (`SimCruiseVehicleSim.cpp`, `ManualClutchCouplingNmPerRadPerSecond`) has no 1/(dt x inertia) cap like the automatic's, and the clutch is viscous (it slips by a small amount for ever). | physics | open (routed) |
| CR6 | Banking near tile edges, rural minor roads, seam crests | CONFIRMED for the first two: superelevation runs out to 0 over 30 m (`PinSpanM`) at each tile boundary; `DesignSpeedKmh` gives 0 (no banking) to all roads below secondary. Seam crests: plausible, not measured. | world | open (routed) |
| CR7 | Tile install stalls (30-90 ms) | PLAUSIBLE: one tile per frame is installed on the game thread (`SimCruiseWorldStreamer.cpp`, mesh sections and collision). See the soak's worst frame (row K1). The physics-thread trace race was not shown. | world | open (routed) |
| CR8 | Recover makes the camera swoop | CONFIRMED and FIXED: `ResetVehicle` only queues the reset for the physics thread, so the snap on the request frame came before the car moved. The camera manager now snaps on the frame it sees the car jump (farther than it can drive in the frame, or turn more than 45 degrees). | release | fixed |
| CR9 | Engine-sound limiter fixed at 6950 rpm | CONFIRMED: `FSimCruiseInlineFourSynth::LimiterRpm = 6950` for every car (NB fuel cut 7250, Mustang 5000, C8 6600), so the Mustang and the C8 never sound their limiter and the NB sounds it 300 rpm early. | cars | open (routed) |
| CR10 | Pad: LT brake and LB clutch on the same finger; HUD on L3 | CONFIRMED and FIXED: clutch A (hold), parking brake B (hold), shift up/down RB/LB, gearbox D-pad right, engine restart D-pad left, HUD X. `docs/controls.md`, HUD hints, the self-test and `SimCruise.Gameplay.Input.BindingsMatchControlsDoc` updated. | release | fixed |
| CR11 | Engine braking about half of the real value | CONFIRMED as an estimate: NB friction is 10 + 0.002 x rpm N m (16 N m at 3000 rpm); a 1.8 L engine with a closed throttle (friction and pumping, about 2 bar) gives about 30 N m. | physics | open (routed) |
| CR12 | Benches step the game at 1/60 s; drive robot drives no C8 or Mustang | Robot: FIXED, `scripts\drive-robot.ps1` now drives nb2001, nb2001_aaron, mustang65 and meridian_z. Benches: CONFIRMED in part: Chaos steps at 1/360 s, but the game frame is fixed at 1/60 s, so frame-rate-dependent game-thread code is not tested by the benches. | physics | robot fixed; step routed |
