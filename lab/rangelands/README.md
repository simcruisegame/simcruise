# Rangeland ground and vegetation

Two annual surveys describe the vegetation in each western driving district. The USDA Rangeland Analysis Platform supplies masked shrub cover. USGS RCMAP supplies mean height in centimetres for the shrub-covered portion of the ground. These are coarse modeled area statistics. The runtime combines them with finer aerial contrast to place a population of 3D shrubs near the car. Individual stems, ages and species are inferred. Both surveys are from 2025. The helper can return an empty layer where either survey lacks support.

The worker first requests one RAP z12 tile. Where the tile has enough cover, it requests one bounded height GeoTIFF and registers its actual projected bounds to the RAP grid. A district spans several kilometres. Returning to the same fresh district reads the cooked local PNG. The Arizona test used 40,484 and 524,702 bytes, then zero external reads on return. This count applies to the two vegetation products. Roads, aerial photographs and other existing services have separate caches and budgets. The local texture and procedural plant mesh use no external service.

The display tile palette was checked against the numerical scientific vegetation raster in Utah, Wyoming and Nebraska. Only confirmed palette entries for 0–40 percent are decoded. The driving layer uses open stands with at least 4 percent cover. Higher cover and unknown colors defer to the other vegetation layers. The field check used separate 2021 USDA red/near-infrared imagery. US 191 showed a small preference for vegetation around the inferred clumps. Near US 163 both sets of sample points had weak infrared vegetation contrast. These checks cannot identify real individual shrubs.

Recent ground color uses the existing Sentinel-2 L2A true-color image and its matching scene-classification layer. Only supported nonvegetated pixels contribute. ESA defines the 8-bit encoding and its 0.25 reflectance saturation point. Red-only saturation can be estimated from neighboring measured soil colors when their slopes agree. The nearby aerial photograph retains finer local contrast. The packed CC0 mineral scan supplies illustrative grain, roughness and small surface normals. Its physical width is 2.53 m. Texture detail and vegetation do not change tire contacts.

Run the following commands from the source checkout.

```sh
bun lab/check-ground-reflectance.mjs
bun lab/check-rangeland.mjs
bun lab/check-rangeland.mjs --live
```

The first test replays two archived 27 × 27 windows against the original 10 m calibrated bands. The sensor RGBA file is row-major uint8. The matched bands file is interleaved red, green, blue, little-endian uint16. Apply a scale of 0.0001 and an offset of −0.1. All fixture pixels had nonvegetated class 5. A separate synthetic test checks unsupported classes, double saturation and missing pixels. Across 1,337 directly measured pixels, mean channel error was 0.00133–0.00190 in reflectance units. For the 106 partially saturated samples, mean red error was 0.01285. This measures sensor decoding. Differences between a street photograph and the rendered scene have many additional causes.

The seed 19990515 chose three of six western corridors before the Commons search. US 191 supplied a usable roadside photograph and a named summit for a heading estimate. AZ 64 supplied a telephoto image with unresolved pose. The 4 km US 50 search returned aerial and orbital imagery. The gallery preserves the dated photograph and same-camera control for US 191. See validation.json for rectangle coordinates, measurement method and limitations. Comparison images and fixture credits are in asset-manifest.json.

- [USDA Rangeland Analysis Platform map tiles](https://rangelands.app/support/71-api-documentation)
- [USGS RCMAP shrub height](https://www.mrlc.gov/data/rcmap-shrub-height)
- [ESA Sentinel-2 true-color encoding](https://sentiwiki.copernicus.eu/web/glossary)
- [Dario Barresi’s Sandy Gravel 02 scan](https://polyhaven.com/a/sandy_gravel_02)
- [Copernicus Sentinel data terms](https://dataspace.copernicus.eu/terms-and-conditions)
