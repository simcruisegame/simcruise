"""Generate the fine-detail kit's source photographs once, offline, on the PC.

Model: Stable Diffusion XL base 1.0 (stabilityai/stable-diffusion-xl-base-1.0),
CreativeML Open RAIL++-M licence (commercial use allowed under its use restrictions).
FLUX.1-schnell (Apache-2.0) was the first choice; its Hugging Face repository is gated
behind a log-in and accepting terms, which this PC does not have.

Usage (an environment with torch + diffusers, see README.md):
    python generate.py --out raw
Writes raw/<name>.png (1024 x 1024) and raw/prompts.json (prompts, seeds, model, steps).
"""
import argparse
import json
import os

import torch
from diffusers import StableDiffusionXLPipeline

MODEL = "stabilityai/stable-diffusion-xl-base-1.0"
COMMON = ("top-down orthographic close-up photograph, seamless texture, flat overcast light, "
          "no shadows, no perspective, evenly lit, high detail, sharp focus, 1 metre across")
NEGATIVE = ("shadow, vignette, perspective, horizon, sky, object, person, text, watermark, logo, "
            "border, frame, blurry, painting, illustration, 3d render, cartoon")
SLICES = [
    ("asphalt", "worn asphalt road surface, grey bitumen with exposed small stone aggregate, fine cracks"),
    ("concrete", "weathered poured concrete slab, fine pores, small pits, faint stains, broom finish"),
    ("stucco", "painted exterior stucco plaster wall, fine sand float finish, slight unevenness, hairline cracks"),
    ("brick", "old common red brick wall, running bond, mortar joints, chipped edges, varied brick colour"),
    ("soil", "bare dry soil with small gravel and pebbles, compacted dirt, few dry grass bits"),
    ("sand", "beach sand, fine quartz and shell fragments, small footprints smoothed by wind"),
    ("litter", "forest floor, dry fallen leaves, pine needles, twigs, dark humus between them"),
    ("bark", "tree bark close-up, deep vertical furrows and ridges, grey brown, lichen spots"),
]
SEED0 = 20261113


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="raw")
    ap.add_argument("--steps", type=int, default=30)
    ap.add_argument("--guidance", type=float, default=6.0)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    pipe = StableDiffusionXLPipeline.from_pretrained(MODEL, torch_dtype=torch.float16, variant="fp16", use_safetensors=True)
    pipe.enable_model_cpu_offload()  # 8 GB laptop GPU
    pipe.vae.enable_tiling()
    record = {"model": MODEL, "licence": "CreativeML Open RAIL++-M", "steps": a.steps, "guidance": a.guidance,
              "size": 1024, "negative": NEGATIVE, "slices": []}
    for i, (name, subject) in enumerate(SLICES):
        if a.only and name not in a.only.split(","):
            continue
        prompt = f"{subject}, {COMMON}"
        seed = SEED0 + i
        g = torch.Generator("cpu").manual_seed(seed)
        img = pipe(prompt, negative_prompt=NEGATIVE, num_inference_steps=a.steps, guidance_scale=a.guidance,
                   width=1024, height=1024, generator=g).images[0]
        img.save(os.path.join(a.out, f"{name}.png"))
        record["slices"].append({"index": i, "name": name, "prompt": prompt, "seed": seed})
        print("made", name, flush=True)
    with open(os.path.join(a.out, "prompts.json"), "w") as f:
        json.dump(record, f, indent=2)


if __name__ == "__main__":
    main()
