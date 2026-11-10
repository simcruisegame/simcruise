<!-- Produced by a multi-agent research and design workflow on 2026-11-10 (4 research sweeps, 3 designs, 2 judges, 1 synthesis). Timings marked as estimates are not yet measured on the target laptop. -->

# SimCruise Detail Engine: recommended plan

## Verdict

Make the detail engine mostly algorithmic, and keep AI out of the hot path. The "early-2000s" look comes mostly from plain `texture()` calls that repeat at fixed periods: terrain.gdshader:60-61 and :93, FieldTerrain.gdshader:25-26, and GroundFinish.gdshaderinc:24-25 and :112-113. Uniform asphalt and box houses add to it. Better lighting barely helped because the surfaces have no detail for SSAO or SSIL to work on.

No local generative model on an 8 GB RTX 2070 Super Max-Q can produce textures while the game runs without costing frames. Generation would compete with Godot for the same GPU and the same VRAM. So the plan has three parts:

- **Most of the visible gain:** a data-seeded procedural surface layer, cheap enough to run every frame.
- **AI, tier 1:** an offline or menu-time tool that fills gaps in the material library.
- **AI, tier 2:** optional per-region variants, generated only when the game is not rendering a scene.

## Architecture

**1. Shared shader include (`game/Stochastic.gdshaderinc`)**
- Hex-tiling with histogram-preserving blending, written fresh from Mikkelsen (JCGT 2022) and Heitz-Neyret / Burley. Do not copy LYGIA (Prosperity licence) or the Unity Labs demos (no licence).
- Use `textureGrad`, and seed the hex offsets from integer world metres so the result is deterministic.
- A no-rotation, offset-only mode for directional textures (siding, brick, lane grain).
- A 2-tap fallback for distant terrain and lower presets.

**2. Aerial macro tint**
- Sample the aerial photo that is already bound at a coarse mip (about 4-8 m), normalised by luma.
- The observed photo then sets the low-frequency colour, and the library texture adds the fine grain.

**3. One global CC0 material library (`game/art/materials/`)**
- About 64-96 ambientCG and Poly Haven CC0 sets, plus the CC0 part of MatSynth with its CC-BY subset filtered out.
- Stored at 1K as Texture2DArrays: BC7 albedo+height, BC5 normals, and a separate ORM array.
- One array per surface family, never one per tile. Expect about 270-300 MB of VRAM.
- Use a first-run fetch with sha256 checks, or Git LFS, rather than plain git.
- `authoring/materials/check-licences.mjs` runs in CI and rejects anything that is not CC0, or not generated from MIT or Apache-licensed weights with model, seed and prompt recorded.

**4. New helper job `atlas surface X Y` (Bun, CPU, worker thread, about 10-300 ms per tile)**
- **Inputs:** OSM road class and surface, NSI bldgtype and med_yr_blt, Overture material and roof colour, aerial palette, state (which sets the style region), and HPMS IRI.
- **Outputs, kept in the cache only** (they are derived from ODbL data):
  - `surface/<tile>.json`: per-feature material layer, age, jitter and window grid. Every field carries `src` set to observed, derived, library or generated.
  - `wear/<tile>.png`: a 512² RGBA road-wear mask. R is tyre polish and the oil strip at real lane offsets. G is crack-seal and cracking. B is patches and joints. A holds provenance.
- Wear density comes from the same condition value that RoadRoughness.gd uses, so what you see matches what you feel.
- The wear masks get their own cache budget, so they cannot evict aerials from the 96 MiB source cache.
- Every random choice uses `xxhash32(z, x, y, featureId, RECIPE_VERSION)`. Integer hashing gives bit-identical results on every machine.

**5. Godot side**
- WorldTile.gd and ObservedBuildings.gd read the recipe on the worker thread that already builds the tile.
- Layer id, age and jitter go into vertex colour or CUSTOM0, so no draw calls are added.
- Walls use footprint-ring UVs rather than triplanar, which would seam at building corners.
- Weathering is ALU only: soot rising from the base, streaks under openings, bleaching by facing direction, and moss on north faces in wet climates.
- Windows come from a procedural grid shader driven by floor count and width, not from a diffusion bitmap.
- An F-key provenance view (also `--provenance`) colours every surface: observed untinted, derived blue, library yellow, generated magenta.

