<!-- Road-engineering knowledge base for SimCruise. Written 2026-11-10 from five research sweeps (cross sections, alignment, urban streets and intersections, pavement, roadside furniture, OSM inference) and a code audit of main 6cadf05 and roads/edge-authority faaf27d. Companion design: road-standards-engine.md. -->

# How US public roads are designed and built: a knowledge base for the road generator

## 0. What this is and how to use it

SimCruise builds real US roads from OpenStreetMap while the game runs. The owner wants those roads to be impossible to tell apart from the real thing. To get there, the generator has to know what a road engineer knows: where a road's edge is and what lies beyond it, how wide each piece is, how a curve is laid out, how a corner is rounded, and what pavement looks like at ten years old.

This document collects that knowledge in a form a generator can use. Every rule has the same parts:

- the **numbers**, in US units (as the sources give them) plus metres for Godot;
- a **source tag** in square brackets, resolved in section 11;
- a **basis**:
  - *standard*: a design value from a public manual;
  - *derived*: computed from standard values;
  - *practice*: typical values found in agency or industry documents;
  - *heuristic*: our own inference, to be tested against photographs.

The design that turns this into code is in [road-standards-engine.md](road-standards-engine.md).

### Source policy

The generator may only use material we are allowed to use:

| Kind | Status | How we use it |
|---|---|---|
| US federal documents: MUTCD, FHWA reports and technical advisories, the PROWAG final rule (36 CFR 1190), 23 CFR, LTPP manual, EPA, DOE/LBNL | Public domain | Cite and paraphrase. Numbers used directly. |
| State DOT manuals (TxDOT, Caltrans, Iowa, WSDOT, MoDOT, FDOT, MnDOT, NCDOT, ODOT, etc.) and municipal standards | Public records | Cite, paraphrase, use the numbers. No long verbatim text. |
| AASHTO Green Book, AASHTO Roadside Design Guide, NCHRP 672 | Copyrighted | Not read directly. Numbers appear here only as reproduced in public state documents (Iowa I.M. 3.210, NCDOT barrier sheet, TxDOT, WSDOT). The secondary source is always the citation. |
| NACTO Urban Street Design Guide | Copyrighted | One number (downtown corner radius) cited as a fact, no text. |
| OSM wiki and taginfo | CC BY-SA / ODbL | Tag semantics paraphrased; OSM data needs ODbL attribution in the game credits. |
| osm2streets / osm2lanes (Apache-2.0), muv-osm (MPL-2.0), OSM2World (MIT) | Open source | Algorithms described; code may be ported with a NOTICE entry. |
| Streetmix | AGPL-3.0 | Numbers only, never code. |

Nothing below is copied text. Every passage is a paraphrase written for this project.

### The one-paragraph summary

A US road is a **template swept along a smooth curve**. The curve is made of tangents, circular arcs and clothoid spirals, and its sharpest radius follows from the design speed. The template is an ordered list of strips, each with a width, a cross slope and a material. On a rural road the list is: lane, edge line, paved shoulder, gravel or turf shoulder, rounded hinge, foreslope, ditch, backslope. On a town street it is: lane, sometimes a parking or bike lane, gutter pan, curb, planting strip, sidewalk. Every edge, line, curb and ditch is an exact offset of that one curve, so none of them can wobble on its own. The pavement colour comes from the material and its age, not from the photograph. The defects the owner sees (zig-zag edges, dirt on the asphalt, missing curbs, road the same colour as the desert) are all places where SimCruise breaks one of these principles.

---

## 1. Anatomy: what "the edge of the road" is

The edge is not one line. It is a sequence of named strips, and which ones exist depends on the **section type**.

### 1.1 Open section (rural, uncurbed): half-section from the centreline outward

```
 CL                                          hinge                         catch point
 |<-- lane 3.35-3.66 -->|<-paved sh.->|<-gravel->|\                      /|  (daylight)
 |  -2%                 |e  -2..-5%   |  -5..-8% | \ foreslope 1:4-1:6  / | backslope 1:3-1:4
 ======================= ============= ~~~~~~~~~~   \________________/    ~~~~~ natural ground
                        ^edge line     ^safety edge  ditch bottom (flat, 0.6-1.2 m)
                        (paint on the  (30 deg bevel)  0.6-1.5 m below hinge
                         shoulder side)
 |<----------------------------- clear zone (no fixed objects) ----------------------->|
```

- **Traveled way**: the lanes. Its outer boundary is the *edge of traveled way* (ETW). Most distances are measured from here, including clear zone, sign offset and delineator offset.
- **Edge line**: solid white, 4-6 in (0.10-0.15 m) [MUTCD11 3A.04]. Its inner edge sits on the ETW, so the paint lies on the shoulder side [MUTCD09 3B.07].
- **Paved shoulder**: asphalt continuing past the edge line, slope 2-5% away from the lanes [CT-HDM 302.2] [TX-RDM 4.10.3].
- **Unpaved shoulder**: gravel, crushed rock or turf, 4-8% (10% maximum) [TX-RDM 4.10.3].
- **Safety edge**: the asphalt ends in a 30-degree bevel, not a vertical drop [TX-RDM 4.10.14] [FHWA-SAFEEDGE].
- **Hinge point**: the break between shoulder and foreslope. It is rounded [CT-HDM 304.1].
- **Foreslope** (fill side), **ditch**, **backslope** (cut side), then the **catch point**, where the designed slope meets natural ground.
- **Clear zone**: a recovery area free of fixed objects, measured from the ETW (section 8.1).
- **Right-of-way (ROW) line**: the property line. Fences and utility poles sit near it.

### 1.2 Closed section (curbed town street): half-section from the centreline outward

```
 CL                          lip of     face of   back of                     ROW
 |<-- lane 3.05-3.35 -->|<-parking->|<-gutter->|curb|<-planting->|<-sidewalk->|frontage|
 |  -2%                 |  -2%      | -5..-8%  |0.15|   +2%      |  +1.5%     |
 ======================= =========== \_______/ |  |~~~~grass~~~~|============|~lawn~~~
                                     flow line ^vertical 6 in reveal
```

- The asphalt ends at the **lip of gutter**. The gutter pan, curb and sidewalk are concrete, a lighter material. That light band is the strongest visual cue for the edge of an American town street (section 7.1).
- Curbed low-speed streets usually have **no edge line**: the MUTCD lets the curb define the edge [MUTCD09 3B.07].
- The sidewalk top sits at curb-top height, 0.15 m above the gutter flow line [PROWAG R302] (practice).

### 1.3 Suspended or mountable section (suburban, 45 mph or more)

A rolled or sloped curb, 3-6 in high with a face of 2:3 or flatter, then turf. TxDOT allows curbs on suburban roads at 50 mph or more only if they are mountable [TX-RDM 4.10.12]. Caltrans avoids curbs at 40 mph or more except as its Table 303.1 allows [CT-HDM 303.1].

### 1.4 Why SimCruise's edges look wrong today (diagnosis, code audit 2026-11-10)

| Real road | SimCruise main 6cadf05 | Status on roads/edge-authority faaf27d |
|---|---|---|
| Edges are exact offsets of one smooth curve | Raw chord centre with a corner at every OSM vertex, paired with a normal smoothed over 16 m; lane markings use a third normal (helper/road-grade.mjs:217-256, game/RoadSurface.gd:77-87, game/LaneMarkings.gd:449) | Vertices filleted (at most 1 m), tangent from the fillet. No spirals, curvature still jumps. |
| Pavement extent set by design | A 1.875 m/texel aerial logistic classifier decides asphalt versus ground (game/terrain.gdshader:162-167, game/RoadEdges.gd:26-45) | Ribbon UV2 is the authority; the classifier no longer adds asphalt |
| Asphalt albedo about 0.10-0.14, always darker than desert soil | Brightness taken from aerial luminance and lifted up to 3.7x toward the surroundings (game/AerialPavement.gdshaderinc:19-29) | Unchanged |
| Town streets: curb, gutter, sidewalk | Only on non-streamed tiles (game/WorldTile.gd:435, :901) | Curb plus 18 in gutter only where a sidewalk is mapped (Kerbs.gd) |
| Crowned 2% section, sloped shoulders | Single planar section, e = 0 on tangents (RoadSurface.gd:83-85) | Unchanged |
| Foreslope 1:4-1:6, ditch | Bench, then batter clamps of 1.2:1 cut and 0.7:1 fill (road-grade.mjs:76, :520-548) | Unchanged |

---

## 2. Taxonomy: class x context x terrain (x surface, era)

A generator chooses a template from four things: what the road **is** (functional class), **where** it is (context), **what ground** it crosses (terrain), and what it is **made of** (surface).

### 2.1 Functional class

OSM highway tags map roughly onto FHWA functional classes. The mapping is approximate, following the OSM US guidance [OSM-US-CLASS] [FHWA-FC].

| OSM `highway` | FHWA class (typical) | Typical lane width, FHWA Tables 3-5/3-6 | Typical shoulder, outside / inside | AADT band, rural / urban [FHWA-FC] |
|---|---|---|---|---|
| motorway | Interstate / other freeway | 12 ft | 10-12 / 4-12 ft | Interstate 12,000-34,000 / 35,000-129,000 |
| trunk | Other freeway-expressway, other principal arterial | 11-12 ft | 8-12 / 0-6 ft | OFE 4,000-18,500 / 13,000-55,000 |
| primary | Other principal arterial (often a US route) | 11-12 ft | 8-12 / 0 ft | 2,000-8,500 / 7,000-27,000 |
| secondary | Minor arterial (often a state route) | 10-12 ft | 4-8 ft | 1,500-6,000 / 3,000-14,000 |
| tertiary | Major collector (county road) | 10-12 ft | 1-6 ft | 300-2,600 / 1,100-6,300 |
| unclassified | Minor collector / rural local | 10-11 ft | 1-4 ft | <750 (heuristic) |
| residential | Local | 8-10 ft (unmarked) | 0-2 ft | 15-400 |
| service | Driveways, alleys, parking aisles | 8-10 ft | 0 | n/a |
| track | Unpaved farm or forest road | n/a | 0 | n/a |
| *_link | Ramp | 12 ft (one lane) | 4 left / 8 right ft [CT-HDM T302.1] | n/a |

OSM has **no traffic volume**, but most design tables need it. The ADT proxy by class [heuristic, from FHWA-FC bands]:

| Class | ADT proxy |
|---|---|
| motorway, trunk | >6,000 |
| primary | rural 1,500-6,000; urban >6,000 |
| secondary | 1,500-6,000 |
| tertiary | 750-1,500 |
| unclassified, residential, track | <750 |

### 2.2 Context

TxDOT designs by five contexts: rural, rural town, suburban, urban, urban core [TX-RDM 5.1.2-8.1.2]. FDOT uses a similar set (natural, rural, rural town, suburban, urban general, urban centre, urban core) [FDOT-FDM 210]. For SimCruise, use five contexts:

| Context | Signals (any of) | Default section |
|---|---|---|
| rural | outside Census urban areas; NLCD 21 or 31-95 dominant; maxspeed of 50 mph or more; fewer than about 1 building per hectare | open |
| rural_town | `place=village` or `town` nearby, buildings along the road, maxspeed 25-40 mph, otherwise rural | closed on the main street, open elsewhere |
| suburban | NLCD 22 (20-49% impervious); 200-1,275 housing units per sq mi | closed on arterials and collectors; residential streets need curb evidence; mountable curb at 45 mph or more |
| urban | NLCD 23 (50-79%); 1,275 or more housing units per sq mi; maxspeed 40 mph or less | closed |
| urban_core | NLCD 24 (80% or more); `shop=*` frontage; buildings at the back of sidewalk | closed, sidewalk from curb to building, no grass strip |

**Sources:**
- Census 2020 urban areas: at least 2,000 housing units or 5,000 people. Initial core blocks have at least 425 housing units per sq mi, fill blocks at least 200, and a nucleus at least 1,275 [CENSUS-UA].
- NLCD developed classes by impervious share: 21 under 20%, 22 20-49%, 23 50-79%, 24 80% or more [NLCD].
- Existing SimCruise rule: a mapped limit of 40 mph or less means town; otherwise at least 15 buildings or 600 m of streets in the 250 m square (game/LaneMarkings.gd:87-100).
- Sidewalk prevalence: 62.6% of US adults report sidewalks on most streets near home, much lower in rural areas [NHIS].

### 2.3 Terrain

Classify terrain from the DEM slope over a 1-2 km window [KTC, after Green Book terrain classes]:

| Terrain | DEM slope |
|---|---|
| level | under 2% |
| rolling | 2-8% |
| mountainous | over 8% |

Terrain sets maximum grades (5.7) and pushes toward narrower sections, steeper backslopes and V-ditches.

### 2.4 Surface

US tagged values (Geofabrik taginfo, 2026-11) [TAGINFO]:
- About 19% of tagged residential ways and 53% of tagged unclassified ways are unpaved.
- Concrete is common on some interstates and on many Texas, Midwest and Plains city streets [FHWA-TA-JOINT] (practice).

Surface sets the crown slope (2% paved, about 4% gravel), the material and the markings (none on unpaved roads) (section 7).

### 2.5 Era (a knob the standards do not give)

Design manuals describe **new construction**. Many real roads are older:
- a 1930s state highway with 10 ft lanes, no shoulders and a 300 ft curve on a 55 mph road;
- a 1950s subdivision without sidewalks;
- a pre-2010 guard rail at 27-28 in instead of the MASH 31 in [FHWA-GR-HEIGHT].

A generator that applies 2026 standards everywhere will make roads look *too good*. Section 9.4 and the engine design treat era as a per-way deterministic variable.

---

## 3. Cross-section elements

### 3.1 Travel lanes

| Situation | Width | Source |
|---|---|---|
| Freeways, ramps, state highways | 12 ft (3.66 m) | [CT-HDM 301.1] [TX-RDM T8-1] |
| Conventional highway at 40 mph or less in town centres, few trucks | 11 ft minimum, 12 ft preferred | [CT-HDM 301.1] |
| TxDOT targets, local / collector / arterial | 12 ft rural, 11-12 rural town, 12 suburban, 11 urban, 11 urban core | [TX-RDM T5-1, T6-1, T7-1] |
| FDOT minimum by design speed, 25-35 / 40-45 / 50+ mph | suburban and urban: 10 / 11 / 12 ft; rural and rural town: 11 / 11 / 12 ft. Undivided 2-lane 2-way gets 12 ft. Freight routes (over 10% trucks) at least 11 ft. Never over 14 ft. | [FDOT-FDM T210.2.1] |
| Rural two-lane total traveled way, AASHTO 2018 via Iowa | Collectors: over 2,000 ADT 22 ft (24 with trucks), 400-2,000 22 ft, under 400 20 ft. Locals: over 2,000 22 ft, 400-2,000 20-22 ft, under 400 18 ft. | [IA-IM3210] |
| Two-way left-turn lane | 40 mph or less: 14 ft preferred, 11 minimum. 45-50 mph: 14 / 12. Over 50 mph: 16 / 14. Maximum 16 ft. FDOT uses TWLTLs only at 40 mph or less, about 1 ft wider than the through lanes. | [TX-RDM T4-22] [FDOT-FDM 210] |
| Absolute generator minimum | 2.75 m (9 ft) per lane | derived |

