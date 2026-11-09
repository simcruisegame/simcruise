# Visible ridges and measured snow

As the driver holds a view, the existing regional elevation mesh identifies up to two 15 km-class imagery cells crossing the visible skyline. Orthophoto tiles use the shared cached USGS endpoint. NASA’s daily MODIS Terra NDSI product supplies positive regional snow evidence. Clouds, night, invalid samples and absent observations leave the underlying ground visible. In a cell with positive evidence, Copernicus Sentinel-2 L2A can refine the snow boundary and supply calibrated red-band reflectance from the same acquisition. Shadow remains approximate. The local cache retains acquisition IDs, observation dates and resolutions. These are satellite acquisition dates.

The fine mask uses the 40 m overview. The red band uses a 40–160 m overview selected within the transfer limit. The cell has separate limits of 260,000 classification bytes and 760,000 optical bytes plus a catalog page up to 168,000 bytes. At most one new cell starts per 45 seconds during steady play. The USGS photographic layer allows two requests per stable direction and a 25 second interval before another sector. The 90 km regional NASA field uses at most four 256-pixel static tiles per region and date. Repeating the complete Wyoming cell used zero external requests. The byte counts in source-ranges.json describe the two measured geographic cells, independently of the photographs.

Recompute the Wyoming face scores from this directory with Python, Pillow, SciPy and NumPy.

```sh
python score.py ../photograph.jpg ../ridge-scores.json wy.jpg wy-summer.jpg wy-broad.jpg semantic.jpg
```

The 23 separately marked silhouette positions and a 48 pixel vertical crop came from the previous camera registration. The scoring strip follows the photographed skyline at an offset of 12–85 pixels. Its limits are x 180–785 and y at most 310 in a 960×640 frame. There was no camera fit to the new snow output. The middle and right renders use the same player engine and camera.
