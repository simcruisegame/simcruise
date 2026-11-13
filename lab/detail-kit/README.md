# AI detail kit

One offline AI-generated layer of fine detail for asphalt, concrete, stucco, brick, soil, sand, leaf litter and bark. In the game it is used on the road and the ground only (docs/research/world-detail-methods.md, "The AI detail kit").

- Model: Stable Diffusion XL base 1.0 (`stabilityai/stable-diffusion-xl-base-1.0`), licence CreativeML Open RAIL++-M (commercial use allowed under its use restrictions). FLUX.1-schnell (Apache-2.0) was the first choice; its Hugging Face repository needs a log-in and accepting its terms, which this PC does not have.
- `generate.py`: prompts, seeds (20261113 + slice index), 30 steps, guidance 6, 1024 px, CPU offload on the 8 GB laptop GPU. Environment: Python 3.12, torch 2.6 cu124, diffusers 0.43 (on the PC: `C:\sc-cache\detailkit-venv`). `--only litter,bark` remakes some slices.
- `raw/`: the eight slices and `prompts.json`.
- `make_kit.py`: writes `data/art/detail-kit/detail-albedo.png`, `detail-normal.png` and `kit.json` (albedo ratio with mean 1, soft log clamp per material; Sobel normals; 4 x 2 atlas of 512 px slices with wrapped borders). The game loads the PNGs at start; no material rebuild is needed after a remake.
- Shader side: `DetailKitCode` (struct `FDK`) in `SimCruiseWorldResources.cpp`, used by the ground and asphalt materials; flag `-SimCruiseNoDetailLayer`.