**Default lane width in metres (rural / urban)**, when OSM has no `width` [derived from FHWA-FC, TX-RDM, FDOT-FDM]:

| Class | Rural | Urban |
|---|---|---|
| motorway | 3.66 | 3.66 |
| trunk | 3.66 | 3.5 |
| primary | 3.6 | 3.35 |
| secondary | 3.5 | 3.3 |
| tertiary | 3.35 | 3.2 |
| unclassified | 3.0 | 3.0 |
| residential | 3.0 (unmarked) | 3.0 (unmarked) |
| service | 2.4-3.0 | 2.4-3.0 |

Keep the width piecewise constant along a way. Change it only with a taper of at least 1:50, or the MUTCD taper formula in 6.6.

### 3.2 Shoulders

| Facility | Paved shoulder | Source |
|---|---|---|
| Freeway, 4 lanes | left 5 ft, right 10 ft | [CT-HDM T302.1] |
| Freeway, 6+ lanes | 10 / 10 ft | [CT-HDM T302.1] |
| Rural freeway (TxDOT) | outside 12 ft; inside 8 ft (4-lane) or 12 ft (6+) | [TX-RDM T8-1] |
| Ramps | left 4, right 8 ft | [CT-HDM T302.1] |
| Multilane divided conventional | 4-lane 5/8 ft; 6+ lanes 8/8 ft | [CT-HDM T302.1] |
| Urban, 45 mph or less with curbed median | left 2, right 8 ft | [CT-HDM T302.1] |
| Two-lane new construction, 24 ft traveled way | 4 ft if ADT under 400, 8 ft if over 400; structural minimum 2 ft | [CT-HDM T307.2] |
| TxDOT rural collector | 10 ft above 2,000 vpd; 8 ft at 1,500-2,000; 6 ft at 400-1,500 (4 ft at 45 mph or less); 2 ft under 400 | [TX-RDM 6.1.7] |
| AASHTO via Iowa, rural collector / local | collector 6 / 4 / 2 ft and local 6 / 3 / 2 ft, for over 2,000 / 400-2,000 / under 400 ADT | [IA-IM3210] |

**Generator defaults, metres per side (outside / median)** [derived]:

| Class | Paved shoulder |
|---|---|
| motorway | 3.05 / 1.2 (3.05 inside on 6+ lanes) |
| trunk and rural primary | 2.4-3.05 / 0.9 |
| rural secondary | 1.8-2.4 / 0 |
| rural tertiary | 1.2 / 0 |
| rural unclassified | 0.6 / 0 |
| track | 0 |
| urban closed section | 0 (the curb replaces the shoulder) |

