# Two measured Anchorage office roofs

Search **601 West 5th Avenue, Anchorage, Alaska** and drive west from the corner of F Street. The nearest tower occupies parcel 00210574000. The neighboring white office slab on the north side of Fifth overlaps the municipal parcel recorded at 620 West 4th Avenue. A separate current KartaView camera at B Street is 264 m east of the first parcel.

The game’s existing `LidarRoof.gd` matcher checks each complete footprint against the packaged roof file. A reloaded or enriched tile uses the same matcher. A changed footprint falls back to ordinary building geometry. The two heights and the upper mechanical areas are independent of the historical camera. No runtime provider requests or new gameplay code are needed.

`author.py` reconstructs the two records from the public survey subset in `returns.npz` and the exact OpenStreetMap footprints in `footprints.json`. The source collection has ground class 2 and unclassified class 1. Only first returns inside the building interiors were fitted. Flight line 110 supplies training and ground elevation; flight 111 supplies separate ground elevation and withheld first returns. The authoring records a median representative of each corroborated roof level, clips 1.8 m cells to the full mapped polygon and emits internal vertical risers. The geometry captures broad upper mechanical enclosures. Smaller roof equipment is unresolved.

The two native 960 × 720 renders used ground tile n62w150, product cc742f079a06785f08fd34a94c66de69-86, with 4,225 measured elevation vertices per visible tile. The vehicle had four contacts. The repeated control was pixel-identical. Approximate camera registration and generic wall materials remain visible. Native images and source credits are in `lab/holdout/views/anchorage-west-fifth`.

```sh
python lab/lidar/anchorage/author.py /tmp/anchorage-roofs.json
cmp /tmp/anchorage-roofs.json game/data/lidar-roofs-5480-18579.json
```

The two-source audit is in `validation.json`. The 2015 project’s point data use local roof-ground differences, avoiding an absolute vertical datum assumption. The nominal published project window extended from May 2015 to September 2016; an exact capture date for these two flight strips was not recovered. `lidar-sources.json` records each downloaded EPT node and its SHA-256 digest. No demographic, financial or owner fields were requested from the municipality. `capture_pair.gd` records the frozen native layer comparison from the QA ReferencePose after the host tile reports building status.

The [project builder](https://www.neeserinc.com/601-west-5th-avenue/) documents a subsequent southeast addition and a new glass exterior at 601 West Fifth. These survey roof levels and the street photograph are historical. The footprint guard cannot verify a building’s current cladding or recover that addition.
