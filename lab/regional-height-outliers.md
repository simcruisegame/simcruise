# Isolated holes in the distant elevation raster

Some zoom-10 Terrarium tiles have clusters of false elevations. In the first 90 km scene examined, an eastern US region, two native pixels encoded −667 m in the middle of a largely flat area. Bilinear interpolation produced a −602.17 m vertex on flat farmland. The zoom-12 tile returned +41.08 m at that position. The separately published USGS 1/3 arc-second bare-earth raster gave +42.35 m, with a 7 by 7 neighborhood ranging from +41.08 to +44.11 m. The erroneous coarse values were present in the original downloaded RGB bytes. The source production stage responsible is unknown. [Terrarium source layer](https://registry.opendata.aws/terrain-tiles/) and [USGS 3DEP 1/3 arc-second products](https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/13/TIFF/current/).

The optional repair runs before skyline selection and triangulation. It looks for deep pits isolated in both horizontal axes, checks a 16-direction collar and a wider low-relief neighborhood, and verifies at two zoom levels higher. Only when the more detailed raster is locally smooth and agrees with the surrounding collar may its median replace the affected coarse pixel and its limited resampling halo. The raw files remain available in the source cache. Boundary candidates without a trustworthy collar, rugged ground, coastal mixtures, absent or malformed witnesses, and pits corroborated by both resolutions remain untouched. The high-resolution witness comes from the same provider. The USGS comparisons are external validation rather than runtime requests. The module makes up to two witness reads taken from the existing four refinement slots. Each is limited to 300 kB and at most 1.6 seconds within the caller’s remaining deadline.

| Matched extrema | Original terrain | Corrected terrain | USGS 1/3″ comparison |
| --- | ---: | ---: | ---: |
| Eastern scene | −602.17 m | +40.88 m | +42.35 m |
| Nebraska | +175.97 m | +375.27 m | +373.63 m |
| Alaska | −47.85 m | unchanged | +7.68 m |
| Hawai‘i | −38.21 m | unchanged | 0 m |
| Death Valley | −112.85 m | unchanged | −80.25 m |

The latter three residuals need different evidence. The Alaska point is near a sharp transition in the finer source. The Hawai‘i official raster is zero for all 49 pixels in its local window. Real below-sea-level land in Death Valley precludes clamping all negative elevations. A region-wide offline replay required 3–24 ms for the screening pass on an 8 GB Apple A18 Pro Mac, with no source reads in Utah, Hawaii, Alaska or Death Valley. The eastern witness was 114,963 compressed bytes, Nebraska 108,628 bytes. [Nebraska](https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/13/TIFF/current/n41w097/USGS_13_n41w097.tif), [Alaska](https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/13/TIFF/current/n61w150/USGS_13_n61w150.tif), [Hawai‘i](https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/13/TIFF/current/n22w159/USGS_13_n22w159.tif), [Death Valley](https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/13/TIFF/current/n37w117/USGS_13_n37w117.tif).

Run `bun lab/check-regional-height-outliers.mjs` for deterministic source-correction and adverse-input checks. The 90 km terrain is visual scenery. Near-road collision and wheel contact use their separate terrain pipeline.

After fixing the anomalous sample, the lowest remaining vertex of the eastern scene was a real, documented below-sea-level quarry depression (−53 m in the mesh against −86 m in the independent 3DEP bare-earth pixel), which the repair correctly left untouched: pits corroborated by both resolutions are not holes.
