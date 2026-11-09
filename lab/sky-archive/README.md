# Archived sky at a dated road photograph

The ordinary street-photo lookup supplies the camera’s reported position and UTC shutter time. The game selects a local sky only when a nearby station and the NOAA GOES cloud mask support the estimate. The two active locations in the four-camera Metal test were Lincoln, Nebraska and Bartlesville, Oklahoma. Cloud evidence conflicted at the two others. [Three-city comparison](evidence/verified-comparison.webp) · [Minneapolis comparison](evidence/prospective-final-comparison.webp)

| Fixed photo region | Capture UTC | No archive, capture sun | Station alone | Final | Final observation |
|---|---|---:|---:|---:|---|
| Lincoln, NE | 2025-08-19 13:41 | 39.48 | 43.14 | **25.31** | LNK, 7.86 km, clear in two reports, 5→9 statute miles. GOES-19 found 47 clear and 2 probably clear pixels. |
| Bartlesville, OK | 2026-02-12 19:27:37 | 29.34 | 32.10 | **8.76** | BVO, 7.51 km, two clear-below-sensor reports with 10+ miles visibility. GOES-19 found 49 cloudy pixels. High cloud is visible in the source. |
| Cheyenne, WY | 2026-10-25 20:28 | 33.86 | 33.86 | **33.86** | CYS, 1.93 km, changed from few to clear and FEW, 4.03 km, reported scattered. GOES found a clear local overhead area. |
| Minneapolis, MN | 2025-04-19 18:27:29 | 38.37 | — | **38.37** | MSP and MIC reported some cloud. The 49 satellite pixels were clear. The panorama shows small cumulus beyond this overhead neighborhood. |

Values are mean absolute displayed RGB error in 0–255 values. Each before/after used a fixed camera, capture sun, and frozen scene at 960×720. The original repeated frame was pixel-identical at all four locations. On the two active sites, the 36 sampled frame-interval medians were 8.016→8.023 ms and 7.937→8.054 ms. These are short wall-clock samples from Godot 4.7.2 Forward Mobile on Apple A18 Pro. Camera exposure, color balance, wet glass, model geometry and pose all influence the result. [Field measurements](evidence/verified-metrics.json) · [Minneapolis measurements](evidence/prospective-final-metrics.json) · [Native receipt](evidence/bartlesville-native.json)

### What the holdouts changed

Station-only clearing made both active sites worse. The first satellite rule cleared an area when at least 90% of the local pixels were clear and the primary station reported fewer than half the sky covered. It improved Cheyenne to 24.93 but failed a fourth, prospectively selected Minneapolis camera, worsening its error to 58.14. Its sky was too dark and desaturated. The final rule checks every usable bracketing observation for clear/SKC, which leaves both scenes at their originals. The final Minneapolis check is a regression check after revising the rule. The wrong-way change and the original preregistration remain available. [Station-only results](evidence/station-comparison.webp) · [Initial Minneapolis failure](evidence/prospective-comparison.webp) · [Selection and scoring region](evidence/minneapolis-preregister.md)

A further unseen Seattle trial under the final rule yielded clear airport reports but 41 clear and 8 cloudy satellite pixels. The rule abstained. The Panoramax camera host failed its TLS handshake, so Seattle has a source check only. Bethel, Maine had no eligible 2012 airport within 30 km. At the Lincoln UTC timestamp, Anchorage’s GOES-18 full disk yielded 49 cloudy pixels in 292,504 range bytes while Honolulu’s GOES-18 Pacific sector yielded 37 clear and 12 cloudy pixels in 195,986 bytes. These two sites tested geographic coverage. Their local sun was below the player’s daylight threshold. [Seattle](evidence/seattle-source.json) · [Alaska / Hawaii runs](evidence/compiled-log.txt)

### Sources and geographic support

