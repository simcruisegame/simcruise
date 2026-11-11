# 11. The AI engine: an off-frame Assist service for traffic demand, extras, detail generation and a reality critic, with provenance and licence gates (ai-assist-engine)
category pipeline · impact 4 · 8 d · depends: traffic-persistence, reality-loop

## Rationale
The user asked directly about this. AI earns its place on SimCruise where data is missing: how much traffic a street carries, where parked cars and pedestrians belong, gap-fill materials and props, and spotting what looks wrong against a photo. It must never cost frames on an 8 GB 2070 Max-Q, and it must never decide physics. detail-engine.md already sets that rule for textures. This proposal extends it into one service covering traffic, extras, detail and critique, with the same gates.

## Code claims (proposer)
TrafficDirector.gd:72-112 and :377-456 already run an opt-in Ollama area-persona label (qwen3 CPU, num_gpu 0, 2 s timeout, rule-based fallback, never on the frame path). docs/research/detail-engine.md defines the gate.json idle gate, the stable-diffusion.cpp sidecar, MatFuse and FLUX.2-klein-4B gap-fill, the provenance F-key view and a licence CI gate, but slices 3-7 are not built. game/art/materials does not exist yet (CarFinish and terrain shaders use flat or noise maps).

## Design (proposer)
One Bun helper subcommand, `atlas assist`, with a job queue. GPU jobs run only when Drive.gd's gate.json reports menu, loading or closed, and only after nvidia-smi shows at least 2 GB free. CPU jobs run at below-normal priority. Every output carries a provenance meta.json (model id and hash, seed, prompt or template, input ids and licences).

Lane A, traffic (behaviour stays IDM/MOBIL):
- Density per segment from HPMS AADT: cars/km = AADT·K/(24·v_free), K about 0.09 for the peak hour, scaled by local clock time from the FHWA hourly profile. Feeds AmbientLife spawn weights.
- The existing LLM persona label stays as the character label.
- Optional, offline: the user-installed SUMO (EPL, never bundled) calibrates signal cycle and split from OSM networks. Results are cached per area.
- Phase 2: small ONNX gap-acceptance policies trained in Godot RL Agents (MIT) on SimCruise's own junctions, used only for merges and unprotected lefts. Validated against HCM critical gaps (left from major 4.1 s, from minor 7.1 s).

Lane B, extras:
- Pedestrians and cyclists are rule-based (OSM sidewalks, POI density, time of day) using NavigationServer RVO.
- Parked-car spots: OSM parking and kerb-lane rules first. Optionally, offline vehicle detection on NAIP 0.6 m imagery with an Apache-2.0 detector (e.g. RT-DETR), cached as provenance=derived spot lists for update_parked.

Lane C, detail: detail-engine slices 2-6. A CC0 library first. MatFuse or FLUX.2-klein-4B gap-fill (US siding, chip seal, stucco). TRELLIS.2 (MIT) for long-tail props (mailboxes, hydrants, newspaper boxes). A human reviews every asset, and it is committed only if derived under MIT or Apache.

Lane D, reality critic: Qwen2.5-VL-7B (Apache), CPU only. It gets a reality-loop capture paired with its reference photo and returns a JSON list of discrepancies, each with a region, a category (edge, marking, material, colour, missing object) and a confidence. These are appended to lab/reality/backlog.json for a human to accept. They are never applied automatically.

All lanes are default-off for the checks. The game is complete without them.

## Gate
Frame safety, measured plugged in at Ultra:
- Driving with assist idle or CPU jobs running: p95 frame time within ±0.3 ms.
- Leaving the menu mid-GPU-job: no frame over 33 ms (the process is killed, not suspended).
- VRAM from nvidia-smi never exceeds 7.5 GB.
Correctness:
- The licence CI check (authoring/materials/check-licences.mjs) rejects non-CC0 or untracked outputs.
- Caches give identical output for identical keys.
- With assist on, TrafficPersistence and TrafficCheck still pass.
- The AADT density test: simulated vehicles per km on a sampled arterial within ±30 % of AADT-derived density.
- The full check.sh suite passes with assist off. No physics or CarTrack impact.

## Files
helper/assist.mjs
helper/atlas.mjs
game/TrafficDirector.gd
game/AmbientLife.gd
game/Drive.gd
authoring/materials/check-licences.mjs
game/art/materials
lab/reality/backlog.json
docs/research/detail-engine.md
docs/technical-overview.md

