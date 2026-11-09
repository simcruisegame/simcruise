"""Rebuild the offline comparisons from the licensed WebP files beside this script."""
import argparse
import base64
from html import escape
import json
from pathlib import Path

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("destination", type=Path)
args = parser.parse_args()
sample = json.loads((here / "sample-locations.json").read_text())
rural = json.loads((here / "rural-views.json").read_text())
records = [
    ("charleston", "Charleston, West Virginia", "Seeded city sample", "38.348449, −81.630658 · 136° southeast",
     "The road and the low horizon line up. The revised asphalt and blue sky are closer to the photograph, but the dome to the left and the long brick wall to the right are missing in this pass.",
     "On the sampled pavement patch the mean channel difference fell from 32.7 to 18.0 on a 0–255 scale. The sky patch fell from 14.3 to 7.3. These three nearby photographs also informed the color change.",
     "KartaView · wolfgang8741 · capture date unknown", "https://kartaview.org/details/3485953/2860"),
    ("lincoln", "Lincoln, Nebraska", "Seeded city sample", "40.808014, −96.675003 · 91° east",
     "The source shows low morning light ahead, mature trees close to the curb, mown verges and varied houses. Compare the height of the tree canopy over the road and the house fronts at right.",
     "The first street-photo catalog returned no matching sequence. A second catalog supplied this 74° horizontal field of view, a reported 5 m horizontal accuracy and an exposure timestamp. Its photo and camera metadata are separate from the game's 3D geometry.",
     "Panoramax · slinky309 · August 19, 2025, 13:41 UTC", "https://panoramax.xyz/#focus=pic&pic=8889f6fa-34ff-4959-adb9-7d162c46b68c"),
    ("key-largo", "Key Largo, Florida", "Seeded sample from five photographed sites", "25.087060, −80.449145 · 223° southwest",
     "Pale sunlit pavement is recognizable in the revision. This first color pass missed the lane arrow, edge stripe, wires and boats. The measured lane-paint comparison below shows the subsequent geometry pass.",
     "The street-color estimator rejected this photograph for glare. The revised game used the local aerial image. On the fixed foreground road patch the mean channel difference fell from 67.3 to 10.3. The capture predates the render by years.",
     "KartaView · cowdog · May 17, 2017", "https://kartaview.org/details/332471/21"),
    ("miami", "South Miami Avenue, Florida", "Development benchmark", "25.748401, −80.208896 · 66° northeast",
     "The camera looks along the double yellow line. The pale road improved, but foreground lawns, driveways and the tall continuous hedge on the right still give the real street a different shape.",
     "The pavement patch fell from 106.0 to 31.3. Local street photographs informed the correction. A separate cockpit comparison shows the later sidewalk and lidar vegetation pass.",
     "KartaView · mrmappenburger · September 2025", "https://kartaview.org/details/11591982/217"),
]
def q(value):
    return escape(str(value), quote=True)
def figure(file, label, alt):
    image = base64.b64encode(file.read_bytes()).decode("ascii")
    mime = {".webp": "image/webp", ".jpg": "image/jpeg"}[file.suffix.lower()]
    return f'<figure><figcaption>{q(label)}</figcaption><img loading="lazy" src="data:{mime};base64,{image}" alt="{q(alt)} — {q(label)}"></figure>'

def photo(site, version, label):
    return figure(here / "views" / f"{site}-{version}.webp", label, site)

def comparison_study(manifest):
    data = json.loads(manifest.read_text())
    images = "".join(figure(manifest.parent / i["file"], i["caption"], data["title"]) for i in data["images"])
    links = " · ".join(f'<a href="{q(url)}">{q(title)}</a>' for title, url in data["links"])
    return (f'<details class="study"><summary>{q(data["title"])}</summary><p>{q(data["summary"])}</p>'
            f'<p>{q(data["measurement"])}</p>{images}<small>{q(data["credit"])} {links}.</small></details>')

