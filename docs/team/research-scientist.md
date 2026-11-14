# Research scientist: study notes

## Rules
- A data layer is a measurement. Record for each: licence, coverage, resolution, date, and known error.
- Aerial photos have baked sun and shadow. De-light before any colour is used as albedo (team lighting notes).
- A model output is a hypothesis until a held-out test at random US places gives its error.
- CPU first: the GPU draws the game. Legal weights only (ODC-BY, CC BY, Apache-2.0, MIT; no non-commercial weights in shipped builds).

## Tests
- Segmentation and detection: IoU / F1 against OSM, lidar or hand labels on at least 6 random test places.
- Canopy and height: RMSE and bias against 3DEP lidar (first return minus bare earth) where lidar exists.
- Colour: patch albedo within the published range for the class (grass 0.08-0.25, asphalt 0.05-0.12, concrete 0.25-0.35).

## Sources
- USDA NAIP imagery: https://naip-usdaonline.hub.arcgis.com/
- USGS 3DEP: https://www.usgs.gov/3d-elevation-program
- Allen Institute for AI, Satlas (models and data, ODC-BY): https://github.com/allenai/satlas
- Weinstein et al., "DeepForest: A Python package for RGB deep learning tree crown delineation", Methods Ecol. Evol. 2020.
- Tolan et al., "Very high resolution canopy height maps from RGB imagery" (Meta CHM), Remote Sens. Environ. 2024.
- MRLC NLCD: https://www.mrlc.gov/ · LANDFIRE EVT: https://landfire.gov/
- Beck et al., "High-resolution (1 km) Köppen-Geiger maps for 1901-2099", Sci. Data 2023 (CC BY 4.0).
- Congalton and Green, *Assessing the Accuracy of Remotely Sensed Data*, 3rd ed., CRC Press, 2019.

## First review (2026-11-14)

The scan engine contract v1 review is in `docs/team/reviews/2026-11-14-scan-engine.md`.