**6. NPCs and traffic** stay algorithmic: HPMS AADT for traffic volume, signals, and VAT pedestrians. A generative model adds nothing per frame there. Traffic behaviour now uses the Intelligent Driver Model with persona parameters (`TrafficDirector.gd`); the only generative step is an optional, once-per-area local-LLM label of the place's traffic character (CPU-only Ollama, 2 s timeout, rule-based fallback), never on the frame path. Measured on this machine: qwen3:4b CPU-only answers a warm request in about 3-4 s and a cold one only after a 4-22 s load, so with the 2 s timeout the rule-based character is what normally applies.

## Models and tools

| Use | Tool / model | Licence | URL |
|---|---|---|---|
| Inference runtime (sidecar exe) | stable-diffusion.cpp, CUDA sm_75 build with Vulkan fallback | MIT | https://github.com/leejet/stable-diffusion.cpp |
| Tileable PBR gap-fill | MatFuse (palette or image conditioned) | MIT (check the weights and training data terms) | https://huggingface.co/gvecchio/MatFuse |
| PBR gap-fill, alternative | StableMaterials (LCM, 4 steps) | OpenRAIL | https://huggingface.co/gvecchio/StableMaterials |
| Offline 1024 px bake | FLUX.2 klein **4B** | Apache-2.0 | https://huggingface.co/black-forest-labs/FLUX.2-klein-4B |
| Offline 1024 px bake, alternative | FLUX.1 schnell (GGUF Q4) | Apache-2.0 | https://huggingface.co/black-forest-labs/FLUX.1-schnell |
| Offline 1024 px bake, faster | SDXL-Lightning 4-step | openrail++ | https://huggingface.co/ByteDance/SDXL-Lightning |
| Menu-time variants, optional | SD 1.5 Q8 GGUF + LCM-LoRA + TAESD | CreativeML OpenRAIL-M / openrail++ / MIT | https://huggingface.co/latent-consistency/lcm-lora-sdv1-5 |
| Facade photo to parameters, optional | Qwen2.5-VL-7B via Ollama, **CPU only** | Apache-2.0 | https://ollama.com/library/qwen2.5vl |
| Prototyping MatFuse / StableMaterials | diffusers | Apache-2.0 | https://github.com/huggingface/diffusers |

**Excluded:**
- SD-Turbo and SDXL-Turbo: Stability Community Licence (registration, revenue cap, mandatory notice).
- FLUX.1-dev and its LoRAs, FLUX.2 klein 9B, Sana: non-commercial.
- ComfyUI (GPL-3.0): never bundled; at most an optional backend the user installs.
- Materialize and DeepBump: GPL code, so nothing is copied from them.

**Ollama image generation:** the installed Ollama cannot be used for images. It is experimental, there is no confirmed Windows support, and it needs about 12-16 GB of VRAM. Re-check before citing this.

**Weights:** never committed or bundled. They are downloaded on first use into `user://forge-models`, after an in-game screen that shows each licence.

## Running alongside the game without costing frames

- **While driving:** no model inference at all. The only new per-frame work is shader taps:
  - ground: about 3 hex fetches × 2 arrays + LUT + wear mask
  - walls: about 3 fetches
  - the budget is at most +0.8 ms GPU at Ultra
  - main thread: at most 0.1 ms per tile for texture and uniform swaps
- **Optional generator (`atlas forge`):** runs at below-normal priority and only when Drive.gd's gate file (`user://…/forge/gate.json`, written about twice a second) reports menu, loading or game closed. It does not run while parked in-scene: Windows time-slices the GPU between processes, so at Forward+ Ultra the game would drop to about 15-25 fps.
  - It checks the gate between denoising steps.
  - If the gate closes mid-job it **kills** the process. Suspending does not cancel work already on the GPU or free its VRAM.
  - It starts only after nvidia-smi confirms at least 2 GB of free VRAM.
- **Shader warm-up:** variants are precompiled on the bundled start, so the first sight of a material does not hitch.