studies = {
    "charleston": ["charleston-clay-center/manifest.json"],
    "lincoln": ["lincoln-city-trees.json","lincoln-lidar/manifest.json","lincoln-canopy/manifest.json"],
    "key-largo": ["key-largo-road-paint/manifest.json"],
    "bethel": ["bethel-low-cover/manifest.json", "bethel-davidson-hall/manifest.json"],
}
for site, data in rural["sites"].items():
    records.append((site, *(data[key] for key in ("title", "method", "pose", "seen", "measurement", "author", "link"))))

for site, directory in (("cheyenne", "cheyenne-capitol"), ("bartlesville", "bartlesville-shops")):
    record = json.loads((here / "views" / directory / "record.json").read_text())
    records.append((site, *(record[key] for key in ("title", "method", "pose", "seen", "measurement", "author", "link"))))
    studies[site] = [directory + "/manifest.json"]

cards = []
for site, title, method, pose, seen, measurement, author, link in records:
    data = rural["sites"].get(site, {})
    frames = data.get("frames", (("photo", "Street photograph"), ("before", "Earlier game render"), ("after", "Revised game render")))
    group = [photo(site, version, label) for version, label in frames]
    license_url = data.get("license_url", "https://creativecommons.org/licenses/by-sa/4.0/")
    license_label = data.get("license", "CC BY-SA 4.0")
    owner = "© Grab and KartaView contributors. " if "KartaView" in author else ""
    deeper = '<p><a href="Sunday Driver - Miami street comparison.html">Inspect the sidewalk and vegetation from the cockpit</a>.</p>' if site == "miami" else ""
    extra = ""
    detail_file = here / "views" / f"{site}-notes.txt"
    if detail_file.is_file():
        extra = f'<p>{q(detail_file.read_text().strip())}</p>'
    if data.get("camera"):
        extra += f'<p>{q(data["camera"])}</p>'
    if weather := data.get("archive"):
        extra += (f'<details><summary>Inspect the cloud-cover control</summary><p>{q(weather["control_note"])} '
                  f'<a href="{q(weather["source_url"])}">Archived station observations</a>.</p>'
                  f'{photo(site, weather["solar_frame"], weather["solar_caption"])}</details>')
    if site == "lincoln":
        extra += comparison_study(here.parent / "sky-archive/manifest.json")
    for study in studies.get(site, []):
        extra += comparison_study(here / "views" / study)
    cards.append(f'<article id="{site}"><div class="heading"><h2>{q(title)}</h2><span>{q(method)}</span></div><p class="pose">{q(pose)}</p><div class="images">{"".join(group)}</div><p>{q(seen)}</p><p>{q(measurement)}</p>{extra}{deeper}<small>{owner}{q(author)}. Photograph resized and converted to WebP. <a href="{q(link)}">Original photograph</a> · <a href="{q(license_url)}">{q(license_label)}</a>.</small></article>')
statuses = {"catalog_unavailable":"Catalog temporarily unavailable", "provider_cooldown":"Skipped during provider cooldown","no_nearby_photos":"No nearby frames returned","three_compatible_poses":"Three compatible frames"}
def coverage_row(s):
    result = rural["commons_queries"].get(s["place"])
    commons = f'{result["returned"]} files returned, {result["selected"]} matched view' if result else "Not queried"
    if result and result["selected"] == 0:
        commons = f'{result["returned"]} files returned, no view selected'
    panora = s["panoramaxResultCount"]
    return (f'<tr><th scope="row">{q(s["place"])}, {q(s["state"])}</th><td>{q(statuses.get(s["firstStatus"],s["firstStatus"]))}</td>'
            f'<td>{q("Not queried" if panora is None else str(panora)+" returned")}</td><td>{q(commons)}</td>'
            f'<td>{q(s.get("roadCheck", {}).get("roads", "—"))}</td></tr>')
