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

## Next

- A local CPU model for the gaps: a small language model (llama.cpp class, 1-3 B
  parameters, quantised) in a background thread writes facts with a low confidence
  ("a 1950s motel in Phoenix: style, colour, sign") into the same cache.
- Photographs as evidence: wall colours sampled from street-level and Commons photos of
  the building, and the AI detail engine conditioned on them.
- More recipes that read the existing detail facts: Mediterranean (tile roofs, arches),
  brick blocks with cornices, desert adobe, MiMo screens.
