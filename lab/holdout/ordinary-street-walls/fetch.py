"""Fetch only the six fixed, credited panoramas, or verify an existing local copy."""
from hashlib import sha256
import json
from pathlib import Path
import time
from urllib.request import Request, urlopen

folder = Path(__file__).resolve().parent
catalog = json.loads((folder / "frames.json").read_text())
for group in catalog.values():
    for frame in group:
        destination = folder / frame["file"]
        if destination.exists() and sha256(destination.read_bytes()).hexdigest() == frame["sha256"]:
            continue
        if frame["license"] != "CC BY-SA 4.0" or frame["size"] > 1_850_000:
            raise ValueError("Unsupported frame license or size")
        request = Request(frame["source"], headers={"User-Agent": "SundayDriver credited image replay"})
        with urlopen(request, timeout=16) as response:
            content = response.read(frame["size"] + 1)
        if len(content) != frame["size"] or sha256(content).hexdigest() != frame["sha256"]:
            raise ValueError(f"The provider changed {frame['id']}. Re-register before using it.")
        destination.write_bytes(content)
        time.sleep(1)
