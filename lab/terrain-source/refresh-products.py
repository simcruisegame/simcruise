"""Refresh the small public USGS 3DEP 1/3 arc-second product index."""
from pathlib import Path
import json
import re
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

prefix = "StagedProducts/Elevation/13/TIFF/current/"
params = {"list-type": "2", "delimiter": "/", "prefix": prefix, "max-keys": "1000"}
names = set()
while True:
    url = "https://prd-tnm.s3.amazonaws.com/?" + urllib.parse.urlencode(params)
    request = urllib.request.Request(url, headers={"User-Agent": "SundayDriver-ElevationIndex/1.0"})
    with urllib.request.urlopen(request, timeout=15) as response:
        root = ET.fromstring(response.read(1_000_000))
    for entry in root.findall("{*}CommonPrefixes/{*}Prefix"):
        product = entry.text.removeprefix(prefix).strip("/")
        if re.fullmatch(r"[ns]\d{2}[ew]\d{3}", product):
            names.add(product)
    token = root.findtext("{*}NextContinuationToken")
    if not token:
        break
    params["continuation-token"] = token
if not {"n62w150", "n22w158", "n41w097"}.issubset(names) or len(names) < 1000:
    raise ValueError("USGS returned an incomplete product listing")
output = Path(__file__).resolve().parents[2] / "helper" / "usgs-elevation-products.json"
output.write_text(json.dumps(sorted(names), separators=(",", ":")) + "\n")
print(f"{len(names)} degree products")
