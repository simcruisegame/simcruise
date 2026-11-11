<!-- Road standards engine: design for SimCruise. Written 2026-11-10 by the road-design lead from the knowledge base road-engineering.md, five research sweeps and a code audit of main 6cadf05 and roads/edge-authority faaf27d. Timings and costs marked "estimate" are not yet measured on the target laptop. -->

# Road standards engine: design

## 0. Verdict

The generator should stop *drawing roads* and start *building roads to standard*. The engine has three parts:

1. **One versioned, sourced data table** (`helper/road-standards.json`). It holds every design number the generator uses, each with its source tag and basis. Helper and game read the same table.
2. **A deterministic section selector.** From a way's OSM tags and its context it picks a cross-section template: an ordered list of strips (lane, edge line, shoulder, gutter, curb, planting strip, sidewalk, foreslope, ditch, backslope), each with a width, slope and material. Explicit tags override single strips, and every strip records where its value came from.
3. **An alignment fitter.** It turns the noisy OSM polyline into tangents, arcs and (above 45 mph) clothoids, within design-speed limits. Every edge, line, curb, rail and ditch is then an exact offset of that one curve.

Everything visible derives from that one curve and that one template: asphalt extent, paint, curbs, ground grading, grip, traffic lanes and roadside object exclusion. Imagery only tints. Measured data (lidar crossfall, surveyed banks, mapped tags) beats inference, and inference beats nothing.

This fixes the four reported defects at their cause:

| Defect | Cause | Fix |
|---|---|---|
| Zig-zag, lobed edges | Edges are offsets of raw chords with a separately smoothed normal | Edges are offsets of a G1 or G2 analytic centreline (sections 7, 8) |
| Aerial dirt on asphalt | A 1.9 m/px classifier sets the asphalt extent | Already fixed by faaf27d's ribbon authority. The engine finishes it: the template, not the photo, owns everything from the centreline to the catch point. |
| No curbs or sidewalks in towns | Drawn only on bundled tiles, or only where a sidewalk is mapped | Closed-section templates inferred from context (section 6) |
| Road the same colour as the desert | Asphalt tone lifted from the photo toward its surroundings | Albedo comes from the material and its age, with the photo as ±0.03 modulation (section 10) |

The plan has eight stages, each gated by headless checks (section 13). The estimate is about 30 developer-days in total; Stages 0-4 (about 15 days) deliver most of the visible change.

---

## 1. Goals, non-goals, acceptance

**Goals.** A road in SimCruise should satisfy the eleven-point checklist in road-engineering.md section 10. In short:
- smooth alignments within design-speed limits;
- edges parallel to the centreline;
- a crowned section with correct banking;
- the right edge treatment for the context: curb and sidewalk in town, shoulder and ditch in the country;
- junction corners rounded to a radius;
- pavement tone set by material;
- roadside objects kept out of the clear zone.

The car must still drive like the real car (owner memory, "driving realism first"). The ribbon is the collider, so every geometric change is also a physics change and needs the ride and handling gates.

**Non-goals for this plan:**
- Lane-level traffic rules beyond what the existing traffic system reads.
- Signal timing.
- Photo-derived road widths. Plan 06's skeptic rejected these, and this design agrees: geometry decides.
- Reproducing a specific road's as-built plans. We infer a standard-conforming *typical* road. Real surveyed data, where we have it, overrides.