## Caching, determinism and provenance

- **Procedural layer:** a pure function of (tile, feature id, RECIPE_VERSION). The same street looks the same on every visit and every machine.
- **Generated swatches:**
  - Cache key: sha256(runtime version, model hash, template version, class, region, palette bucket, variant). The seed is the first 32 bits of the key.
  - Stored in `user://forge-cache/` under a separate budget of about 1.5 GB, LRU. Slots that recently built tiles reference are pinned.
  - A cached swatch is never regenerated, so determinism comes from the cache. Regenerating on CUDA after a cache wipe may differ by a few LSBs.
  - A generated swatch replaces its CC0 fallback only on a tile rebuild or when the surface is out of view, so nothing visibly pops.
- **meta.json for every swatch:** `provenance: generated`, model id and hash, seed, prompt template, input ids with their licences, and the licence of the weights.
- **Street photos** (Panoramax and KartaView, CC-BY-SA):
  - Pixels stay in the user cache with their licence tag.
  - Only derived facts (colour, storeys, window grid) feed the default pipeline.
  - Photo pixels are never used as conditioning for packs and are never committed.

## Slices

1. **Ground anti-tiling (buildable today).**
   - Build: `Stochastic.gdshaderinc` and the aerial macro tint, applied to terrain.gdshader:60-61 and :93, FieldTerrain.gdshader:25-26, and GroundFinish.gdshaderinc:24-25 and :112-113.
   - Re-take the baseline first. The 7.1 ms Ultra figure in the doc was measured on battery while clocks were ramping.
   - Measurement protocol, at your standing rule of highest graphics settings:
     - plugged in
     - Ultra preset forced with `--quality=ultra`, adaptive scaling off, 4x MSAA. Ultra is not auto-selected on a 2070S.
     - once on Mobile and once with `--rendering-method forward_plus`
     - command: `godot --path game -- --bundled-start --quality=ultra --quality-bench <dir>`
   - Report:
     - GPU p50/p95/p99
     - nvidia-smi `memory.used` sampled every 200 ms
     - draw count (must be unchanged)
     - FFT autocorrelation drop at the old 0.8, 2, 24 and 32 m periods, on a top-down capture
     - a pixel-identical diff of two renders of the same pose
   - Pass: p95 within +0.5 ms on both renderers.
2. **CC0 library, manifest and CI licence gate.** Swap the grass, soil and asphalt arrays to the library. Use BC5 normals.
3. **`atlas surface` with road-wear masks**, tied to RoadRoughness.gd, plus the provenance channel and the F-key view.
4. **Building recipes:** wall and roof layers from NSI and Overture, footprint UVs, age-driven weathering, and the procedural window grid.
5. **Offline AI gap-fill:** MatFuse or FLUX.2 klein 4B through stable-diffusion.cpp or diffusers. Covers US vinyl siding, chip-seal and stucco. A human reviews every output, which is tagged generated and committed only if MIT/Apache-derived.
6. **Optional menu-time `atlas forge` tier.**
   - Benchmark it at Ultra first: seconds per swatch with the game closed and with the game at the menu, and peak VRAM.
   - Gate test: start generating, leave the menu, and check that no frame exceeds 33 ms.
   - If it fails, keep only overnight `--bake-region`.
7. **Optional CPU VLM facade labelling** from photos into recipe parameters. Off by default, because US photo coverage is sparse.

## Honest limits

- This reaches "well-textured mid-2010s", not photoreal. Box massing, missing overhangs, porches and garages, and vegetation variety need the geometry recipes in real-world-detail.md section 4.
- NSI med_yr_blt is a census-block median, so the era of an individual house will often be wrong. The ±15-year jitter hides this but does not fix it.
- CC0 libraries lean European and clean. Gap-fill AI quality at 512 px is mushy and has lighting baked in.
- stable-diffusion.cpp's circular padding for seamless output is unconfirmed and may need a patch.
- Every timing and VRAM figure here is an estimate scaled from other GPUs until it is measured on this laptop at Ultra.
- A street seen before its swatch was generated shows the CC0 fallback first.
