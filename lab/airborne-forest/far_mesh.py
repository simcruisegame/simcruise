"""Replace each verified indexed branch quad with its two silhouette triangles in a far-only copy."""

from pathlib import Path
import json, struct
import numpy as np
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "data",
    type=Path,
    help="Directory containing forest-conifer-2.meshdata and crown-leaf-2.meshdata",
)
data = parser.parse_args().data
for name, records in [
    ("forest-conifer-2", {1: "fold", 2: "fan"}),
    ("crown-leaf-2", {1: "fan"}),
]:
    source = (data / (name + ".meshdata")).read_bytes()
    size = struct.unpack("<I", source[12:16])[0]
    model = json.loads(source[20 : 20 + size])
    payload = bytearray(source[28 + size :])
    for index, kind in records.items():
        prim = model["meshes"][0]["primitives"][index]
        acc = model["accessors"][prim["indices"]]
        view = model["bufferViews"][acc["bufferView"]]
        dt = {5123: "<u2", 5125: "<u4"}[acc["componentType"]]
        offset = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        ix = (
            np.frombuffer(payload, dtype=dt, count=acc["count"], offset=offset)
            .copy()
            .reshape(-1, 12)
        )
        pattern = (
            [0, 3, 1, 1, 3, 4, 1, 4, 2, 2, 4, 5]
            if kind == "fold"
            else [0, 2, 4, 2, 8, 4, 8, 6, 4, 6, 0, 4]
        )
        if not np.all(
            ix.astype("int64") - ix[:, 0, None].astype("int64") == np.array(pattern)
        ):
            raise ValueError("Unrecognized source patch layout")
        chosen = [0, 1, 8, 8, 1, 11] if kind == "fold" else [0, 1, 4, 0, 4, 7]
        reduced = np.ascontiguousarray(ix[:, chosen])
        blob = reduced.tobytes()
        payload[offset : offset + len(blob)] = blob
        acc["count"] = reduced.size
        acc["min"] = [int(reduced.min())]
        acc["max"] = [int(reduced.max())]
    j = json.dumps(model, separators=(",", ":")).encode()
    j += b" " * (-len(j) % 4)
    total = 12 + 8 + len(j) + 8 + len(payload)
    f = data / ("als-far-" + name + ".meshdata")
    f.write_bytes(
        struct.pack("<4sII", b"glTF", 2, total)
        + struct.pack("<I4s", len(j), b"JSON")
        + j
        + struct.pack("<I4s", len(payload), b"BIN\0")
        + payload
    )