**Acceptance.** Every stage's checks pass on a fixed, cached fixture set plus 10 random US places, the dragon and the capitol shot. Visual captures use Ultra settings (Forward+ where evaluated), per the owner's testing rule. README, the tech overview, the roadmap, CHANGELOG and the screenshots are updated with each stage that changes what the player sees (owner's docs rule).

---

## 2. Starting point (do not redo)

**roads/edge-authority, faaf27d (not merged)** already does Phase A of plan 06 and parts of B and C:
- The ribbon's UV2 = (lateral, half) owns the carriageway in terrain.gdshader.
- `RoadSurface.lateral()` drives `wheel_grip`.
- Edge lines stay on the asphalt.
- Every edge is classified per station as `EDGE_RURAL`, `EDGE_TOWN` or `EDGE_KERB` (faaf27d RoadSurface.gd:52-54, `classify_edges` at :275).
- Kerbs.gd builds a 6 in curb plus an 18 in gutter where a sidewalk is mapped.
- The helper fillets every mapped vertex: `chainVertices` / `chainPoint` / `arcPoint`, `FILLET_MAX_M = 1`, `TANGENT_SIGMA = 4`, road_grade summary v3.
- `lower_grid` is fixed.
- New check: checks/RoadEdgeCheck.gd, headless plus `--capture`.

**Stage 0 of this plan is to merge faaf27d.** The engine then builds on it:

| faaf27d piece | What the engine does with it |
|---|---|
| `chainPoint(V, s)` | Keeps its interface (`[x, y, tx, ty]`, plus kappa) but gets a better fitter behind it |
| `EDGE_*` | Generalised into section kinds |
| Kerbs.gd | Grows into the closed-section builder |
| RoadEdgeCheck | Gains the straightness, cross-section and appearance metrics |

**Not on faaf27d** (code audit, 2026-11-10):
1. The fitted centreline is not exported to `info.roads`. Traffic, `road_grid`, `street_image` and the geometric prior still read raw polylines (plan 06 Phase B).
2. Fillets have no spirals, so curvature jumps.
3. z14 quantisation jitter of about 0.49 m is not filtered.
4. Seam joints more than 5 cm apart stay hard corners.
5. Tight inner edges fold over (swallowtails).
6. There is no normal crown. Superelevation is a Gaussian on a capped heuristic, and is applied to secondary roads and above only.
7. There is no ditch or foreslope template; the earthwork slopes are 1.2:1 cut and 0.7:1 fill.
8. Widths depend on class only.
9. There is no junction model.
10. Curbs exist only where a sidewalk is mapped.
11. Pavement tone is pulled toward the soil.

**Ignore `scripts/simcruise/`.** It is an untracked older copy of the repo, not work in progress.

---

## 3. Architecture

```
                         helper/road-standards.json  (versioned data, every value sourced)
                                    |
             +----------------------+---------------------+
             v                                            v (hash-checked copy)
 helper/road-standards.mjs  (pure functions)        game/RoadStandards.json + RoadStandards.gd
   designSpeed, context, terrainClass,                (materials, curb/gutter/walk dims,
   sectionFor, limits, superelevation, K, gmax         marking table, clear zone, grip)
             |
 helper/road-tags.mjs (new): per-way tags from Esri (+ Overpass), matched like lane-counts.mjs
             |
 helper/road-align.mjs (new): fitAlignment(chain, limits) -> elements + 1 m stations (x,y,heading,kappa)
             |
 helper/road-grade.mjs: chains -> alignment -> profile (K-bounded) -> crown/e(s) -> template earthwork
             |                                                        -> h grid (coarse ground)
             v
 tile JSON:  roads (FITTED polylines, Phase B)   road_surface v4 rows (stations + section profiles)
             |                                       |
 game:  WorldTile.street_image / road_grid / traffic |  RoadSurface.gd   carriageway ribbon + collider
        (all read the fitted lines)                  |  RoadsideMesh.gd  (new) shoulder->catch point strip
                                                     |  Kerbs.gd         curb, gutter, strip, sidewalk, cuts
                                                     |  LaneMarkings.gd  lines from the same profile
                                                     |  Junction.gd      pads, curb returns, bars, crosswalks
                                                     |  GuardRails.gd    warrant from clear zone + slope, LON
                                                     |  terrain.gdshader material ids -> albedo table
                                                     |  Roadside/Grove/Rangeland: clear-zone exclusion
```

**Design rules:**
- **The helper decides, the game builds.** Context, section choice, alignment, crown and earthwork are computed once in the helper (Bun), where they are testable without Godot. They are serialised into the tile, so both tiles of a seam receive the same numbers.
- **Every decision is a pure function** of OSM data, a world-fixed position and the standards version. Variety comes from a hash of the way id (FNV-1a of `way:<id>:<key>`), never from tile-local randomness.
- **Precedence:**
  1. measured survey (lidar crown, surveyed banks, footprint surveys);
  2. explicit OSM tag;
  3. inferred template;
  4. class fallback.

  Provenance is kept per element (`t` tagged, `i` inferred, `d` default, `m` measured).
- **Versioning:** `road-standards.json` has a `version`. road_grade's summary version goes to 4, and the tile cache keys include the standards version. Signatures keyed on class reference widths (SurveyOutcropGround `road_signature`, road-layout.mjs `laneRoadSignature`) keep using `referenceWidths`, so surveyed data stays valid (helper/road-widths.mjs:19-25).

---

## 4. The data table: `helper/road-standards.json`

### 4.1 Rules

- **One file, JSON, UTF-8, SI units.** Lengths in metres, slopes as fractions (0.02 = 2%). Speeds in mph wherever the source tables are in mph, with an `_mph` suffix.
- **Every group carries provenance:**
  - `src`: a list of source tags resolving to `sources` in the same file (the same tags as road-engineering.md section 11);
  - `basis`: `standard`, `derived`, `practice` or `heuristic`.
  A value with no `src` fails the check.
- **Tables are keyed by their natural index**, either a speed or a class. Interpolation rules are stated in the group: `interp: "linear"`, `"linear-in-1/R"`, `"step"` or `"none"`.
- **Ranges are `[min, default, max]`.** The selector takes the default and lets a deterministic per-way hash move within the range only where `vary: true`.
- **No copyrighted text.** Values only, with source tags. AASHTO-derived numbers name the public secondary source in `src`; the secondary's licence class goes in `sources[tag].licence`.
- **`version`** is `YYYY.MM.N`. Any change to a value bumps it, which invalidates the tile cache through the road_grade cache key.

### 4.2 Shape (excerpt; the full file is written in Stage 0)

```json
{
  "schema": 1,
  "version": "2026.11.1",
  "units": {"length": "m", "slope": "fraction", "speed": "mph unless *_kmh"},
  "sources": {
    "TX-RDM":   {"title": "TxDOT Roadway Design Manual", "url": "https://www.txdot.gov/manuals/des/rdw/", "licence": "PR", "accessed": "2026-11-10"},
    "IA-IM3210":{"title": "Iowa DOT I.M. 3.210 (reproduces AASHTO GB 2018 T5-5, T6-5)", "url": "https://www.iowadot.gov/local_systems/publications/im/3210.pdf", "licence": "PR+C2"},
    "MUTCD11":  {"title": "MUTCD 11th ed. (2023)", "url": "https://mutcd.fhwa.dot.gov/pdfs/11th_Edition/part3.pdf", "licence": "PD"}
  },
  "design_speed": {
    "src": ["IA-DM","TX-RDM"], "basis": "heuristic",
    "untagged_mph": {"motorway": {"rural": 70, "urban": 55}, "trunk": {"rural": 60, "urban": 45},
                     "primary": {"rural": 55, "urban": 40}, "secondary": {"rural": 55, "urban": 35},
                     "tertiary": {"rural": 45, "urban": 30}, "unclassified": {"rural": 45, "urban": 25},
                     "residential": 25, "living_street": 15, "service": 15, "link": 40, "track": 20},
    "rural_design_plus_mph": 5, "low_speed_max_mph": 45
  },
  "context": {
    "src": ["CENSUS-UA","NLCD","TX-RDM"], "basis": "derived",
    "cell_m": 250, "urban_core": {"impervious_min": 0.80}, "urban": {"hu_per_sqmi_min": 1275, "maxspeed_max": 40},
    "suburban": {"hu_per_sqmi_min": 200}, "rural_town": {"place": ["village","town"], "maxspeed_max": 40},
    "buildings_per_cell_town": 15
  },
  "lane_width": {
    "src": ["FHWA-FC","TX-RDM","FDOT-FDM"], "basis": "derived",
    "rural": {"motorway": 3.66, "trunk": 3.66, "primary": 3.6, "secondary": 3.5, "tertiary": 3.35, "unclassified": 3.0, "residential": 3.0, "service": 2.75},
    "urban": {"motorway": 3.66, "trunk": 3.5,  "primary": 3.35, "secondary": 3.3, "tertiary": 3.2,  "unclassified": 3.0, "residential": 3.05, "service": 2.75},
    "min": 2.75, "max": 4.2, "change_taper_min": 50
  },
  "alignment": {
    "src": ["IA-DM","TX-RDM","WS-DM"], "basis": "standard", "interp": "linear",
    "speeds_mph": [15,20,25,30,35,40,45,50,55,60,65,70,75,80],
    "f_max":      [.32,.27,.23,.20,.18,.16,.15,.14,.13,.12,.11,.10,.09,.08],
    "rel_gradient": [.0089,.0080,.0073,.0067,.0062,.0057,.0053,.0050,.0050,.0050,.0050,.0050,.0050,.0050],
    "k_crest_m": [0.9,2.1,3.7,5.8,8.8,13.4,18.6,25.6,34.7,46.0,58.8,75.3,95.1,117.0],
    "k_sag_m":   [3.0,5.2,7.9,11.3,14.9,19.5,24.1,29.3,35.1,41.5,47.9,55.2,62.8,70.4],
    "no_curve_deflection_deg": {"high": 0.5, "low": 1.0},
    "compound_ratio": 1.5, "broken_back_tangent_m_per_mph": 4.6,
    "spiral": {"min_mph": 45, "steer_time_s": 2.0, "comfort_C": 1.2, "offset_p_m": [0.2, 1.0], "basis": "heuristic-until-verified"}
  },
  "superelevation": {
    "src": ["TX-RDM","IA-DM"], "basis": "standard", "interp": "linear-in-1/R", "round_up": 0.002,
    "e_max": {"rural": 0.08, "snow": 0.06, "urban_high_speed": 0.04, "urban_low_speed": 0.04},
    "rows_e6_ft": {"nc": [868,1580,2290,3130,4100,5230,6480,7870,9410,11100,12600,14100,15700,17400],
                   "e4": [151,309,511,766,1070,1440,1840,2300,2810,3390,3950,4550,5220,5950]},
    "rc_over_nc": {"value": 0.69, "basis": "derived", "note": "from the e_max 4% rows; replace with TxDOT T4-6/4-7 in full"},
    "multilane_bw": {"1": 1.0, "1.5": 0.83, "2": 0.75, "2.5": 0.70, "3": 0.67, "3.5": 0.64, "4": 0.63},
    "on_tangent": {"low": [0.80,0.85,0.90], "high": [0.70,0.75,0.80,0.85]}
  },
  "materials": {
    "src": ["EPA-CP","LBNL-49283"], "basis": "standard",
    "asphalt_fresh": {"albedo": 0.05}, "asphalt_aged": {"albedo": [0.10,0.12,0.14]},
    "concrete_new": {"albedo": 0.37}, "concrete_aged": {"albedo": [0.22,0.25,0.32]},
    "chipseal": {"albedo": [0.12,0.16,0.20]}, "curb_gutter_walk": {"albedo": [0.35,0.40,0.45]},
    "photo_modulation_max": 0.03, "grip": {"asphalt": 1.0, "concrete": 1.0, "gravel": 0.76, "turf": 0.65}
  },
  "elements": {
    "curb_vertical": {"src": ["CT-HDM","TX-RDM","SDDOT-650"], "reveal": 0.15, "top": 0.15, "batter": 0.25, "fillet": 0.02},
    "curb_mountable": {"src": ["CT-HDM"], "reveal": 0.10, "face_slope": 0.667},
    "gutter": {"src": ["CT-HDM","SDDOT-650","MnDOT-B618"], "width": [0.45,0.61,0.61], "slope": [0.05,0.06,0.0833]},
    "sidewalk": {"src": ["PROWAG","FHWA-PEDSAFE","TX-RDM"], "clear_min": 1.22, "cross_slope": [0.010,0.015,0.021], "joint_m": 1.5},
    "safety_edge": {"src": ["TX-RDM","FHWA-SAFEEDGE"], "angle_deg": 30, "height": 0.05},
    "hinge_round": {"src": ["CT-HDM"], "length": [1.2,1.5,3.0]}
  },
  "templates": {
    "T2_rural_2lane_paved_shoulder": {
      "src": ["CT-HDM","FHWA-RUMBLE","IA-DM"], "basis": "standard", "section": "open",
      "half": [
        {"kind": "lane", "w": 3.66, "slope": -0.02, "mat": "asphalt_aged"},
        {"kind": "edge_line", "w": 0.15, "paint": "white"},
        {"kind": "paved_shoulder", "w": [1.2,2.4,3.05], "slope": -0.04, "mat": "asphalt_aged", "rumble": "srs"},
        {"kind": "safety_edge"},
        {"kind": "gravel_shoulder", "w": [0.6,0.6,1.2], "slope": -0.05, "mat": "gravel"},
        {"kind": "hinge"},
        {"kind": "foreslope", "ratio": [4,4,6], "mat": "turf_mown"},
        {"kind": "ditch", "bottom": 1.2, "depth": [0.9,0.9,1.5], "mat": "turf"},
        {"kind": "backslope", "ratio": [3,3,4], "mat": "turf_wild"}
      ]
    }
  }
}
```

The full file carries all nine templates from road-engineering.md section 4 (T1-T9) plus the urban-core and suburban-mountable variants. It also carries:
- lane and shoulder defaults;
- clear-zone and runout tables;
- marking geometry;
- corner radii;
- curb-ramp and driveway geometry;
- furniture spacing;
- the maximum-grade tables.

### 4.3 Consumers

**Helper.** `helper/road-standards.mjs` loads the JSON once, freezes it, and exports pure functions:

| Function | Purpose |
|---|---|
| `designSpeed(way, ctx)` | Design speed for a way in its context |
| `context(pos, data)` | Context at a world position |
| `terrainClass(dem, pos)` | Level, rolling or mountainous |
| `sectionFor(way, ctx)` | Template choice (section 6) |
| `limits(V, ctx)` | The alignment limits in 7.2 |
| `superelevation(R, V, emax)` | The bank rate for a curve |
| `runoff(...)` | Runoff and runout lengths |
| `kCrest(V)`, `kSag(V)` | Vertical-curve K values |
| `gMax(ctx, cls, terrain, V)` | Maximum grade |
| `clearZone(V, adt, slope, R)` | Clear-zone width |

**Game.** Godot cannot import from `helper/`, so the game reads a copy at `game/RoadStandards.json`. `scripts/sync-standards` writes the copy, and `lab/check-road-standards.mjs` fails when the two hashes differ. `game/RoadStandards.gd` loads it and is read by:
- the material, albedo and grip tables;
- curb, gutter and sidewalk dimensions;
- the marking table;
- the clear zone, for vegetation;
- the GuardRails constants.

Geometry already decided in the helper is **not** recomputed in the game.

---

## 5. Context, design speed, terrain

All three are pure functions of world position plus OSM data.

**Context.** `context(pos)` is evaluated on the world-fixed 250 m grid that LaneMarkings already uses (game/LaneMarkings.gd:90-100), then smoothed per street the same way (`TOWN_SHARE` 0.65, `TOWN_RUN_M` 150). It moves to the helper so the section selector can use it. Inputs:
- building count per cell (NSI and Overture, already loaded for the tile);
- `landuse` polygons and `place` nodes from the vector tile;
- maxspeed from Esri;
- optionally NLCD impervious share, which is a new public raster: Stage 2b, optional.

Output: `rural | rural_town | suburban | urban | urban_core`, using the thresholds in road-engineering.md 2.2. The game's own `dense()` and `town_share()` (game/LaneMarkings.gd:339-373) stay as the fallback for tiles built before v4.

**Design speed.**
- Mapped `maxspeed`, including the mph suffix and km/h conversion. `maxspeed:advisory` lowers the local value on curves.
- Otherwise the untagged default for class and context (road-engineering.md 5.1).
- Rural open sections add 5 mph.
- `lowSpeed = V <= 45`.

Today's `designSpeed(cls)` returns 90 / 60 / 0 km/h by class only (helper/road-grade.mjs:337). It is replaced.

**Terrain.** The DEM slope over a 1.5 km window centred on the tile, from the mosaic the helper already reads: level under 2%, rolling 2-8%, mountainous over 8%. It is cached per z14 tile, so every z16 tile inside agrees. A z14 seam can change class, which only changes grade limits and slope defaults, and those blend across the joint (6.4).

**Climate flags.** `snow` is true if elevation exceeds 1,500 m in the western states, if `regional-snow.mjs` or `summit-snow.mjs` say so, or if the latitude is in the northern tier above 43 degrees N (heuristic). `snow` sets e_max to 6%, enables snow poles, and keeps the shoulder in the lane plane.

---

## 6. Section selection (deterministic)

### 6.1 Inputs

For each tile road piece, the helper builds a `way` record:
- class, oneway and ramp from the vector tile;
- raw surface, lanes and lanes per direction, width, sidewalk, smoothness, lit, cycleway and maxspeed from Esri, matched by geometry as in `lane-counts.mjs` (6 m, alignment 0.94);
- side-specific sidewalk, shoulder, kerb, parking, `turn:lanes`, placement and `lane_markings` from a new optional Overpass query (Stage 2b);
- separately mapped sidewalks and their offsets from `pedestrian.mjs`;
- kerb and crossing nodes within the block.

The new module `helper/road-tags.mjs` does all of this. It caches per z14 tile group for 28 days, like lane-counts.

### 6.2 Decision table (first match wins; each step may be overridden by a tag)

```
section_kind(way, ctx):
  unpaved surface or highway=track                         -> UNPAVED        (T9)
  highway=living_street|pedestrian                         -> SHARED (flush, no curb)
  motorway | motorway_link | expressway=yes                -> OPEN_FREEWAY   (T6)
  curb evidence on this block (kerb node lowered/flush/raised/rolled,
      footway=crossing, sidewalk!=no tagged, a matched footway=sidewalk,
      barrier=kerb line, parking:*=lane)                   -> CLOSED (or MOUNTABLE if V>=45)
  ctx in {urban, urban_core}                and V<=40      -> CLOSED
  ctx == rural_town and on a through street and V<=40      -> CLOSED
  ctx == suburban and cls in {primary..tertiary} and V<=40 -> CLOSED
  ctx == suburban and V>=45                                -> MOUNTABLE      (rolled curb, turf)
  ctx == suburban and cls==residential                     -> CLOSED if hash(way)<p_curbed else OPEN_LOCAL
  terrain == mountainous and cls not in {motorway,trunk}   -> OPEN_MOUNTAIN  (T7)
  arid (desert climate flag) and rural                     -> OPEN_DESERT    (T8)
  cls in {trunk, primary, secondary} rural                 -> OPEN_PAVED_SH  (T2)
  otherwise                                                -> OPEN_LOCAL     (T1)
```

The decision is made **per side**. A street can be curbed on one side only, as when a sidewalk is mapped on one side, so the selector runs once per side with that side's evidence.

**`p_curbed`** (suburban residential, no evidence) is 0.75. It reflects the FHWA PEDSAFE share and the "1 in 4 suburban locals lack sidewalks" heuristic. It is listed in the JSON as a heuristic.

### 6.3 Filling the template

1. Take the template's strips, with defaults at the middle of their ranges.
2. **Lanes.** Count from `lanes`, `lanes:forward` and `lanes:backward`. Width from `width:lanes`, otherwise the class and context table.
3. **Known carriageway width.** If `width` or `est_width` is present and passes the sanity range (road-engineering.md 9.3), distribute it with the muv-osm weights: motor lane 2, bike 1, parallel parking 2, angled parking 4. If the result gives a lane outside [2.7, 4.2] m, drop the tag and record provenance `d!`.
4. **Paved shoulder.** From `shoulder:*`, `shoulder:width` and `shoulder:surface`. Otherwise the class row of the template range, chosen by the ADT proxy.
5. **Closed sections:**
   - **Gutter:** 0.61 m (0.45 m on residential streets).
   - **Curb:** vertical 0.15 m, mountable 0.10 m.
   - **Planting strip:** from the matched separate sidewalk's offset, which is the most reliable; then `verge:width`; then the range, varied by way hash.
   - **Sidewalk width:** `sidewalk:<side>:width`, otherwise the context default.
   - `sidewalk=separate` gives no attached sidewalk mesh, but the strip still fills the gap up to the mapped footway.
6. **Parking and bike lanes** only where tagged, or where the tagged width leaves room of at least lanes x 3.0 + 4.2 m. **No parking above 35 mph.**
7. **Edge-line, centreline and rumble-strip flags** follow the MUTCD warrants (road-engineering.md 3.4, 7.6), using the ADT proxy and the total width.
8. **Era (heuristic).** Ways carrying the unedited TIGER marker (`tiger:cfcc` with no later edit, where the tag is available) on rural unclassified, tertiary or secondary roads may take the low end of each range: narrow lanes, 0.3 m paved strip, older 0.70 m rail top. The way hash decides, with probability 0.4 (heuristic). This answers the "every road brand-new" defect.
9. **Output a `profile`:** a list per side of `{kind, w, slope, mat, prov}`, plus flags (`edge_line`, `centre_line`, `srs`, `clrs`, `rpm`, `delineators`) and `etw`, the edge of the traveled way.

### 6.4 Changes along a chain

A chain can join ways with different profiles. Profiles are emitted per chain as `segments`: `[[s_from, s_to, profile_index]]`. The game interpolates strip widths across a transition length:
- lane-count changes use the MUTCD taper, L = W x S below 45 mph and L = W x S at 45 mph or more, with the 30/60 m minimums;
- other strip changes use a 1:50 width taper with a 30 m minimum (road-engineering.md 3.1);
- curb-to-open transitions use a 3 ft curb end ramp plus a 10 m blend of the ground (FDOT Index 300).

This replaces the 20 m `WIDTH_TAPER` average (helper/road-grade.mjs:304-313).

### 6.5 Determinism at seams

**What already holds.** Profiles are keyed on the matched OSM way id and depend only on that way's tags and world-fixed context cells. Two tiles that matched the same way therefore get the same profile.

**The residual risk is an asymmetric fetch.** If one tile has the Esri or Overpass answer and its neighbour does not, they disagree. Mitigations:
1. Query by z14 group (all 16 z16 tiles share one answer) with a 300 m margin, so a way crossing a group seam is in both groups' answers.
2. A failed fetch marks the tile `degraded` and triggers a retry. It is never cached as final, which is the same policy lane-counts uses.
3. At a chain joint where two different profiles meet, the transition taper of 6.4 applies, so a disagreement is a smooth change rather than a step.

SeamCheck gains a profile-equality assertion over the shared apron.

---

## 7. Alignment fitting

### 7.1 Inputs, pins and the locality rule

**Input:** the chain's mapped vertices in chain order. The existing chaining (helper/road-grade.mjs:143-196) is kept, with two changes:
- **Seam joints.** Where two pieces' ends are within `MERGE` = 2 m but not identical, both ends are replaced by their midpoint, which becomes an ordinary vertex. Today faaf27d keeps a hard corner there.
- **Input reach.** Vector lines are loaded out to `FIT_REACH` = 450 m beyond the tile (today `need = SAMPLE_APRON + WINDOW`, helper/road-grade.mjs:368). Neighbouring z14 tiles come through the existing `readVector` path (:383-386).

**Pins** are points the fit must keep:

| Pin | Tolerance | Notes |
|---|---|---|
| Junction node (shared by 2 or more tile lines) | 0.05 m after projection | See projection below |
| Bridge or tunnel ends | 0.10 m | Also fixes the vertical profile |
| Railway level crossings | 0.10 m | Flattens the grade |
| Chain ends that are real dead ends or tile-box exits | — | Tile-box exits are not pins; see locality |

**Junction projection** works by class rank, then way id. The higher-ranked road is fitted first, treating the junction as an ordinary vertex. The junction node is then projected onto its fitted curve, and the lower-ranked road is pinned to the projected point. The move is capped at 1.0 m; beyond that the original node is kept and the major road takes a tighter local fit there. This keeps every junction on both fitted centrelines. It is deterministic because rank and way id are tile-independent.

**Locality rule:** every decision that affects a station reads at most `FIT_REACH` of chain either side of it. Long groups are split at world-fixed z18 grid crossings (about 125 m apart at 35 degrees N). Tiles that see the same 450 m of road therefore build the same curve. `lab/check-alignment.mjs` asserts this to 1 mm.

### 7.2 Limits per design speed (from road-standards.json; metric)

| V mph | R_min m, e 6% (8%) | No-curve deflection | Min arc length L_c, open sections only | Broken-back tangent min | Spiral L_s at R_min | Max vertex deviation delta_max | dkappa/ds max, 1/m^2 (C = 0.6 m/s^3) | Relative gradient |
|---|---|---|---|---|---|---|---|---|
| 15 | 12 (11.6) | 1.0 deg | — | — | — (plain arcs) | 0.5 | — | 0.89% |
| 20 | 25 (23.2) | 1.0 | — | — | — | 0.5 | — | 0.80% |
| 25 | 44 (40.8) | 1.0 | — | — | — | 0.6 | — | 0.73% |
| 30 | 70 (65.2) | 1.0 | 137 m (soft) | 138 m | — | 0.6 | — | 0.67% |
| 35 | 104 (95.7) | 1.0 | 160 | 161 | — | 1.0 | — | 0.62% |
| 40 | 148 (135.3) | 1.0 | 183 | 184 | — | 1.0 | — | 0.57% |
| 45 | 196 (178.9) | 1.0 | 206 | 207 | 40 m | 1.0 | 7.4e-5 | 0.53% |
| 50 | 254 (231) | 0.5 | 229 | 230 | 45 | 1.5 | 5.4e-5 | 0.50% |
| 55 | 323 (293) | 0.5 | 251 | 253 | 49 | 1.5 | 4.0e-5 | 0.50% |
| 60 | 405 (366) | 0.5 | 274 | 276 | 54 | 2.0 | 3.1e-5 | 0.50% |
| 65 | 506 (451) | 0.5 | 297 | 299 | 58 | 2.0 | 2.45e-5 | 0.50% |
| 70 | 622 (552) | 0.5 | 320 | 322 | 63 | 2.0 | 2.0e-5 | 0.50% |

**How the columns are derived:**
- **L_c.** The sourced rule is 15 V ft for V of 60 mph or less, and 30 V ft is desirable on high-speed controlled-access roads (road-engineering.md 5.6). The column uses 15 V ft (4.57 V m) throughout; above 60 mph it extends the same rule, and motorways may use 30 V ft. The value is "soft", because mapped rural curves are often shorter than new-construction practice: an arc shorter than L_c is lengthened only if the deviation stays within delta_max. A short arc is flattened only if its deflection is under 10 degrees and removing it keeps every vertex within delta_max. Large-deflection short curves are real corners (a hairpin) and stay. L_c is not applied to closed-section streets.
- **Broken-back tangent:** 4.6 V m.
- **L_s:** clamp(max(2 s x v, 0.0214 V_kmh^3 / (1.2 R)), sqrt(4.8 R), sqrt(24 R)). Only at 45 mph or more. Plain arcs below that, with G1 continuity.
- **delta_max:** the largest allowed distance between the fit and an unpinned mapped vertex. It is a *SimCruise tolerance* (heuristic), set at about half a lane at speed and tight in towns, because curbs and mapped sidewalks are mapped relative to the line.
- **dkappa/ds:** C / v^3, used by AF2 only (7.4).

e_max by context follows section 5. With 4% (urban high speed), use the Method-2 radius rows from road-engineering.md 5.2.

### 7.3 AF1: the local design-aware fitter (Stage 1)

AF1 replaces faaf27d's `chainVertices` / `chainPoint` / `arcPoint` in a new module, `helper/road-align.mjs`. `chainPoint(V, s)` keeps its signature `[x, y, tx, ty]` and adds a fifth value, kappa.

```
fitAlignment(chain, L):                      # L = limits(V, ctx) from 7.2
  P = vertices(chain)                        # flags: pin, joint
  # 1. clean (local)
  merge seam joints to their midpoint; drop vertices < 0.5 m apart
  jitter filter: drop an unpinned vertex whose distance to the chord of its two
    neighbours is < 0.35 m (z14 quantisation, ~0.49 m step) and whose legs are
    both < 40 m; repeat until stable (each pass is local; 3 passes max)
  # 2. straight blocks (closed sections, or V <= 45)
  for each run between consecutive pins with arc length <= 300 m:
    if every vertex is within max(0.5 m, 0.15 lane) of the pin-to-pin chord and
       the run's heading spread <= 2 deg: replace the run by the chord
  # 3. sub-threshold deflections
  remove unpinned vertices whose deflection < L.theta_min (recompute after each removal)
  # 4. curve groups
  group = maximal run of consecutive unpinned vertices turning the same way whose
          interior legs are < max(30 m, 2 L_s); split any group longer than 400 m
          at the world z18 grid crossing nearest its middle
  for each group:
    Delta = total deflection between entry leg and exit leg
    R_fit = single vertex: the largest R allowed below; else Pratt circle fit
            through the group's vertices and chord midpoints
    R_leg = largest R whose tangent length T(R) = R tan(Delta/2) + spiral half-length
            fits within half of each outer leg (shared with the neighbouring group)
    R_dev = largest R keeping every vertex of the group within delta_max
    R = min(max(R_fit, R_min), R_leg, R_dev)
    if R < R_min: group.forced = true; group.V_local = sqrt(127 R (e_max + f)) km/h
  # 5. consistency (design rules, road-engineering.md 5.6)
  same-turn neighbours with tangent < 4.6 V m -> merge into one group and refit (4)
  opposite-turn neighbours with tangent < runoff_tan(1) + runoff_tan(2):
      both Delta >= 10 deg -> reverse curve with back-to-back spirals
      else drop the smaller-Delta group if all its vertices stay within delta_max
  enforce compound ratio R_large <= 1.5 R_small (2.0 at ramps and loops) by
      raising the smaller R where R_leg/R_dev allow, else flag
  # 6. spirals (V >= 45 mph)
  L_s per 7.2, shrunk to fit the legs; if L_s < sqrt(4.8 R) drop the spiral (flag)
  shift the arc inward by p = L_s^2 / (24 R)
  # 7. sample
  elements: [{type: line|arc|spiral, s0, length, x0, y0, h0, k0, k1}]
  stations every 1 m (0.5 m where |kappa| > 0.01) with x, y, heading, kappa
```

**Elements and their primitives:**
- Arcs are exact.
- Spirals use Fresnel integrals, evaluated by an 8-term series for small arguments and a 0.25 m RK4 otherwise.
- Lines are exact.

These are OpenDRIVE planView primitives, which leaves the door open to a future export.

**Edge offsets** are computed from the analytic normal, then clamped. On the inside of a curve, an offset d with d x kappa of 0.9 or more is clamped to 0.9 / kappa. Points that move backwards along the tangent are merged, which removes the swallowtails today's RoadSurface builds on tight inner edges (game/RoadSurface.gd:77-99). A clamped station is flagged; the fit should normally have avoided it by respecting R_min of at least half plus shoulder.

**Cost** (estimate): O(n) per chain, under 20 ms for a dense urban tile in Bun.

### 7.4 AF2: span optimiser in curvature space (only if needed)

AF2 runs only if AF1 fails the Stage 1 gates on open roads at 50 mph or more. Its job is to make long highway curves design-consistent.

**Spans.** It works between consecutive pins, plus virtual breakpoints where the AF1 result crosses the z17 grid. At a virtual breakpoint AF2 holds position, heading and curvature at their AF1 values, so it is deterministic.

**Unknowns.** Curvature at knots h = 6 m apart (10 m at 60 mph or more), piecewise linear, plus the start pose.

**Objective.** Minimise sum(d_lat^2 / sigma^2) + lambda x sum |kappa_{j+1} - kappa_j|, where:
- sigma is 2 m, or 1 m where the source is a survey;
- the L1 term is handled by IRLS with epsilon = 1e-6, and lambda = 4 (to tune on fixtures).

**Constraints:**
- |kappa| at most 1 / R_min;
- |kappa_{j+1} - kappa_j| / h at most dkappa_max(V);
- the span end poses.

**Solve.** Gauss-Newton with projection onto the box constraints, 10 iterations. Positions are integrated with Simpson's rule at 0.5 m. A 400 m span at h = 6 m has about 67 unknowns and a dense 67 x 67 system: well under 1 ms.

**Prior art:** [LIPAR], [TITS-2023], [ARXIV-2110.15814] (road-engineering.md 5.10).

### 7.5 Vertical profile (Stage 3)

Today the profile is a Gaussian local-linear fit, sigma 16 m, with one Tukey pass (helper/road-grade.mjs:75, :198-236, :287-291). It is replaced by a **K-bounded smoothing fit**:

1. **Data term.** The existing robust DEM samples (the `Z` and `W` rows from `chainProfile`), at 2 m stations. Bridge decks, the major road's junction heights and rail crossings are fixed.
2. **Minimise** sum w(z - z_dem)^2 + mu x sum (z'')^2 with a pentadiagonal banded solve, O(n). Then project:
   - Where z'' exceeds 1 / (100 K_crest) on a crest, or 1 / (100 K_sag) in a sag, clamp it and re-solve with those stations' second differences fixed, as an active set, at most 5 rounds.
   - Where |z'| exceeds g_max + allowance (road-engineering.md 5.7), keep the DEM, since the road exists, but mark the span `steep`.
3. **Locality.** The banded smoother's influence decays with a length of about (mu / w)^(1/4). With the chosen mu that is about 12 m, so truncating at `FIT_REACH` gives seam agreement below 1 mm.
4. **Coordination check** (WSDOT 1220): flag any horizontal-curve start or end within L_vc / 2 of a crest. In Stage 3 this is only reported, not fixed.

### 7.6 Crown and superelevation (Stage 3)

**Section representation.** The section stores two lane slopes per station, `sl` and `sr`, instead of a single e:
- On tangents (normal crown): sl = sr = -2%, falling away from the crown. The crown is on the centreline for two-way roads and on the inner edge for one-way carriageways. One-way carriageways rotate about the inner edge with the b_w factor.
- **Unpaved roads:** -4%.

**Rate e(R, V, e_max).** Interpolate linearly in 1/R between the table anchors (road-engineering.md 5.3):
- NC above R_NC;
- RC (+2% on the outside) from R_RC;
- e = 4% at R_e4;
- e_max at R_min;
- round up to 0.2%.

Low-speed closed sections keep NC unless R is below the e = 0 radius.

**Transitions.** Piecewise linear, from 7.2 and road-engineering.md 5.4:
- runout L_t;
- runoff L_r, with the tabulated share on the tangent;
- with spirals, runoff = L_s, placed on the spiral.

This replaces `E[i] = sign * min(.06, .5 V^2 / (127 R))` and the sigma-16 blur (helper/road-grade.mjs:244-251) and the `RUNOUT_M` patch (:636-666).

**Shoulders.** On the high side, the shoulder may roll over at most 8% from the lane slope. On the low side it takes the steeper of e and its own slope.

**One profile for every consumer.** RoadSurface, RoadsideMesh, LaneMarkings and GuardRails all read the same `sl`, `sr` and strip slopes. RoadCrossfallGround's lidar crown stays authoritative where it is surveyed (precedence `m`).

---

## 8. Strip geometry and materials

### 8.1 Section points (contract between helper and game)

Each road_surface v4 station carries:

```
[x, y, z, sl, sr, tx, ty, kappa, wL, wR, catchL, catchR, flags]
```

| Field | Meaning |
|---|---|
| wL, wR | Paved half widths: centreline to the paved edge, which is the lip of gutter in closed sections |
| catchL, catchR | Offset of the catch point: where the open-section template meets natural ground, or the back of sidewalk plus frontage in closed sections. Smoothed along the station (8.3). |
| flags | Bits: forced curve, clamped offset, steep, bridge, junction mouth, driveway |

The **per-chain** profile (6.3) gives the strip list. The **per-station** fields give everything that varies.

**RoadSurface.gd keeps its 5-point carriageway section.** GuardRails reads `sections[i][0/4]` and LaneMarkings reads `[e]`, so the indices keep their meaning. Point 2 is now the crown, and its height uses `sl` and `sr` instead of the single plane `z - e x lateral`. The other points:
- **Points 1 and 3:** the paved edge.
- **Points 0 and 4:** the "shoulder edge", which becomes the hinge in open sections and the face of curb in closed sections. This keeps GuardRails' meaning: rail face = shoulder edge + 0.6 m (MGS practice).

The fixed `SHOULDER_M` 1.2 m and `SHOULDER_DROP_M` 5 cm (game/RoadSurface.gd:30-31) are retired.

**LaneMarkings reads `etw`**, the edge of the traveled way, and the line flags from the profile. Its line normal comes from the section itself, `sections[k][4] - sections[k][2]`, so paint and pavement share one normal (fixes game/LaneMarkings.gd:449 and :633).

### 8.2 Meshes

| Mesh | Owner | Points per station per side | Collider | Material |
|---|---|---|---|---|
| Carriageway (centre to shoulder edge) | RoadSurface.gd | 5 for the whole section, as now | yes (as now) | terrain material; UV2 = (lateral, half) from faaf27d; vertex CUSTOM0 = (material id, condition, joint phase, wheel-path mask) |
| Open roadside: shoulder edge to catch point | **RoadsideMesh.gd (new)** | about 8: gravel outer edge, hinge-round (3 pts), foreslope toe, ditch bottom (2), backslope catch | yes (cars leave the road) | terrain material, CUSTOM0 material id (gravel, turf_mown, turf_wild, native_desert, rock) |
| Closed roadside: lip of gutter to back of frontage | Kerbs.gd (extended) | gutter (2), curb face (2), curb top (2), strip (2), walk (2), frontage (1) | curb and walk yes | concrete (gutter, curb, walk), turf (strip) |
| Junction pad | Junction.gd (Stage 6) | triangulated polygon | yes | asphalt, plus concrete curb returns |

RoadsideMesh is **render plus collider, separate from RoadSurface's quads**. That keeps `height()`, `join()` and the GuardRails indices unchanged, which plan 06's skeptic required. `WorldTile.height_at` (game/WorldTile.gd:43-50) asks RoadSurface first, then RoadsideMesh, then the terrain grid.

### 8.3 Materials and the strip-to-shader mapping

Material ids come from `road-standards.json` `materials`.

**The terrain shader branches on the material id** instead of on aerial luminance. Asphalt albedo is the table value times an aggregate and age texture, plus at most ±0.03 of photo modulation, which carries only patches and stains. This replaces:
- the luminance-derived `pavement_light` (game/GroundFinish.gdshaderinc:32-41);
- the 3.7x neighbourhood lift (game/AerialPavement.gdshaderinc:19-29).

**Grip by material:**

| Material | Grip |
|---|---|
| asphalt, concrete, chip seal | 1.0 |
| gravel shoulder | 0.76 (the existing loose constant) |
| turf | 0.65 |

`wheel_grip` reads it through `RoadSurface.lateral()` (faaf27d :231), extended to return the strip, or through `RoadsideMesh.lateral()` off the carriageway. The measured-section and capture precedence is kept.

**Catch-point smoothing.** The template is first solved per station against the DEM. The catch offsets are then low-pass filtered along the station (Gaussian sigma 10 m, which is the "over at least 20 m" rule), and the backslope ratio is re-solved to meet the smoothed catch. This stops cut and fill boundaries lobing with the DEM's 10 m texture.

---

## 9. How the ground meets each edge type

The helper writes target heights into the coarse `h` grid (FINE = 128 per tile, about 3.9 m spacing at 35 degrees N) so that distant terrain matches. The exact shape lives in RoadsideMesh or Kerbs. Grid nodes under those meshes are pushed 6 cm below them, as `lower_grid` does now (game/RoadSurface.gd:168-177). Beyond the catch point the terrain is untouched; inside the transition band (catch + 3 m) it blends with a smoothstep.

| Edge type | Profile from the shoulder edge outward | Ground rule |
|---|---|---|
| **OPEN, cut** (terrain above the hinge) | gravel shoulder, hinge round, foreslope 1:N to the ditch, flat ditch bottom 0.6-1.2 m, backslope 1:3 (1:2 in mountains) up to daylight | Daylight point = intersection of the backslope with the DEM. If that is beyond ROW or 25 m (the current `BATTER_REACH`), steepen the backslope stepwise to 1:2, 1:1.5, 1:1 (rock; heuristic) and flag `steep_cut`. Smooth catch offsets (8.3). |
| **OPEN, low fill** (hinge above terrain, height under the ditch depth) | shoulder, hinge, foreslope 1:N to the ditch, ditch, short backslope to terrain | The ditch bottom follows a profile with at least 0.3% grade (no bumps). If no fall is available it is held at the minimum depth of 0.6 m. |
| **OPEN, high fill** (fill height at least the ditch depth) | shoulder, hinge, foreslope straight to the toe, toe round | Foreslope 1:N per class. If the fill is steeper than 1:3, or N cannot fit inside ROW, GuardRails' warrant fires (8.2 of the knowledge base), and the ground between shoulder and rail is graded at 10:1 [TX-RDM 4.10.13]. |
| **OPEN, along a guard rail** | shoulder, 0.6 m to the rail face, 0.6 m platform behind the posts, then the fill slope | The rail run comes from the warrant plus length of need (Stage 8). The platform is part of RoadsideMesh. |
| **OPEN, mapped ditch** (`waterway=ditch` or `drain` within 10 m) | The generated ditch is snapped to the mapped line, or suppressed | |
| **OPEN, driveway** (`service=driveway` meeting an open section) | The driveway apron crosses the ditch on a culvert, with the entrance slope 6:1 at 45 mph or more (4:1 below) and the ditch continuing underneath; a dark pipe mouth is drawn | RoadsideMesh warps the strip over the apron width. |
| **MOUNTABLE** (suburban, 45 mph or more) | 0.10 m rolled curb, then turf at +2% for 3 m, then blend | |
| **CLOSED** | gutter (6% toward the curb), 0.15 m face, curb top, strip at +2%, sidewalk at +1.5%, frontage 0.3 m, then ground graded at 1:4 or flatter to meet the terrain; if the terrain is higher, a 1:3 backslope or (urban core) a building line | The sidewalk top = curb top, and its longitudinal profile follows the road grade (PROWAG lets it match the street). Driveways: the curb drops to 0.03 m across width + 2 x flare, the apron ramps across the strip, the walk stays continuous at 2%. Crossings and corners: curb ramps (PROWAG, road-engineering.md 6.4). |
| **SHARED** (`living_street`, `pedestrian`) | flush surface, no curb, a paver or concrete band at the edge | |
| **UNPAVED** | 4% straight crown, flush shoulder, foreslope 1:3 from the road edge, V-ditch | |
| **Bridge end** | The template narrows to the deck's barrier line over 15 m. The guard rail transitions to the bridge rail (25 ft transition, MGS practice). Ground is fixed at the abutment. | |
| **Junction mouth** | The ditch and curb stop at the curb-return tangent points. Ground inside the return is the junction pad (Stage 6). | |

**Clear zone.** Each station also carries the clear-zone width computed from its V, ADT proxy, foreslope and radius (road-engineering.md 8.1). Roadside.gd:69, Grove.gd:150 and Rangeland.gd:170 exclude trees, rocks and poles inside it (the call sites plan 06's skeptic identified). The band from the shoulder to the clear zone is shaded as mown turf; arid regions use bare gravel instead.

---

## 10. Pavement appearance (Stage 7)

- **Albedo** from the material and age table, with the photo as ±0.03 modulation (8.3). Default age is `aged`. `smoothness` and the era hash pick `fresh`, `aged` or `worn`.
- **Concrete** (`surface=concrete`, or an aerial reflectance of about 0.22 or more):
  - transverse joints every 4.57 m, continuous across lanes, with the phase reset only at bridges and junctions;
  - longitudinal joints on lane lines and 0.3-0.6 m outside the edge line;
  - 20 mm longitudinal tining in the normal map;
  - joint faulting steps of 0-4 mm, felt by the ride model.
- **Asphalt:**
  - longitudinal paving seams at lane lines, offset up to 0.15 m;
  - paver passes differing by ±0.015 in albedo;
  - wheel-path bands at ±0.8 m from the lane centre (lighter and polished, glossy bleeding in hot climates);
  - a darker centre drip strip;
  - cracks, tar snakes and patches driven by one condition scalar from `smoothness` or the era hash;
  - edge raveling only on open sections with unpaved shoulders.
- **Shoulder rumble strips and centreline rumble strips** per the 6.3 flags, as a normal map plus physics bump. Their pitch feeds tyre audio (about 93 Hz at 100 km/h).
- **Markings** keep their dash phase on the fitted arc length, so a dash never restarts at a segment join. Wear goes only on low-speed streets and where tyres cross the paint.

---

## 11. Junctions (Stage 6, summary)

1. **Find the junction.** A junction node is shared by two or more fitted lines. Its approaches are trimmed at the major road's shoulder edge, or its lip of gutter in closed sections.
2. **Corner fillets.** Fit a fillet between each adjacent pair of edge lines (curb lines in closed sections), with radius R by the higher class and context (road-engineering.md 6.1). Subtract parking and bike widths. On skews under 75 degrees, give the obtuse corners the larger radius. Never go below 3 m.
3. **The pad.** Triangulate one pad polygon lying in the **major road's** plane: its crown or superelevation. The minor road's profile and crown are warped onto it over 15-30 m (capping grade at 4-5% within 30 m). This replaces the height snap in `RoadSurface.join()` (game/RoadSurface.gd:105-130).
4. **Markings:**
   - edge lines and lane lines stop at the PCs;
   - stop bars on approaches tagged `highway=stop` or `traffic_signals`, 1.22 m before the crosswalk, or 1.5-3 m before the PC;
   - yield triangles at `give_way` approaches and roundabout entries;
   - crosswalks per the tags, or on all legs at signals.

   Junction.gd's signal placement (game/Junction.gd:50-80) moves to the trimmed approach geometry.
5. **Approach deflection.** The FDOT table (road-engineering.md 6.6) caps it. A larger deflection is straightened through the junction and moved into a taper outside it.
6. **Roundabouts.** Least-squares circle fit to the OSM ring, then the FHWA category geometry.

---

## 12. Integration map (file:line on main 6cadf05 unless marked faaf27d)

| Where | Today | Change | Stage |
|---|---|---|---|
| helper/road-widths.mjs:25-41 `roadWidth`, `referenceWidths` | Class constant; one-way = 2 x 3.6 m + shoulder table | `roadWidth` becomes the class fallback. Real width = `sectionFor().paved`. `referenceWidths` unchanged (survey signatures). | 2 |
| helper/road-grade.mjs:75-78 `STEP`, `SIGMA`, `CUT`, `FILL`, `SAMPLE_APRON`, `MERGE` | Gaussian profile, steep batters | Profile per 7.5; `CUT`/`FILL` replaced by template slopes; `FIT_REACH` 450 m added | 1, 3, 4 |
| helper/road-grade.mjs:143-196 `chainPieces` | Joints within 2 m; hard corners kept (faaf27d) | Midpoint merge of differing joint ends | 1 |
| faaf27d helper/road-grade.mjs `chainVertices`, `chainPoint`, `arcPoint`, `FILLET_MAX_M`, `TANGENT_SIGMA` | Vertex fillets of at most 1 m | Replaced by road-align.mjs AF1 behind the same `chainPoint` interface, plus kappa | 1 |
| helper/road-grade.mjs:214-277 `chainProfile` | Stations restart per piece; E from chord headings :238-251 | Stations from the fitted elements, uniform over chain arc length; E from 7.6 | 1, 3 |
| helper/road-grade.mjs:287-291 `profileAt` | Smoothed tangent, sigma 4 (faaf27d) | Analytic tangent from the fit (no smoothing) | 1 |
| helper/road-grade.mjs:304-313 `widthAlong` | 20 m mean taper | Profile segments with MUTCD and 1:50 tapers (6.4) | 2 |
| helper/road-grade.mjs:314-335 `roadSurface` | Rows `[x,y,z,e,tx,ty,w]` | v4 rows (8.1) plus per-chain profiles and segments; old rows still readable | 1-4 |
| helper/road-grade.mjs:337 `designSpeed` | 90 / 60 / 0 km/h by class | `std.designSpeed(way, ctx)` | 3 |
| helper/road-grade.mjs:344 summary version | 2 (3 on faaf27d) | 4, plus the standards version in the cache key | 1 |
| helper/road-grade.mjs:368, :383-386 `need`, `readVector` | Neighbours within apron + window | Neighbours within `FIT_REACH` | 1 |
| helper/road-grade.mjs:445-450, :520-548 bench and batter | Flat bench to half + max(1.5, 1.2 cell); cut 1.2:1 and fill 0.7:1 clamps | Template target heights and catch points (section 9) | 4 |
| helper/road-grade.mjs:636-666 `superelevationRunout` | 20 m runout patch | Removed; junction pads take over | 6 |
| helper/atlas.mjs:107 | `streamedTerrain` call | Passes the standards and way tags | 2 |
| helper/atlas.mjs:115-136 (`roads` rows) | Raw z14 polylines | **Fitted polylines** (plan 06 Phase B), so `street_image`, `geometric_prior`, traffic `segments`, `road_grid` and RoadLayout agree with the ribbon; bump the RoadLayout signature version | 1 |
| helper/atlas.mjs:129 | surface collapsed to paved / unpaved | Raw surface from road-tags.mjs | 2 |
| helper/lane-counts.mjs (outFields), helper/road-layout.mjs:13-19 | lanes, oneway, maxspeed | New helper/road-tags.mjs: width, sidewalk, surface, smoothness, lit, cycleway, tracktype (Esri) plus optional Overpass keys | 2, 2b |
| helper/pedestrian.mjs:4-14 | Mapped footways as paint | Matched sidewalks give curb evidence and strip width | 2 |
| game/RoadSurface.gd:30-31 | `SHOULDER_M` 1.2, `SHOULDER_DROP_M` .05 | Removed; offsets from v4 rows | 2 |
| game/RoadSurface.gd:65-104 `build` | Planar `z - e x lateral` | Two-plane crown (`sl`, `sr`); offsets from `wL`/`wR`/shoulder edge; offset clamp d x kappa < 0.9 | 1, 3 |
| game/RoadSurface.gd:105-130 `join` | Height snap, 15 m fade | Replaced by junction pads | 6 |
| game/RoadSurface.gd:179-190 `mesh` | UV2 (faaf27d) | Plus CUSTOM0 material, condition, joint phase | 7 |
| faaf27d game/RoadSurface.gd:52-54, :275 `EDGE_*`, `classify_edges` | Game-side kerb, town or rural per station | Reads the helper's per-side section kind; keeps its mapped-sidewalk test as the pre-v4 fallback | 2 |
| faaf27d game/Kerbs.gd | Curb and 18 in gutter where a sidewalk is mapped | Full closed section: gutter, curb, strip, walk, driveway cuts, ramps | 5 |
| **game/RoadsideMesh.gd (new)** | — | Open-section strip and collider (8.2, 9) | 4 |
| game/LaneMarkings.gd:159-244 `cross_section` | Lane widths from class; edge-line rule by class and town | Reads profile `etw`, line flags, lane widths | 2 |
| game/LaneMarkings.gd:449, :633 | Raw chord tangent | Section normal | 1 |
| game/Junction.gd:50-80 | Signal pole 9 m and stop bar 6 m from the raw segment | Junction model (11) | 6 |
| game/GuardRails.gd:32-45 | `DROP_M` 3, `MIN_RUN_M` 12, `LEAD_M` 8 | Warrant from clear zone and slope; length of need from the runout table; terminals | 8 |
| game/GuardRails.gd:316 `inferred_runs` | Probe 8 m beyond the shoulder | Reads catch offsets and slope from v4 rows | 4, 8 |
| game/WorldTile.gd:394 (`geometric_prior`), :410-480 (`street_image`), :602-648 (`road_index`, `wheel_grip`, `surface_grip`) | Raw `info.roads` | Fitted roads (Stage 1); grip by material strip (Stage 4) | 1, 4 |
| game/WorldTile.gd:858, :871 | RoadSurface, GuardRails build | Add RoadsideMesh after RoadSurface | 4 |
| game/WorldTile.gd:901, :942-969 `street_details` | Legacy curbs and sidewalks on bundled tiles | Kept for bundled tiles until they are rebuilt with v4; then retired | 5 |
| game/terrain.gdshader:150-167; game/GroundFinish.gdshaderinc:32-41; game/AerialPavement.gdshaderinc:15-29 | Pavement tone from the photo | Material-id albedo with ±0.03 photo | 7 |
| game/RoadCrossfallGround.gd:25, :130; game/RoadBankMesh.gd:8 | Lidar crown, surveyed banks | Unchanged, highest precedence; the v4 template applies only outside surveyed runs | — |
| game/Roadside.gd:69, Grove.gd:150, Rangeland.gd:170 | `surface_grip` threshold | Clear-zone query | 4 |

### Relation to plan 06 (road-edge-authority)

| Plan 06 phase | Here |
|---|---|
| A | Done on faaf27d; merge in Stage 0 |
| B (one smoothed centreline at the source, exported as `roads`) | Stage 1, using AF1 instead of the Gaussian or fillet smoother |
| C (rural per-class shoulders, a render-only skirt mesh, no new section points) | Replaced by Stages 2 and 4. Shoulders depend on the section kind and context rather than class alone, which answers the skeptic's objection that urban curbed streets have no shoulder. The skirt becomes RoadsideMesh. |
| Step 3 (photo-derived width) | Stays dropped |

---

## 13. Staged implementation plan and gates

Each stage is one branch and one PR. Every stage runs the existing gates that apply:
- SeamCheck, RoadProfile, LaneMarkingCheck, TrafficCheck, RoadEdgeCheck;
- RideCheck, RoadNoiseCheck, SustainedSurfaceCheck, SplitGrip, TractionControlCheck;
- RoadDrive with 0 ABS/TC events on a straight dry run;
- CrossfallDrive, CrownArrivalOrder, VegetationCheck.

The new checks are listed per stage.

**Fixture set** (`lab/fixtures/roads/`). Cached `atlas tile` JSON plus the raw vector and DEM inputs, so every check runs offline and deterministically:

| Fixture | What it tests |
|---|---|
| Tail of the Dragon (US-129) | Hairpins |
| Vidalia GA US-280 | Divided highway |
| Eureka CA US-101 couplet | One-way couplet |
| A Midwest grid town | Straight blocks |
| A Phoenix arterial | Urban arterial, desert |
| Nevada US-95 desert straight | Desert highway |
| A Colorado mountain pass | Mountain road |
| A rural Iowa county road | Gravel and paved local |
| An interstate interchange | Freeway |
| Miami Beach bundled tiles | Bundled-tile regression |

Visual captures use Ultra (Forward+ where it is being evaluated), at 10 random places plus the dragon and capitol shots.

### Stage 0: groundwork (about 1.5 days)

- Merge roads/edge-authority faaf27d.
- Write `helper/road-standards.json` in full, `helper/road-standards.mjs` and `game/RoadStandards.json` + `RoadStandards.gd`.
- **Gate:** `lab/check-road-standards.mjs`. It passes when:
  - the schema is valid;
  - every group has `src` and `basis`, and every `src` resolves;
  - tables are monotonic: R_min rises with V, f_max and Delta fall, K rises;
  - spot rows reproduce their sources (R_min at 6% for 55 mph = 323 m within 1%; the K crest at 60 mph = 46.0 m/%);
  - the game copy's hash is equal;
  - no value is changed without a version bump (compared with git HEAD).

### Stage 1: alignment AF1 and the fitted centreline everywhere (about 4 days)

- road-align.mjs; road-grade.mjs uses it.
- `roads` export of fitted lines (plan 06 Phase B).
- LaneMarkings normal fix; offset clamp.
- **New gate: `lab/check-alignment.mjs`**, on all fixtures plus synthetic cases:
  - **Heading continuity:** a jump of more than 0.5 degrees between 1 m samples fails, except at flagged junction corners.
  - **Curvature continuity:** at 45 mph or more, |kappa_{i+1} - kappa_i| must be at most max(1.5 x dkappa_max(V) x ds, 1e-4) except at the ends of forced curves. Below 45 mph, kappa may step only at arc-to-tangent points.
  - **Bound:** |kappa| at most 1 / R_min(V) unless the curve is flagged forced. Report forced curves per km, with a target under 1 per km outside mountain fixtures.
  - **Reversal rule:** at 30 mph or more, curvature may not change sign twice within 100 m, except at flagged reverse curves.
  - **Fidelity:** unpinned vertices within delta_max(V); pins within 0.05 / 0.10 m.
  - **Straight blocks:** in the grid-town fixture, each junction-to-junction block's heading spread is under 0.2 degrees.
  - **Seam determinism:** fit the same road from both tiles of 20 seams; stations over the shared reach must agree within 1 mm.
  - **Offset validity:** d x kappa < 0.9 for every edge offset, with no clamped stations outside forced curves.
  - **Synthetic recovery** (Lipar-style): arcs of R = 200 and 300 m with ±1 m noise at 20-35 m spacing must recover R within 10%, and a z14-quantised straight must recover zero curvature with a residual under 0.05 m.
- **Game gates:**
  - RoadEdgeCheck `KINKS_PER_KM` = 0 and `WOBBLE_RMS_M` < 0.05;
  - RoadProfile kink extension;
  - SeamCheck;
  - TrafficCheck: cars stay on the fitted lanes, 0 off-lane events.
- **New capture metric** (RoadEdgeCheck `--capture`): the **edge straightness score (ESS)** and the **lobe amplitude**.
  - At 4.7 cm per pixel, the drawn asphalt edge is measured along each station normal to sub-pixel precision.
  - ESS is the RMS of (drawn offset - analytic offset), detrended over 60 m. Gate: under 0.03 m.
  - Lobe amplitude is the maximum of that residual band-passed to 5-50 m wavelengths. Gate: under 0.08 m.
  - Because the analytic edge is known, the score measures the defect directly and does not depend on any copy of the shader logic.

### Stage 2: context and section selection (about 3 days; 2b, Overpass, about 2 days more)

- road-tags.mjs (Esri fields).
- Context in the helper.
- `sectionFor`, profiles and segments in the v4 rows.
- RoadSurface widths from the profile; LaneMarkings from the profile.
- `classify_edges` reads the helper.
- **New gate: `lab/check-road-sections.mjs`:**
  - **Golden template IDs** per side for about 120 hand-checked fixture ways: an Iowa county road is T1, US-95 is T8, a Phoenix arterial is T4, a grid-town residential street is T5, and so on. Any change needs a reviewed golden update.
  - **Conformance:** every strip is within its JSON range; tagged values are used verbatim (provenance `t`); width-distribution lanes are within [2.7, 4.2] m.
  - **Determinism:** the same way from two tiles gives the same profile.
  - **Coverage report:** share of strips tagged, inferred or default.
- **Game:** a new `CrossSectionCheck.gd`. It samples built ribbons at 200 random stations per fixture tile and asserts that the built paved width, edge-line offset and curb side match the profile within 2 cm.

### Stage 3: profile, crown and superelevation (about 2 days)

- K-bounded vertical fit; e(R, V) table; runoff and runout; two-plane crown.
- **New gate: `lab/check-superelevation.mjs`:**
  - e reproduces the table anchors exactly;
  - the runoff's relative gradient is at most Delta(V) + 0.01%;
  - the tangent share is as tabulated;
  - no e change on NC curves;
  - vertical z'' stays within 1 / (100 K) except at DEM-forced `steep` spans, which are reported.
- **CrossSectionCheck adds:**
  - lane cross slope on tangents 2% ± 0.1%;
  - lane-to-shoulder difference of 8% or less;
  - high-side rollover of 8% or less.
- **Gates:** CrossfallDrive, RideCheck, RoadNoiseCheck, plus a RoadDrive straight run with 0 ABS/TC events. The crown must not add steering pull beyond what real cars show; compare against the lidar-crowned surveyed roads.

### Stage 4: open roadside: shoulders, safety edge, foreslope, ditch, backslope (about 4 days)

- RoadsideMesh.gd; helper template earthwork replaces the bench and batter; catch smoothing; grip by strip; clear-zone vegetation exclusion.
- **CrossSectionCheck adds:**
  - foreslope and backslope ratios within ±10% of the template, or flagged `steep`;
  - ditch depth within range and ditch-bottom grade of at least 0.3% or flagged;
  - catch-offset smoothness: RMS of the second difference along stations under 0.05 m per station^2;
  - no vertical gap over 2 cm between RoadsideMesh and the terrain at the catch;
  - terrain poke-through above any strip = 0.
- **VegetationCheck:** 0 trees or rocks inside the clear zone on roads at 50 mph or more.
- **GuardRails:** the warrant fires on every fixture fill steeper than 1:3 with a drop over 2 m inside the clear zone.
- **Capture:** a hinge-crease metric. The maximum slope change across the hinge in the rendered depth must be under the template change plus 2 degrees.

### Stage 5: closed sections (about 4 days)

- Kerbs.gd full section; inferred curbs; strip and sidewalk; driveway cuts; curb ramps at crossings and corners; retire `street_details` for v4 tiles.
- **CrossSectionCheck (closed) adds:**
  - curb reveal 0.15 ± 0.01 (0.10 mountable);
  - gutter width per the profile;
  - sidewalk cross slope of 2.1% or less, everywhere including across driveways;
  - ramp running slope of 8.3% or less with a 1.22 m width.
- **StreetCheck.**
- **New `KerbDrive.gd`:**
  - a car at 15 km/h hitting a 0.15 m vertical curb at 10 degrees is deflected or mounts with a measured vertical step;
  - at 5 km/h it mounts a 0.10 m rolled curb;
  - no tunnelling through the collider.
- **RoadEdgeCheck:** `carriageway_ground_frac` = 0 with curbs present.

### Stage 6: junctions (about 5 days)

- Trimming, corner fillets, pads on the major road's plane, PC-terminated lines, stop bars, crosswalks, roundabout circle fit.
- **New `JunctionCheck.gd`:**
  - pad coverage with no holes; overlap area under 1% (no z-fighting);
  - corner radius within ±10% of the selected value;
  - lines end at the PC within 0.3 m;
  - stop line at least 1.22 m before the crosswalk;
  - approach deflection within the FDOT table;
  - the minor road's profile meets the pad within 1 cm and its grade within 30 m is 5% or less, where the DEM allows.
- **Gates:** TrafficCheck, SignalControlCheck.

### Stage 7: pavement appearance (about 3 days)

- Material-id albedo; concrete joints; seams; wheel paths; condition decals; rumble strips; RPMs.
- **New `PavementAppearanceCheck.gd`** (capture, Ultra):
  - carriageway linear albedo within the material range;
  - on arid fixtures, the carriageway-to-ground luminance ratio, measured over a 3-10 m band outside the catch, is 0.7 or less;
  - aerial contamination: carriageway albedo variance at the 1-3 m scale is under 0.02^2;
  - concrete joint pitch is 4.57 m ± 2%.
- **LightingCheck; Ultra captures at random places.** Run the taste-test comparison against licensed photographs (the 2026-11-10 taste-test harness) before and after.

### Stage 8: roadside furniture by the standards (about 3 days)

- Delineators, chevrons and curve signs from fitted curves; mile markers; guard-rail length of need and terminals; poles, lights, hydrants and mailboxes by the rules; snow poles.
- **Gate:** `lab/check-furniture.mjs`:
  - spacing and offsets within the MUTCD and DOT values;
  - no fixed object inside the clear zone unless shielded or breakaway;
  - chevron spacing by advisory speed;
  - rail length of need at least X(L_R).

**Order and dependencies:**

```
0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6
               \--------> 7   (independent after 2)
                     4 -> 8
```

Stages 0-4 give the owner the visible win: straight edges, real shoulders and ditches, curbs in town (inferred), and correct banking. They take about 15 days.

---

## 14. Costs

### Development

| Stage | Estimate |
|---|---|
| 0 | 1.5 d |
| 1 | 4 d |
| 2 (+2b) | 3 d (+2 d) |
| 3 | 2 d |
| 4 | 4 d |
| 5 | 4 d |
| 6 | 5 d |
| 7 | 3 d |
| 8 | 3 d |

Total about 30 developer-days (estimate). Stage 6 (junctions) carries the most risk; it is the stage most likely to overrun.

### Runtime (estimates; to be measured with QualityBench and RenderCost)

| Item | Estimate |
|---|---|
| Helper, per z16 tile | AF1 5-20 ms; profile solve 2-5 ms; section selection under 2 ms; template earthwork 10-30 ms, replacing today's bench loop at similar cost. Total +20-60 ms over today, off the main thread, cached. |
| Helper, first fetch per z14 group | Esri tag query reuses the lane-counts pattern: +1 request. Overpass (2b): +1 request, about 0.5-3 s, cached 28 days. |
| Tile JSON | +25-40% on `road_surface`: 13 fields per station instead of 7, plus profiles. A dense town tile goes from about 150 KB to about 200 KB for road_surface. |
| Game build, per tile | RoadsideMesh 20-60k vertices and a trimesh collider (+3-10 ms on the build worker); Kerbs full section 10-40k vertices in towns. Material ids add no draw calls (same terrain material); concrete curb and walk can share one material. |
| GPU | Material branching and procedural joints, wheel paths and cracks: about +0.1-0.3 ms at 1080p Ultra on the RTX 2070 Super Max-Q (estimate), well under the detail-engine budget. |
| Physics | One more trimesh collider per tile; curb colliders already exist on faaf27d. Expected cost is negligible against the vehicle substeps. Gate it with RideCheck timing. |

### Cache and data

- Bumping road_grade to v4 and adding the standards version to the cache key **rebuilds every cached tile** on first visit after the update. That is a one-time cost of about one helper run per tile.
- Bump the RoadLayout signature version (Stage 1).
- Survey data keyed on class reference widths stays valid because `referenceWidths` is unchanged.

### Licensing

- Federal and state sources need no licence.
- OSM data needs ODbL attribution in the credits; it is already present for the vector tiles. Add Esri's OSM replica and Overpass if used.
- If muv-osm code is ported rather than reimplemented, its modified files stay MPL-2.0 and need a NOTICE entry.

---

## 15. Honest limits

1. **Standards describe new construction, not the road that exists.** Many US roads predate the current Green Book (1950s-1970s alignments, 10 ft lanes, no shoulders, no spirals). A generator that applies 2026 values everywhere makes roads that are too regular. The era knob (6.3, step 8) is a heuristic. Only photographs or survey data can say what a particular road is really like.
2. **OSM does not carry most cross-section facts.** More than 90% of residential and service attributes and most shoulder, curb and parking facts are inferred (road-engineering.md 9.2). Inference picks a *plausible* section, not *the* section. A town street mapped without sidewalks will get a sidewalk if its context says so, and sometimes that is wrong.
3. **No traffic volumes.** Every ADT-keyed rule (shoulder width, edge-line warrants, clear zone, rumble strips) uses a class proxy, so a busy county road and a quiet one look the same.
4. **The centreline itself is uncertain.** OSM rural geometry, especially unedited TIGER, can be 5-10 m off. The fitter makes it smooth and design-consistent, not correct. Where the mapped line is wrong, the road is now *smoothly* wrong, and it may sit beside the road in the aerial photo. Photo alignment was rejected for geometry; a future surveyed-alignment source could fix this.
5. **Determinism and optimality pull against each other.** The locality rule (`FIT_REACH`, world-grid splits) gives up a truly global alignment fit. Very long sweeping curves may be split into two arcs of equal radius. This is invisible in practice but not ideal.
6. **The DEM is 10 m (USGS 1/3 arc-second where available) or coarser.** Template earthwork shapes the ground near the road but cannot recover real ditches, cut faces or terraces. A 0.6 m ditch is an invention, consistent with the standard but not observed.
7. **Mountain and desert templates are the weakest-sourced.** Rock-cut slopes, desert drainage practice and curve widening had no public numbers in this sweep (road-engineering.md 12).
8. **State variation.** Values are taken mainly from TxDOT, Caltrans, Iowa, WSDOT, FDOT and MoDOT. Texas concrete streets, Florida curbs and Iowa ditches differ in practice. Regional palettes (curb shape, gutter width, ramp dome colour) are deferred.
9. **The MUTCD 11th edition and its December 2025 revision** renumbered and may have changed some values. Several AASHTO numbers are known only through state reproductions. Everything flagged in road-engineering.md 12 needs verifying before it is relied on.
10. **Physics side effects.** A crowned, banked, curbed road changes how the car drives: crown pull, kerb strikes, gravel grip on shoulders. That is the point, but each stage needs the ride and handling gates. A visually right change that degrades "drives like the real car" must not ship.
11. **Checks prove conformance, not realism.** Every gate in section 13 measures agreement with the standard or internal consistency. Whether the result looks *indistinguishable from real* is judged only by the photograph taste test and the owner's eye, at random places.
12. **Junctions are the hardest part.** Complex OSM junctions (slip lanes, offset T-junctions, interchanges, mapped-as-areas) will produce failures that need case-by-case fallbacks. Stage 6 should ship with a "simple junction" fallback (today's behaviour) for anything its model rejects.

---

## 16. Decisions for the owner

1. **Merge roads/edge-authority faaf27d first (Stage 0)?** Recommended.
2. **Stage 2b's Overpass query.** It adds a third-party runtime dependency, with rate limits, for side-specific sidewalk, shoulder, kerb, parking and turn-lane tags. Recommended, cached for 28 days, with inference as the fallback.
3. **The era knob.** Should older, rougher, narrower roads appear by hash (recommended), or should every road be built to current standards?
4. **NLCD impervious raster** as a context input (Stage 2, optional): a new public download per region, adding precision to urban, suburban and rural classification.
