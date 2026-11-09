# Testing automatic roof reconstruction

The GPL-3.0 Roofer 1.0.0 command-line authoring tool ran locally on Apple Silicon on two Bartlesville building footprints. Its verified official macOS release digest is in release.json. This experiment is separate from the playable 1.0.5 roof meshes. The neighboring Bartlesville source record identifies the exact public USGS point-cloud tile, date and SHA-256. Roof geometry is evaluated against spatially withheld returns.

The training set retained 40,752 returns and excluded 10,188 using a meter-grid modulo split before running Roofer. These counts include nearby ground. We transformed the USGS Web Mercator XY into metric UTM 15N and retained the measured elevations. The survey classified ground as 2 and above-ground returns as 1. The two supplied footprints limit reconstruction.

For the simpler building, 118 withheld roof returns had median/90th percentile residuals of 0.013/0.029 m. A constant-height roof estimated from the training returns scored 0.050/0.110 m. For the restaurant, 183 withheld roof returns scored 0.015/1.130 m. It estimated a maximum 5.32 m above ground and missed the entrance tower. The held roof points reached 8.06 m at the 99th percentile. The native game retains the more detailed measured roof on that footprint.

To rerun, download the leaf from ../bartlesville/source.json and extract the Apple Silicon Roofer release with its share/proj and share/gdal directories. Use Python with laspy/lazrs, NumPy, pyproj and Shapely. Pass a fresh directory for WORK.

```sh
python prepare.py path/to/11-1298-1358-1021.laz "$WORK"
mkdir -p "$WORK/output"
PROJ_DATA="$ROOFER_DIR/share/proj" GDAL_DATA="$ROOFER_DIR/share/gdal" "$ROOFER_DIR/bin/roofer" -j 2 --srs EPSG:32615 --bld-class 1 --grnd-class 2 --id-attribute bid --lod22 "$WORK/train.las" "$WORK/footprints.geojson" "$WORK/output"
python score.py "$WORK"
```

[Source and license](https://github.com/3DBAG/roofer) · [CLI documentation](https://3dbag.github.io/roofer/) · [USGS open lidar](https://registry.opendata.aws/usgs-lidar/)