The bundled 137,984-byte station index contains 2,677 U.S. entries in all 50 states plus the IEM GM, GU, PR and VI codes. It includes monitoring dates and elevation. One request asks the Iowa Environmental Mesonet for five UTC hours at up to two eligible sites within 30 km and 200 m of the roadway elevation. Both sides of the shutter must have usable sky reports within 65 minutes, spanning at most 95 minutes. Missing layers, unrecognized groups, major precipitation, obscuration, unavailable days and sites outside the period of record leave the fallback in place. [IEM station locator](https://mesonet.agron.iastate.edu/geojson/network/AZOS.geojson?only_online=false) · [IEM archive fields and sources](https://mesonet.agron.iastate.edu/request/download.phtml) · [IEM service terms](https://mesonet.agron.iastate.edu/api/)

NWS describes automated CLR as clear up to the ceilometer’s 12,000 ft limit. A 10SM report is a lower bound. A finite pair below this ceiling on reported visibility permits interpolation in statute miles. Lighting blends the estimate with the existing reference settings. A neighboring airport cannot measure the haze directly in front of a windshield. [NWS observation codes](https://www.weather.gov/media/asos/appen.pdf) · [ASOS visibility](https://www.weather.gov/asos/Visibility.html)

The NOAA GOES-R ABI Level 2 Clear Sky Mask supplies a four-category field and a per-pixel quality flag on a nominal 2 km fixed grid. The helper retrieves the hourly S3 object listing for the operational spacecraft and sector, then selects the closest scan midpoint within 5 minutes. It reads the geostationary projection and grid scales from the actual NetCDF4/HDF5 file and samples 7×7 cells. At least 40 good-quality pixels and 90% agreement are required. The physical ground area grows toward the limb. Views past 76° satellite zenith abstain. Actual scan time at the road can differ from the scan midpoint. The game cannot place individual clouds on a bearing. [NOAA data record](https://www.ncei.noaa.gov/access/metadata/landing-page/bin/iso?id=gov.noaa.ncdc%3AC01503) · [Public archive](https://registry.opendata.aws/noaa-goes/) · [Spacecraft transitions](https://ospo.noaa.gov/operations/goes/status.html) · [ABI projection](https://www.star.nesdis.noaa.gov/atmospheric-composition-training/satellite_data_goes_imager_projection.php)

Early GHCNh reports are available at many stations. The station/year or global/day distribution is a poor match for a single camera lookup. IEM also archives station-centered satellite cloud products at some sites going back to 1993. This release uses the quality-filtered satellite neighborhood at the road itself and abstains before the applicable GOES-R archive. [NCEI GHCNh data](https://www.ncei.noaa.gov/access/metadata/landing-page/bin/iso?id=gov.noaa.ncdc%3AC01688) · [Historical station SCP](https://mesonet.agron.iastate.edu/sites/scp.php?network=IA_ASOS&station=MUT)

### Ordinary play and budgets

A single low-priority Godot worker runs when a qualified dated photograph is within the existing 145 m support radius, the chosen historical sunlight is above 7°, and the player is on reference weather. Authored sky samples retain precedence. Live weather, photos with unknown UTC, pending lookups, night scenes and unsupported areas use the established fallback. No data call blocks a physics tick. At Lincoln, ordinary place selection chose the original Panoramax frame with no forced date, no camera override, and all four wheels in contact. Toggling live and undated weather cleared the archived result. [Normal-game check](evidence/normal-log.txt)

Each satellite worker accepts at most 24 range reads of 32 KiB and a 20,000-byte listing. The METAR response cap is 96 KiB. One complete cold Minneapolis run made eight requests. Its immediate repeat made zero. Lincoln and Bartlesville each needed 196,608 bytes of satellite ranges, and Cheyenne needed 163,840 bytes. Historical data uses a one-year disk TTL under the existing 96 MiB source-cache policy. IEM requests are separated by at least 61 seconds. The cache honors HTTP 429/503 Retry-After and supplies stale entries when available. An unsuccessful camera request waits at least 15 minutes before a runtime retry. The read covers cloud classes, their quality flags and projection metadata. [Minneapolis source receipt](evidence/minneapolis-prospective.json) · [Lincoln source receipt](evidence/lincoln-source.json)

The NIST h5wasm 0.10.3 reader and upstream HDF5 library run only inside the disposable helper used for these range reads. Driver startup retains the existing runtime libraries. The upstream 3,544,184-byte WebAssembly payload is stored once in a 775,346-byte Brotli asset. The regenerated payload SHA-256 matches the upstream module. With production Bun flags, the candidate increased the compiled executable by 1,040,256 bytes and its level-6 deflated size by 874,321 bytes. Other simultaneous features can change the final application size. Retain the complete upstream license and acknowledgment. [Measured build sizes](evidence/release-costs.json) · [Upstream NIST project](https://github.com/usnistgov/h5wasm) · [Full redistribution notices](../../helper/vendor/h5wasm/LICENSE.txt)

### Reproduce

From the source root, run the following commands. The sample command contacts the archive when the named cache is empty. Its input is latitude, longitude, UTC Unix seconds, and road elevation in metres. An isolated test cache preserves normal player settings.

```sh
bun lab/sky-archive/check.mjs
bun lab/daylight/conditions-check.mjs
bun helper/atlas.mjs archive-weather /tmp/sunday-sky-test 40.808014 -96.675003 1755610860 374
bun helper/atlas.mjs archive-weather /tmp/sunday-sky-test 40.808014 -96.675003 1755610860 374
python3 lab/sky-archive/build-stations.py --output /tmp/sunday-stations.json
```

The shipped vendored reader supports existing frozen source builds without a new main-project dependency. To regenerate from the pinned upstream package, run `bun install --frozen-lockfile` followed by `bun prepare-reader.mjs` in `lab/sky-archive`. The native Normal check is `-- --archive-normal-check`. The viewport comparison is `-- --reference LAT LON HEADING HFOV OUTPUT.jpg PITCH_DOWN ROLL --reference-epoch UTC_SECONDS --archive-weather-ab`. It writes before, after, and repeat PNGs plus a source receipt. The reference command is appropriate for a dated *daytime* source. Skyline comparison positions, scoring regions, and verification are in the local JSON. [Scoring regions](evidence/scoring-regions.json)

### Photographs

- Lincoln. Panoramax contributor slinky309, August 19, 2025. Resized comparison crop under CC BY-SA 4.0. [Original](https://panoramax.xyz/#focus=pic&pic=8889f6fa-34ff-4959-adb9-7d162c46b68c)
- Bartlesville. Panoramax contributor Baloo Uriza, February 12, 2026. The registered perspective crop includes an invalid black margin on the right. The scoring patch excludes it. CC BY-SA 4.0. [Original](https://panoramax.xyz/#focus=pic&pic=37d05f23-c4c9-428d-90b0-0a47b4e6be13)
- Cheyenne. Dough4872, October 25, 2026, CC BY-SA 4.0. Resized registered landmark photo. [Original and license](https://commons.wikimedia.org/wiki/File:Wyoming_State_Capitol_from_24th_Street_and_Capitol_Avenue.jpeg)
- Minneapolis. Panoramax contributor slinky309, April 19, 2025, CC BY-SA 4.0. The comparison reprojects the equirectangular original to a 74° horizontal field of view at catalog bearing 292°, pitched up 15°. The image contains nearby buildings. [Original](https://panoramax.xyz/#focus=pic&pic=5eebb471-7017-40c6-b62d-32cffbc7f9eb) · [Creative Commons license](https://creativecommons.org/licenses/by-sa/4.0/)