rows = "".join(coverage_row(s) for s in sample["sample"])
page = """<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Sunday Driver • Street comparisons</title><style>
*{box-sizing:border-box}body{margin:0 auto;max-width:1540px;background:#f5f3ed;color:#22302a;font:16px/1.5 'Avenir Next',Helvetica,Arial,sans-serif}main{padding:clamp(16px,3vw,48px)}h1{font-size:clamp(32px,4vw,60px);letter-spacing:-.04em;line-height:1.04;margin:0 0 18px}p{max-width:96ch;margin:9px 0}.heading{display:flex;align-items:baseline;justify-content:space-between;gap:14px;flex-wrap:wrap}h2{font-size:clamp(23px,2.1vw,34px);line-height:1.2;margin:0}.heading span,.pose,small{font-size:14px;color:#485b50}.pose{margin:4px 0 13px}article,section{margin-top:34px;border-top:1px solid #9ea99d;padding-top:18px}.images{display:grid;grid-auto-columns:1fr;grid-auto-flow:column;gap:8px;margin:10px 0 14px}figure{margin:0}figcaption{font-weight:620;font-size:14px;padding:0 0 4px}figure img{display:block;width:100%;height:auto;aspect-ratio:4/3;object-fit:contain;background:#d0d5ca}small{display:block;margin-top:8px}a{color:#165740;text-underline-offset:3px}details{margin:10px 0}summary{cursor:pointer;font-weight:620}details figure{max-width:650px}.study figure{max-width:1100px;margin:14px 0}.study img{aspect-ratio:auto}.table-scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;max-width:900px;margin-top:12px;font-size:14px;text-align:left}td,th{border-top:1px solid #bec5b9;padding:6px 8px 6px 0}nav{display:flex;flex-wrap:wrap;gap:8px 18px;margin:15px 0}section h2{margin:0 0 12px}@media(max-width:650px){.images{grid-auto-flow:row;grid-template-columns:1fr}.heading span{font-size:13px}figcaption{padding-top:3px}article{margin-top:25px}}</style></head><body><main><h1>The real street and the drive</h1><p>Compare street and roadside photographs with Godot frames rendered on the Mac. Each view lists its position and bearing. The roadside cameras report where the lens pointed, while some driving sequences supply only a direction of travel. Unknown camera height, pitch and lens distortion limit alignment.</p><nav aria-label="Comparisons">"""
page += "".join(f'<a href="#{site}">{q(title.split(",")[0])}</a>' for site,title,*_ in records)
landscapes = here.parent / "landscapes"
landscape_views = json.loads((landscapes / "views.json").read_text())
page += ''.join(f'<a href="#{q(row["place"])}">{q(row["title"])}</a>' for row in landscape_views)
page += '<a href="#medford">Medford</a><a href="#princeton">Princeton Road</a><a href="#coverage">All eight sampled places</a><a href="#miata">1999 Miata Sport</a></nav>'+''.join(cards)
for row in landscape_views:
    district = landscapes / row["place"]
    shots = "".join(figure(district / filename, caption, row["title"]) for filename, caption in (
        ("photograph.jpg", "Location photograph"), ("before.jpg", row.get("before_caption", "1.0.2 game render")), ("after.jpg", row.get("after_caption", "Regional terrain game render"))))
    drive = ""
    if (district / "driver.jpg").is_file():
        drive = ('<details><summary>Drive across the region boundary</summary>'
                 + figure(district / "driver.jpg", "Stock Miata cockpit after the handoff", row["title"])
                 + '<p>The game drove 640 m on US 163. All four tires stayed in contact for 2,824 physics ticks. The old terrain remained visible until the neighboring mesh arrived, 44 m after the boundary.</p></details>')
    for study in sorted(district.glob("*/manifest.json")):
        drive += comparison_study(study)
    page += (f'<article id="{q(row["place"])}"><div class="heading"><h2>{q(row["title"])}</h2><span>{q(row["method"])}</span></div>'
             f'<p class="pose">{q(row["pose"])}</p><div class="images">{shots}</div><p>{q(row["finding"])}</p>'
             f'{drive}<small>Photograph by {q(row["author"])}. Resized for display. <a href="{q(row["url"])}">Original and camera metadata</a> · '
             f'<a href="{q(row.get("license_url", "https://creativecommons.org/licenses/by-sa/4.0/"))}">{q(row.get("license", "CC BY-SA 4.0"))}</a>. '
             'Game frames use USGS aerial imagery, Mapzen elevation and OpenStreetMap.</small></article>')