## Skeptic verdict
claims_hold false · already_done false · keep true

### Wrong claims
- 'behaviour stays IDM/MOBIL': there is no MOBIL anywhere. TrafficDirector.gd has IDM (idm() at :121), persona turn choice, signal stops and van pull-over via car.lat. AmbientLife.lane_point puts every car at a fixed lane offset, so there are no lane changes.
- Lane A says density 'feeds AmbientLife spawn weights', but no spawn weights exist. AmbientLife.spawn() (:247-266) picks a segment uniformly with rng.randi() % segs.size() inside a 45-260 m ring. The global budget is fixed by GraphicsQuality presets: 6/12/20/32 cars (GraphicsQuality.gd:23-26).
- The density formula cars/km = AADT·K/(24·v_free) is wrong by a factor of 24. K (about 0.09) already turns AADT into peak-hour volume (veh/h), so density = K·AADT/v. Off-peak it is AADT·h(t)/v with h(t) the hourly fraction (sum of h = 1). It is also missing the directional split D (about 0.55) and division by lanes, and v_free underestimates density in congestion.
- The ±30 % absolute AADT density gate cannot pass with a 32-car Ultra budget. A peak arterial (AADT 30k, 50 km/h) carries about 54 veh/km, and the 260 m spawn ring holds several km of road. The step loop also does an all-pairs leader search, so raising the budget costs frames. Only a relative-share test is achievable.
- The gate 'TrafficPersistence still passes' refers to a check that does not exist. game/checks has TrafficCheck, TrafficLineup and AmbientCheck only. traffic-persistence is a dependency, so this is not built yet.
- The design calls the gate 'Drive.gd's gate.json' as if it exists. Nothing in game/*.gd writes a gate file. It exists only as a design in docs/research/detail-engine.md:84, so the whole GPU-job scheduling rests on unbuilt plumbing.
- code_claims says detail-engine 'slices 3-7 are not built'. In fact only slice 1 is built (Stochastic.gdshaderinc, CHANGELOG line 20). Slice 2 is not built either: there is no authoring/materials/, no check-licences.mjs and no game/art/materials.
- 'CarFinish and terrain shaders use flat or noise maps' is inaccurate. Terrain and road already sample photographic textures (art/asphalt.jpg, art/f*.jpg) through Stochastic.gdshaderinc and GroundFinish.gdshaderinc.
- TRELLIS.2 (MIT) for props is not runnable here. It needs an NVIDIA GPU with at least 24 GB, and this machine is an RTX 2070 Super Max-Q with 8 GB (nvidia-smi: 8192 MiB). The 7.5 GB VRAM gate rules it out.
- Phase 2 'small ONNX policies trained in Godot RL Agents' do not fit this codebase. Godot RL Agents' ONNX inference runs through Godot .NET/C#. The project is GDScript-only (no .csproj or .sln), so it would need a .NET switch or a third-party onnxruntime GDExtension.
- The licence gate wording 'rejects non-CC0 or untracked outputs' contradicts Lane C, which commits MIT/Apache-derived generated assets. detail-engine.md:32 accepts CC0, or output generated from MIT/Apache weights with model, seed and prompt recorded.
- 'Caches give identical output for identical keys' is trivially true for a cache hit. Bit-identical regeneration on CUDA (stable-diffusion.cpp, FLUX) is not guaranteed.
- Parked cars are already rule-based kerb placement in WorldTile.gd:960-978 (tile.parked, consumed by Drive.update_parked :775). They do not use OSM parking tags, so 'OSM parking rules first' is new work, not existing behaviour.

