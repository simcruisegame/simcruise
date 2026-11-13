# The place knowledge graph

SimCruise must put the right small details on every building, anywhere: the eyebrow
ledges and neon of an Art Deco hotel in Miami Beach, the earth-tone stucco of Phoenix,
the brick and cornices of an old Chicago block. The place knowledge graph decides this
from facts. It runs on the CPU, in real time, with no network on the frame.

Code: `Source/SimCruise/{Public,Private}/Knowledge/`. Rules: `data/knowledge/place-graph.json`.
Bundled open data: `data/knowledge/places/<x>_<y>.json`.

## Facts

A fact is a token `kind:value` with a confidence from 0 to 1, and a reason.

| Kind | Example | From |
|---|---|---|
| `form`, `size` | `form:lowrise`, `size:small` | tile data: height, footprint |
| `poi` | `poi:hotel`, `poi:bar` | named places on the footprint (OSM/Overture) |
| `region`, `climate`, `district` | `climate:tropical`, `district:art_deco` | regions in the rule file; latitude bands elsewhere |
| `near` | `near:water` | water on the tile |
| `style`, `era` | `style:streamline_moderne`, `era:1930s`, `era:before_1945` | OSM `building:architecture`, `start_date`; Wikidata P149, P571 |
| `architect`, `heritage`, `material`, `roof`, `use` | `heritage:nrhp` | OSM tags; Wikidata P84, P1435, P186 |
| `live` | `live:night`, `live:rain`, `live:wind_strong`, `live:traffic_heavy`, `live:photos` | live feeds, every 5 s |

## Rules

Each rule joins facts to new facts with a weight:

```json
{ "if": ["family:deco", "poi:hotel"], "then": { "detail:neon_sign": 0.9 }, "why": "Deco hotels carry their names in neon" }
```

- The confidence of the `if` part is its weakest fact; `unless` facts reduce it.
- Several rules for one fact add as independent evidence (noisy OR: 1 - (1-a)(1-b)).
- A negative weight suppresses (strong wind takes the awnings in).
- The engine runs the rules until nothing changes (at most six passes).
- `live_rules` run at run time on the baked facts plus the live facts.

Outputs are facts too: `recipe:*` (which geometry recipe builds the building: `recipe:deco`,
`recipe:resort_tower`, `recipe:mediterranean`, `recipe:main_street`, `recipe:house`,
`recipe:downtown`, `recipe:industrial`),
`detail:*` (how likely each detail is), `palette:*` (wall and trim colours), and from the
live rules `light:*`, `street:*`, `surface:*`, `air:*`, `vegetation:*`.

## Spread

Most buildings have no data. A building with no style or era takes them from the dated
buildings within 140 m, at 0.7 times their share. One Wikidata hotel per block is enough
to make the block read as its real period.

## Bake

`SimCruisePlaceHub::BakeForBuild` runs on the tile build's worker thread: tile facts,
records matched to footprints (inside, or the nearest within 18 m), spread, rules, then
the colours. A seed per building keeps the result the same each time. The plan of each
tile stays in the hub for the console and other systems.

`SimCruiseTileDetail::BuildBuildings` reads the plan: the recipe (deco, resort tower or
generic), the chance of each detail (with the old fixed values as defaults), the mapped
or regional wall colour, and wear (age, salt air, soot, less restoration).

## Open data

`USimCruisePlaceGraphSubsystem` fetches the tiles within two of the car before the
streamer builds them, one request at a time:

- **OpenStreetMap** through the Overpass API (ODbL; "(c) OpenStreetMap contributors").
- **Wikidata** SPARQL `wikibase:around` (CC0).

Each answer is saved in `Saved/Knowledge/places/`. The bundled tiles have theirs in
`data/knowledge/places/` (`-run=SimCruisePlaceFacts` writes them). Network follows the
live world: off in unattended runs unless `-SimCruiseLive`; `-SimCruiseNoPlaceFetch`
turns it off.

## Console

- `SimCruise.Knowledge.Explain [n]`: the n buildings nearest the car, each fact, its
  confidence and why; the live view now; Wikimedia Commons photographs of the place.
- `SimCruise.Knowledge.Live`: the live facts.
- `SimCruise.Knowledge.Report`: tiles, buildings, matched records, bake time.

## Local AI for the gaps

