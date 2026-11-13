"""Licensed Wikimedia Commons photographs near a point, with what a realism pose needs.

usage: python lab/realism/commons-near.py LAT LON [RADIUS_M]
Prints: title | lat lon | heading (if tagged) | date taken | licence | author | page.
Photographs are never committed: poses.json keeps only the link (lab/realism/README.md).
"""
import json
import sys
import urllib.parse
import urllib.request

API = "https://commons.wikimedia.org/w/api.php"
HEADERS = {"User-Agent": "SimCruise-realism/1.0 (game development; realism score)"}


def get(params):
    url = API + "?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=30) as r:
        return json.load(r)


def main():
    lat, lon = float(sys.argv[1]), float(sys.argv[2])
    radius = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
    found = get({"action": "query", "list": "geosearch", "gscoord": f"{lat}|{lon}", "gsradius": min(radius, 10000),
                 "gslimit": 50, "gsnamespace": 6, "format": "json"})["query"]["geosearch"]
    titles = [p["title"] for p in found if p["title"].lower().endswith((".jpg", ".jpeg"))]
    for i in range(0, len(titles), 20):
        chunk = titles[i:i + 20]
        info = get({"action": "query", "titles": "|".join(chunk), "prop": "imageinfo|coordinates", "iiprop": "extmetadata|size|url",
                    "coprop": "type|name|dim|country|region|globe", "format": "json"})
        for page in info["query"]["pages"].values():
            ii = (page.get("imageinfo") or [{}])[0]
            md = ii.get("extmetadata", {})
            c = (page.get("coordinates") or [{}])[0]
            def v(k):
                return md.get(k, {}).get("value", "")
            print(" | ".join([page["title"], f"{c.get('lat', '')} {c.get('lon', '')}", v("GPSMapDatum") and "" or "",
                              v("DateTimeOriginal")[:19], v("LicenseShortName"), v("Artist")[:40].replace("\n", " "),
                              f"{ii.get('width')}x{ii.get('height')}", ii.get("descriptionurl", "")]))


if __name__ == "__main__":
    main()
