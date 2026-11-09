"""Publish a 30 m stand-form mask from two categorical agency products; never adds a tree."""
import argparse
import base64
import collections
import csv
import hashlib
import io
import json
import math
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from PIL import Image

CIRCUMFERENCE = 40075016.68557849
ZOOM = 14
LEGEND = "https://www.landfire.gov/sites/default/files/CSV/LF2025/LF2025_EVT.csv"
EVT = "https://lfps.usgs.gov/arcgis/rest/services/Landfire_LF2025/LF2025_EVT_{}/ImageServer/exportImage"
NLCD = "https://www.mrlc.gov/geoserver/mrlc_download/{}/wcs"


def fetch(url, cache, maximum=600000):
    key = hashlib.sha256(url.encode()).hexdigest()
    destination = cache / key
    if not destination.exists():
        with urlopen(Request(url, headers={"User-Agent": "SundayDriver-ForestFormLab/1.0"}), timeout=25) as response:
            data = response.read(maximum + 1)
        if not data or len(data) > maximum or data.startswith(b"<"):
            raise ValueError(f"Invalid or oversized response from {url.split('?')[0]}: {data[:100]!r}")
        destination.write_bytes(data)
    data = destination.read_bytes()
    return data, {"url": url, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


class Grid:
    def __init__(self, data):
        im = Image.open(io.BytesIO(data))
        if im.width > 512 or im.height > 512 or im.format != "TIFF":
            raise ValueError("Unexpected raster size or format")
        self.pixels = im.copy()
        tags = im.tag_v2
        keys = tags[34735]
        fields = {keys[i]: keys[i+3] for i in range(4, len(keys), 4)}
        if fields.get(3072) != 3857 or fields.get(1025) != 1:
            raise ValueError("Expected Web Mercator projected pixel areas")
        if 34264 in tags:
            m = tags[34264]
            if m[1] != 0 or m[4] != 0:
                raise ValueError("Unsupported raster rotation")
            self.west, self.north, self.dx, self.dy = m[3], m[7], m[0], -m[5]
        else:
            x, y, _, self.west, self.north, _ = tags[33922]
            self.dx, self.dy, _ = tags[33550]
            if x != 0 or y != 0:
                raise ValueError("Unsupported tiepoint")
        if min(self.dx, self.dy) <= 0:
            raise ValueError("Invalid pixel spacing")

    def value(self, x, y):
        i, j = math.floor((x-self.west)/self.dx), math.floor((self.north-y)/self.dy)
        if 0 <= i < self.pixels.width and 0 <= j < self.pixels.height:
            return int(self.pixels.getpixel((i, j)))
        return 0


def conifer_codes(text):
    rows = csv.DictReader(io.StringIO(text))
    # Exclude mixed-evergreen hardwood systems. Mixed *conifer* alone is unambiguous.
    return {int(row["VALUE"]): row["EVT_NAME"] for row in rows
            if row["EVT_LF"] == "Tree" and row["EVT_PHYS"] == "Conifer"
            and any(name in row["EVT_NAME"].lower() for name in ("spruce","lodgepole","hemlock","fir"))
            and all(word not in row["EVT_NAME"].lower() for word in ("mixed evergreen","treeline","dwarf","krummholz"))}


def author(tx, ty, cache, output):
    n = 2**ZOOM
    side = CIRCUMFERENCE/n
    west, north = (tx/n-.5)*CIRCUMFERENCE, (.5-ty/n)*CIRCUMFERENCE
    lat = math.degrees(math.atan(math.sinh(math.pi*(1-2*(ty+.5)/n))))
    lon = (tx+.5)/n*360-180
    if 24 <= lat <= 51 and -125 <= lon <= -66:
        region, cover = "CONUS", "NLCD_2021_Land_Cover_L48"
    elif 50.5 <= lat <= 72.5 and (lon <= -129.5 or lon >= 169):
        region, cover = "AK", "NLCD_2016_Land_Cover_AK"
    else:
        return None
    grid = max(12, round(side*math.cos(math.radians(lat))/30))
    step = side/grid
    # A native-cell halo allows equal neighborhood checks on both sides of tile seams.
    bbox = ",".join(f"{v:.6f}" for v in (west-step, north-side-step, west+side+step, north+step))
    size = grid+2
    evt_url = EVT.format(region)+"?"+urlencode(dict(bbox=bbox,bboxSR=3857,imageSR=3857,size=f"{size},{size}",pixelType="S16",format="tiff",interpolation="RSP_NearestNeighbor",renderingRule=json.dumps({"rasterFunction":"None"}),f="image"))
    cover_url = NLCD.format(cover)+"?"+urlencode(dict(service="WCS",version="1.0.0",request="GetCoverage",coverage="mrlc_download:"+cover,crs="EPSG:3857",response_crs="EPSG:3857",bbox=bbox,width=size,height=size,format="GeoTIFF",interpolation="nearest neighbor"))
    legend_data, legend_src = fetch(LEGEND,cache)
    codes = conifer_codes(legend_data.decode("utf-8-sig"))
    evt_data, evt_src = fetch(evt_url,cache)
    cover_data, nlcd_src = fetch(cover_url,cache)
    evt, nlcd = Grid(evt_data), Grid(cover_data)
    cross = {}
    pairs = collections.Counter()
    accepted = collections.Counter()
    for j in range(-1,grid+1):
        for i in range(-1,grid+1):
            x,y = west+(i+.5)*step, north-(j+.5)*step
            pair = evt.value(x,y),nlcd.value(x,y)
            cross[i,j] = pair
            if 0 <= i < grid and 0 <= j < grid: pairs[pair] += 1
    def agrees(i,j):
        cls,lc = cross[i,j]
        return cls in codes and lc == 42
    bits = bytearray((grid*grid+7)//8)
    for j in range(grid):
        for i in range(grid):
            if agrees(i,j) and sum(agrees(i+dx,j+dy) for dx,dy in ((1,0),(-1,0),(0,1),(0,-1))) >= 2:
                pos=j*grid+i
                bits[pos>>3] |= 1 << (pos&7)
                accepted[codes[cross[i,j][0]]] += 1
    payload={"version":1,"evtYear":2025,"nlcdYear":2016 if region=="AK" else 2021,"tile":[tx,ty],"grid":grid,"bits":base64.b64encode(bits).decode()}
    output.mkdir(parents=True,exist_ok=True)
    name=f"forest-form-{tx}-{ty}"
    if accepted:
        (output/(name+".json")).write_text(json.dumps(payload,separators=(",",":"))+"\n")
    else:
        (output/(name+".json")).unlink(missing_ok=True)
    report={"tile":[tx,ty],"zoom":ZOOM,"center":[lat,lon],"grid":grid,"accepted":accepted,"acceptedCells":sum(accepted.values()),"totalCells":grid*grid,"classPairs":{f"{a}/{b}":cnt for (a,b),cnt in sorted(pairs.items())},"sources":[legend_src,evt_src,nlcd_src]}
    (cache/(name+"-receipt.json")).write_text(json.dumps(report,indent=2)+"\n")
    return report


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lat",type=float);parser.add_argument("lon",type=float)
    parser.add_argument("--cache",required=True,type=Path);parser.add_argument("--output",required=True,type=Path)
    parser.add_argument("--north",action="store_true",help="also sample the adjacent tile to the north")
    args=parser.parse_args()
    args.cache.mkdir(parents=True,exist_ok=True)
    tx=int((args.lon+180)/360*(2**ZOOM))
    ty=int((1-math.asinh(math.tan(math.radians(args.lat)))/math.pi)*(2**(ZOOM-1)))
    for y in range(ty-int(args.north),ty+1):
        r=author(tx,y,args.cache,args.output)
        print(json.dumps({k:r[k] for k in ("tile","grid","accepted","acceptedCells","totalCells")} if r else {"status":"unsupported"}),flush=True)
