"""Size-limited, locally cached reads from the public survey layers."""

import hashlib
import json
import os
import urllib.parse
import urllib.request
from pathlib import Path

CACHE = Path(os.environ.get("SIMCRUISE_ROAD_CACHE", Path.home() / ".cache/simcruise/road-survey"))
UT = "https://services.arcgis.com/pA2nEVnB6tquxgOW/arcgis/rest/services/Pavement_Striping/FeatureServer"


def url_read(url, max_bytes):
    digest = hashlib.sha256(url.encode()).hexdigest()
    CACHE.mkdir(parents=True, exist_ok=True)
    path = CACHE / digest
    if path.is_file() and path.stat().st_size <= max_bytes:
        return path.read_bytes()
    request = urllib.request.Request(url, headers={"User-Agent": "SundayDriver/road-survey-lab"})
    with urllib.request.urlopen(request, timeout=20) as response:
        data = response.read(max_bytes + 1)
    if len(data) > max_bytes:
        raise ValueError("Survey source exceeded its response budget")
    temp = CACHE / (digest + "." + str(os.getpid()) + ".tmp")
    temp.write_bytes(data)
    temp.replace(path)
    return data


def fetch(url, params):
    full_url = url + "?" + urllib.parse.urlencode({"f": "json", **params})
    return json.loads(url_read(full_url, 650000))
