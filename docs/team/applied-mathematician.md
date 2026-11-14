# Applied mathematician: study notes

## Rules
- Sample above Nyquist or prefilter: any detail with a period shorter than 2 texels at its draw distance must fade out (mip or analytic band limit), or it shimmers.
- Procedural detail is a function of world position (metres from a fixed geodetic anchor), never of tile-local UV, so it never seams. Use 64-bit or split (tile + offset) coordinates before float32 maths.
- Non-repeating: hash-based or aperiodic layouts; no visible period inside the view distance.
- Fits: least squares with a robust loss (Huber) and explicit regularisation; report residuals.
- Every threshold has units and an error budget.

## Tests
- Seam test: the function at a tile border gives the same value from both tiles (to 1e-4 of its range).
- Temporal stability: frame-to-frame luma change on a static camera < 1 code value on average.
- Self-check: rendered top-down tile vs de-lit aerial: SSIM and colour error per class, with a pass line.

## Sources
- Pharr, Jakob, Humphreys, *Physically Based Rendering*, 4th ed., 2023 (sampling and reconstruction).
- Perlin, "Improving Noise", SIGGRAPH 2002; Worley, "A Cellular Texture Basis Function", SIGGRAPH 1996.
- Quilez, articles on noise, domain warping and texture repetition: https://iquilezles.org/articles/
- Heitz and Neyret, "High-Performance By-Example Noise using a Histogram-Preserving Blending Operator", HPG 2018.
- Wang, Bovik et al., "Image Quality Assessment: From Error Visibility to Structural Similarity" (SSIM), IEEE TIP 2004.
- Nocedal and Wright, *Numerical Optimization*, 2nd ed., Springer, 2006.
- Snyder, *Map Projections: A Working Manual*, USGS PP 1395, 1987.

## First review (2026-11-14)

The scan engine contract v1 review is in `docs/team/reviews/2026-11-14-scan-engine.md`.