Real two-lane roads almost never end the asphalt exactly at the edge line. Even when there is "no shoulder", leave a 0.3-0.6 m paved strip beyond the line, then 0.6-1.8 m of gravel or turf to the hinge [derived from CT-HDM T307.2's 2 ft structural minimum].

### 3.3 Crown and cross slope

| Surface or element | Slope | Source |
|---|---|---|
| Traveled way, paved | 2% standard (1.5-3% when matching existing); TxDOT 2% recommended, 1% minimum, 3% maximum on tangent | [CT-HDM 301.3] [TX-RDM 4.10.4] |
| Unpaved road (gravel, treated earth) | 2.5-5%. FHWA gravel guide: straight-line crown about 4%, not over about 6% | [CT-HDM 301.3] [FHWA-GRAVEL 1.9] |
| Undivided road | crown on the centreline, uniform slope to each edge | [CT-HDM 301.3] |
| Divided road | may crown on a lane line (tent section). Algebraic difference at the crown: 4% maximum new, 6% rehab | [CT-HDM 301.3] |
| Right shoulder | 2-5% away from traffic; 5% common | [CT-HDM 302.2] |
| Shoulder by material | bituminous or concrete 2-6%; gravel 4-6%; turf about 8%; unsurfaced 10% maximum | [TX-RDM 4.10.3] |
| Lane-to-shoulder slope difference | 8% maximum (Caltrans, Iowa); 6% (TxDOT) | [CT-HDM 302.2] [IA-DM 2B] |
| Adjacent lanes | 5% maximum difference | [MO-EPG 230.1] |
| Urban crossover crown | 5-8% at 20 mph or less; 5-6% at 25-30 mph; 4-5% at 35 mph or more | [IA-DM 2B] |

Snow regions may continue the right shoulder in the lane plane, so snowplows do not catch a slope break [CT-HDM 302.2].

### 3.4 Edge line and centreline: placement and warrants

| Line | Warrant | Source |
|---|---|---|
| Edge line | **Required** on paved freeways, expressways and rural arterials with a traveled way of 20 ft or more and ADT of 6,000 or more. **Should** be used on rural arterials and collectors 20 ft or wider with ADT of 3,000 or more. **May** be omitted where curbs, parking or other markings define the edge. Not carried through intersections or major driveways; not broken at minor driveways. | Warrants [MUTCD09 3B.07]; intersection and driveway rules [MUTCD09 3B.06] (11th ed. renumbered, about 3B.08-3B.09) |
| Centreline | **Required** on urban arterials and collectors 20 ft or wider with ADT of 6,000 or more, and on any two-way road with 3 or more lanes. **Should** be used on urban arterials and collectors at 4,000 or more ADT, and on rural arterials and collectors 18 ft or wider at 3,000 or more. **May** be used on roads 16 ft or wider. | [MUTCD09 3B.01] |

Colour and width:
- Right edge: white.
- Left edge of divided roads, one-way roads and ramps: yellow.
- Normal line 4-6 in (0.10-0.15 m); wide line at least twice that.
- FHWA promotes 6 in edge lines as a safety countermeasure [FHWA-WIDE].

**Generator rule** [derived]:

| Situation | Rule |
|---|---|
| Edge line | Only on motorway, trunk and primary everywhere; on secondary in rural or suburban uncurbed contexts; on rural tertiary with lane width of 3.05 m or more. |
| Edge line width | 0.15 m on motorway, trunk and primary; 0.10-0.12 m elsewhere. |
| No edge line | Curbed sections; next to parking; residential, service and unclassified roads unless `lane_markings=yes`. |
| No centreline | Total width under 4.9 m (16 ft), or `lane_markings=no`. |
| Placement | Line offsets come from the same profile as the asphalt edge, so paint can never float off the pavement. |

### 3.5 Curbs

| Type | Geometry | Use | Source |
|---|---|---|---|
| Vertical (barrier) | Face about 1:4 batter, 4-8 in, 6 in typical (Caltrans A1-6, A2-6). 8 in only at 35 mph or less. TxDOT: 6 in or more counts as vertical. | Urban streets at 35 mph or less; next to sidewalks | [CT-HDM 303.2, T303.1] [TX-RDM 4.10.12] |
| Sloped (mountable) | Face 2:3 or flatter, 3-6 in (Caltrans B-6, B-4, B4 at 3 in). TxDOT: under 6 in, preferably 4 in or less. | B-6 at 40 mph; B-4 or D at 45 mph or more; subdivision streets | [CT-HDM T303.1] |
| FDOT Type F / Type E | F: 6 in vertical with a 2 ft gutter, outside edge at 45 mph or less. E: sloped, on the median side, and on both sides at 50-55 mph. | | [FDOT-FDM 210.5] |
| Curb end | Full height to zero over 3 ft | | [FDOT-I300] |

**Offset from the ETW to the face of curb:**
- TxDOT: 1-2 ft (2 ft in urban and suburban target tables).
- Caltrans: through lanes keep a curb offset at least as wide as the Table 302.1 shoulder; right-turn lanes may drop to 2 ft [TX-RDM 4.10.12] [CT-HDM 303.5].
- FDOT high-speed curbed: lip of gutter 4-6.5 ft from the traveled way [FDOT-FDM 210.5.1].

**Generator defaults** [derived]:

| Context | Curb |
|---|---|
| Closed section | 0.15 m reveal, 1:4 face (top 0.04 m back from toe), 0.15 m top, 0.02 m top fillet |
| Suburban, 45 mph or more | 0.10 m sloped curb, 2:3 face |
| Residential subdivision | 0.10 m rolled curb |
| Dropped curb (`kerb=lowered`/`flush`, driveways, ramps) | 0-0.03 m, ramped over 0.9 m (3 ft) |

### 3.6 Gutter pan

| Parameter | Value | Source |
|---|---|---|
| Width | 2 ft (0.61 m) typical, range 1-4 ft. SDDOT Type B: 24 in pan plus 6 in top, 32 in overall. MnDOT B618: 18 in overall. | [CT-HDM 303.2] [SDDOT-650] [MnDOT-B618] |
| Cross slope toward the curb | Caltrans about 8.33% (1:12); SDDOT about 5%; MnDOT 3/4 in per ft (6.25%) | same |
| Delineation-only gutters | In the pavement plane | [CT-HDM 303.2] |
| High side of a superelevated road | Gutter slope matches the pavement | [FDOT-I300] |
| Position | Curbs and gutters lie wholly outside the traveled way | [CT-HDM 303.2] |

**Generator default:** 0.45-0.61 m concrete, sloping 6% toward the curb, with a visible longitudinal joint at the asphalt seam [derived].

### 3.7 Sidewalks

| Parameter | Value | Source |
|---|---|---|
| Clear width, pedestrian access route | 48 in (1.22 m) minimum; 60x60 in passing spaces every 200 ft or less if narrower than 60 in | [PROWAG R302.2-R302.3] |
| Grade | 5% maximum, or match the adjacent street grade | [PROWAG R302.4] |
| Cross slope | 1:48 (2.1%) maximum | [PROWAG R302.5.1] |
| Changes in level | Up to 1/4 in vertical; 1/4-1/2 in bevelled at 1:2; over 1/2 in needs a ramp | [PROWAG R302.6] |
| Typical built widths | local and collector 5 ft; arterial 6-8 ft; commercial / CBD 8-12 ft (12 ft or more without a planter strip; add 2 ft at storefronts) | [FHWA-PEDSAFE] |
| TxDOT | 5 ft minimum, 6 ft in busier areas, about 8 ft downtown. Curb-attached with no buffer: 6 ft minimum. Commercial: 8 ft. | [TX-RDM 19.3] |
| FDOT by context | rural 5 ft; rural town, suburban and urban general 6 ft (up to 8); urban centre 10 ft; urban core 12 ft | [FDOT-FDM T222.2.1] |
| Joints | Transverse control joints about every 5 ft (1.5 m panels) | practice |

**Where sidewalks exist** [FHWA-PEDSAFE] [FDOT-FDM 222.2.1]:
- **Commercial urban streets:** both sides.
- **Urban local streets:** both sides when over 4 dwelling units per acre.
- **Suburban highways at 1-4 du/acre:** both sides.
- **Rural highways:** shoulders instead, at least 4-5 ft.
- **FDOT curbed roads:** a sidewalk on every curbed roadway, except short isolated pieces with no connecting pedestrian facility.
- **Flush-shoulder roads that do have sidewalks:** the walk sits near the ROW line or outside the clear zone, never against the shoulder.
- **Bridges:** sidewalks continue across.

### 3.8 Buffer, planting strip, border and parking

| Element | Value | Source |
|---|---|---|
| Planting strip (buffer) | local and collector 2-4 ft; arterial 4-6 ft; 6 ft ideal | [FHWA-PEDSAFE] |
| TxDOT buffer | at least 4 ft from the face of curb (6 ft or more desirable, more above 35 mph). Uncurbed roads: at least 10 ft from the edge of pavement. | [TX-RDM 19.3] |
| FDOT border (lip of gutter to ROW) | 12 ft at 25-40 mph, 14 ft at 45 mph and in urban core; 8 ft absolute minimum; subtract 2 ft with a bike lane | [FDOT-FDM T210.7.1] |
| TxDOT border | local 10 ft, collector 12 ft or more, arterial 8 ft minimum (12 ft or more preferred); target tables 10-20 ft | [TX-RDM 4.10.9] |
| Parking lane | TxDOT local 9 ft, collector and arterial 10 ft (12 ft next to a bike lane), 7-9 ft typical; MUTCD figure shows 8 ft | [TX-RDM 4.10.12] [MUTCD11 3B.27] |
| Parking setback from junctions | at least 20 ft from a crosswalk; 30 ft on STOP, YIELD or signal approaches. FDOT larger by speed and control (for example 90 ft upstream of an unsignalized approach under 35 mph). | [MUTCD11 3B.27] [FDOT-FDM T212.11.2] |
| On-street parking | only at posted speeds of 35 mph or less | [FDOT-FDM 210.2.3] |
| Lateral offset to objects behind a curb | 1.5 ft minimum; 3 ft at intersections and driveways | [IA-DM 8A-2] |

**Generator defaults** [derived]:

| Element | Default |
|---|---|
| Planting strip | 1.2 m residential; 1.5-1.8 m arterials; 0 in urban_core (tree grates every 7-9 m instead) |
| Parallel parking | 2.4 m on arterials; 2.1-2.4 m on residential streets; 45-degree angle parking about 5.5 m deep (heuristic) |

### 3.9 Bike lanes

| Topic | Rule | Source |
|---|---|---|
| Widths (FDOT) | New construction: 7 ft buffered lane. Retrofit order: 7 ft buffered, 6 ft buffered, 5 ft, 4 ft. No lane if under 4 ft. Next to parallel parking: 4 ft lane plus 3 ft buffer. | [FDOT-FDM 223.2] |
| Markings | Lane line 6 in solid white. Bike symbol after each major junction. | [MUTCD11 9E] |
| Before right-turn conflicts | The line turns dotted (2 ft line, 6 ft gap) over 50-200 ft | [MUTCD11 9E] |
| Paved shoulder as a bike facility | Must be at least 4 ft | [FDOT-FDM 223] |

**Generator default:** 1.5 m (1.8 m at 40 mph or more) [derived].

### 3.10 Foreslope, backslope and hinge (open sections)

| Rule | Value | Source |
|---|---|---|
| Caltrans fill slope | 4:1 or flatter for new work; 6:1 suggested for high speed; 10:1 desirable. Round the top and toe. A uniform catch point at least 18 ft from the shoulder edge where slopes would catch closer. 10 ft (15 ft preferred) between the catch point and ROW. | [CT-HDM 304.1] |
| TxDOT front slope | 6:1 or flatter ideal; 4:1 acceptable; 3:1 constrained; 2:1 for bridge headers and ditch sides. Back slope 4:1 or flatter typical. 10:1 between the road and a guard rail. | [TX-RDM 4.10.13] |
| Recoverability | 4:1 or flatter is recoverable; steeper than 4:1 to 3:1 is traversable but not recoverable; steeper than 3:1 is critical (rollover) | [IA-DM 8A-2] |
| Low-volume foreslope | 4:1 over 1,500 ADT; 3:1 for lower volumes | [IA-IM3210] |

**Generator defaults** [derived]:

| Element | Default |
|---|---|
| Foreslope | motorway and trunk 1:6; primary and secondary 1:4; tertiary and unclassified 1:3; track 1:2 |
| Backslope | 1:3 in cut (1:2 in rock or mountainous terrain) |
| Hinge, toe and catch | Rounded with a 1.2-3 m parabolic fillet |
| Catch-point offsets | Low-pass filtered along the road over at least 20 m, so cut and fill boundaries do not lobe |

### 3.11 Ditches

| Rule | Value | Source |
|---|---|---|
| Shape | Flat-bottom ditches preferred; V-ditches erode more and suit restricted sites | [IA-DM 3G-1] |
| Normal rural ditch | 5 ft deep and 10 ft wide from profile grade. Median ditch 4 ft deep. Minimum desirable depth 3 ft, minimum acceptable 2 ft. | [IA-DM 3G-1] |
| Ditch grade | 0.2-1.0% minimum; 1-3% desirable. TxDOT minimum profile grades: 0.5% unpaved ditch, 0.3% curbed, 0.25% lined. | [IA-DM 3G-1] [TX-RDM T6-1] |
| Depth | Ditch bottom at least 6 in below the subgrade crown | [TX-RDM 4.10.13.5] |
| Trapezoidal side ditches | Side slopes 1V:3H to 1V:8H, bottom 4 ft or more | [TX-RDM 4.12.7] |

**Generator default** [derived]: bottom 0.6-1.5 m below the shoulder edge (0.9 m secondary and tertiary, 1.5 m trunk); flat bottom 1.2 m (0.6 m on local roads; V on tracks and in mountains). Drop the ditch where the road is on a fill higher than the ditch depth. Shade the ditch with grass, not asphalt or dirt.

### 3.12 Pavement edge (safety edge, dike, drop-off)

| Topic | Rule | Source |
|---|---|---|
| Safety edge | A 1V:1.75H (about 30 degree) wedge at the asphalt edge, built during paving | [TX-RDM 4.10.14] [FHWA-SAFEEDGE] |
| Where Caltrans uses a tapered edge | On all pavement edges, except next to curbs, dikes, guard rail or walls, and within 3 ft of driveways and intersections | [CT-HDM 302.3] |
| Asphalt dikes | Type C 2 in, Type E 4 in (preferred), Types A and D 6 in | [CT-HDM 303.3] |
| Drop-off | FHWA advises keeping drop-offs at 2 in or less on high-speed roads; 2.5 in or more is a problem at 55-60 mph. With a safety edge, drops up to about 5 in are traversable. Shoulder material starts flush, then settles, erodes and wears. | [FHWA-SAFEEDGE] |
| Edge cracking | LTPP edge cracking lies within 0.6 m of the edge, and only where shoulders are unpaved | [FHWA-LTPP ACP 3] |

---

## 4. Typical sections (templates)

Each template is a **half-section from the centreline outward**:
- Widths in metres; slopes signed, negative falling away from the crown.
- Mirrored for the other side unless noted.
- Values are defaults; OSM tags override individual elements (section 9).
- "Lane slope -2%" means the normal crown. On curves, the lanes rotate to `e` (section 5.3) and the shoulders follow the rollover limits in 3.3.

### T1. Rural two-lane, gravel shoulder and ditch (county road, 400-2,000 ADT, 45-55 mph)

Basis: AASHTO 2018 rural local and collector values via [IA-IM3210]; ditch from [IA-DM 3G-1]; slopes from [TX-RDM 4.10.3, 4.10.13].

| # | Element | Width | Slope | Material |
|---|---|---|---|---|
| 1 | lane | 3.05-3.35 | -2% | asphalt or chip seal |
| 2 | edge line (if lane is 3.05 m or more and the road is tertiary) | 0.10 | — | white paint, on the shoulder side |
| 3 | paved strip | 0.3 | -2% | asphalt |
| 4 | safety edge | 0.09 (horizontal run of a 50 mm bevel at 30 degrees) | 30 deg | asphalt |
| 5 | gravel shoulder | 0.6-0.9 | -5% | crushed rock |
| 6 | hinge rounding | 1.2 | parabolic | turf |
| 7 | foreslope | to ditch | 1:3 (under 1,500 ADT) to 1:4 | mown turf |
| 8 | ditch bottom | 0.6-1.2 flat, 0.6-0.9 below hinge | 0 across, grade 0.3% or more along | grass |
| 9 | backslope | to catch | 1:3 | turf, then native |

- Clear zone: 3.0-4.3 m (section 8.1).
- No centreline under 4.9 m total width.

```
CL  lane 3.2      .3 .6        1:3            ditch     1:3
|===============|==|..|\__                  ________   __/~~~~
                         \______________/        \_/
```

### T2. Rural two-lane state highway, paved shoulder, rumble strips (over 400 ADT, 55-65 mph)

Basis: [CT-HDM 307.2]: 12 ft lane and 8 ft shoulder, 40 ft roadbed. Rumble strips from [FHWA-RUMBLE] [MnDOT-RS]; slopes from [CT-HDM 302.2, 304.1].

| # | Element | Width | Slope | Material |
|---|---|---|---|---|
| 0 | centreline rumble strip (undivided, 50 mph or more) | 0.30-0.36 centred on the CL | — | milled, with the yellow lines painted over it |
| 1 | lane | 3.66 | -2% | asphalt |
| 2 | edge line | 0.15 | — | white, 6 in |
| 3 | paved shoulder | 2.4 | -4% (lane-to-shoulder difference 8% or less) | asphalt, often a slightly different tone with a seam at the edge line |
| 3a | shoulder rumble strip | 0.41 across x 0.18 along, 0.3 m pitch, 12-13 mm deep, inner edge 0.15 m outside the edge line | — | milled |
| 4 | safety edge | 0.09 | 30 deg | |
| 5 | gravel shoulder | 0.6 | -5% | |
| 6 | hinge rounding | 1.5-3 | | |
| 7 | foreslope | | 1:4 (1:6 preferred at 60 mph or more) | mown turf |
| 8 | ditch | 1.2 flat, 0.9-1.5 deep | | grass |
| 9 | backslope | | 1:3 to 1:4 | |

- Same road under 400 ADT: 4 ft (1.2 m) shoulders, 32 ft roadbed [CT-HDM 307.2].
- Furniture: delineators at about 161 m on tangents (section 8.5); RPMs on the centreline in some states.
- Shoulder rumble strips are omitted on shoulders narrower than about 4-6 ft, and broken at driveways and junctions [FHWA-RUMBLE] [MnDOT-RS].

### T3. Suburban collector: curb and gutter, planting strip, sidewalk (25-40 mph)

Basis: [TX-RDM T6-1]; [FHWA-PEDSAFE] buffer and sidewalk; [SDDOT-650] gutter; [CT-HDM T303.1] curb.

| # | Element | Width | Slope | Material |
|---|---|---|---|---|
| 1 | lane | 3.35 | -2% | asphalt |
| 2 | bike lane (optional) | 1.5 | -2% | asphalt, white line |
| 3 | gutter pan | 0.61 | -6% toward the curb | concrete |
| 4 | curb | face 0.15 reveal, top 0.15 | vertical (1:4) | concrete |
| 5 | planting strip | 1.2-1.8 | +2% | mown turf, street trees at least 0.45 m behind the curb face |
| 6 | sidewalk | 1.5-1.8 | +1.5% (2.1% max) | concrete, 1.5 m panels |
| 7 | frontage | 0.3 | +2% | turf, then private lawn or driveway |

- No edge lines.
- Centreline on collectors with 4,000 or more ADT (MUTCD "should"); otherwise optional.
- Corner radius 7.6 m (section 6.1).
- At 45 mph or more, use a 0.10 m mountable curb [TX-RDM 4.10.12].

```
CL  lane 3.35   gut  curb  grass 1.5   walk 1.6
|=============|\___|""|~~~~~~~~~~~~|==========|~~~
```

### T4. Urban arterial (4 lanes, 30-45 mph)

Basis: [TX-RDM T7-1]; [FDOT-FDM T210.2.1, T210.3.1].

| # | Element | Width | Notes |
|---|---|---|---|
| 0 | median: raised 4.7 m (35 mph or less) or 6.7 m (40-45 mph), or a TWLTL of 3.66-4.27 m | half of it | Raised median: 0.15 m curb, yellow nose markings. TWLTL: yellow solid plus broken lines. |
| 1 | inner lane | 3.35 (3.66 at 50 mph) | -2% |
| 2 | outer lane | 3.35-3.66 | -2% |
| 3 | parking 2.4-3.05 or bike lane 1.5 | | parking only at 35 mph or less |
| 4 | gutter | 0.61 | concrete |
| 5 | curb | 0.15 vertical (FDOT Type F) | |
| 6 | buffer | 1.2-1.8 | trees in grates or a grass strip |
| 7 | sidewalk | 1.8-2.4 | concrete |
| 8 | border to ROW | total about 6.1 m (20 ft) from the curb | |

- Corner radius 9.1-15 m.
- Urban-core variant: lanes 3.05-3.35, parallel parking 2.4, a back-of-curb sidewalk 3.0-3.7 m wide with tree grates every 7-9 m, corner radius 3.0-4.6 m, bulb-outs at crosswalks.

### T5. Residential local street (25 mph)

Basis: [TX-RDM T5-1]; [FDOT-FDM T210.2.1]; [FHWA-PEDSAFE].

| # | Element | Width | Notes |
|---|---|---|---|
| 1 | travel (unmarked) | 3.05 | -2%, no centreline |
| 2 | parking (one or both sides) | 2.1-2.4 | only if the face-to-face width allows |
| 3 | gutter | 0.45 | concrete |
| 4 | curb | 0.10 rolled (subdivision) or 0.15 vertical (older grid) | |
| 5 | planting strip | 1.2 | grass, street trees |
| 6 | sidewalk | 1.5 | concrete |

- Face-to-face 8.5-11 m (28-36 ft) [derived from FDOT, TxDOT].
- Corner radius 6.1 m.
- About 1 in 4 post-1950 suburban local streets has no sidewalk (heuristic, seeded per way).

### T6. Rural interstate (per carriageway)

Basis: [TX-RDM T8-1, 4.10.15]; [CT-HDM T302.1].

| # | Element | Width | Slope |
|---|---|---|---|
| 0 | median (depressed, with ditch) | 14.6-23.2 m total (48-76 ft) | 1:6 foreslopes |
| 1 | inside shoulder | 1.2 (4 lanes) / 3.05 (6+) | -4%, yellow edge line, often with rumble strip |
| 2 | lanes (2-3) | 3.66 each | -2%; a 3-lane carriageway may crown on a lane line |
| 3 | outside shoulder | 3.05-3.66 | -4%, white 6 in edge line, rumble strip |
| 4 | gravel or turf shoulder, hinge | 0.6-1.0 | |
| 5 | foreslope | | 1:6 |
| 6 | ditch, backslope | | 1:4 |

- Clear zone: 9.1 m (30 ft) [CT-HDM 309.1] [TX-RDM T4-20].
- ROW fence on the access-control line [ODOT-HDM400 407].
- Urban freeways replace the depressed median with a concrete barrier (F-shape or single slope, 32 or 42 in) (practice).

### T7. Mountain road (two-lane, mountainous terrain, 25-45 mph)

Basis: grades [WS-DM 1220]; shoulders [IA-IM3210] low-volume values; slopes and ditches [TX-RDM 4.10.13] and [IA-DM 3G-1]; guard rail warrants [NCDOT-BD].

| # | Element | Width | Notes |
|---|---|---|---|
| 1 | lane | 3.05-3.35 | crowned on tangents; superelevated up to 8% (6% in snow states) on curves |
| 2 | paved shoulder | 0-0.6 | |
| 3 cut side | V gutter or ditch | 0.6-0.9 deep | 1:4 to 1:2 sides |
| 4 cut side | backslope | | 1:2 in soil. Rock cuts are far steeper, often near vertical. (No public number was found in this sweep; heuristic.) |
| 3 fill side | foreslope | | as steep as 1:2 with a guard rail where the drop exceeds about 2 m inside the clear zone |
| 4 fill side | guard rail (MGS) | rail face about 0.6 m beyond the shoulder edge | section 8.2 |

- Grades up to 10% (25 mph) or 7% (45 mph) on rural roads in mountainous terrain [WS-DM T1220-2].
- Curves below the design-speed minimum radius get an advisory speed and curve warning signs (section 8.6).
- Curve widening on sharp curves is standard practice, but this sweep did not capture the widening amounts. Flagged for follow-up.
- Snow poles above about 1,500 m in western states (section 8.13).

### T8. Desert highway (rural two-lane, arid, level or rolling)

**Basis:** T2 for the paved parts. There is **no single public "desert" design standard**; the arid-specific items below are heuristics, labelled as such.

| # | Element | Width | Notes |
|---|---|---|---|
| 1 | lane | 3.66 | -2%; aged asphalt albedo 0.10-0.14, or chip seal 0.12-0.20 on low-volume roads [LBNL-49283] |
| 2 | edge line | 0.10-0.15 | |
| 3 | paved shoulder | 1.2-2.4 (by class) | -4% |
| 4 | safety edge, then native or gravel shoulder | 0.6-1.2 | flush, light tan |
| 5 | foreslope | | 1:6 or flatter (cheap earthwork in sand; heuristic) |
| 6 | swale | broad and shallow, 0.3-0.6 m, or none | heuristic: little runoff |

- **Washes:** dip crossings where washes cross. The MUTCD has a DIP warning sign, W8-2; frequency is a heuristic.
- **Clear zone:** bladed or bare, free of shrubs inside the clear zone (heuristic from [FHWA-VEG] zones).
- **Colour:** the asphalt must read clearly **darker** than the desert soil (soil albedo about 0.25-0.35; a literature range, not from EPA-CP, still to be sourced, see 12). This is the fix for "road and desert the same colour".

### T9. Gravel road

Basis: [FHWA-GRAVEL]; widths from [IA-IM3210].

| # | Element | Width | Notes |
|---|---|---|---|
| 1 | traveled way | 2.75 each side (5.5 m total under 400 ADT); 3.0-3.35 on busier county roads | straight-line crown 4%, never parabolic |
| 2 | shoulder | flush with the road edge | no high shoulder (berm) and no drop-off |
| 3 | foreslope | | 1:3, or straight from the road edge on narrow ROW |
| 4 | V-ditch | 0.6-0.9 deep | |
| 5 | backslope | | 1:2-1:3 |

- **Surface look:**
  - two compacted wheel tracks 0.4-0.6 m wide, about 1.5 m apart;
  - a loose windrow on the crown and edges;
  - washboard of 0.5-0.9 m wavelength within 30-60 m of stops and junctions (empirical, unsourced).
- **Albedo** is local aggregate or soil, 0.2-0.35.
- **No markings.**

---

## 5. Alignment: the curve the template is swept along

### 5.1 Design speed

- **Rural highways:** design speed is usually posted plus 5 mph.
- **Low-speed urban streets (45 mph or less):** design speed equals posted.
- **Speed regimes:** TxDOT and Iowa switch method at 45 mph. At 45 mph or less, superelevation follows distribution Method 2; above it, Method 5 [IA-DM 2B] [TX-RDM 4.7].

**Untagged defaults, mph** (rules of thumb from US statutory practice, *heuristic*):

| Class | Default |
|---|---|
| motorway | 65-70 rural / 55-60 urban |
| trunk | 55-65 / 45 |
| primary | 55 / 35-45 |
| secondary | 45-55 / 30-35 |
| tertiary | 45 / 30 |
| unclassified | 35-45 / 25 |
| residential | 25 |
| living_street | 15 |
| service | 15 |
| link | 35-45 |
| track | 15-25 |

### 5.2 Minimum radius

The point-mass formula: R = V^2 / (15(e+f)) in feet and mph, or R = V^2 / (127(e+f)) in metres and km/h. Side friction factors f_max come from [IA-DM T2B.1] and [WS-DM 1250], and derive from the Green Book.

| V mph | 15 | 20 | 25 | 30 | 35 | 40 | 45 | 50 | 55 | 60 | 65 | 70 | 75 | 80 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| f_max | .32 | .27 | .23 | .20 | .18 | .16 | .15 | .14 | .13 | .12 | .11 | .10 | .09 | .08 |
| R_min, e_max 6%, m | 12 | 25 | 44 | 70 | 104 | 148 | 196 | 254 | 323 | 405 | 506 | 622 | 762 | 930 |
| R_min, e_max 8%, m (derived from ft) | 11.6 | 23.2 | 40.8 | 65.2 | 95.7 | 135.3 | 178.9 | 231.0 | 292.6 | 365.8 | 451.1 | 551.7 | 673.6 | 813.8 |

Low-speed urban streets (Method 2) [IA-DM 2B]:
- **Radius at e = 4%, m:** 15 mph 12.8; 20 26.2; 25 46.9; 30 76.2; 35 113.1; 40 162.5; 45 216.7.
- **Radius at e = 0%, m:** 25 mph 55.2; 30 91.4; 35 138.4; 40 203.3; 45 274.3.

**Maximum superelevation by agency** [TX-RDM 4.7] [IA-DM 2B]:
- TxDOT normally uses 6%.
- Iowa allows 8%, uses 6% for new construction, and usually limits urban roads to 4%.

**Generator e_max by context:**
- rural 8%;
- snow states 6%;
- urban and suburban high-speed 4%;
- low-speed urban 4% or 0.

Where OSM geometry really requires a smaller radius (switchbacks, loops, roundabouts), keep the geometry and **lower the local design speed** to V = sqrt(127 R (e+f)). That speed then drives signs and crossfall.

### 5.3 Superelevation rate

Method 5 (high speed) [TX-RDM T4-5 to T4-7] [IA-DM 2B]:
- At large radius the road keeps its normal crown (NC).
- At moderate radius it uses a reverse crown (RC): the whole section tilted at the crown slope.
- Below that the rate rises toward e_max.
- Between the NC and RC radii use NC; do not interpolate. Round e up to the next 0.2%.

| V mph | 15 | 20 | 25 | 30 | 35 | 40 | 45 | 50 | 55 | 60 | 65 | 70 | 75 | 80 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| R at or above which NC applies, e_max 6%, ft | 868 | 1580 | 2290 | 3130 | 4100 | 5230 | 6480 | 7870 | 9410 | 11100 | 12600 | 14100 | 15700 | 17400 |
| R for e = 4%, e_max 6%, ft | 151 | 309 | 511 | 766 | 1070 | 1440 | 1840 | 2300 | 2810 | 3390 | 3950 | 4550 | 5220 | 5950 |
| R for e = 4%, e_max 8%, ft | 277 | 490 | 729 | 1030 | 1370 | 1770 | 2220 | 2720 | 3270 | 3890 | 4450 | 5050 | 5710 | 6420 |

- With e_max 8%, NC applies from 932 ft (15 mph) up to 17,800 ft (80 mph).
- With e_max 4% at 50 / 55 / 60 mph:
  - NC from 7,220 / 8,650 / 10,300 ft;
  - RC from 4,940 / 5,950 / 7,080 ft;
  - e = 3% at 2,290 / 2,860 / 3,530 ft;
  - e = 4% at 926 / 1,190 / 1,500 ft.
- The ratio of RC radius to NC radius is about 0.69 at these speeds. This is *derived*, and is used only to fill the e_max 6% and 8% RC rows until the full TxDOT tables are transcribed.

Low-speed urban streets with curbs usually keep NC (Method 2), because driveways, cross streets and stops limit the tilt [IA-DM 2B].

### 5.4 Superelevation transitions

Two lengths matter [TX-RDM T4-8 to T4-10] [IA-DM 2B]:
- **Runoff L_r** rotates the section from 0 to the full e.
- **Tangent runout L_t** removes the adverse crown, from -NC to 0.

The formulas:
- L_r = (w x n1 x e_d / Delta) x b_w;
- L_t = (e_NC / e_d) x L_r.

| V mph | 15 | 20 | 25 | 30 | 35 | 40 | 45 | 50+ |
|---|---|---|---|---|---|---|---|---|
| Max relative gradient Delta, % | 0.89 | 0.80 | 0.73 | 0.67 | 0.62 | 0.57 | 0.53 | 0.50 |

**Multilane factor b_w:**

| Lanes rotated | b_w |
|---|---|
| 1 | 1.0 |
| 1.5 | 0.83 |
| 2 | 0.75 |
| 2.5 | 0.70 |
| 3 | 0.67 |
| 3.5 | 0.64 |
| 4 | 0.63 |
| 4.5 | 0.61 |
| 5 | 0.60 |

**Share of the runoff placed on the tangent:**
- At 15-45 mph: 0.80 / 0.85 / 0.90 for 1 / 1.5 / 2-3.5 lanes.
- At 50-80 mph: 0.70 / 0.75 / 0.80 / 0.85.
- Iowa's standard is 70/30.
- With spirals, the spiral length equals the runoff length [MO-EPG 230.1].

**Worked example:** a two-lane road, 3.6 m lanes, rotated about the centreline, e = 6%, 50 mph or more. L_r = 3.6 x 0.06 / 0.005 = 43 m and L_t = 14 m.

**Build e(s) as a piecewise-linear ramp, never a Gaussian blur.** The current game blurs e with a sigma of 16 m (helper/road-grade.mjs:250-251), so the bank starts and ends in the wrong places.

### 5.5 Spirals (clothoids)

In a clothoid, curvature changes linearly with distance, so the driver's steering input ramps continuously [KTC-6.8] [MO-EPG 230.1].
- Spirals are common at 60 mph and above, and recommended wherever feasible.
- Traditional desirable length: 2.0 s of travel at design speed.
- Comfort minimum: L = 0.0214 V^3 / (R C), with V in km/h, R in metres, and C = 0.3-0.9 (up to 1.2) m/s^3.
- Clothoid parameter A = sqrt(R L).
- MoDOT requires spirals where ADT exceeds 400 and the radius is below its standard-plan threshold.
- Green Book offset limits are cited only after verification: L_min = sqrt(24 x 0.20 R) and L_max = sqrt(24 x 1.0 R) in metres. They were *not verified* this session against a public copy; treat them as a heuristic until checked.

**Generator rule:** for V of 45 mph or more, L_s = clamp(max(2.0 s x v, 0.0214 V^3 / (1.2 R)), sqrt(4.8 R), sqrt(24 R)). Shift the arc inward by p = L_s^2 / (24 R). Low-speed streets use plain arcs, but they still need G1 continuity (no heading jumps).

### 5.6 Curve-sequence consistency (the anti-zig-zag rules)

These rules come from [TX-RDM 4.7.1] [IA-DM 2A] [MO-EPG 230.1] [NE-HA] [MassDOT-PDDG 4]:

| Rule | Value |
|---|---|
| Compound curves | R_large no more than 1.5 x R_small; up to 2 x at intersections, loops and ramps |
| Broken-back curves (same direction, short tangent between) | Avoid. TxDOT recommends a tangent of at least 15 V ft (about 4.6 V m, V in mph); otherwise use one curve. A recommendation, not a design-exception trigger. |
| Reverse curves | The tangent between them must hold both tangent-side runoff portions (Iowa: the sum of 70% of each curve's runoff); otherwise join them with back-to-back spirals |
| Minimum curve length | 15 V ft on two-lane highways and expressways; 30 V ft on fully access-controlled roads and interstates, never below 15 V where 30 V cannot be reached [IA-DM 2A]. For small deflections: 500 ft at 5 degrees, plus 100 ft for each degree less (Green Book rule as reproduced in state manuals; not re-checked this session). |
| Deflection that needs no curve | TxDOT 0.5 deg (high speed) / 1 deg (low speed); Nebraska 0.5 / 1; MoDOT 10 minutes (none needed at 1 deg or less when ADT is under 400); Iowa about 1.5 deg on two-lane roads and expressways, none allowed on interstates and access-controlled roads [IA-DM 2A] |

**What this means for OSM data.** OSM position noise of 1-10 m shows up as 1-3 degree kinks. Under these rules a kink below the no-curve threshold is a **straight**, not a curve.

The generator applies the rules in this order:
1. Delete sub-threshold deflections.
2. Merge same-direction curves separated by less than 4.6 V m.
3. Treat a sign reversal without room for two runoffs as noise, unless both deflections are large (over about 10 degrees and longer than the minimum curve length).
4. Lengthen short arcs to the minimum curve length.
5. Enforce the 1.5 compound ratio.

### 5.7 Vertical alignment

Vertical curves are parabolas with L = K x A, where A is the algebraic grade difference in percent [TX-RDM T4-12] [WS-DM 1220].

| V mph | 15 | 20 | 25 | 30 | 35 | 40 | 45 | 50 | 55 | 60 | 65 | 70 | 75 | 80 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Crest K, ft/% | 3 | 7 | 12 | 19 | 29 | 44 | 61 | 84 | 114 | 151 | 193 | 247 | 312 | 384 |
| Sag K, ft/% | 10 | 17 | 26 | 37 | 49 | 64 | 79 | 96 | 115 | 136 | 157 | 181 | 206 | 231 |
| Crest K, m/% (derived) | 0.9 | 2.1 | 3.7 | 5.8 | 8.8 | 13.4 | 18.6 | 25.6 | 34.7 | 46.0 | 58.8 | 75.3 | 95.1 | 117.0 |
| Sag K, m/% (derived) | 3.0 | 5.2 | 7.9 | 11.3 | 14.9 | 19.5 | 24.1 | 29.3 | 35.1 | 41.5 | 47.9 | 55.2 | 62.8 | 70.4 |

**Geometry.** Profile curvature is 1/R_v = 1/(100 K), with K in m/%. For example, a 60 mph crest needs R_v of 4,600 m or more. Offsets: y = A x^2 / (200 L); middle ordinate E = A L / 800.

**Length rules:**
- Preferable minimum length: 3 V ft.
- For appearance, WSDOT suggests 2-3 times the stopping-sight length.
- Zero-length vertical curves only where A is 1.0% or less (45 mph or less) or 0.5% or less (50 mph or more).
- On curbed roads, K over 167 ft/% needs drainage care.

**Maximum grades, % (WSDOT Exhibit 1220-2):**

| Road type | Terrain | Grades by speed |
|---|---|---|
| Rural, 25-80 mph | Level | 5/5/5/5/5/4/4/3/3/3/3/3 |
| | Rolling | 8/7/7/6/6/5/5/4/4/4/4/4 |
| | Mountainous | 10/9/8/8/7/7/6/6/5/5/5/5 |
| Suburban and urban, 20-60 mph | Level | 8/7/7/7/7/6/6/5/5 |
| | Rolling | 10/10/9/8/8/7/7/6/6 |
| | Mountainous | 13/12/11/10/10/9/9/8/8 |
| Interstate, 50-80 mph | Level | 4/4/3/3/3/3/3 |
| | Rolling | 5/5/4/4/4/4/4 |
| | Mountainous | 6/6/6/5/5/5/5 |

**Other grade rules:**
- TxDOT: urban local streets 8% in all terrain.
- Allowances: +1% for one-way downgrades shorter than 500 ft; +1% in urban areas; +2% on low-volume rural roads [KTC summary].
- Minimum grade on curbed roads: 0.3%.

**For SimCruise:**
- Where the DEM requires a steeper grade than the limit, keep the DEM (the road exists) and mark the segment steep.
- Treat profile waves shorter than 3 V ft as DEM artefacts.

### 5.8 Horizontal and vertical coordination

These rules come from WSDOT 1220.02(1) and 1220.03 [WS-DM 1220]:
- Do not start or end a horizontal curve at or near a crest. The horizontal curve should lead the vertical curve and be longer than it at both ends.
- Do not put a sharp horizontal curve near the low point of a pronounced sag.
- Avoid roller-coaster profiles, hidden dips, and broken-back grade lines.
- Keep the flat part of a vertical curve away from the zero-cross-slope point of a superelevation transition, so water drains.
- Flatten grade and curvature through intersections.

### 5.9 Urban street grids

- Design speeds are 15-45 mph. Superelevation is not needed where side friction alone carries the curve (Method 2).
- Urban e is usually 4% or less [IA-DM 2B].
- Grid blocks are straight between intersection nodes. Angle points of 1 degree or less are acceptable on low-speed roads with no curve [TX-RDM 4.7].

**Generator rule:** on a way under about 300 m between junctions, if the heading spread is below 2 degrees, snap the block to the straight chord between the junction nodes.

### 5.10 Fitting a design-consistent alignment to noisy OSM

The data has these properties:
- OSM vertex spacing is 2-100 m.
- Position noise is 1-10 m. Use a vertex sigma of 2-5 m, or 1 m where the way was surveyed or aligned to imagery.
- The z14 vector tiles add quantisation of 1/4096 of a tile, about 0.49 m at 35 degrees N.

**Lipar et al. 2011** [LIPAR] — heading-diagram segmentation:
- Fit in the heading diagram psi(s), heading against arc length.
- In that diagram, tangents are flat, arcs are straight lines with slope kappa, and clothoids are parabolas.
- Grow segments greedily: start with 10-20 points, add one at a time, and stop when the maximum lateral deviation exceeds the tolerance. Lipar used 1 m; for OSM use 1.5-3 m, or half a lane width.
- Lipar recovered R = 299 m and 200 m arcs from data perturbed by about 1 m at 20-35 m spacing.

**US Patent 7,152,022** [US7152022] (now public prior art) describes the same psi-s segmentation into lines, arcs and clothoids.

**Global refinement** [TITS-2023] [ARXIV-2110.15814]:
- Unknowns: piecewise-linear curvature kappa(s), plus start position and heading.
- Minimise squared lateral deviation plus an L1 (total-variation) penalty on d(kappa)/ds. That penalty favours piecewise-constant curvature: arcs and tangents joined by clothoids.
- Constraints:
  - |kappa| at most 1/R_min(V);
  - |d(kappa)/ds| at most about C / v^3, with C about 0.6 m/s^3;
  - junction anchors fixed.
- Solve with Gauss-Newton or SQP, integrating the Fresnel equations at each iteration.

**Output format:** the same primitives as ASAM OpenDRIVE planView (line, arc, spiral).

**Bertolazzi and Frego (2015)** G1 clothoid splines, and their open-source Clothoids library, are cited from prior knowledge. Check the licence before use.

**Offsetting edges.** Compute edges from the analytic centreline normal: x_edge = x_c + d n(s). This is valid only while d < R. Cap d x kappa below 0.9; beyond that, the inside edge folds over (swallowtails).

**Never smooth an edge on its own.** That recreates the zig-zags.

---

## 6. Intersections

### 6.1 Curb-return (corner) radii

| Situation | Radius | Source |
|---|---|---|
| Passenger cars, minor cross streets and major intersections with parking | 15-25 ft (4.6-7.6 m) | [FDOT-FDM 212.12.1] |
| New construction, minor cross streets | at least 25 ft (7.6 m) | [FDOT-FDM 212.12.1] |
| Occasional truck turns | at least 30 ft (9.1 m) | [FDOT-FDM 212.12.1] |
| Frequent buses or semis | at least 40 ft (12.2 m), or a three-centred curve or curve with tapers | [FDOT-FDM 212.12.1] |
| Simple edge-of-traveled-way radius at 90 deg, minimum turn | Single-unit truck about 60 ft. Combination trucks 100-150 ft, or a three-centred curve. | [FDOT-FDM T212.12.1] |
| Local codes | local 20 ft, collector 25-30 ft, arterial 35 ft | [SAN-BENITO] (municipal) |
| Urban downtown | typically 10-15 ft | [NACTO] (number only) |
| Intersection angle | 75-90 degrees | [FDOT-FDM 212.5] |

Parking and bike lanes make the **effective** turning radius larger than the curb radius.

**Generator defaults** [derived]:

| Junction type | Radius |
|---|---|
| Downtown grid | 3.0-4.6 m |
| Residential-residential | 6.1 m |
| Residential-collector | 7.6 m |
| Collector | 9.1 m |
| Arterial with trucks | 12.2-15 m, or three-centred |
| Rural uncurbed | 15-23 m (edge of pavement) |

- Never use less than 3 m.
- On skews under 75 degrees, give the obtuse corner the larger radius.
- Stop edge lines and lane lines at the arc tangent points (PC/PT), not at the OSM node.

### 6.2 Stop lines and yield lines

**Stop line** [MUTCD11 3B.19]:
- Solid white across all approach lanes, 12-24 in wide (0.30-0.61 m).
- Required at STOP signs; should be used at signals; never used at YIELD signs.
- Placement:
  - With a marked crosswalk: at least 4 ft (1.22 m) before it.
  - Without one: at the desired stopping point, 4-30 ft from the edge of the intersecting traveled way.
  - Uncontrolled multilane crosswalks: 20-50 ft before the crosswalk, with no parking in between.

**Yield line** (shark teeth) [MUTCD11 3B.19]:
- White isosceles triangles pointing at approaching drivers.
- Base 12-24 in, height 1.5 x base, spacing 3-12 in.
- Used only with a YIELD sign or its equivalent, and at roundabout entries.

### 6.3 Crosswalks

**Markings** [MUTCD11 3C]:

| Topic | Rule |
|---|---|
| Colour and width | White. Minimum width 6 ft (1.83 m). Midblock at 40 mph or more: 8 ft. |
| Transverse lines | Two lines, each 6-24 in |
| Longitudinal (continental) bars | 12-24 in wide; gaps 12-60 in and no more than 2.5 x the bar width; at least 3 bars; parallel to traffic; may be shifted out of wheel paths |
| Ladder | Transverse lines plus interior bars |
| Bar-pair | Bars 8-12 in, pairs 24-60 in apart |
| Curb ramps | Must lie within the extension of the crosswalk lines |
| Signalized junctions | Crosswalks should be marked |

**FDOT practice** [FDOT-FDM 222.2.3]:
- Mark all legs of signalized intersections.
- Use high-visibility markings except on stop-controlled approaches.
- Consider a two-stage crossing with a median refuge where a crossing is longer than 80 ft.

**Cross slope inside the crosswalk** [PROWAG R302.5.2]: 1:48 (2.1%) at stop- or yield-controlled approaches; 1:20 (5%) at uncontrolled and at signalized or beacon approaches; at midblock and roundabout crossings, no more than the street grade.

### 6.4 Curb ramps and detectable warnings

**Curb ramps** [PROWAG R304-R305]:

| Element | Rule |
|---|---|
| Running slope | 1:12 (8.3%) maximum. The ramp never needs to exceed 15 ft, even if it ends steeper. |
| Cross slope | 2.1% maximum |
| Width | 48 in minimum, excluding flares |
| Flares | 1:10 maximum |
| Top landing | 48x48 in |
| Clear space beyond the bottom grade break | 48x48 in, 5% maximum running slope |
| Grade change at the gutter | 13.3% maximum, or a 24 in transition at 2.1% or less |
| Blended transitions | 1:20 or flatter |

With a 0.15 m curb, the ramp is about 1.8 m long [derived].

**Detectable warnings** [PROWAG R305]:
- Truncated domes 0.2 in high, 0.9-1.4 in base diameter, on 1.6-2.4 in centres.
- A field 24 in deep across the full ramp width, at the back of curb.
- They must contrast visually with their surroundings. Common practice is yellow on light concrete and dark red on dark surfaces.
- Also used at rail crossings, refuge islands, and stop, yield or signal-controlled driveways.

### 6.5 Turn lanes and bays

**Length** [FDOT-FDM 212.14]:
- Total length = deceleration length + queue storage + approach taper.
- Approach taper 50 ft for a single lane, 100 ft for a double.
- Minimum storage without a study: 100 ft urban, 50 ft rural.
- Width equals the adjacent lane; median left-turn lanes at most 15 ft.

**Markings** [MUTCD11 3B.06, 3B.07, 3B.23]:
- Solid white line between a through lane and a mandatory turn lane.
- Optional dotted extension (2 ft line, 2-6 ft gap).
- At least two lane-use arrows per turn lane.
- Word markings at least 6 ft tall.

The FDOT Exhibit 212-1 deceleration lengths could **not be read** this session. The 45-75 m decel length at 40 mph or more is a heuristic.

### 6.6 Lane tapers and deflection through junctions

**Taper length** [MUTCD11 3B.12-3B.13]:
- L = W x S for S of 45 mph or more.
- L = W x S^2 / 60 below 45 mph.
- W and L are in the same unit; S is in mph.
- MUTCD sets no minimum for lane-reduction tapers. The 100 ft urban / 200 ft rural minimum it gives for centre-lane (TWLTL) transitions [MUTCD09 3B.02] is used here as a generator floor (derived).
- A shifting taper is L/2 [FDOT-FDM 212.6].

**Turn-lane taper rates** (FDOT 2018, via search): 1:25 below 30 mph, 1:30 at 30-45 mph, 1:40 above 45 mph.

**Maximum deflection of a through lane across a junction** [FDOT-FDM T212.7.1]:

| Speed, mph | 20 | 25 | 30 | 35 | 40 | 45 |
|---|---|---|---|---|---|---|
| Max deflection, deg | 16 | 11 | 8 | 6 | 5 | 3 |

The lateral shift is at most 6 ft, stop bar to stop bar.

**This deflection cap is a strong anti-zig-zag guard.** OSM centrelines that jog across a junction must be straightened, with the shift moved into a taper outside the junction.

### 6.7 Medians and islands

**FDOT minimum median widths on curbed roads** [FDOT-FDM T210.3.1]:

| Context | Width |
|---|---|
| Urban general, centre and core, 25-35 mph | 15.5 ft (4.7 m) |
| Urban general, centre and core, 40-45 mph | 22 ft (6.7 m) |
| Suburban | 22 ft |
| High-speed curbed | 30 ft |
| Flush-shoulder rural | 40 ft |
| Urban core with no left-turn lanes | 6 ft |

Raised medians are required on divided roads at 45 mph or more.

**Pedestrian refuge islands:**
- At least 6 ft wide (8 ft preferred).
- Cut-through at least 5 ft, and at least as wide as the crosswalk [PROWAG R302.2.1].

**Markings** [MUTCD11 3J]:
- Flush painted medians: two double solid yellow lines with yellow diagonals.
- Raised island noses: yellow curb markings, plus an approach taper.

### 6.8 Roundabouts

**Categories** [FHWA-RAB Ch. 6] — inscribed diameter / maximum entry speed / design vehicle:

| Category | Diameter | Entry speed | Design vehicle |
|---|---|---|---|
| Mini | 45-80 ft | 15 mph | SU truck; fully traversable centre |
| Urban compact | 80-100 ft | 15 mph | |
| Urban single-lane | 100-130 ft | 20 mph | WB-50 |
| Urban double-lane | 150-180 ft | 25 mph | |
| Rural single-lane | 115-130 ft | 25 mph | WB-67 |
| Rural double-lane | 180-200 ft | 30 mph | |

The 2010 technical summary groups them as mini 45-90 ft, single-lane 90-180 ft and multilane 150-300 ft.

**Geometry:**

| Element | Value |
|---|---|
| Single-lane entry | 14-16 ft |
| Circulatory roadway | At least the widest entry, up to 120% of it, constant all the way round |
| Truck apron | 3-13 ft, raised at least 1.2 in, cross slope 3-4% |
| Entry radius | 33-98 ft |
| Exit radius | 50 ft or more |
| Splitter islands | At least 50 ft long |
| Crosswalk | About one car length (25 ft) back from the yield line |

**Markings** [MUTCD11 3D]:
- Yield line at every entry.
- No concentric lane lines in the circle.
- Outer edge line not carried across exits.

**For SimCruise:** fit a true circle to the OSM ring by least squares and snap the ring to it. That removes the lobed ring edges.

### 6.9 Driveways and alleys

**FDOT curbed driveways** [FDOT-FDM T214.2.1, T214.3.1]:

| Category | Trips per day | Width | Flares or returns |
|---|---|---|---|
| A (homes) | 1-20 | 12-24 ft | Flared drop curb, flares at least 10 ft, angle 60-90 deg |
| B (small commercial) | 21-600 | 24-36 ft | |
| C and D | over 600 | | Radial returns: 25 ft minimum, 50 standard, 75 maximum |

- **Flush-shoulder (rural) roads:** radial returns, Category A 15 ft minimum and 25 ft standard.
- **Maximum driveway grade:** 28% residential, 10% commercial.
- **TxDOT:** residential driveways at most 24 ft wide; returns at least 15 ft; farm or ranch 25 ft radius with a 20 ft throat [TX-AMM].
- **Sidewalk across a driveway:** the walk continues across at a cross slope of 2% or less [PROWAG R302.5.1].
- **Edge lines:** not broken at minor driveways [MUTCD09 3B.06].
- **Driveway culverts** in open sections [ODOT-HDM400 404] [MD-SHA]:
  - pipe 15-18 in minimum;
  - at least 24 ft long;
  - pipes up to 24 in get a sloped end: 6:1 at 45 mph or more, 4:1 below.

**Alleys:** there is no federal standard. Treat them as a commercial driveway: flared drop curb, continuous sidewalk. Pavement is 12-20 ft, often concrete with an inverted-crown centre gutter. This is a *heuristic*; verify against a city standard such as Iowa SUDAS.

---

## 7. Pavement construction and appearance

### 7.1 Base colour from measured albedo, not from the photo

Measured albedo (linear) for each surface, with the sRGB grey from sRGB = 1.055 L^(1/2.4) - 0.055 [EPA-CP] [LBNL-49283]:

| Surface | Albedo (linear) | sRGB grey |
|---|---|---|
| Fresh hot-mix asphalt (HMA) | 0.04-0.06 | 0.22-0.27 |
| HMA after 5+ years | about 0.12 (EPA range 0.10-0.20) | 0.38 (0.35-0.48) |
| New portland cement concrete (PCC) | 0.35-0.40 | 0.63-0.67 |
| PCC in service | 0.18-0.35, typically about 0.25 | about 0.54 |
| Fresh chip seal | about the aggregate's albedo (0.20 in the San Jose study) | — |
| Chip seal after about 5 years | about 0.12 | — |
| Desert or bare soil | 0.25-0.35 (literature range; not in EPA-CP or LBNL-49283) | — |
| Concrete curb, gutter and walk | 0.35-0.45 | — |

Ageing works in opposite directions for the two materials. Asphalt gets **lighter** as the binder oxidises and the aggregate is exposed. Concrete gets **darker** from tyre marks, oil and dirt. Rain and hand-scrubbing barely change a soiled chip seal [LBNL-49283].

Surface temperature falls about 4 C (7 F) for each +0.1 of albedo (an LBNL estimate quoted in [EPA-CP]). EPA-CP also gives asphalt at about 0.05 new, rising to 0.15-0.20 with age (about 0.20 after seven years), and concrete at up to 0.40 new, falling to about 0.25 after five years. That figure can feed heat shimmer and the drying of a wet road.

**Rule:**
- Albedo = material(surface, age) ± 0.03 aerial modulation (stains and patches only).
- Aged asphalt (0.12) must render darker than desert soil. If a terrain shader produces asphalt near 0.12 and the soil darker than it, the soil is wrong.
- Today's shader (game/GroundFinish.gdshaderinc:32-41, game/AerialPavement.gdshaderinc:19-29) does the opposite: it lifts the asphalt toward its surroundings.

### 7.2 Jointed plain concrete pavement (JPCP)

Panels and joints [FHWA-TA-JOINT]:
- Panels are typically 15 ft long by 12 ft wide (4.57 x 3.66 m).
- Rule of thumb: the maximum panel dimension in ft is at most 1.5-2 times the slab thickness in inches. The length-to-width ratio is at most 1.5.
- Transverse joints are square to the road and continuous across all lanes.
- Longitudinal joints run on the lane lines.
- The outside lane is often built 13-14 ft wide but striped at 12 ft. Its joint then sits 1-2 ft outside the edge line, a clue visible from the driver's seat.
- Older pavements may use a repeating "random" spacing such as 12-13-18-17 ft, often with skewed joints. The skew ratio is not given in the advisory; about 1:6 is an assumption.
- Manholes are isolated by their own joints.

**What a joint looks like** [FHWA-TA-JOINT] [FHWA-LTPP]:
- A dark line 3 mm wide (unsealed) to 6-13 mm (sealed, with the sealant 6-10 mm below the surface).
- Spalls along joints run from under 75 mm to over 150 mm wide.

**Surface texture:**
- Concrete: longitudinal tining at about 20 mm spacing, which gives a directional sheen at grazing light [FHWA-HIF-18-038].
- Asphalt: an isotropic aggregate texture.

### 7.3 Hot-mix asphalt construction marks

Lifts and joints [AI-JOINT] [TX-PDM 7.2]:
- HMA is laid in paver-width passes. That leaves longitudinal construction joints at or near the lane lines, often offset about 150 mm so the stripe is not on the cold joint.
- Joints in successive lifts are offset 6-12 in from each other.
- Lifts are 3-6 times the nominal maximum aggregate size, roughly 38-50 mm for a surface course. Each overlay adds that much height at an untreated edge.

**Render:**
- A faint seam at each lane line and at the lane-to-shoulder boundary.
- Adjacent passes differ in albedo by ±0.01-0.02.
- Never put a seam in a wheel path.

### 7.4 Wheel paths and distress

Wheel paths [FHWA-LTPP Fig. 136]:
- The LTPP convention: each wheel path is 0.76 m wide, centred about 0.76 m and 2.9 m from the edge stripe of a 3.65 m lane. That spacing is truck-oriented. (Fig. 136 is a drawing; these dimensions were not confirmed from the manual's text. The text does confirm 0.75 m from the outside edge stripe as the outer wheel path for faulting measurements.)
- For cars, use about ±0.8 m from the lane centre. This is an assumption.
- Wheel paths gather polishing, bleeding (glossy black in hot climates) and fatigue cracking (pieces under 0.3 m).
- The strip between the wheel paths collects oil drips. That is an observation only; no federal source quantifies it.

**Condition bands** [23CFR490]:

| Measure | Good | Fair | Poor |
|---|---|---|---|
| IRI | under 95 in/mi (1.5 m/km) | 95-170 | over 170 (2.7 m/km) |
| Rutting | under 0.20 in | 0.20-0.40 in | over 0.40 in |
| JPCP faulting | under 0.10 in | 0.10-0.15 in | over 0.15 in |

Faulting is felt as a periodic thump: at 100 km/h, joints every 4.57 m give about 6 Hz.

**Cracking** [FHWA-LTPP]:
- Transverse cracks, by severity: 6 mm or less (low), 6-19 mm (moderate), over 19 mm (high).
- Block cracking: pieces of 0.1-10 m².
- Edge cracking: within 0.6 m of the edge, and only where shoulders are unpaved.

**Sealed cracks ("tar snakes")** [FHWA-RD-99-147]:
- An overband 75-125 mm wide, glossy for 1-2 years.
- Avoided on heavy-traffic and snowplow routes.

**Patches** [FHWA-LTPP] [SPRINGVILLE] [DALLAS-CUT]:
- A patch is a saw-cut rectangle of at least 0.1 m².
- Trench restorations are cut back at least 12 in beyond the excavation.
- In concrete, whole panels are often replaced.

**Map OSM smoothness to condition:**

| `smoothness` | IRI |
|---|---|
| excellent / good | about 1.0-1.5 m/km |
| intermediate | 1.5-2.7 |
| bad or worse | over 2.7 |

The same scalar should drive both the crack and patch decals and the ride roughness the car feels.

### 7.5 Utility covers

Sewer manholes [IL-370] [ODOT-RD335] [IA-SW601]:
- They sit at line ends, at changes in grade, size or alignment, and at sewer intersections.
- Maximum spacing: 400 ft for sewers of 15 in or less; 500 ft for 18-30 in.
- Covers are cast-iron discs of about 24-25 in, set flush.

**Generator default:**
- A sanitary manhole at each junction centre, then every 90-120 m near the centreline.
- Storm inlets at the curb line, at low points and corners.
- Valve boxes about 0.15-0.25 m across (unsourced typical value).

### 7.6 Rumble strips and raised pavement markers

**Shoulder rumble strips** [FHWA-RUMBLE] [FHWA-TA-SRS] [FLH-RS] [MnDOT-RS]:

| Parameter | Value |
|---|---|
| Milled size | 16 in (or 12 in) across, 7 in along, 0.5 in deep (0.375-0.625 in), 12 in centres |
| Offset from the edge line | 6 in most often (range 0-30 in) |
| Rumble stripe | An edge line painted over the strip |
| Where omitted | Usually on shoulders narrower than 4-6 ft |
| Bicycle gaps | 12 ft every 40-60 ft |
| Breaks (MnDOT) | About 50 ft either side of residential entrances; about 130 ft at commercial entrances |

**Centreline rumble strips:**
- 12-16 in wide, 0.375-0.5 in deep, on undivided rural highways.
- Discontinued through intersections.

**Audio and force-feedback hook:** a 0.3 m pitch at 100 km/h is about 93 Hz.

**Raised pavement markers (RPMs)** [MUTCD11 3B.14-3B.17]. Spacing is in multiples of N, the dash-plus-gap cycle (40 ft = 12.2 m):

| Use | Spacing |
|---|---|
| Positioning guides | 2N |
| Sharp curves | N or less |
| Straight freeways | 3N |
| Supplementing solid lines | at most N |
| Supplementing edge and channelizing lines | at most N/2 |

Marker colour matches the line it supplements; red faces wrong-way traffic. Markers should not supplement right edge lines without a study.

### 7.7 Marking wear and geometry

**Patterns** [MUTCD11 3A.04-3A.05]:

| Line type | Line | Gap |
|---|---|---|
| Broken | 10 ft | 30 ft |
| Dotted lane line | 3 ft | 9 ft |
| Dotted extension | 2 ft | 2-6 ft |

- The gap in a double line is at most twice the single-line width.
- Minimum maintained retroreflectivity: 50 mcd/m²/lx at 35 mph or more; 100 at 70 mph or more.

**Practice:**
- Paint stands 0.5-2 mm proud; thermoplastic 2-3 mm.
- Fresh white paint has an albedo of about 0.6-0.8.
- Wear concentrates on low-speed streets and where tyres cross: crosswalks, stop bars, merges.
- On worn paint, 30-70% of the asphalt shows through (heuristic).

Dash phase must follow arc length along the smoothed centreline, so dashes never restart at segment joins.

### 7.8 Chip seal and gravel

**Chip seal** [UARK-T2] [CT-MTAG7] [FHWA-CHIP]:
- Single-sized crushed aggregate, 3/8-1/2 in.
- Looks like the stone when fresh, ages to albedo 0.12.
- Loose chips at the edges; bleeding in the wheel paths.
- A heuristic default for low-volume rural paved roads in the West and Plains.

**Gravel roads** are covered in template T9 [FHWA-GRAVEL].

---

## 8. Roadside furniture and environment

### 8.1 Clear zone

**AASHTO RDG Table 3-1, as reproduced in NCDOT RDM Table 4-5** [NCDOT-BD]. Values are feet from the edge of the traveled way. Each cell gives two foreslope bands, "6:1 or flatter / 5:1-4:1":

| Design speed | ADT under 750 | 750-1,500 | 1,500-6,000 | over 6,000 |
|---|---|---|---|---|
| 40 mph or less | 7-10 / 7-10 | 10-12 / 12-14 | 12-14 / 14-16 | 14-16 / 16-18 |
| 45-50 | 10-12 / 12-14 | 14-16 / 16-20 | 16-18 / 20-26 | 20-22 / 24-28 |
| 55 | 12-14 / 14-18 | 16-18 / 20-24 | 20-22 / 24-30 | 22-24 / 26-32 |
| 60 | 16-18 / 20-24 | 20-24 / 26-32 | 26-30 / 32-40 | 30-32 / 36-44 |
| 65-70 | 18-20 / 20-26 | 24-26 / 28-36 | 28-32 / 34-42 | 30-34 / 38-46 |

**Notes on the table:**
- 3:1 slopes are traversable but not recoverable, and do not count toward the zone.
- A practical cap is 30 ft (9.1 m) [CT-HDM 309.1] [TX-RDM T4-20].
- Iowa 8A-2 publishes a similar table [IA-DM 8A-2].

**Curves and urban streets:**
- On the outside of curves with R under 2,950 ft, multiply by the curve factor K_cz, 1.2-1.5 [TX-RDM T4-21].
- Curbed urban streets at 45 mph or less: 4 ft minimum, 6 ft preferred, from the face of curb.
- The often-quoted 18 in behind the curb is an operational offset, not a clear zone [FHWA-CZ].

**Generator rule:**
- Compute one clear-zone width per edge from speed, ADT proxy, slope and radius, using the mid-range value, in metres.
- Exclude trees, rocks and non-breakaway poles from that band.
- Render the clear zone as mown grass, shorter and greener than the wild vegetation beyond.

### 8.2 Guard rail

**Warrants** [NCDOT-BD] [MO-EPG 606.1]:
- First choose among: remove, redesign, relocate, make breakaway, shield, delineate.
- Shield these when they lie inside the clear zone and cannot be removed:
  - bridge piers, abutments and rail ends;
  - fills steeper than 1:3;
  - steep parallel ditches;
  - water;
  - walls and headwalls;
  - non-breakaway supports.
- MoDOT treats fill-slope rail as optional under 400 AADT.
- Join runs whose gap is 300 ft (91 m) or less.

**MGS hardware** [FHWA-GR-HEIGHT] [MO-EPG 606.1] [FHWA-B211]:

| Part | Value |
|---|---|
| Rail top | 31 in (787 mm). Existing rail at 27¾ in or more may stay; older 27-28 in G4(1S) rail is common. |
| Posts | W6x8.5 steel or 6x8 wood, 6 ft long, at 6 ft 3 in (1.905 m) spacing |
| Panels | 12 ft 6 in |
| Blockouts | 12 in |
| Shop-bent rail | Where R is 150 ft or less |
| Dynamic deflection | MGS 2.5 ft; half post spacing 2 ft; quarter spacing 1.5 ft; cable 8 ft; rigid concrete 0 |
| Placement | Rail face about 2 ft beyond the shoulder edge; 2 ft of widening behind the posts; 1:10 or flatter in front |
| Curbs | A 6 in curb no more than 6 in in front of the rail face |

**Runout length L_R, ft** (RDG 4th ed. Table 5.10, via NCDOT). Columns are ADT: over 10,000 / 5,000-10,000 / 1,000-5,000 / under 1,000:

| Design speed | L_R, ft |
|---|---|
| 80 mph | 470 / 430 / 380 / 330 |
| 70 | 360 / 330 / 290 / 250 |
| 60 | 300 / 250 / 210 / 200 |
| 50 | 230 / 190 / 160 / 150 |
| 40 | 160 / 130 / 110 / 100 |
| 30 | 110 / 90 / 80 / 70 |

**Length of need** for a parallel rail: X = L_R (L_A - L_2) / L_A, where L_A is the lateral distance to the back of the hazard (capped at the clear zone) and L_2 is the rail offset. Flare rates are in Table 5.9 (for example, 15:1 semi-rigid beyond the shy line at 70 mph).

**Terminals:**
- Crashworthy; a typical tangent energy-absorbing terminal is about 50 ft (industry-typical value).
- Graded 1:10 to 1:6 around the terminal.
- Within 10 ft of the edge line, the terminal gets an object marker.

**Today's game** uses a 12 m minimum run plus 8 m leads and no terminals (game/GuardRails.gd:32-36), which is too short.

### 8.3 Signs

**Height to the sign bottom** [MUTCD11 2A.13-2A.17]:
- Rural: 5 ft (1.52 m).
- Business, residential or pedestrian areas: 7 ft (2.13 m).
- Freeways: 7 ft.
- Overhead signs: at least 17 ft of clearance.

**Lateral offset:**
- 12 ft (3.66 m) from the traveled-way edge, or 6 ft beyond a shoulder wider than 6 ft.
- Constrained conventional roads: at least 2 ft.
- Urban: at least 2 ft from the face of curb is typical (1 ft minimum).

**Orientation:**
- Face the traffic served, turned slightly away to avoid mirror glare.
- Not within 4 ft in advance of a crosswalk.

### 8.4 Warning signs and chevrons

**Advance placement** [MUTCD09 2C.05, T2C-4 to T2C-6]:
- Condition A (lane drops and merges): 225 ft at 20 mph up to 1,350 ft at 75 mph.
- Condition B (slowing to an advisory speed): tabulated by posted and advisory speed.

**Curve signs** (Table 2C-5, roads over 1,000 AADT), by posted minus advisory speed:

| Difference | Curve sign and speed plaque | Chevrons |
|---|---|---|
| 5 mph | recommended | — |
| 10 mph | required | recommended |
| 15 mph or more | required | required |

**Chevron spacing** (Table 2C-6):

| Advisory speed / radius | Spacing |
|---|---|
| 15 mph or less, R under 200 ft | 40 ft |
| 20-30 mph | 80 ft |
| 35-45 mph | 120 ft |
| 50-60 mph | 160 ft |
| over 60 mph | 200 ft |

### 8.5 Delineators

**Mounting** [MUTCD09 3F] [MO-EPG 620.5]:
- Reflector about 4 ft (1.22 m) above the near edge.
- 2-8 ft outside the shoulder.
- Colour matches the edge line.
- Required on the right side of freeways and expressways, and on at least one side of ramps.

**Spacing:**

| Location | Spacing |
|---|---|
| Tangents | 200-530 ft (61-161 m) |
| Ramps | 100 ft |
| Curves | S = 3 sqrt(R - 50) ft, from 20 to 300 ft (Table 3F-1) |
| Lead-in and lead-out of curves | 2S, 3S, 6S |

**Driveways:** a post may move up to S/4 to clear a driveway; delineators inside the opening are dropped.

### 8.6 Mile markers

[MUTCD09 2H.05-2H.06]:
- Green, with white numerals.
- At least 4 ft high.
- On the right-hand side, up to 30 ft from the pavement edge.
- Numbered from the south or west state line or route terminus.
- On divided highways, measured on the northbound or eastbound roadway.

### 8.7 Utility poles

**Placement** [FHWA-CZ] [FHWA-UTIL-NP]:
- FHWA policy: as close to the ROW line as feasible, behind curbs and sidewalks.
- Urban: 6 ft behind the curb as noteworthy practice; 18 in absolute minimum.

**Wire clearance:**
- NESC Table 232-1: 15.5 ft for communications and about 16-18.5 ft for power over truck areas [NESC via WPS].
- Some states require 18 ft over highways [NCDOT-0421].

**Spans** (secondary sources, medium confidence):
- 100-150 ft urban; 250-300 ft rural.
- Poles typically 35-45 ft of wood.

### 8.8 Street lighting

**MnDOT highway lighting** [MnDOT-LIGHT]:
- 40 ft poles with 9-12 ft arms at 240-250 ft.
- Spacing normally no more than 5-6 mounting heights.
- Units inside the clear zone are breakaway.

**TxDOT pole offsets** [TX-HIM]:

| Road type | Minimum | Desirable |
|---|---|---|
| Curbed, 45 mph or less | 2.5 ft from the face of curb | 10 ft |
| Uncurbed, 45 mph or more | 15 ft | 20 ft |

**Municipal examples:**
- Irvine: 25 ft poles on locals, 30 ft on arterials, at 150 ft.
- Queen Creek: a light at every intersection.

**Generator defaults:**
- Residential: one-sided cobra heads, 7.6 m, every 45-75 m, plus one at each corner.
- Arterials: 9-12 m, about 5 mounting heights apart.

### 8.9 Mailboxes, hydrants, fences, culverts

**Rural mailboxes** [USPS-PB] [MO-EPG 645]:
- 41-45 in from the road surface to the box bottom.
- The door 6-8 in behind the curb face or road edge.
- Breakaway supports: at most a 4x4 in wood post or 2 in pipe.
- MoDOT builds a turnout where the shoulder is narrower than 8 ft.
- Mailboxes sit only where there is no sidewalk.

**Fire hydrants** [WSSC-W24] [RICHARDSON]:
- Spacing set by local code: about 300 ft commercial, 500 ft residential.
- 2 ft behind the curb face, in the verge.
- A federal standard and the nozzle height were not found.

**Fences** [ODOT-HDM400 407]:
- On the access-control line of freeways.
- Elsewhere only where a ROW agreement requires (farmland).
- Snow fences are set 35 x their height from the centreline.

**Culverts** are covered in 6.9 and [ODOT-HDM400 404].

### 8.10 Vegetation zones

**Mowing** [MO-EPG 822.1] [WisDOT-MM] [INDOT-MOW] [HDOT-LS] [FHWA-VEG]:
- MoDOT mows 15 ft from the shoulder on the first passes, and up to 30 ft later.
- Stubble is about 6 in.

**FHWA three-zone model:**
1. Vegetation-free strip.
2. Mown operational zone.
3. Natural transition zone.

**Trees:**
- A tree with a trunk over 4 in is a fixed object. Do not plant one inside the clear zone on new or rebuilt roads.
- Urban street trees: at least 6 ft from the edge of travel at 45 mph or less [ODOT-HDM400].

**Arid regions:** replace turf with bare gravel or desert scrub, but keep the zone boundaries so the road edge stays distinct.

### 8.11 Snow poles

[ITD-628] [MnDOT-SM]:
- Flexible poles reaching 6-8 ft (1.8-2.4 m), attached to the delineator posts.
- Orange or black, with a reflective band.
- Used in snowy regions.

---

## 9. OSM tag mapping and inference defaults

### 9.1 What tags the game can see

**OpenMapTiles transportation layer** (the game's geometry, z14) [OMT] carries only:
- class, subclass;
- oneway, ramp, brunnel, service, layer;
- access, toll, expressway;
- surface collapsed to paved / unpaved.

It has no lanes, width, sidewalk, shoulder, kerb, parking, turn:lanes or placement. Coordinates are quantised to about 0.49 m.

**The Esri OSM_NA_Highways FeatureServer** [ESRI-OSM] (already used by lane-counts.mjs and road-layout.mjs) adds:
- lanes, lanes_forward, lanes_backward;
- width, sidewalk, surface, smoothness, tracktype;
- lit, maxspeed, cycleway, crossing, footway, junction, incline.

It does **not** carry:
- sidewalk:left/right/both;
- shoulder*, kerb, parking:*;
- turn:lanes, placement, lane_markings, est_width;
- lanes:both_ways, cycleway:left/right/both.

**Overpass, or a Geofabrik extract, is needed for the rest.** On primary roads the side-specific sidewalk keys (13.5% each) are more common than plain `sidewalk=*` (13.1%) [TAGINFO]. So a plain-field source misses about half the sidewalk data.

### 9.2 US tag coverage (share of ways, Geofabrik taginfo, data to 2026-11-09) [TAGINFO]

| Key | motorway | trunk | primary | secondary | tertiary | unclassified | residential | service |
|---|---|---|---|---|---|---|---|---|
| lanes | 99.5 | 96.6 | 86.2 | 72.1 | 48.8 | 18.5 | 9.5 | 1.5 |
| maxspeed | 93.7 | 79.6 | 67.5 | 51.9 | 32.4 | 12.4 | 10.4 | 0.6 |
| surface | 57.8 | 64.6 | 68.0 | 57.3 | 51.3 | 53.8 | 22.0 | 12.7 |
| sidewalk | 1.7 | 13.4 | 13.1 | 8.7 | 6.9 | 2.9 | 3.4 | 0.2 |
| shoulder | 2.6 | 4.4 | 1.4 | 0.8 | 0.35 | 0.1 | 0.02 | — |
| width | 1.9 | 1.4 | 2.1 | 2.3 | 1.6 | 0.55 | 1.3 | 0.05 |
| turn:lanes | 19.0 | 17.8 | 17.2 | 12.8 | 3.8 | 0.8 | 0.08 | — |
| lit | 17.1 | 6.7 | 7.4 | 5.5 | 3.0 | 1.1 | 1.2 | — |
| tiger:cfcc (unedited TIGER) | 29 | 42 | 43 | 50 | 58 | 55 | 68 | 10 |

**Other counts:**
- `footway=sidewalk`: 4,429,808 US ways.
- `footway=crossing`: 2,960,753 ways.
- `highway=crossing`: 4,799,968 nodes.
- `kerb=*` nodes: lowered 982k, flush 185k, raised 68k, rolled 7.7k.

**For residential and service roads, more than 90% of every cross-section attribute must be inferred.** In towns, the separately mapped sidewalk geometry is the best evidence for a curb.

### 9.3 Tag semantics and precedence

| Element | Tags, highest precedence first | Notes |
|---|---|---|
| Carriageway width | `width` > `est_width` > sum of `width:lanes` > lanes x lane width | `width` is kerb-to-kerb or edge-to-edge, in metres by default (units allowed). It includes parking lanes and painted bike lanes, and excludes sidewalks and street-side bays [OSM-WIKI width]. Whether shoulders are included has no consensus. Reject a width under 2.5 m x motor lanes, or over 4.5 m x lanes + 7.2 m. |
| Splitting a known width | muv-osm weights: motor or bus lane 2; bike or foot 1; parallel parking 2; angled parking 4. Tagged sub-widths are subtracted first. [MUV] | If the result gives a lane under 2.7 m or over 4.2 m, distrust the tag. |
| Lanes | `lanes`, `lanes:forward/backward`, `lanes:both_ways=1` (TWLTL) | Counts travel lanes only. Defaults: 2 two-way, 1 one-way; unclassified and service 1 shared narrow lane. Odd counts on urban arterials suggest a TWLTL (heuristic). |
| Shoulder | `shoulder=yes/no/left/right/both`, `shoulder:<side>=*`, `shoulder:width`, `shoulder:surface` | A bare `yes` means paved and car-wide. On a one-way road it means the right side. |
| Sidewalk | `sidewalk:<side>` > `sidewalk:both` > `sidewalk` (both/left/right/no/separate) | `separate` means use the parallel `footway=sidewalk` way, not an attached mesh. Attributes: `sidewalk:<side>:width/kerb/surface`. |
| Kerb | `kerb=raised/lowered/flush/rolled/no`, `kerb:height`, `sidewalk:<side>:kerb`, `barrier=kerb` lines | Mostly on nodes. Any lowered or flush kerb node, or `footway=crossing`, proves the block is curbed. |
| Parking | `parking:<side>=lane/street_side/on_kerb/half_on_kerb/shoulder/no/separate` + `:orientation` | Translate the deprecated `parking:lane:*`. Lane parking is inside `width`. |
| Cycleway | `cycleway[:side]=lane/track/shared_lane/no` | `lane` is inside the carriageway; `track` is beyond the curb. |
| Surface | `surface`, `smoothness`, `tracktype` | Read the raw value (Esri), not OMT's paved/unpaved. helper/atlas.mjs:129 currently collapses it. |
| Lit | `lit` | Defaults: urban yes; suburban arterials yes; rural no (except interchanges). |
| Turn lanes | `turn:lanes[:forward/backward]`; `|` separates lanes, `;` alternatives | Drives arrows and bays. |
| Placement | `placement[:forward/backward/:start/:end]` | Absent means the way runs on the carriageway centre. Shift the centreline before building edges, smoothed over at least 60 m. |
| Oneway | `oneway`; dual carriageways are two one-way ways | Pair antiparallel same-name or same-ref ways within 40 m; the gap between them is the median. |
| Crossings | `highway=crossing` + `crossing=*`, `crossing:markings=*` | Paint unless `no` or `unmarked`. US iD's legacy `uncontrolled` means marked. |
| Junction control | `highway=stop/give_way/traffic_signals`, `direction`, `stop=all` | |
| Roundabout | `junction=roundabout` ring; `highway=mini_roundabout` node | |
| Structures | `bridge`, `tunnel`, `embankment`, `cutting`, `waterway=ditch/drain` near the road | A mapped ditch within 10 m replaces the generated one. |
| Barriers | `barrier=guard_rail/jersey_barrier/fence/wall` | Mapped barriers are authoritative. |
| Point furniture | `highway=street_lamp`, `power=pole/line`, `emergency=fire_hydrant`, `man_made=manhole`, `highway=milestone`, `traffic_sign=*`, `natural=tree/tree_row` | Mapped objects first; generated ones fill only the gaps. |

### 9.4 Inference order for an untagged way

1. **Class, oneway, ramp, surface** come from the vector tile. Raw surface, lanes, maxspeed, sidewalk, width and smoothness come from Esri, matched by geometry, as lane-counts.mjs already does.
2. **Design speed:** maxspeed, else the class and context default (5.1). Rural ways add 5 mph.
3. **Context** (2.2): from the building density already loaded (NSI, Overture), NLCD class and maxspeed. A pure function of position, so it is identical across tiles.
4. **Section type:** closed when the context is urban, urban_core or rural_town and maxspeed is 40 mph or less, or when there is curb evidence. Suspended (mountable curb) when suburban and 45 mph or more. Otherwise open.
5. **Lanes and lane width** (3.1); **shoulders** (3.2) in open sections; **gutter, curb, buffer, sidewalk** (3.5-3.8) in closed sections; **parking and bikes** only where tagged, or where the tagged width allows.
6. **Era and variety** (heuristic): a deterministic hash of the way id chooses, within the sourced ranges:
   - planting strip width;
   - sidewalk gaps on suburban local streets (about 1 in 4);
   - older narrower lanes on roads that still carry their unedited TIGER marker.
7. **Record provenance** for every element (tagged, inferred or default), as osm2lanes does, so checks can tell a bad guess from bad data.

### 9.5 Prior art

| Tool | Licence | What to take |
|---|---|---|
| osm2streets / osm2lanes (A/B Street) [OSM2STREETS] | Apache-2.0 | Ordered lane-list model (Driving, Parking, Sidewalk, Shoulder, Biking, SharedLeftTurn, Buffer(Curb/Verge/Planters)); the `infer_sidewalk_tags` rule (urban-biased, so gate it on context); road trimming so polygons meet intersection polygons square. Runs in Bun via wasm-pack but needs raw OSM XML; the npm build is stale (0.1.4, 2023). Its generic widths (lane 3.0, sidewalk 1.5, shoulder 0.5, curb 0.1, verge 2.0 m) must be replaced with the US values above. |
| muv-osm [MUV] | MPL-2.0 | The `distribute_full_width` weights. File-level copyleft: modified files stay open. |
| OSM2World [OSM2WORLD] | MIT | Kerb strip 0.15 m wide; heights lowered 0.03 m, raised 0.12 m; width fallback order. |
| Streetmix [STREETMIX] | AGPL-3.0 | Numbers only (sidewalk 1.8 m, planter 1.2 m, curb height 0.15 m). No code. |

---

## 10. Checklist: what a realistic US road must get right

The engine design uses this list as its acceptance criteria.

| # | The real thing | Typical violation in a game |
|---|---|---|
| 1 | Edges are parallel offsets of a G1 (G2 above 45 mph) centreline | Zig-zag at map vertices, lobes between them |
| 2 | No curve sharper than R_min(V) unless signed for a lower speed | Map-vertex kinks read as hairpins |
| 3 | Straight blocks are straight between junctions | Wobbly town streets |
| 4 | Crown 2% on tangents; bank ramps over L_r, mostly on the tangent | Flat or randomly tilted road |
| 5 | Pavement albedo 0.10-0.14 (asphalt) or 0.20-0.35 (concrete), set by material | Road tone copied from the photo, including dirt and shadow |
| 6 | Town streets: concrete gutter, 6 in curb, sidewalk, grass strip | Asphalt meeting dirt directly |
| 7 | Rural roads: edge line on asphalt, paved strip, gravel, rounded hinge, 1:4-1:6 foreslope, ditch | A sharp crease, then a 35-50 degree batter |
| 8 | Junction corners are arcs of 4.5-15 m; lines stop at the tangent points | Overlapping square ribbon ends |
| 9 | Stop bars and crosswalks placed by the MUTCD | None, or at fixed offsets |
| 10 | Furniture respects the clear zone and offsets | Trees on the shoulder, poles in the ditch |
| 11 | Old roads look old: narrower, rougher, patched | Every road brand-new |

---

## 11. Source register

**Licence key:**
- **PD:** US federal work, public domain.
- **PR:** state or municipal public record; cited and paraphrased.
- **C2:** copyrighted, numbers used only via the secondary source listed.
- **OSS:** open-source licence as stated.

### Federal

| Tag | Document | Licence |
|---|---|---|
| MUTCD09 | FHWA Manual on Uniform Traffic Control Devices, 2009 ed.: Parts 2A, 2B, 2C, 2H, 3B, 3F. https://mutcd.fhwa.dot.gov/htm/2009/ | PD |
| MUTCD11 | MUTCD 11th ed. (Dec 2023), Parts 2, 3, 9. https://mutcd.fhwa.dot.gov/pdfs/11th_Edition/part3.pdf (Revision 1, Dec 2025, renumbers some sections: re-check) | PD |
| PROWAG | US Access Board, Public Right-of-Way Accessibility Guidelines, final rule, 88 FR 53604 (8 Aug 2023), 36 CFR 1190. https://www.govinfo.gov/content/pkg/FR-2023-08-08/html/2023-16149.htm | PD |
| FHWA-FC | FHWA Highway Functional Classification Concepts, Criteria and Procedures (2023), Section 3, Tables 3-5 and 3-6. https://www.fhwa.dot.gov/planning/processes/statewide/related/highway_functional_classifications/section03.cfm | PD |
| FHWA-PEDSAFE | FHWA PEDSAFE sidewalk guidelines. https://mail.walkinginfo.org/pedsafe/moreinfo_sidewalks/ | PD |
| FHWA-RAB | FHWA Roundabouts: An Informational Guide, FHWA-RD-00-067, Ch. 6. https://www.fhwa.dot.gov/publications/research/safety/00067/000676.pdf | PD |
| FHWA-TA-JOINT | FHWA TA T 5040.30, Concrete Pavement Joints (2019). https://www.fhwa.dot.gov/pavement/ta504030.pdf | PD |
| FHWA-HIF-18-038 | FHWA tech brief on tining and texture; TA T 5040.36. https://www.fhwa.dot.gov/pavement/concrete/trailer/resources/hif18038.pdf | PD |
| FHWA-LTPP | LTPP Distress Identification Manual, FHWA-HRT-13-092. https://www.fhwa.dot.gov/publications/research/infrastructure/pavements/ltpp/13092/13092.pdf | PD |
| FHWA-RD-99-147 | Sealing and filling cracks in asphalt pavements. https://infopave.fhwa.dot.gov/InfoPave_Repository/Reports/C30/C30_60/99147a.pdf | PD |
| FHWA-RUMBLE | FHWA-HRT-17-026, state of the practice for shoulder and centreline rumble strips. https://www.fhwa.dot.gov/publications/research/safety/17026/003.cfm | PD |
| FHWA-TA-SRS | FHWA TA T 5040.39 Rev. 1 (2011) | PD |
| FLH-RS | FHWA Federal Lands rumble strip guidelines | PD |
| FHWA-SAFEEDGE | FHWA Every Day Counts, Safety Edge. https://www.fhwa.dot.gov/innovation/everydaycounts/edc-1/safetyedge.cfm | PD |
| FHWA-GRAVEL | FHWA Gravel Roads Construction and Maintenance Guide, FHWA-OTS-15-0002 (2015). https://www.fhwa.dot.gov/construction/pubs/ots15002.pdf | PD |
| FHWA-CHIP | FHWA chip seal checklist. https://www.fhwa.dot.gov/Pavement/preservation/ppcl02.pdf | PD |
| FHWA-CZ | FHWA Clear Zone and Horizontal Clearance. https://www.fhwa.dot.gov/programadmin/clearzone.cfm | PD |
| FHWA-UTIL-NP | FHWA noteworthy practices, roadside tree and utility pole management | PD |
| FHWA-VEG | FHWA Vegetation Control for Safety, FHWA-SA-07-018 | PD |
| FHWA-WIDE | FHWA proven safety countermeasure: wider edge lines | PD |
| FHWA-GR-HEIGHT | FHWA guard rail height memo (May 2010), via CDOT | PD |
| FHWA-B211 | FHWA eligibility letter B-211 | PD |
| 23CFR490 | 23 CFR 490.313, pavement condition thresholds | PD |
| EPA-CP | US EPA, Reducing Urban Heat Islands, Ch. 5 Cool Pavements | PD |
| LBNL-49283 | Pomerantz et al., LBNL-49283 (DOE lab report) | PD (US Government work) |
| CENSUS-UA | US Census Bureau, 2020 urban area criteria | PD |
| NLCD | MRLC NLCD class legend | PD |
| NHIS | NHIS 2015 sidewalk prevalence (CDC / PMC5866529) | PD |
| USPS-PB | USPS Postal Bulletins 22258 and 22310; mailbox kit | PD |

### State and municipal

| Tag | Document | Licence |
|---|---|---|
| CT-HDM | Caltrans Highway Design Manual, Ch. 300 (Dec 2020): Indexes 301-309, Tables 302.1, 303.1, 307.2 | PR |
| CT-MTAG7 | Caltrans MTAG Ch. 7, Chip Seal | PR |
| TX-RDM | TxDOT Roadway Design Manual: 4.7, 4.8, 4.10.x, 4.12.7, 5.1.2, 6.1.2, 6.1.7, 7.1.2, 8.1.2, 19.3, 19.4; Tables 4-4 to 4-12, 4-20 to 4-22, 5-1, 6-1, 7-1, 8-1. https://www.txdot.gov/manuals/des/rdw/ | PR |
| TX-AMM | TxDOT Access Management Manual, driveway design | PR |
| TX-HIM | TxDOT Highway Illumination Manual, pole placement | PR |
| TX-PDM | TxDOT Pavement Manual 7.2 | PR |
| IA-IM3210 | Iowa DOT I.M. 3.210 (reproduces AASHTO Green Book 2018 Tables 5-5 and 6-5). https://www.iowadot.gov/local_systems/publications/im/3210.pdf | PR; C2 for the reproduced numbers |
| IA-DM | Iowa DOT Design Manual: 2A Horizontal Alignment, 2B Superelevation, 3G-1 Roadway Ditches, 8A-2 Clear Zones. https://iowadot.gov/design-manual/chapter-2-alignments/2b-superelevation | PR |
| IA-SW601 | Iowa DOT SW-601 | PR |
| WS-DM | WSDOT Design Manual M 22-01.23 (Sept 2024): Ch. 1220 and 1250 | PR |
| MO-EPG | MoDOT Engineering Policy Guide: 230.1, 606.1, 620.5, 645, 750.7, 822.1 | PR |
| NE-HA | Nebraska DOT DES 22-02, horizontal alignment policy | PR |
| MassDOT-PDDG | MassDOT Project Development and Design Guide, Ch. 4 | PR |
| FDOT-FDM | FDOT Design Manual 2021/2022: 210, 212, 214, 222, 223 | PR |
| FDOT-I300 | FDOT Design Standards Index 300, curb and gutter | PR |
| SDDOT-650 | SDDOT Standard Plate 650.01 | PR |
| MnDOT-B618 | MnDOT B618 curb and gutter, via city standard details | PR |
| MnDOT-RS | MnDOT rumble strip typical details (2017) | PR |
| MnDOT-LIGHT | MnDOT Roadway Lighting Design Manual (2010) | PR |
| MnDOT-SM | MnDOT surface-mount delineator and snow pole specs | PR |
| NCDOT-BD | NCDOT barrier design sheet (2023), reproducing AASHTO RDG 4th ed. Tables 3-1, 5.7, 5.9, 5.10 | PR; C2 for the RDG numbers |
| NCDOT-0421 | NCDOT 19A NCAC 02E .0421 | PR |
| ODOT-HDM400 | Oregon DOT Highway Design Manual, Ch. 400 (Jan 2025) | PR |
| ODOT-RD335 | Oregon DOT standard drawing RD335 | PR |
| MD-SHA | Maryland SHA culvert guidelines | PR |
| WisDOT-MM | WisDOT Maintenance Manual 07-05-35 | PR |
| WisDOT-9A11 | WisDOT SDD 9A11 | PR |
| INDOT-MOW | INDOT mowing policy | PR |
| HDOT-LS | Hawaii DOT landscape, Ch. 3 | PR |
| ITD-628 | Idaho TD standard drawing 628-1, snow poles | PR |
| IL-370 | Illinois Administrative Code Title 35, Part 370 | PR |
| KTC | Kentucky Transportation Center: Safe Design 6.8; Green Book summary | PR (university, public) |
| WSSC-W24 | WSSC Water standard W-24 | PR |
| RICHARDSON | Richardson, TX standards | PR |
| SPRINGVILLE | Springville, UT standard specs, Ch. 11 | PR |
| DALLAS-CUT | Dallas Street Cut Manual | PR |
| SAN-BENITO | San Benito County, CA code | PR |
| Rockville | Rockville, MD construction standards | PR |
| UARK-T2 | University of Arkansas T2 chip seal handout | PR (university, public) |
| AI-JOINT | Asphalt Institute / FHWA longitudinal joint best practices (summary) | industry, cited |
| NESC via WPS | NESC Table 232-1 values via a utility joint-use standard | C2 |

### Copyrighted, secondary only

| Tag | Document | Licence |
|---|---|---|
| NACTO | NACTO Urban Street Design Guide, Corner Radii (number only) | C2 |
| AASHTO-GB / RDG | Not read; numbers only via IA-IM3210, IA-DM, TX-RDM, WS-DM, NCDOT-BD | C2 |

### Research and data

| Tag | Document | Licence |
|---|---|---|
| LIPAR | Lipar, Lakner, Maher, Zura, "Estimation of Road Centerline Curvature from Raw GPS Data", Baltic J. Road and Bridge Eng. 6(3), 2011, doi:10.3846/bjrbe.2011.21 | academic |
| US7152022 | US Patent 7,152,022 | public prior art |
| TITS-2023 | "Clothoid-Based Reference Path Reconstruction for HD Map Generation", IEEE T-ITS 2023, doi:10.1109/TITS.2023.3305198 | academic |
| ARXIV-2110.15814 | Curvature-based road reconstruction for simulation | academic |
| OSM-WIKI | OSM wiki: Key:width, lanes, shoulder, sidewalk, kerb, parking, crossing:markings, turn, placement, oneway, surface, lit | CC BY-SA |
| OSM-US-CLASS | OSM wiki, United States/Road classification | CC BY-SA |
| TAGINFO | Geofabrik taginfo, north-america:us (2026-11-10) | ODbL |
| OMT | OpenMapTiles schema, transportation layer | CC BY |
| ESRI-OSM | Esri OSM_NA_Highways FeatureServer field list (2026-11-10) | ODbL data |
| OSM2STREETS | a-b-street/osm2streets | OSS Apache-2.0 |
| MUV | LeLuxNet/Muv (muv-osm) | OSS MPL-2.0 |
| OSM2WORLD | tordanik/OSM2World | OSS MIT |
| STREETMIX | streetmix/streetmix | OSS AGPL-3.0, numbers only |

---

## 12. Open items (not yet verified)

1. **MUTCD 11th edition renumbering.** It moved the edge-line and delineator sections; Revision 1 (Dec 2025) may change values. Re-check the 6 in edge-line trigger and the Part 3F/3K numbering.
2. **Green Book spiral offset limits.** The 0.20 m and 1.0 m limits were not verified against a public reproduction.
3. **RC radii for e_max 6% and 8%.** These are derived from the e_max 4% ratio. Transcribe TxDOT Tables 4-6 and 4-7 in full.
4. **Sources not read this session:**
   - FDOT Exhibit 212-1 deceleration lengths;
   - Caltrans A87A and WSDOT F-10.12 curb profiles;
   - a sourced alley width.
5. **Values without a public source:**
   - curve widening amounts on sharp two-lane curves;
   - rock-cut backslopes;
   - desert-specific ditch practice;
   - washboard wavelength;
   - hydrant nozzle height;
   - utility pole spans;
   - ROW widths.
6. **Gutter cross slope** varies by agency (5%, 6.25%, 8.33%). The default of 6% is a compromise.
7. **Desert soil albedo** (0.25-0.35) needs a public source; it is the reference for the arid colour gate.
8. **Relative gradient above 50 mph.** The table in 5.4 follows Iowa 2B, which holds 0.50% from 50 to 80 mph. Some manuals are reported to keep reducing it at high speed, which lengthens runoff; that was not verified this session. Check TxDOT and WSDOT before Stage 3.

**Verification pass, 2026-11-10.** Checked against the live sources: FHWA-FC Tables 3-5 and 3-6 (lane and shoulder widths confirmed; motorway and trunk AADT bands corrected); IA-DM 2B (f_max, relative gradients, low-speed radii at e = 0 and 4%, e_max policy, 70/30 runoff split all confirmed); IA-DM 2A (minimum curve length corrected); TX-RDM 4.7.1 (15 V broken-back tangent and 1.5 / 2.0 compound ratios confirmed); PROWAG R302-R305 (widths, grades, 1:48 cross slope, ramps, 13.3% gutter grade change and domes confirmed; crosswalk cross slope corrected); MUTCD 2009 3B.01, 3B.06, 3B.07, 3B.09, 3B.16, 3B.18, 3F and 2C (warrants, tapers, stop lines, crosswalk bars, delineators, Tables 2C-4 to 2C-6 confirmed; taper minimum re-attributed); FHWA-HRT-17-026 (rumble strip dimensions, offsets and bicycle gaps confirmed); EPA-CP (asphalt and concrete albedo confirmed; temperature figure corrected; soil albedo found not to be from EPA-CP); LTPP DIM (crack severity widths, block size and edge-crack band confirmed).
