# Davidson Hall, Bethel, Maine

Choose **Find a place → Davidson Hall · Bethel, ME**. The car starts northbound along Church Street, with the west and south walls on the right. Searching “Gould Academy” also finds the stop. A new drive still begins on a random road.

## Evidence

Nick Kline photographed Davidson Hall on June 29, 2012 at 18:44:36 UTC. His iPhone reported 44.408000, −70.793333 and a true bearing of 25.65°. The registered roadside camera uses 44.407994, −70.793253, a bearing of 23.87°, an upward pitch of 3.82°, roll of 2.79° and an estimated 61.04° horizontal field of view. The eye height is assumed to be 1.65 m. Rooflines, two visible corners and a weaker tree-obscured corner constrain the camera. The manually masked and rectified source pixels cover 30–63% of each of four candidate wall planes. [Original photograph](https://commons.wikimedia.org/wiki/File:Gould_Academy_Davidson_Hall.JPG)

Two 2016 USGS flight strips independently returned a median flat roof elevation of 218.91 m NAVD88, using 1,271 and 1,080 interior first returns respectively. Ground around the perimeter estimates 209.41 m at the footprint center. This implies a height of 9.50 m. The photograph fit gives 9.08 m on the game terrain and the original generic structure used 5 m. The lower entrance roof uses a separate mesh. The photographed corner and laser returns support an estimated 6.03 m span and 3.18 m projection. A plane fitted to 26 returns from strip 154 gave a median absolute error of 0.078 m on all 24 first returns inside the buffered footprint from strip 155. Seventeen landed within 0.25 m. The seven remaining returns struck the main roof or lower surfaces near the boundary. The fascia thickness and visible masonry pier widths come from the image and remain approximate. [USGS point tile](https://www.sciencebase.gov/catalog/item/644b2bc8d34e45f6ddcd3d94)

The native 1280 × 956 Apple Metal comparison uses the same loaded scene for the standard and registered facade. Over 280,475 building pixels, mean absolute RGB channel difference fell from 86.21 to 58.75 on a 0–255 scale. Source-edge correlation rose from 0.081 to 0.571. These figures measure agreement with the source used for reconstruction. Neither nearby open campus photograph examined contains an unobstructed second view. The second comparison uses the normal Miata cockpit at a different position on Church Street with four tire contacts. Both camera poses returned identical pixels when the standard building was restored. The summer lawn, foreground planting, trees and entrance recess still differ from the photograph. This asset describes a historical observation of one campus building.

## Reproduce

Use an independent working directory copied from this folder. Python dependencies are NumPy, SciPy, Pillow, pyproj, Shapely 2, laspy and lazrs. The JSON source receipt contains the exact original image URL, 2016 public point tile and SHA-256 hashes. Download them once into that working copy as `davidson-original.jpg` and `2016.laz`. The retained low-resolution label raster can be used directly. The project’s offline semantic tool used the original photograph resized to 513 × 383 for its raw ADE20K label indices. Full-resolution hand masks reject foliage, objects, ground, source shadows and the projecting entrance roof. Neutral glazing preserves the measured window frames. Unobserved parts of these four walls use a small sample of the same photographed masonry. Rear walls keep the existing building materials. [Semantic workflow](../../semantic/README.md)

```sh
python geometry.py
python check_lidar.py
python awning.py
python bake.py
```

The five PNGs and tile manifest appear in `assets/`. The point file and photographs are authoring inputs only. The game uses the local packaged results. It checks the tile, ring, winding and every edge position, rejects elevated or shifted footprints, and rejects an incompatible measured eave before drawing the materials. Wall collisions follow the fitted building height. The projecting entrance roof and piers add visual geometry.

For a source or packaged Metal build, run the following with an isolated game user-data directory/cache. The output directory must already exist. The test selects the visible menu button, loads the normal scene, enables the USDA observation for both states, waits for the canopy and building feeds, and checks the contact count. The registered camera uses the photographic UTC exposure; the ordinary Miata uses the normal afternoon preset.

```sh
godot --path game --windowed --resolution 1280x956 -- --bethel-facade-ab /absolute/output
godot --headless --path game --script res://checks/PhotoFacadeCheck.gd
python score.py /absolute/output --photo /absolute/path/to/davidson-original.jpg
```

The baseline is rebuilt from the same tile with the optional survey record withheld. The two structures share the lighting, terrain and vegetation. The surface error includes pixels obscured by mismatched simulated trees. The game package's credit files list each adaptation and its source. The photographed facade textures, masks and comparison material are licensed under CC BY-SA 3.0. The footprint is derived from OpenStreetMap under ODbL, the United States Geological Survey laser returns are public domain, and the terrain heights come from Mapzen Terrain Tiles. [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/) · [OpenStreetMap attribution](https://www.openstreetmap.org/copyright) · [Terrain source](https://registry.opendata.aws/terrain-tiles/)
