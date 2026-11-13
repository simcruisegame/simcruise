# AI detail kit (unfinished)

One offline AI-generated layer of fine detail for asphalt, concrete, stucco, brick, soil, sand, leaf litter and bark.

- Model: Stable Diffusion XL base 1.0 (`stabilityai/stable-diffusion-xl-base-1.0`), licence CreativeML Open RAIL++-M (commercial use allowed under its use restrictions). FLUX.1-schnell (Apache-2.0) was the first choice; its Hugging Face repository needs a log-in and accepting its terms, which this PC does not have.
- `generate.py`: prompts, seeds (20261113 + slice index), 30 steps, guidance 6, 1024 px, CPU offload on the 8 GB laptop GPU. Environment: Python 3.12, torch 2.6 cu124, diffusers 0.43.
- `raw/`: the six slices made before the work stopped (asphalt, concrete, stucco, brick, soil, sand). Litter and bark are not made yet.
- `make_kit.py`: turns them into `data/art/detail-kit/detail-albedo.png` and `detail-normal.png` (albedo ratio with mean 1, clamped per material; Sobel normals; 4 x 2 atlas of 512 px slices with wrapped borders). Not run yet.
- Shader side (on this branch): `DetailKitCode` (struct `FDK`) in `SimCruiseWorldResources.cpp`, used by the ground, asphalt, concrete and building shaders; flag `-SimCruiseNoDetailLayer`. Compiles in C++; the HLSL has not been run in the game.