`SimCruisePlaceAI` reads, it does not guess. For each area (a zoom-14 tile, about 2.2 km)
Wikipedia's geosearch lists the articles about places within 1.5 km of its centre (free
API; text CC BY-SA 4.0). Their lead extracts go to a small language model on the CPU
through Ollama on localhost (`num_gpu` 0 and 4 threads, so the 28 W GPU keeps rendering).
The model must answer in a JSON schema whose words are the graph's own tokens: the
styles with shares, the main decade, the palette and the roof form.

| Area | Model guessing from memory (4 B) | Model reading the extracts (3 B) |
|---|---|---|
| Ocean Drive, Miami Beach | Pueblo Revival, 1967 | Art Deco 0.75, MiMo 0.25, 1930s |
| Back Bay, Boston | | Victorian 0.75, Neoclassical 0.25, 1890s, red brick |
| Santa Fe Plaza | | Pueblo Revival 0.85, 1920s, adobe |
| Maryvale, Phoenix | | nothing (no extracts): an honest empty answer |

The answer becomes weak tile facts: style at 0.6 x its share, era, palette and roof at
0.4. They sit below a building's own open data (1.0) and the spread from dated neighbours
(0.7), and alone they cannot switch a recipe. 8-90 s per area, once, in the background;
answers are cached in `Saved/Knowledge/areas/`, and the start areas ship in
`data/knowledge/areas/` (`-run=SimCruisePlaceFacts -AI -NoOSM`). Without Ollama, or with
`-SimCruiseNoPlaceAI`, the graph works without it. The model is the smallest local chat
model of 1-8 GB (`simcruise.Knowledge.OllamaUrl` sets the server).

## Wall colours from each building's own photograph

`SimCruisePlacePhotos`: Wikidata names a photograph for many buildings (P18). Commons'
imageinfo gives its date, licence, author and categories. Archive pictures (before 1990)
and postcards are left out, because their colours are not today's paint (on the start
tiles 260 of 319 photographs are 1930s-1950s postcards). In a 480 px thumbnail, the facade
band (the middle 56 % of the height, 76 % of the width) drops sky, leaves, shadow and
glass; the largest colour cluster is the wall when it covers at least a fifth of what is
left. The colour goes on the building's record with its credit (file, year, author,
licence) and the plan uses it as mapped data, over any palette. 67 start-tile buildings
have a measured wall (`-run=SimCruisePlaceFacts -Photos -NoOSM`). The buildings thread
owns facade textures from photographs; this only measures the paint.

## Street furniture

`SimCruiseFurniture` (data) and `World/SimCruiseStreetFurniture` (geometry, Props layer):
what OpenStreetMap maps where it maps it (benches, litter bins, bicycle racks, USPS
collection boxes, pillar fire hydrants, bollards, bus stop flags and shelters, picnic
tables), at real size (a US dry-barrel hydrant about 0.75 m, a USPS box 1.24 m, a bench
1.8 m with the seat at 0.45 m). A point mapped in the carriageway or the gutter is moved
behind the kerb. Where the map has no hydrants, one stands every 300 ft (91 m) along curbed
streets, sides alternating, anchored to the world so the rhythm runs across tile seams
(US practice; NFPA 1 / IFC Appendix C spacing). Colours: an object's own OSM `colour`,
else the graph's `hydrant:*`, `bench:*`, `bin:*`, `bollard:*`, `street_metal:*` facts of
the region (South Florida hydrants chrome yellow), else defaults. The items are solid, like
the light poles. The furniture comes in the same Overpass answer as the building facts; the
start tiles ship theirs in `data/knowledge/furniture/` (`-run=SimCruisePlaceFacts -Furniture
-NoOSM`). `simcruise.World.Furniture` 0 / 1 / 2 (none / all / mapped only).

## Robustness

- Nothing of the graph runs on the game thread but a 5 s live-fact update and a 0.5 s card
  check on a shared plan (no copy). Overpass and Wikidata answers are parsed on a worker
  thread. The run's worst game-thread times are in the log at the end
  (`SimCruise.Knowledge.Report`): 0.4-1.2 ms in play tests.
- No network: every request fails cleanly, the local AI turns itself off, and the tiles
  bake from what is cached or bundled. A partial answer (Overpass busy) is kept for the run
  only and asked again next time. `-SimCruisePlaceOffline` sends every graph request to a
  closed port, to test this.

## Next

- Photographs as evidence: wall colours sampled from street-level and Commons photos of
  the building, and the AI detail engine conditioned on them.
- More recipes that read the existing detail facts: Mediterranean (tile roofs, arches),
  brick blocks with cornices, desert adobe, MiMo screens.
