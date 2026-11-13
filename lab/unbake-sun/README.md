# Unbake the sun, and corridor relief

Prototypes for [docs/research/world-detail-methods.md](../../docs/research/world-detail-methods.md) (methods 1 and 2).

Needs Python 3 with NumPy, SciPy, Pillow, pyproj, laspy and lazrs. All inputs are US public domain: NOAA Miami-Dade 2021 lidar and USGS 3DEP lidar (Entwine point tiles on AWS), and the bundled USGS aerial photographs.

## Unbake the sun (Miami Beach start)

Run from a scratch folder that holds a copy of `data/tiles/aerial/a-*.jpg`:

    python fetch.py 18180 27907 10          # lidar of one z16 tile and a 60 m margin -> pts-18180-27907.npz
    python unbake.py 18180 27907            # fit the sun to the photograph's shadows (about 100 s), then lift them
    python unbake.py 18179 27907 170,44     # later tiles of the same photograph: reuse the fitted sun (about 3 s)
    python stitch.py                        # -> unbaked/a-*.jpg; copy to data/tiles/aerial/unbaked/

The committed `data/tiles/aerial/unbaked/` covers the 4 bundled aerial tiles (sun azimuth 170 degrees, elevation 44 degrees). `metrics.txt` has the shaded/lit luminance per tile before and after. The game uses these photographs by default; `-SimCruiseBakedAerial` uses the originals.

Limits: the photograph is not a true orthophoto, so tall towers lean away from their lidar footprint and a thin bright or dark rim can stay; the lift is limited to 4x and to the lit surface of the same class nearby, so the deepest tower shadows stay a little dark.

## Corridor relief (Tail of the Dragon)

    python relief.py USGS_LPC_TN_BlountCo_2015_LAS_2017 -83.9327265 35.4792763 200 12 dragon
    python relief_img.py dragon             # -> relief-dragon.png: 10 m terrain | lidar ground 0.5 m | lidar intensity

It prints the height difference between the lidar ground and a 10 m grid (as the game's terrain) and the share of ground steeper than 35 degrees.