page += ('<article id="needles-highway"><h2>Needles Highway, South Dakota</h2>'
         + comparison_study(landscapes / "south-dakota/needles-outcrop/manifest.json") + '</article>')
page += '<article id="anchorage-grade"><h2>Downtown Anchorage, Alaska</h2>' + comparison_study(here.parent / "terrain-source/manifest.json") + comparison_study(here / "views/anchorage-west-fifth/manifest.json") + '</article>'
page += '<article id="ordinary-walls"><h2>Iowa City and Minneapolis</h2>' + comparison_study(here / "views/ordinary-street-walls/manifest.json") + '</article>'
page += '<article id="seeded-signals"><h2>Carson City, Indianapolis and Trenton</h2>' + comparison_study(here / "views/seeded-signal-controls/manifest.json") + '</article>'
page += '<article id="indianapolis-frontage"><h2>Indianapolis, Indiana</h2>' + comparison_study(here.parent / "mapped-storefronts/manifest.json") + '</article>'
page += '<article id="one-lane-streets"><h2>Washington, DC and Portland, Oregon</h2>' + comparison_study(here.parent / "road-layout/manifest.json") + '</article>'
page += '<article id="street-vehicles"><h2>I Street, Anchorage, Alaska</h2>' + comparison_study(here.parent / "street-vehicles/manifest.json") + '</article>'
page += '<article id="cambridge-tree-wells"><h2>Columbia Street, Cambridge, Massachusetts</h2>' + comparison_study(here.parent / "city-crowns/manifest.json") + '</article>'
page += '<article id="medford"><h2>Mystic Valley Parkway, Massachusetts</h2>' + comparison_study(here.parent / "ma-road-section/manifest.json") + '</article>'
route_dir = here.parent / "sustained"
route = json.loads((route_dir / "sources.json").read_text())
usgs = q(route["imagery"]["usgs"])
county = q(route["county_independent_check"]["layer"])
sheet = figure(route_dir / "princeton-road.webp", "Paired cockpit renders at 100 m, 770 m and 1,350 m", "Princeton Road")
page += (f'<section id="princeton" class="study"><div class="heading"><h2>Princeton Road, Nebraska</h2><span>Gravel road · aerial reference</span></div>'
         f'<p class="pose">40.56724, −96.59090 · eastbound from South 96th Street</p>{sheet}'
         '<p>The county engineer identifies this section as gravel. The mapped unpaved surface now carries the broad color from the USGS aerial image into the driving view. '
         'At 770 m and 1,350 m, mean absolute channel difference against the registered aerial fell from 86.4 to 5.8 and from 65.6 to 14.6 over 152 and 147 road pixels, respectively.</p>'
         '<p>The first 100 m patch informed the brightness check. KartaView and Panoramax returned no local street-level image, so these scores measure agreement with the overhead source.</p>'
         f'<small>© OpenStreetMap contributors via OpenFreeMap. <a href="{usgs}">USGS aerial imagery</a> · <a href="{county}">Lancaster County road surface</a> · '
         f'<a href="{q(route["vector"]["license"])}">OSM attribution</a>.</small></section>')
