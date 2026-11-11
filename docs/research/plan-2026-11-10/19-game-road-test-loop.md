# 19. A purpose that celebrates the realism: in-game Road Test against the published figures, lit signals, posted limit, drive-to and scenic drives (game-road-test-loop)
category game · impact 4 · 6 d · depends: traffic-persistence, test-procedure-fidelity

## Rationale
The calibrated dynamics are the core asset, yet nothing in play lets the player feel or measure them, and the game has no goal. A Road Test panel shows the published C&D 0-60, 70-0 and skidpad figures next to the player's own result, which makes the realism visible. AI cars obey signal lenses that the player sees painted permanently green, which breaks the shared rules. A destination and scenic drives turn the real map into drives.

## Code claims (proposer)
Junction.gd:76 draws signal lenses once ('signal_go' if lens==2). TrafficDirector.signal_phase (TrafficDirector.gd:164-175) runs a hidden cycle that AI obeys (248-262). LaneMarkings.gd:87-92 resolves the posted limit but nothing shows it (Hud.refresh, Hud.gd:205). The HUD has only mph, gear/rpm and trip (Drive.gd:765). data/start-pool.json entries are {title, lon, lat, bearing} only. There is no destination or route code outside traffic (grep).

## Design (proposer)
1) Road Test panel: auto-times 0-60 (with the per-car rollout), 70-0 distance and the skidpad on the built-in test pad, reusing Track and CarTrack timing code. Shows the published figure, its source, and the player's delta, with bronze/silver/gold at 115/105/102 % of the published figure.
2) Signal lenses: each signal keeps 3 emissive quad materials per approach. TrafficDirector sets them on its 0.25 s tick from signal_phase.
3) Posted-limit chip (MUTCD R2-1 style) from LaneMarkings, labelled 'est.' when the limit is defaulted. An optional driving record: speeding more than 10 % for over 3 s, red-light runs with a 2 s grace, and contacts.
4) Drive-to: pin a destination in the atlas without teleporting. A* over the streamed road graph shared with traffic (great-circle bearing beyond loaded tiles). Minimap line, distance and ETA. Trip summary with max lateral g and a smoothness score (lateral RMS against the 1.5 m/s² comfort threshold).
5) The start-pool schema gains end, length_mi and tags, for scenic drives such as Tail of the Dragon end to end.
6) Later: a replay ring buffer (pose, wheels, steer, rpm at 30 Hz) and ghosts.

## Gate
Headless checks:
- The HUD Road Test timer on the scripted CarTrack 0-60 matches within 1 tick.
- TrafficCheck asserts the lit lens equals signal_phase on every approach, and no AI car is stopped at a green.
- RulesCheck on the bundled Miami tiles: one red-light event and one speeding event from a scripted run.
- RouteCheck: route length within 10 % of the OSM shortest path. Recompute under 20 ms per tile load.
- PreferenceCheck round-trips any new keys.
RenderCost on Low passes with the signal materials. No physics change, so CarTrack is untouched.

## Files
game/Hud.gd
game/RoadMap.gd
game/Junction.gd
game/TrafficDirector.gd
game/RoadTest.gd
game/Route.gd
game/StartPool.gd
data/start-pool.json
game/checks/RulesCheck.gd
game/checks/RouteCheck.gd
README.md
docs/controls.md

## Skeptic verdict
claims_hold false · already_done false · keep false

### Wrong claims
- Lens rendering is only partly right. Junction.gd:69-76 does paint the bottom lens of every head 'signal_go' permanently. But it draws only one approach: 2 heads on a single mast arm facing -dir of the best-scoring segment. The second renderer, SignalControls._approach (SignalControls.gd:102-122), draws one head per incoming arm with all three lenses as unlit 'signal_dark' posts. So 'painted permanently green' is true only for street_objects signals. SignalControls signals are permanently dark.
- 'Each signal keeps 3 emissive quad materials per approach' does not fit the renderer. WorldTile.quad/tool (WorldTile.gd:330-343) batches every quad by material key into one SurfaceTool per tile. The material objects come from Drive.gd:136-137 and are shared by every tile, because the tile's materials dict is only a shallow duplicate (WorldTile.gd:845). Changing 'signal_go' would switch every signal in the world at once. Giving each approach its own material breaks the batching and adds draw calls per signal.
- Junction and the director do not share a signal axis. Junction picks its facing from the nearest trunk/primary-biased segment in the tile (Junction.gd:51-59). TrafficDirector.rebuild_signals sets sig.axis to the highest-RANK segment end within 9 m of the deduplicated point (TrafficDirector.gd:347-361), and also merges in SignalControls points. The one lit head can face a different approach from the one signal_phase treats as the main axis.
- LaneMarkings.gd:87-92 does not resolve a posted limit. Those lines are constants. RURAL_MPH is a rural default used only for passing-sight distance (line 553), and there is no in-town default at all. The mapped mph is read per station at lines 511-512 inside the marking build. Nothing shows it is kept anywhere the HUD could query by the player's position.
- The start pool lives at game/data/start-pool.json, not data/start-pool.json. Its entries already carry street, state and kind besides title/lon/lat/bearing.
- The HUD shows more than mph, gear/rpm and trip. Hud.refresh (Hud.gd:205-212) also shows the street name, the place title, a minimap redraw, view and sound buttons. A minimap already exists (MapHUD.gd).
- No 'built-in test pad' exists in the game. The flat 2500x5000 box exists only in the headless checks (Track.gd, CarTrack.gd); 'Zero on test pads' in Powertrain.gd:147 refers to those checks. There is no per-car rollout in CarTrack either (a60 is timed from rest, CarTrack.gd:58), so that part depends wholly on the unbuilt test-procedure-fidelity proposal.
- The proposal plans a new game/RoadTest.gd while game/checks/RoadTest.gd already exists, as an unrelated place-hopping QA script (Stowe/Anchorage/Honolulu). The names will be confused.
- TrafficCheck is a headless SceneTree with no map. The director header says nothing runs in headless or QA runs (traffic count zero). 'No AI car is stopped at a green' therefore cannot be asserted in TrafficCheck as written. It needs a synthetic AmbientLife graph or an in-game QA drive.
- The 'no route code outside traffic' grep is correct. The only routing is TrafficDirector.plan/AmbientLife.route, and it is a greedy persona choice a few segments ahead, not a path search. So A* is fully new work, and the streamed graph only covers the loaded tiles.

