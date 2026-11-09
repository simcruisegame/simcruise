# Rebuild the Needles Highway granite solid

Requires Python with NumPy, SciPy and OpenCV, and a C++17 compiler. From this directory, with an empty output directory:

    python bake_granite.py ./out
    python compact.py ./out

The first script samples the two independent camera masks, reconstructed depths and public 3DEP survey on a 0.37 m local volume. It emits a watertight mesh and two screened WebP maps. The second compiles meshoptimizer v0.25 in the output directory, protects sensitive topology and checks every edge has two incident triangles. The executable is removed after use.

The three final output bytes correspond to the game resources:

| Bake output | Runtime filename |
|---|---|
| needles-volume.meshdata | needles-granite.meshdata |
| needles-volume-a.webp | needles-granite-a.meshdata |
| needles-volume-b.webp | needles-granite-b.meshdata |

The two JPEG inputs are w_lemay’s 960-pixel Wikimedia Commons photographs 149021336 and 149021324, CC BY-SA 2.0. Their source links, companion photos, timestamps and licenses are in the adjacent receipt. Their unrestricted rock pixels are projected onto newly reconstructed measured surfaces. The screened maps use alpha to exclude sky and identifiable foreground vegetation. Foliage that hides a face can leave its color and depth uncertain.

The prematched inputs in survey.npz use metres in local east, up and north axes. They contain 3DEP bare-earth heights, clipped last-return laser samples, independent optical camera poses and intrinsics, directly stereo-matched visible points and a bounded interpolated front. The latter is the post-registration starting point for this compact rebake. Regenerating those measurements from statewide raw data is a separate acquisition and matching step. Source metadata names the archive, LAS entry, two flight lines and byte budget. The manual foreground and silhouette boundaries used here were traced on the two authoring photographs; the held-out frame does not enter this bake.

The terrain clips bundled alongside the solid store centimeter NAVD88 targets and blend weights, with one local game datum alignment. Their weights taper over road margins and independently reach zero at every tile edge. The existing road footprint signature must match before either clip is used. The normal game terrain shader paints these cells.

The photographic intermediate measurements, masked maps and finished appearance are distributed under CC BY-SA 2.0. USGS and USDA survey observations are public-domain federal data. meshoptimizer is MIT-licensed by Arseny Kapoulkine; its original license is in meshopt/LICENSE.md.