vehicle = here.parent / "miata" / "appearance"
car_receipt = json.loads((vehicle / "receipt.json").read_text())
car_sources = car_receipt["credits"]
page += ('<section id="miata" class="study"><div class="heading"><h2>1999 Mazda Miata Sport</h2><span>Player car · model-year comparison</span></div>'
         '<p>The U.S. 1999 Sports Package has flush headlamps, a body-color bumper and a recessed intake. The candidate’s rendered opening spans about 62% of the bumper width, compared with 53% in 1.0.3 and roughly 60% in a higher-angle photograph of an original car. Camera perspective and hand-marked outlines limit the precision.</p>'
         + figure(vehicle / "front-pair.webp", "1.0.3 exterior and updated model in the same Metal view", "Miata nose and lights")
         + '<details><summary>Compare the original 15-inch Sport wheels</summary>'
         + figure(vehicle / "wheel-pair.webp", "Game wheel before and after measured five-spoke casting", "1999 Sport wheel geometry")
         + '<p>Five openings were outlined in a photograph of one rear wheel and checked against the opposite side of the same original car. Across five normalized radii, mean absolute discrepancy in the fraction occupied by metal fell from 23.1 to 3.4 percentage points. The photographs are oblique and the outer flange was fitted by hand.</p></details>'
         + '<details><summary>Open the instrument comparison</summary>'
         + figure(vehicle / "instrument-bench-pair.webp", "The steering wheel was hidden for both instrument close-ups", "Miata instrument cluster")
         + '<p>The center trip display records distance driven in miles. The speedometer carries inner km/h numbers and the ivory needles follow actual vehicle speed and engine RPM. In both test drives, the three brake lamps illuminated, released and stayed fixed to the chassis through 4.5 wheel revolutions.</p></details>'
         + '<details><summary>See what happens when the front wheels lock at 50 mph</summary>'
         + figure(here.parent / 'vehicle/locked-braking/brake-lock.webp', 'Simulated heading after a steering input under full braking', 'Stock Miata brake test')
         + '<p>In the flat-pad test both front tires locked. Steering then rotated the earlier model 71.74° and the corrected model 0.33°. Straight-line braking distance was 27.67 m in both simulations. The test verifies the behavior of a sliding tire. It does not measure stopping distance on a real road.</p></details>'
         + f'<small>Front lens adapted from a photograph by Elise240SX. Existing rear lens adapted from a photograph by Kevauto. Model and render comparisons: CC BY-SA 4.0. <a href="{q(car_sources["nose_lens"])}">Front lens source</a> · <a href="{q(car_sources["rear_lens"])}">Rear source</a> · <a href="{q(car_sources["adapted_lenses_license"])}">License</a> · <a href="{q(car_sources["sport_reference"])}">Original U.S. Sport</a> · <a href="{q(car_sources["brochure"])}">1999 Mazda brochure</a>.</small></section>')
page += '<section id="coverage"><h2>Where we looked</h2><p>We chose four city samples and four small-town or rural samples with seed 19991107 before looking at the pictures. The game had road and elevation data at the sampled districts. Street-camera coverage was much thinner. The table retains the original street-catalog results. The later Wikimedia Commons search covered a 1.2 km radius at Lewisburg, Bethel and Choteau, with a limit of 24 files per town. Many files depict individual buildings, and only two photographs supplied usable camera poses for this comparison.</p><div class="table-scroll"><table><thead><tr><th>Place</th><th>KartaView</th><th>Panoramax</th><th>Wikimedia Commons</th><th>Road segments</th></tr></thead><tbody>'+rows+'</tbody></table></div><p>Key Largo was chosen separately from five sites with existing photography. Miami was already a development benchmark. The patch scores measure displayed color at particular spots. They do not measure the accuracy of an entire street or a particular house.</p><p><a href="Sunday Driver - world and data.html">Explore the data sources</a>.</p></section></main></body></html>'
args.destination.parent.mkdir(parents=True,exist_ok=True)
args.destination.write_text(page)