### Design problems
- The design bundles four unrelated programmes (traffic demand, pedestrians and parked cars, generative assets, a VLM critic) plus an RL phase into 8 days. Pedestrians alone do not exist: there are no models, animation or AI, only OSM walkway ribbons in WorldTile.gd:419. A realistic estimate is several times larger, so it should be split into independent slices with their own gates.
- Most of Lane A and Lane B is not AI at all: AADT density, pedestrians from OSM and POIs, OSM parking. Calling it 'the AI engine' is fine as an umbrella, but the useful parts are open-data plumbing that should land first and default-on. It needs nothing from the Assist queue.
- HPMS AADT exists only for sampled federal-aid sections. Most residential and local streets have no AADT, so a functional-class fallback table is required. The roadmap still marks AADT→traffic as [unverified].
- The RL gap-acceptance phase replaces a rule that does not exist. TrafficDirector has no unsignalled-junction yielding at all. The general, realistic method is HCM critical headway plus follow-up headway (left from major 4.1 s and 2.2 s; left from minor 7.1 s and 3.5 s on a two-lane major), with a lognormal per-persona spread. That is a few hours of GDScript with a headless check, with no ONNX and no .NET.
- NAIP vehicle detection at 0.6 m is weak. A car is about 7x3 pixels, imagery is leaf-on and occluded by canopy, and it is a single-date snapshot that also catches driveways and moving traffic. OSM parking:lane/parking:both tags (same Esri OSM replica lane-counts.mjs already queries) plus the existing kerb rule is better provenance. Note that Ultralytics' RT-DETR is AGPL, so only the lyuwenyu/PaddleDetection Apache release qualifies.
- Long-tail props (mailboxes, hydrants, newspaper boxes) are simple primitives and fit a procedural recipe like HouseRecipe.gd, or CC0 packs. Generative 3D is unnecessary and infeasible on 8 GB.
- For GPU jobs, the kill-on-leave path depends on gate polling about twice a second plus CUDA context teardown. The 33 ms gate must be measured on scene load, when tile streaming also spikes, and the gate file must be built first (detail-engine slice 6 already owns this). Lane C should not duplicate it: point at detail-engine slices 2-6 rather than restating them.
- The Lane D critic depends on a reality loop that does not exist (no lab/reality). Qwen2.5-VL-7B on CPU takes minutes per pair, which is fine offline, but the false-positive rate on rendered-vs-photo comparisons is unknown. The gate should include a precision measurement on a hand-labelled set before any backlog is trusted.
- Physics and CarTrack/LiftOff are untouched, since everything is off-frame and default-off, so those gates are safe. The real frame risk is Lane A/B increasing on-frame agent counts, not the AI jobs.

### Corrected design (USE THIS where it differs)
Answer the user's question with a narrower, staged plan. All AI stays off the frame path and never touches physics. Keep the existing opt-in Ollama persona label (TrafficDirector.gd:72-112, :365-490) unchanged.

Slice 1, traffic demand (data, not AI, default on, about 2 days):
- New helper module helper/traffic-demand.mjs: fetch the HPMS AADT, K and D per section and match it to tile segments the way lane-counts.mjs does. Fall back to a functional-class AADT table where HPMS has no section.
- Density k(t) = AADT·h(t)·D / (v·lanes), with h(t) from the FHWA hourly profile (K ≈ 0.09 at peak).
- In AmbientLife.spawn, replace uniform segment choice with weighted choice by k·length within the ring. The total budget stays the preset's 6-32.
- Gate: on a sampled multi-class area, the simulated per-segment share of cars correlates with the AADT-derived share (relative, not absolute density). TrafficCheck, TrafficLineup and AmbientCheck pass. Frame p95 is unchanged at Ultra.

Slice 2, junction gap acceptance (rules, about 1 day):
- HCM critical and follow-up headways for unsignalled turns, with a per-persona lognormal spread, in TrafficDirector.
- A headless check against the HCM values. No RL and no ONNX.

Slice 3, parked cars and extras (data):
- Read OSM parking:lane / parking:both / parking=street_side to gate and side-select the existing WorldTile kerb rule.
- Pedestrians come later as their own proposal, because they need models, animation and a frame budget.

Slice 4, detail: defer to detail-engine.md slices 2-6 as written. Build the CC0 library and check-licences.mjs first; the licence gate accepts CC0, or MIT/Apache-weight output with full meta.json. The gate.json writer in Drive.gd plus the `atlas forge` kill-on-leave test come before any GPU generation. Drop TRELLIS.2 (it needs 24 GB); make props procedural recipes or CC0.

Slice 5, optional offline critic (after a reality loop exists): Qwen2.5-VL-7B (Apache) via the user's own Ollama or llama.cpp on CPU, writing suggestions to a backlog for human acceptance only. Gate: precision of at least 0.6 on a hand-labelled set of 30 capture/photo pairs before it is used.

All AI lanes are default-off. With them off, the full check.sh suite, CarTrack and LiftOff pass unchanged. Docs to update with each slice: technical-overview.md, roadmap.md, detail-engine.md and CHANGELOG.md.
