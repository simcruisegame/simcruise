# Round plan (audit 2026-11-10)

## Vision
SimCruise should become a "digital twin you can drive". The north star is that the 1999 Miata, the 1965 Mustang and the Corvette C8 each drive like the real car and look like it down to visible parts. Every claim of that is checked against reality in a repeatable loop, using published test data, the owner's own green NB, and street photos of the exact spot. There is no hand-tuning by eye.

Physics comes first, in this order:
- Measure what the model actually does today. Run CarTrack for all three kept cars, ISO 4138 understeer and roll gradients, ISO 7401 step steer, and tick-rate convergence. Fix the test procedures that bias calibration: the C&D 1 ft rollout and the instant brake step.
- Replace the tanh tyre with a data-backed Magic Formula/TMeasy combined-slip tyre. It peaks at 6-8 deg for radials and about 11 deg for bias-ply, its cornering stiffness saturates with load, and it outputs aligning torque.
- Couple the chassis inside the substeps.

Feel comes next, because the owner drives with an Xbox pad. Steering should cap only into-turn steer, so countersteer and slide catches work. Pedals get ramps. Rumble and tyre audio come from each tyre's own slip relative to its peak, and the aligning torque becomes the "goes light" cue. The two standing complaints are fixed with general methods that come with headless gates:
- Traffic keeps its identity across tile streaming and rebase.
- The road ribbon, not an aerial-colour classifier, decides where the asphalt is.

The AI engine the owner asked about is an Assist service with one hard rule: AI never runs on the frame path, and the game is complete with it off.
- **Traffic:** behaviour stays IDM/MOBIL. AI and data set how much traffic there is and what character it has (HPMS AADT density, the existing once-per-area local-LLM persona label, optional offline SUMO signal timing). Later, small ONNX gap-acceptance policies trained in Godot RL Agents could handle merges and unprotected lefts.
- **Extras:** pedestrians and parked cars are placed from OSM and offline aerial detection.
- **Detail:** offline or menu-time generation (MatFuse/FLUX.2-klein-4B textures, TRELLIS.2 props) only fills gaps in the CC0 library, with a provenance record for every asset.
- **Reality critic:** a CPU vision model compares in-game captures with reference photos and writes structured differences into the reality-loop backlog. A human confirms each one.

The result is a sim whose realism is a measured, reproducible number for every car and every road.

- 1. dyn-verification-suite: Dynamics verification suite: all kept cars, ISO 4138 gradients, ISO 7401 step steer, tick convergence, working harness (keep true)
- 2. tyre-mf-combined-core: Data-backed tyre: Magic Formula/TMeasy pure + combined slip, saturating load stiffness, per-construction defaults, aligning torque (keep true)
- 3. test-procedure-fidelity: Match the real test procedures before recalibrating: C&D 1-ft rollout, brake application ramp, plateau decel (keep true)
- 4. pad-steering-pedals: First-class Xbox feel: state-aware countersteer allowance, caster self-centring, pedal shaping, per-car steering ratio, 1:1 wheel mode (keep false)
- 5. traffic-persistence: Stop traffic vanishing: stable segment identity across tile streaming and rebase, 'minor' streets, view-aware spawn/despawn, CI check (keep true)
- 6. road-edge-authority: Straight, clean road edges: the ribbon owns the carriageway; edges fitted in road coordinates; smoothed centreline; per-class shoulders; grip from geometry (keep true)
- 7. reality-loop: 'Taste reality' harness: reference-matched captures of cars and exact street spots, numeric scores, receipts in every change (keep true)
- 8. substep-chassis-coupling: Semi-implicit chassis coupling inside the substeps, plus a measured tick-rate decision (60 vs 120 Hz) (keep true)
- 9. slip-haptics-audio: Grip you can feel and hear: slip-driven Xbox rumble and trigger rumble, tyre scrub/squeal, ABS pulse, 40 ms audio, per-engine voices (keep true)
- 10. powertrain-drivability: Powertrain that behaves like the real one: throttle-aware shift map, displacement-based engine braking, air-path lag, coast-biasing Torsen, Coulomb clutch, DCT shift torque (keep true)
- 11. ai-assist-engine: The AI engine: an off-frame Assist service for traffic demand, extras, detail generation and a reality critic, with provenance and licence gates (keep true)
- 12. suspension-geometry: Suspension geometry that matters: roll centres, anti-dive/squat, camber effect on grip, bump/roll/compliance steer, live rear axle for the Mustang (keep true)
- 13. car-look-pipeline: Cars that look real down to the parts: one material-role table, shared paint/glass/tyre/disc/lens shaders, windscreen, interior grain, cockpit shadows, real mirrors (keep false)
- 14. owner-car-twin: The owner's green NB as a measured reference twin: lowered, wide wheels, cage, carbon spoiler, validated against the real car's logs (keep false)
- 15. camera-hud-telemetry: Head physics, instruments and teaching telemetry for every kept car; chase camera that shows slip; g-g ring at the published skidpad g (keep false)
- 16. wheel-dof-contact: Unsprung wheel DOF with tyre vertical stiffness and an enveloping multi-ray contact: wheel hop, smooth unloading, curbs (keep true)
- 17. low-speed-stick: Real standstill: carcass-deflection tyre with a stick state, so cars hold on grades and stop jittering (keep true)
- 18. road-load-weather: Road load and conditions: per-wheel rolling resistance f0+f2v², aero lift and altitude density, wet grip from live weather (keep false)
- 19. game-road-test-loop: A purpose that celebrates the realism: in-game Road Test against the published figures, lit signals, posted limit, drive-to and scenic drives (keep false)