### Design problems
- The proposal does not answer the relayed user request. The user asked about 'the AI engine we discussed to assist with traffic, extras, generation of detail'. This proposal is a game-loop/scoring feature. It never mentions that TrafficDirector.gd:20-24 and 72-100 already has an opt-in local Ollama hook (--traffic-llm, qwen3/phi4-mini/granite4) used only for the place character. That hook is the natural seam for the AI engine the user means.
- Bundling 6 unrelated features into 6 days is too much: Road Test, lit signals, limit chip and driving record, A* drive-to with ETA, scenic schema, replay and ghosts. A* over a streamed graph with great-circle fallback plus a minimap route line is alone several days.
- A Road Test on real map roads conflicts with realism-first scoring. Grade, roughness (RoadRoughness), surface, temperature and keyboard throttle all move 0-60 and 70-0. Gold at 102% of the published figure is inside CarTrack's own ±0.15 s / ±5 ft tolerance band, so the medal mostly measures noise. The direction of the 115/105/102% thresholds is also unspecified for the skidpad, where higher is better.
- A skidpad needs a ~300 ft circle of flat pavement, which a real-world map rarely offers. The design needs a dedicated pad scene, which the proposal never budgets.
- The driving record will misfire: speeding events on 'est.' defaulted limits (no in-town default exists), and red-light runs at SignalControls junctions whose lenses are dark. Both penalise the player for rules the world never showed.
- Per-approach emissive materials (the lit-signal design) would multiply draw calls on Low. RenderCost would likely regress, or the approach forces separate MeshInstances per head.
- The parallel_safe=false and depends_on chain (traffic-persistence, test-procedure-fidelity) leaves the cheap, high-value fix (lit signals) blocked behind unrelated work.

### Corrected design (USE THIS where it differs)
Split the work, and answer the user's actual question first.

A) AI engine (what the user asked about):
- Build on the existing opt-in Ollama seam in TrafficDirector.gd (llm_enabled, probe/ask, 2 s timeout, rule-based fallback).
- Generalise it into one async 'LocalModel' service with strict JSON schemas and deterministic rule fallbacks.
- Uses: per-area traffic character (already exists), extras/pedestrian density and persona mix, and per-tile detail hints (storefront signage, street furniture density) cached per z14 area.
- Never run it in headless, QA or CarTrack.
- Gate: the same seed with the LLM off produces identical output, and the LLM's answer arrives in under 2 s or is ignored.

B) Lit signals (small, traffic-related, about 1 day):
- Give signal heads a single shared ShaderMaterial (signal_lens) in both Junction.build_signal and SignalControls._approach.
- Encode in each lens vertex (CUSTOM0/UV2): the lens index, the signal's world-position hash (cycle and offset, computed exactly as in rebuild_signals: world = origin*side + at, h = hash(Vector2i(world.round())), cycle 60 + |h|%41, offset |h>>8|%97, split .55), and whether the head faces the main axis.
- Pick the main axis by the same RANK rule as rebuild_signals, applied to the tile segments.
- The shader evaluates signal_phase from a global 'signal_time' uniform that TrafficDirector.now sets, or Drive's clock when traffic is off. This keeps batching and one material, with zero per-signal draw calls.
- Light SignalControls lenses too.
- Gate: in TrafficCheck, a GDScript port of the shader phase matches TrafficDirector.signal_phase for every head over 2 cycles. RenderCost on Low is unchanged.

C) Posted-limit chip (half a day):
- LaneMarkings keeps a per-segment mph map (from counts[3]) on the tile, and Hud.refresh shows it.
- Hide the chip when it is unmapped rather than inventing an in-town default.

D) Defer to separate proposals: Road Test (needs a pad scene plus rollout from test-procedure-fidelity; show the delta to the published figure without medals inside the ±0.15 s band), drive-to routing, scenic start-pool fields (start-pool lives at game/data/start-pool.json), and replay. Rename any new in-game Road Test file to avoid clashing with checks/RoadTest.gd.
