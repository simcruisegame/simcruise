#!/usr/bin/env python3
"""Build docs/how-it-works/index.html and diagram.svg from one model.

Both files come out of the same stage, edge and layer definitions below, so the
static picture embedded in README.md and the interactive page always carry the
same nodes and labels. Standard library only; output is deterministic.

    python3 docs/how-it-works/build.py          # rewrite index.html and diagram.svg
    python3 docs/how-it-works/build.py --check  # exit 1 when the committed files are stale

Every number and file name in the model was read from the code it names
(helper/*.mjs, game/*.gd, scripts/build-macos.sh, licenses/). When one of those
changes, change the model here and rebuild; nothing is fetched or guessed.
"""
from __future__ import annotations

import json
import sys
from html import escape
from pathlib import Path

HERE = Path(__file__).resolve().parent

# ---------------------------------------------------------------------------
# Colours. One set of variables drives the page and both SVGs.
# ---------------------------------------------------------------------------
LIGHT = {
    "bg": "#f6f4ec", "panel": "#ffffff", "ink": "#17231f", "muted": "#5b6761",
    "line": "#8a948d", "edge": "#6c7770", "node": "#ffffff", "node-stroke": "#c8c3b3",
    "node-hover": "#eef6e8", "accent": "#2f7d5a", "accent-soft": "#dcefd9",
    "data": "#3b6ea5", "data-soft": "#e3ecf6", "cache": "#c7791f", "cache-track": "#ece6d6",
    "loaded": "#9fcf86", "loading": "#ffd27a", "unloaded": "#e6e2d6", "car": "#15533e",
    "focus": "#1f5fbf", "chip": "#f1f4f8", "chip-ink": "#223449", "tag": "#5b6c82",
}
DARK = {
    "bg": "#15221f", "panel": "#1d2b27", "ink": "#f1ecd9", "muted": "#a6b0a8",
    "line": "#7d8b84", "edge": "#9aa8a0", "node": "#22322d", "node-stroke": "#3b4c45",
    "node-hover": "#2b3f36", "accent": "#8fd39a", "accent-soft": "#284338",
    "data": "#8fb7e6", "data-soft": "#243546", "cache": "#e8a94f", "cache-track": "#2b3a35",
    "loaded": "#5a9d58", "loading": "#d8a83c", "unloaded": "#2e3b36", "car": "#c6ef96",
    "focus": "#8fc1ff", "chip": "#243546", "chip-ink": "#dbe6f3", "tag": "#9fb2c9",
}
FONT = '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif'
MONO = 'ui-monospace, SFMono-Regular, Menlo, Consolas, monospace'


def css_vars(palette: dict) -> str:
    return "".join(f"--{k}:{v};" for k, v in palette.items())


# ---------------------------------------------------------------------------
# The model. Stages are the loop; layers hang off the "Public data" stage.
# ---------------------------------------------------------------------------
STAGES = [
    {
        "id": "player",
        "title": "Player",
        "tooltip": "Keyboard or Xbox controller in; picture and sound out.",
        "kicker": "You, at the keyboard or the pad",
        "what": (
            "Drive.gd registers the controls as Godot InputMap actions at start-up: W/↑ or the right trigger "
            "to accelerate, S/↓ or the left trigger to brake and reverse, A/D or the left stick to steer, "
            "Space for the service brake, H or A for the rear parking brake, Shift or LB for the clutch, E/Q or RB/X "
            "to shift, I or the right-stick click to restart a stalled engine, C or Y for the camera, F2 or the "
            "left-stick click for the HUD level, F3 for the frame-rate/tile/request counters, Tab or Menu for the road "
            "atlas and / for Find a place. The thumbstick gets its own centre-weighted curve "
            "(Powertrain.thumbstick_command), and keys and pads both drive the Assisted steering map, whose rack travel "
            "is capped by the grip the tyres can use at that speed (available_steering), so a tap at 45 mph is a lane "
            "correction rather than lock. What comes back is one Camera3D and three procedural sound generators."
        ),
        "files": ["game/Drive.gd (_ready, _unhandled_key_input, _unhandled_input)", "game/Hud.gd", "game/Powertrain.gd (thumbstick_command, available_steering)", "game/DrivingPreferences.gd"],
        "sources": [],
        "bytes": "Preferences (transmission, street photos, ground cover, sound, HUD level) are remembered in user://driving-preferences.",
        "limits": "No steering-wheel support yet. The Direct map (the full 35.5° rack, for a device that positions the rack itself) is what the headless benches drive, and --direct-steering selects it in the game.",
    },
    {
        "id": "client",
        "title": "Godot client",
        "tooltip": "Drive.gd owns the scene: tiles, car, camera, HUD, sun and weather, with helper jobs on worker threads.",
        "kicker": "Drive.gd · Godot 4.7.2 · Jolt · 60 physics ticks per second",
        "what": (
            "A Godot 4.7.2 project using the Mobile renderer on Metal and Jolt physics at a fixed 60 Hz. Drive.gd owns "
            "the scene: it decides which tile is needed next and runs one tile job at a time on a worker Thread (the "
            "helper call and the WorldTile build), while each other layer's job (aerial photo, canopy, recent colour, "
            "weather, buildings and roofs, road banks, horizon and more) runs on a worker Thread of its own; it attaches "
            "the finished tiles, parks the car on the nearest mapped road, and drives the camera, HUD, sun and "
            "weather. When the tile under the car, or the one 0.3 s ahead along its velocity, is not loaded yet, the car "
            "is frozen with “Loading the road ahead…” and released with its velocity restored. Bundled "
            "starter tiles (game/data/X_Y.json) skip the helper; everything else is a helper job. A place search is a "
            "jump: the target tile and up to eight neighbours are tried for a matching road before giving up."
        ),
        "files": ["game/Drive.gd", "game/project.godot", "game/Hud.gd", "game/RoadMap.gd (road atlas)", "game/MapHUD.gd (minimap)", "game/DrivingPreferences.gd"],
        "sources": [],
        "bytes": "F3 shows frames per second, active tiles and the running count of upstream requests reported by helper jobs.",
        "limits": "One tile job at a time, so a cold area streams one tile per helper run; a failed tile is retried after 15 s; the road atlas pauses physics while it is open.",
    },
    {
        "id": "grid",
        "title": "Tile grid",
        "tooltip": "Web Mercator zoom-16 tiles, about 486 m at the Virginia start; a 3 × 3 ring streams around the car and the origin is re-centred every 6 tiles.",
        "kicker": "3 × 3 ring around the car · floating origin",
        "what": (
            "The world is Web Mercator zoom-16 tiles: 611 m × cos(latitude) across, about 486 m at the Virginia start "
            "and 400 to 550 m over the lower 48 (Drive.scale_at). stream() keeps the 3 × 3 ring around the car: every "
            "missing neighbour is scored by its distance now (weight 0.4) and 5 s ahead along the velocity (0.6), the "
            "best one is launched, and tiles more than one step away are freed. Shared edge heights are collected from "
            "loaded neighbours (TerrainEdges) so seams match. Physics coordinates stay small: when the car is 6 or more "
            "tiles from the origin, rebase() shifts every tile, the car, camera and reflection probe back by whole tiles."
        ),
        "files": ["game/Drive.gd (stream, tile_id, tile_offset, rebase, _physics_process)", "game/TerrainEdges.gd", "game/WorldTile.gd (seam_boundaries)"],
        "sources": [],
        "bytes": "A tile within min(400 m, 0.94 × side) of the car qualifies for streaming; the horizon beyond the ring is the display-only regional mesh.",
        "limits": "Nothing drivable exists beyond the ring; a boundary road is only seamless when both tiles received the same source heights.",
    },
    {
        "id": "helper",
        "title": "Atlas helper",
        "tooltip": "Bun-compiled native binary beside the app; one process per job; one rate-limited fetcher; a 96 MiB least-recently-used cache.",
        "kicker": "helper/atlas.mjs → tools/atlas · one process per job · ≤ 96 MiB cache",
        "what": (
            "helper/atlas.mjs and the modules beside it are compiled by Bun into one native executable, tools/atlas, "
            "installed as Contents/MacOS/atlas next to the game. Godot never opens a socket: for every job (tile, aerial, "
            "canopy, buildings, roofs, road-layout, road-banks, landscape, conditions, recent, street-sample, search and "
            "more) a worker thread runs the helper once with the cache directory and coordinates; the helper fetches, "
            "decodes and converts, writes PNG and JSON files into the cache and prints one JSON object. atlas-cache.mjs "
            "is the single fetcher: per-host concurrency slots (4 for tiles.openfreemap.org, 1 elsewhere), a minimum "
            "spacing per host, Retry-After cooldowns on 429/503, time-to-live per entry, byte budgets and atomic writes. "
            "cache-eviction.mjs then trims the cache to 96 MiB, least recently used first, leaving files just handed to "
            "Godot alone for 60 s."
        ),
        "files": ["helper/atlas.mjs", "helper/atlas-cache.mjs", "helper/cache-eviction.mjs", "scripts/build-macos.sh (bun build --compile)", "game/Drive.gd (OS.execute)"],
        "sources": [],
        "bytes": "Cache cap 96 MiB under user://map-cache (~/Library/Application Support/Godot/app_userdata/SimCruise/map-cache); default TTL 30 days, the tile manifest 1 day, searches 7 days; 9 s request timeout; User-Agent SimCruise/1.1.",
        "limits": "One process per job pays Bun start-up on every call; a provider pause stops that host until Retry-After; a burst of recently used files can briefly exceed the budget.",
    },
    {
        "id": "sources",
        "title": "Public data",
        "tooltip": "Fifteen layers, each fetched only when a job needs it, each with its own licence.",
        "kicker": "Fifteen layers, each fetched only when a job needs it",
        "what": (
            "Every layer is a free, keyless public service and is read on demand by one helper module. The base road "
            "tile needs only vector tiles and elevation; everything else arrives later and refines what is already "
            "drivable. ODbL and ShareAlike data stays in the player's cache and is never committed to the repository "
            "except the marked starter tiles. Click a layer for its source, licence, files and limits."
        ),
        "files": ["helper/*.mjs (one module per layer)", "licenses/Road Atlas.txt", "Credits.txt"],
        "sources": [],
        "bytes": "",
        "limits": "Coverage, dates and latency differ per provider; the game keeps driving when a layer is slow or missing.",
    },
    {
        "id": "worldtile",
        "title": "WorldTile builder",
        "tooltip": "Turns one tile's JSON into terrain (65 × 65 heights), roads with lane paint and curbs, buildings, trees, grass and a collision trimesh.",
        "kicker": "WorldTile.build(): JSON → terrain, roads, buildings, trees, collision",
        "what": (
            "WorldTile.build() turns one tile's JSON into geometry on the worker thread. A 65 × 65 height grid (grid 64; 33 × 33 "
            "only in 25 legacy starter tiles) "
            "becomes the terrain mesh after road-corridor smoothing: a robust local-linear fit of each centreline "
            "(Gaussian window, ±27 m) keeps real grades, rejects ditch and bank outliers and blends corridor nodes onto "
            "the profile with a clamped cross slope, never moving authored, surveyed or boundary nodes. Roads are drawn "
            "into a 1024-px semantic surface texture (pavement, curbs, asphalt, dashes, crosswalks) plus raised lane-paint "
            "quads; sidewalks and curbs get collision; buildings are extruded from footprints with wall finishes, "
            "storefronts, roof tags and measured or inferred roof shapes; canopy crowns are placed off roads and "
            "footprints for Grove's three mesh detail levels; Rangeland adds shrubs and grass. The ground and walls become "
            "a ConcavePolygonShape3D collider, and wheel_grip() answers the surface class under each tyre."
        ),
        "files": ["game/WorldTile.gd", "game/Grove.gd", "game/Junction.gd", "game/RoadLayout.gd", "game/LidarRoof.gd", "game/Rangeland.gd", "game/GroundFinish.gd", "game/terrain.gdshader"],
        "sources": [],
        "bytes": "Surface classes: 1.0 paved, 0.76 gravel or mapped unpaved, 0.65 verge or field, 0.72 while a tile is still loading.",
        "limits": "Facades, road widths and tree species are representative unless a source says otherwise; no grade-separated bridge decks; a tile is swapped in whole, so later passes (buildings, roofs, lane paint) replace layers rather than edit them.",
    },
    {
        "id": "powertrain",
        "title": "Powertrain",
        "tooltip": "Jolt rigid body with four raycast suspension corners, slip-angle tyres, 5-speed, clutch and differential, calibrated to the 1999 road-test numbers.",
        "kicker": "Powertrain.gd + NB1999Sport.gd · 1,043 kg · 51.5 % front",
        "what": (
            "Powertrain.gd is a Jolt RigidBody3D of 1,043 kg with a 51.5 % front weight split, custom centre of mass "
            "and inertia. Each physics tick it casts four rays at the suspension mounts and computes spring, damper and "
            "anti-roll loads from NB1999Sport.gd (168/130 lb/in coils, lever ratios and bar dimensions from the NB Sport "
            "row of a published suspension calculator, a published Bilstein rear dynamometer trace, an estimated front "
            "damper); a slip-angle tyre model builds lateral force over a 0.35 m relaxation length and shares one grip "
            "budget between cornering and braking. Six sub-steps then run the engine (tabulated torque to 7,000 rpm, "
            "cut at 7,040), a clutch that can stall in manual mode, the 5-speed (3.136, 1.888, 1.330, 1.000, 0.814; "
            "reverse 3.758; final drive 4.30), a Torsen-style differential with a 2:1 bias cap, separate front and rear "
            "brakes (1,150 / 650 N·m, parking brake 900 N·m rear), aerodynamic drag (½ · 1.225 · 0.65 · v²) and rolling "
            "resistance. The surface class from WorldTile scales each tyre's friction."
        ),
        "files": ["game/Powertrain.gd", "game/NB1999Sport.gd", "game/checks/Track.gd", "game/checks/TurningCircle.gd", "game/checks/RideCheck.gd", "game/checks/Wall.gd"],
        "sources": [
            {"name": "Car and Driver, 1999 Mazda MX-5 Miata road test", "licence": "calibration targets", "note": "0–60 mph 7.97 s, 70–0 mph 173 ft, 300 ft skidpad 0.89 g"},
            {"name": "Fat Cat Motorsports NB Ride Harmonizer, NB Sport row", "licence": "published suspension data", "note": "coil rates, lever ratios, bar dimensions; Bilstein Hard S rear dyno trace"},
            {"name": "Mazda specifications", "licence": "published", "note": "gear and final-drive ratios, 195/50R15 tyres"},
        ],
        "bytes": "Headless Track.gd at 60 Hz: 0–60 mph 7.95 s (target 7.97 ± 0.10), 70–0 mph 173.0 ft (173 ± 3), skidpad 0.885 g (0.89 ± 0.01); TurningCircle.gd 30.4–30.6 ft against the 30.2 ft specification.",
        "limits": "No public tyre, dyno or front-damper data exists for this exact car; the relaxation length and differential bias are provisional; the tyre curve peaks near 14° of slip where a real curve is sharper.",
    },
    {
        "id": "render",
        "title": "Picture & sound",
        "tooltip": "Mobile renderer on Metal with one sun on the real clock, haze and 180 m shadows; a damped head camera; procedural engine, tyre and wind sound.",
        "kicker": "Mobile renderer on Metal · one sun · real clock · procedural audio",
        "what": (
            "Godot's Mobile renderer on Metal: 2× MSAA at 0.9 render scale, debanding, 16× anisotropic filtering, a "
            "Filmic tonemap whose exposure is set for the sunlit ground, and one DirectionalLight3D sun with a 2,048-px "
            "shadow map in four blended splits reaching 180 m. It is the only sun: on this renderer the sky radiance map "
            "and reflection probe clip sky radiance at 2.0, so the Poly Haven panorama's photographed solar disc never "
            "lights the scene (SurveySky.gdshader also caps it at 8 for an HDR path). The sun follows the real clock in "
            "30 s steps; until a night mode exists it is held at 7° elevation, on the sunset bearing through the evening "
            "and the sunrise bearing after solar midnight; one haze model takes fog colour from the sky "
            "(aerial perspective) and density from METAR visibility (Koschmieder). The driver view is a damped head "
            "(9 rad/s spring, 70 % horizon follow, gaze lead, micro-vibration) with chase and scenic views on C; the HUD "
            "has Off, Glance and Full levels on F2. Sound is synthesised at 22.05 kHz with no sample files: "
            "InlineFourSound fires the four-cylinder at rpm/30 Hz with limiter stutter above 6,950 rpm and overrun pops; "
            "RoadNoise adds tyre hiss (∝ v^1.5), gravel crunch, suspension rumble and wind (∝ v²) on a "
            "Master ← Cabin ← Engine/Road/Wind bus tree with a 2.4 kHz cabin low-pass in the driver view."
        ),
        "files": ["game/SurveyDaylight.gd", "game/SurveySky.gdshader", "game/DaylightWeather.gd", "game/Cockpit.gd (head_view)", "game/Hud.gd", "game/InlineFourSound.gd", "game/RoadNoise.gd", "game/project.godot"],
        "sources": [
            {"name": "Poly Haven day HDR panorama and CC0 textures", "licence": "CC0", "note": "art/day.hdr; its solar disc is clipped on the Mobile path and capped in the sky shader"},
            {"name": "NOAA Global Monitoring Laboratory solar position equations", "licence": "US public domain", "note": "SurveyDaylight.solar_for_day"},
        ],
        "bytes": "Checks: LightingCheck.gd holds the clear-sky shadow-to-sun ratio to the 0.18–0.32 photographs show and checks the 7° night hold on the sunset and sunrise bearings; RoadNoiseCheck.gd measures each audio layer; HeadCameraCheck.gd and HudLevelCheck.gd cover the view and HUD.",
        "limits": "No night or seasons yet; the Mobile renderer has no SSAO, SSR, global illumination or volumetric fog; engine envelopes are approximations, not microphone measurements; the renderer is tested only on Apple Silicon Macs.",
    },
]

# Public data layers. tag = licence shown on the chip; sources carry the detail.
LAYERS = [
    {
        "id": "vector", "title": "Roads & buildings", "tag": "ODbL",
        "tooltip": "OpenStreetMap via OpenFreeMap vector tiles: roads, footprints, land use, water, places. ODbL 1.0.",
        "kicker": "OpenStreetMap via OpenFreeMap vector tiles · the base of every tile",
        "what": "One zoom-14 vector tile covers a 4 × 4 block of driving tiles. The helper clips roads (class, one-way, name, estimated width, surface tag), building footprints with mapped heights, landcover, park and water polygons, named points of interest and pedestrian ways to each zoom-16 cell, and names unnamed road pieces from the transportation_name layer. The road atlas (Tab) draws lower-zoom tiles from the same source. The tile manifest is refreshed daily.",
        "files": ["helper/atlas.mjs (chunk)", "helper/vector-tile.mjs", "helper/pedestrian.mjs", "helper/cartography.mjs", "game/WorldTile.gd", "game/RoadMap.gd"],
        "sources": [
            {"name": "OpenStreetMap contributors", "licence": "ODbL 1.0", "url": "https://www.openstreetmap.org/copyright", "note": "data"},
            {"name": "OpenFreeMap (OpenMapTiles schema)", "licence": "ODbL 1.0 data; free hosting", "url": "https://openfreemap.org/", "note": "tiles.openfreemap.org, 4 concurrent requests allowed"},
        ],
        "bytes": "Vector tiles are gzip-compressed PBF of varying size, cached 30 days; the manifest 1 day. Starter tiles under game/data/ are marked as ODbL derivative databases.",
        "limits": "Road widths are estimated from class unless a survey says otherwise; ODbL data is cached on the player's disk and never redistributed in the MIT repository.",
    },
    {
        "id": "search", "title": "Place search", "tag": "ODbL",
        "tooltip": "Photon geocoder on OpenStreetMap data; US results only; 7-day cache.",
        "kicker": "Photon geocoder on OpenStreetMap data",
        "what": "Find a place sends the text (trimmed to 200 characters) once to Photon with limit 6. Results are filtered to the US, street abbreviations normalised (rd → road, …), and a typed street with a town must match the street and the town or county. Coordinates and the bundled Virginia places never touch the network.",
        "files": ["helper/atlas.mjs (search)", "game/Drive.gd (search, work)", "game/Hud.gd"],
        "sources": [
            {"name": "Photon (komoot)", "licence": "ODbL 1.0 data", "url": "https://photon.komoot.io/", "note": "photon.komoot.io, 1 s spacing"},
        ],
        "bytes": "Each query is cached 7 days under its hash.",
        "limits": "US places only; no autocomplete; one request per search.",
    },
    {
        "id": "overture", "title": "Building heights & roofs", "tag": "ODbL",
        "tooltip": "Overture buildings archive (heights, roof tags, extra footprints) checked against Esri's OSM replica; the Overture base theme's infrastructure layer for traffic signals.",
        "kicker": "Overture Maps buildings archive + Esri OSM replica · a second pass after the road tile",
        "what": "The active district and the one ahead get a building pass once the road tile is drivable. The helper reads byte ranges of Overture's versioned buildings PMTiles archive, pairs each base footprint with an Overture footprint by rasterised overlap (intersection over union ≥ 0.87 on a 32 × 32 sample grid, one partner on each side), applies a precise height when the OSM way revision and numeric tag agree, passes roof shape, material and colour tags, and admits extra Microsoft footprints that clear roads and existing buildings. Signalised crossings come from the Overture base theme archive (zoom 13, infrastructure layer, transportation subtype, traffic_signals class), kept only for single-source OpenStreetMap signal nodes and then matched to the OpenFreeMap road layer.",
        "files": ["helper/overture-buildings.mjs", "helper/overture-archive.mjs", "helper/verified-footprints.mjs", "helper/signal-controls.mjs", "game/BuildingStream.gd", "game/ObservedBuildings.gd", "game/SignalControls.gd"],
        "sources": [
            {"name": "Overture Maps Foundation buildings (OpenStreetMap, Microsoft ML Buildings, Esri Community Maps)", "licence": "ODbL 1.0; CC BY 4.0 for Esri entries", "url": "https://docs.overturemaps.org/attribution/", "note": "stac.overturemaps.org catalogue, PMTiles ranges on S3"},
            {"name": "Esri OpenStreetMap Buildings for North America", "licence": "ODbL 1.0", "url": "https://www.openstreetmap.org/copyright", "note": "services6.arcgis.com replica for way revisions and raw tags"},
        ],
        "bytes": "Budget 2 MiB and 7 calls per district (overture-buildings.mjs); signals ≤ 768 KiB and 1,600 records; archive entries live 45 days.",
        "limits": "Only attributed flat or gabled roofs, material classes and tagged colours are drawn; unsupported geometry keeps its form; the base road never waits for this pass.",
    },
    {
        "id": "lanes", "title": "Lanes & one-way", "tag": "ODbL",
        "tooltip": "Esri-hosted OpenStreetMap North America highways replica: lane, one-way and bridge tags for a 2 × 2 tile group, used to drop the inferred centre line on single-lane one-way roads.",
        "kicker": "Esri's OSM North America highway replica · lane counts and direction",
        "what": "For a 2 × 2 tile group the helper asks the replica for named one-way roads with their lanes, bridge, tunnel, layer and junction tags. Where a stretch of a tile's major road matches exactly one explicit single-lane one-way way (no bridge, tunnel, layer or junction), RoadLayout removes the inferred centre dashes along it; it adds no lane dashes of its own, and surveyed paint from a bundled road-layout file is drawn as raised stripes. WorldTile prepares the new lane paint on a worker thread and swaps only that layer.",
        "files": ["helper/road-layout.mjs", "game/RoadLayout.gd", "game/WorldTile.gd (prepare_lane_update, install_lane_update)", "game/BuildingStream.gd (layout job)"],
        "sources": [
            {"name": "OpenStreetMap contributors via Esri's OSM_NA_Highways feature service", "licence": "ODbL 1.0", "url": "https://www.openstreetmap.org/copyright", "note": "services6.arcgis.com"},
        ],
        "bytes": "≤ 280 features per group, 28-day lifetime.",
        "limits": "Explicit lane counts exist on only part of the network; elsewhere stripes are inferred from road class and estimated width.",
    },
    {
        "id": "elevation", "title": "Ground elevation", "tag": "PD",
        "tooltip": "USGS 3DEP 1/3-arc-second DEM blocks and Terrarium tiles for each tile's 65 × 65 height grid; 1 m 3DEP for road banks. US public domain.",
        "kicker": "USGS 3DEP 1/3″ GeoTIFF ranges · Terrarium zoom-13 tiles · 1 m banks",
        "what": "One Terrarium zoom-13 tile serves an 8 × 8 block of driving tiles. Where a 3DEP 1/3-arc-second product exists, the helper reads only the GeoTIFF directory and the compressed raster blocks under the tile and its road apron, fits paved-road grades to the samples (preserving measured terrain near mapped bridges, tunnels and clipped boundaries) and hands Godot a 65 × 65 grid (grid 64, a node about every 7.6 m at the start; only the 25 legacy starter tiles carry 33 × 33). Beside mapped roads a 129 × 129 window of 1 m bare earth from the 3DEP image service can add banks.",
        "files": ["helper/road-grade.mjs", "helper/usgs-ground.mjs", "helper/usgs-elevation-products.json", "helper/bank-source.mjs", "helper/road-banks.mjs", "game/WorldTile.gd (height_at, smooth_road_corridors)", "game/RoadBankStream.gd", "game/RoadBankMesh.gd"],
        "sources": [
            {"name": "USGS 3D Elevation Program, 1/3-arc-second seamless DEM and 1 m products (The National Map)", "licence": "US public domain", "url": "https://www.usgs.gov/3d-elevation-program/about-3dep-products-services", "note": "prd-tnm.s3.amazonaws.com, elevation.nationalmap.gov"},
            {"name": "Mapzen / Tilezen Terrain Tiles on AWS (Terrarium)", "licence": "public data with per-source attribution", "url": "https://registry.opendata.aws/terrain-tiles/", "note": "s3.amazonaws.com/elevation-tiles-prod"},
        ],
        "bytes": "A 14 KB product index (usgs-elevation-products.json) locates the national 1/3″ tiles; a 32 KiB GeoTIFF header then only the needed blocks; bank TIFF ≤ 100 KiB; 1 s spacing to federal hosts.",
        "limits": "No bridge decks: terrain is preserved there instead. The grid point-samples the elevation model, so one node can sit on the pavement and the next in the ditch; that is why WorldTile smooths road corridors.",
    },
    {
        "id": "aerial", "title": "Aerial imagery", "tag": "PD",
        "tooltip": "USGS National Map imagery (1024-px JPEG per 4 × 4 block), USDA NAIP for Hawaii, USFS Alaska orthophotos. Public domain.",
        "kicker": "USGS The National Map · USDA NAIP · USDA Forest Service Alaska",
        "what": "After a tile is drivable, the helper fetches one 1,024 × 1,024 JPEG for its 4 × 4 block from the National Map imagery export, or a 512-px PNG from the NAIP Hawaii or Forest Service Alaska image services where those cover the spot. The image drapes the ground and low roofs and feeds a small logistic classifier (RoadEdges) that refines shoulders from the pixels against the mapped centrelines.",
        "files": ["helper/atlas.mjs (photograph)", "helper/regional-aerial.mjs", "game/Drive.gd (stream_photo)", "game/WorldTile.gd (apply_photo, prepare_pixels)", "game/RoadEdges.gd"],
        "sources": [
            {"name": "USGS The National Map, USGSImageryOnly", "licence": "US public domain", "url": "https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer", "note": "basemap.nationalmap.gov"},
            {"name": "USDA Farm Service Agency NAIP 2021 Hawaii; USDA Forest Service Alaska Region orthophotos", "licence": "US public domain / CC0", "note": "imagery.geoplatform.gov"},
            {"name": "Virginia VGIN orthoimagery for the bundled start", "licence": "public domain (VGIN clarification)", "note": "bundled in game/data/"},
        ],
        "bytes": "1,024 × 1,024 JPEG per 16 tiles; regional PNG ≤ 2 MiB; a failed image is remembered as a .miss file for one hour.",
        "limits": "Dates and resolution vary (keyless imagery tops out near 0.6 m/px); blank or transparent regional mosaics fall back to terrain colour.",
    },
    {
        "id": "canopy", "title": "Tree canopy", "tag": "CC BY 4.0",
        "tooltip": "WRI / Meta High Resolution Canopy Height Maps v2, one 512-px block per tile. CC BY 4.0.",
        "kicker": "WRI / Meta canopy height map v2 · crown peaks become trees",
        "what": "The canopy job reads the one 512-pixel block of the zoom-10 Cloud-Optimised GeoTIFF that covers the tile, finds crown peaks on a 128-cell grid and returns heights and crown widths. WorldTile keeps crowns off roads and footprints; Grove instances three mesh detail levels of each crown, so nothing is a sprite.",
        "files": ["helper/canopy.mjs", "game/Drive.gd (stream_canopy)", "game/WorldTile.gd (replace_crowns, foliage)", "game/Grove.gd"],
        "sources": [
            {"name": "WRI and Meta, High Resolution Canopy Height Maps v2", "licence": "CC BY 4.0", "url": "https://registry.opendata.aws/dataforgood-fb-forestsv2/", "note": "dataforgood-fb-data S3 bucket"},
        ],
        "bytes": "One 512 × 512 8-bit block per tile.",
        "limits": "Species and branch shapes are inferred; crowns are clamped between 2.5 and 78 m; the mapped canopy date is not the drive date.",
    },
    {
        "id": "sentinel", "title": "Recent colour", "tag": "Copernicus",
        "tooltip": "Copernicus Sentinel-2 L2A via Element 84 Earth Search: recent cloud-masked ground colour and scene class. Optional.",
        "kicker": "Copernicus Sentinel-2 via Element 84 Earth Search · optional Imagery layer",
        "what": "With Imagery on, the helper searches recent scenes for the zoom-14 neighbourhood, drops cloud, shadow and no-data pixels by scene classification and composites a 128-px surface-colour window plus a bare-soil mask; the atlas shows the accepted capture date. Summit snow on distant ridges comes from the same catalogue.",
        "files": ["helper/recent.mjs", "helper/bare-reflectance.mjs", "helper/summit-snow.mjs", "helper/ridge-band.mjs", "game/Drive.gd (stream_recent)", "game/terrain.gdshader", "game/SummitSnow.gd"],
        "sources": [
            {"name": "Copernicus Sentinel-2 L2A, accessed through Element 84 Earth Search", "licence": "Copernicus Sentinel data terms (free, attribution)", "url": "https://earth-search.aws.element84.com/v1/collections/sentinel-2-c1-l2a", "note": "earth-search.aws.element84.com"},
        ],
        "bytes": "128-px window read as GeoTIFF block ranges, each ≤ 4 MiB; re-checked daily per neighbourhood.",
        "limits": "10 m pixels colour the ground only; clouds can leave gaps; a neighbourhood keeps the survey imagery until a clear scene exists.",
    },
    {
        "id": "weather", "title": "Weather & sky", "tag": "PD",
        "tooltip": "NOAA METAR observations, the IEM station archive, GOES cloud mask and NASA snow drive sky, fog and lighting. Public domain.",
        "kicker": "NOAA METAR · IEM archive · GOES cloud mask · NASA GIBS snow",
        "what": "With Weather on, Drive asks every 15 minutes for the METARs in a 1.3° × 1.5° latitude-longitude box around the car and uses the nearest station within 85 km that reported in the last 2.5 hours: cloud cover swaps in the procedural overcast sky and visibility sets the fog density. A dated street photograph can instead reconstruct its archived sky from the Iowa Environmental Mesonet station archive, kept only when the GOES clear-sky mask agrees. NASA Terra snow observations colour distant relief.",
        "files": ["helper/conditions.mjs", "helper/photo-weather.mjs", "helper/observed-cloud.mjs", "helper/cloud-reader.mjs", "helper/sky-fusion.mjs", "helper/regional-snow.mjs", "game/DaylightWeather.gd", "game/ArchiveWeather.gd", "game/SurveyDaylight.gd", "game/RegionalSnow.gd"],
        "sources": [
            {"name": "NOAA Aviation Weather Center METAR API", "licence": "US public domain", "url": "https://aviationweather.gov/data/api/", "note": "aviationweather.gov, 60 s spacing"},
            {"name": "Iowa Environmental Mesonet ASOS/AWOS archive", "licence": "US public domain data", "url": "https://mesonet.agron.iastate.edu/request/download.phtml", "note": "mesonet.agron.iastate.edu, 61 s spacing"},
            {"name": "NOAA GOES-R ABI Clear Sky Mask (NOAA Open Data Dissemination)", "licence": "US public domain", "url": "https://registry.opendata.aws/noaa-goes/", "note": "read with h5wasm; 180 ms spacing"},
            {"name": "NASA GIBS Terra snow cover", "licence": "US public domain", "note": "gibs.earthdata.nasa.gov"},
        ],
        "bytes": "METAR JSON cached 15 minutes; archive and satellite records cached a year.",
        "limits": "Observations may come from a nearby airfield; no precipitation, night or seasons yet.",
    },
    {
        "id": "photos", "title": "Street photos", "tag": "CC BY-SA",
        "tooltip": "KartaView and Panoramax thumbnails sample pavement and verge colour near the capture track. CC BY-SA 4.0, credits kept.",
        "kicker": "KartaView · Panoramax · colour hints near a short capture track",
        "what": "With Street photos on, a cold lookup is one catalogue request and up to three processed thumbnails near the tile. Three consistent daylight views can shift pavement and green-verge colour within 85 m of the capture track; shaded, obstructed or dissimilar views keep the existing materials. Photo credit on the atlas opens the attributed sequence; Panoramax supplies licensed camera poses where KartaView has none.",
        "files": ["helper/street-samples.mjs", "helper/street-appearance.mjs", "helper/panoramax.mjs", "game/StreetDetail.gd"],
        "sources": [
            {"name": "Grab and KartaView contributors", "licence": "CC BY-SA 4.0", "url": "https://kartaview.org/terms", "note": "api.kartaview.org, cdn.kartaview.org"},
            {"name": "Panoramax contributors", "licence": "CC BY-SA 4.0 / CC BY 4.0 / CC0 per photo", "url": "https://panoramax.xyz/", "note": "api.panoramax.xyz, 1 s spacing; images stay on the host"},
        ],
        "bytes": "Catalogue list ≤ 384 KiB, each thumbnail ≤ 320 KiB, Panoramax ≤ 480 KiB; its own side cache of at most 640 files / 32 MiB.",
        "limits": "Colours describe the capture date, not today; coverage is sparse outside cities.",
    },
    {
        "id": "nsi", "title": "Building use", "tag": "PD",
        "tooltip": "US Army Corps of Engineers National Structure Inventory: modelled use and storeys for a footprint. Public domain.",
        "kicker": "USACE National Structure Inventory · a use for a generic footprint",
        "what": "An inventory point that falls unambiguously inside a matched generic footprint supplies single-family, duplex, multi-family or non-residential use and an optional storey count. A residential use suppresses inferred storefronts along large streets and sizes house floors. No raw response is written to disk.",
        "files": ["helper/nsi-buildings.mjs", "game/BuildingStream.gd", "game/WorldTile.gd (survey_use, survey_building)"],
        "sources": [
            {"name": "US Army Corps of Engineers, National Structure Inventory structures API", "licence": "US public domain", "url": "https://www.hec.usace.army.mil/confluence/nsi/technicalreferences/latest/technical-documentation", "note": "nsi.sec.usace.army.mil"},
        ],
        "bytes": "≤ 768 KiB and 1,024 rows per request.",
        "limits": "NSI is a modelled hazard inventory; uses and storey counts can differ from current conditions; ambiguous footprints keep their original classification.",
    },
    {
        "id": "trees", "title": "City trees", "tag": "PDDL · city",
        "tooltip": "Lincoln NE and Cambridge MA municipal tree inventories move canopy crowns onto recorded trunks. City open-data terms; PDDL 1.0 (Cambridge).",
        "kicker": "Lincoln, Nebraska and Cambridge, Massachusetts inventories",
        "what": "In the two covered cities the canopy job also requests one bounded city tile of stems (location, diameter, species). Uniquely matching canopy crowns move onto the recorded trunks and some extra stems are sized from nearby trees of similar diameter; buildings and pavement can block a placement.",
        "files": ["helper/civic-trees.mjs", "helper/municipal-crowns.mjs", "game/MunicipalCrownDisplay.gd", "game/WorldTile.gd (replace_crowns)"],
        "sources": [
            {"name": "City of Lincoln / Lancaster County, Parks and Recreation tree inventory", "licence": "City open-data policy", "note": "gis.lincoln.ne.gov"},
            {"name": "City of Cambridge GIS, Environmental Street Trees", "licence": "PDDL 1.0", "url": "https://opendatacommons.org/licenses/pddl/1-0/", "note": "services1.arcgis.com"},
        ],
        "bytes": "≤ 448 KiB and 650 rows per request.",
        "limits": "Other cities use the canopy model alone; an inventory's edit timestamp is not a survey date.",
    },
    {
        "id": "lidar", "title": "Lidar roofs & camber", "tag": "PD · CC0",
        "tooltip": "USGS 3DEP point clouds (Entwine Point Tiles on AWS) fit roof shapes and road camber where a flight exists; Miami-Dade lidar for 15 bundled roofs.",
        "kicker": "USGS 3DEP Entwine Point Tiles · Miami-Dade 2021 lidar",
        "what": "Where a USGS project covers the district, the roofs job reads bounded node ranges of classified returns (decoded with laz-perf), fits roof planes clipped to each footprint and, in districts with verified road registers, resolves road crown and camber into the driving mesh. The lab studies under lab/lidar/ (Miami, Charleston, Cheyenne, Bartlesville, Anchorage) document the method and the houses that failed to qualify.",
        "files": ["helper/roof-survey.mjs", "helper/roof-stream.mjs", "helper/roof-fit.mjs", "helper/road-crossfall.mjs", "helper/lidar-projects.json", "game/BuildingStream.gd (roofs)", "game/LidarRoof.gd", "game/RoadCrossfallGround.gd", "lab/lidar/"],
        "sources": [
            {"name": "USGS 3D Elevation Program lidar point clouds (EPT on AWS)", "licence": "US public domain", "url": "https://registry.opendata.aws/usgs-lidar/", "note": "usgs-lidar-public S3; project index at index.nationalmap.gov"},
            {"name": "Miami-Dade County ITD 2021 lidar via NOAA Office for Coastal Management", "licence": "CC0 1.0", "url": "https://www.fisheries.noaa.gov/inport/item/76028", "note": "15 roofs bundled for the South Miami Avenue tile"},
        ],
        "bytes": "Bounded EPT node reads; results cached per district together with the map fingerprint.",
        "limits": "Runtime roofs need a qualifying flight and locally consistent ground; arbitrary roof shapes are not reconstructed nationwide; survey dates differ from imagery dates.",
    },
    {
        "id": "landcover", "title": "Forest & shrub cover", "tag": "PD",
        "tooltip": "LANDFIRE and NLCD stand types, Rangeland Analysis Platform shrub cover, RCMAP shrub height and NAIP low vegetation. Public domain.",
        "kicker": "LANDFIRE · NLCD · Rangeland Analysis Platform · RCMAP · NAIP",
        "what": "One zoom-14 stand observation serves sixteen driving tiles and turns a majority of unsurveyed crowns into conifers where the stand says so; rangeland density and height place swaying grass and shrub clumps, mown beside mapped roads; the opt-in USDA ground cover pass detects low vegetation from NAIP pixels paired with the canopy tile.",
        "files": ["helper/forest-form.mjs", "helper/shrub-cover.mjs", "helper/spectral-ground.mjs", "game/ForestFormStream.gd", "game/ForestForm.gd", "game/Rangeland.gd", "game/SpectralGround.gd"],
        "sources": [
            {"name": "USGS/USDA LANDFIRE Existing Vegetation Type; USGS/MRLC NLCD", "licence": "US public domain", "url": "https://landfire.gov/vegetation/evt", "note": "lfps.usgs.gov, mrlc.gov"},
            {"name": "USDA Rangeland Analysis Platform shrub cover; MRLC RCMAP shrub height", "licence": "US public domain", "url": "https://www.mrlc.gov/data/rcmap-shrub-height", "note": "storage.googleapis.com, mrlc.gov"},
            {"name": "USDA NAIP via the GeoPlatform image service", "licence": "US public domain", "note": "imagery.geoplatform.gov, opt-in"},
        ],
        "bytes": "Stand rasters ≤ 96 KiB; NAIP budget 200 KiB per request; shrub rasters 256 px.",
        "limits": "30 m stand classes describe a majority, not each tree; plant shapes and blades are representative.",
    },
    {
        "id": "landscape", "title": "Horizon terrain", "tag": "PD · ISC",
        "tooltip": "A display-only relief mesh reaching 45 km: 1,025 × 1,025 Terrarium heights simplified by Martini, draped with National Map imagery, ridge imagery and snow.",
        "kicker": "Terrarium + National Map imagery · 45 km · Martini mesh",
        "what": "A separate worker samples a 1,025 × 1,025 grid of heights reaching 45 km from the car on every side from coarse Terrarium tiles; first a 512-spoke × 128-ring skyline scan picks the few cells where finer elevation is worth fetching. Martini simplifies the grid (4 m tolerance, raised until at most 140,000 triangles remain), the ocean is masked with Natural Earth polygons, National Map images are draped on it, and Godot receives a reusable binary mesh. Ridge imagery, summit snow and Sentinel colour are draped on relief in the driver's view.",
        "files": ["helper/regional-terrain.mjs", "helper/landscape-roads.mjs", "helper/regional-ocean.mjs", "helper/ridge-imagery.mjs", "game/RegionalTerrain.gd", "game/RidgeView.gd", "game/RegionalTerrain.gdshader"],
        "sources": [
            {"name": "Mapzen / Tilezen Terrain Tiles; USGS National Map imagery", "licence": "public domain / per-source attribution", "url": "https://registry.opendata.aws/terrain-tiles/", "note": "as for Ground elevation and Aerial imagery"},
            {"name": "Natural Earth Ocean 1:10m", "licence": "public domain", "url": "https://www.naturalearthdata.com/about/terms-of-use/", "note": "helper/coast-atlas.json"},
            {"name": "Mapbox Martini 0.2.0", "licence": "ISC", "note": "licenses/Martini-ISC.txt"},
        ],
        "bytes": "Mesh cached as relief-tin-v4-X-Y.bin; the horizon can take several seconds on a first visit.",
        "limits": "No collision beyond the tile ring; distant roads are only painted, within a 13 km middle band, from motor-road corridors in OpenFreeMap tiles already in the cache; seasonal appearance follows source dates.",
    },
]

# Edges of the loop. Geometry is in SVG units; labels sit beside the arrows,
# split over two lines where the 50-unit gap between columns needs it.
EDGES = [
    {"id": "e-input", "d": "M165,190 H213", "label": "input", "lx": 189, "ly": 180, "anchor": "middle"},
    {"id": "e-next", "d": "M415,190 H463", "label": "next tile", "lx": 439, "ly": 180, "anchor": "middle"},
    {"id": "e-exec", "d": "M665,190 H713", "label": "atlas tile", "label2": "X Y", "lx": 689, "ly": 168, "anchor": "middle"},
    {"id": "e-fetch", "d": "M915,190 H963", "label": "HTTPS", "lx": 939, "ly": 180, "anchor": "middle", "both": True},
    {"id": "e-json", "d": "M815,272 V418", "label": "JSON on stdout", "label2": "PNG · JPG in cache", "lx": 824, "ly": 338, "anchor": "start"},
    {"id": "e-build", "d": "M715,490 H667", "label": "collision", "label2": "grip", "lx": 691, "ly": 472, "anchor": "middle"},
    {"id": "e-body", "d": "M465,490 H417", "label": "pose", "label2": "rpm · load", "lx": 441, "ly": 472, "anchor": "middle"},
    {"id": "e-out", "d": "M215,490 H167", "label": "picture", "label2": "sound", "lx": 191, "ly": 472, "anchor": "middle"},
    {"id": "e-cam", "d": "M300,272 V418", "label": "camera · HUD · sun", "lx": 292, "ly": 348, "anchor": "end"},
    {"id": "e-ctl", "d": "M386,272 V330 H565 V418", "label": "pedals · steering · 60 Hz", "lx": 476, "ly": 322, "anchor": "middle"},
]

# Box geometry: x, y, w, h. Five columns: player 136, three stages of 200,
# the data column 206, with 50-unit gaps; the top row is 160 tall to hold the
# mini tile grid and the cache gauge.
BOX = {
    "player": (29, 112, 136, 448),
    "client": (215, 112, 200, 160),
    "grid": (465, 112, 200, 160),
    "helper": (715, 112, 200, 160),
    "sources": (965, 112, 206, 448),
    "render": (215, 420, 200, 140),
    "powertrain": (465, 420, 200, 140),
    "worldtile": (715, 420, 200, 140),
}
W, H = 1200, 600
GRID_COLS, GRID_ROWS, CELL, GAP = 7, 3, 23, 3
GRID_X, GRID_Y = 475, 166
GAUGE = (727, 220, 176, 10)  # x, y, w, h
CHIP_Y0, CHIP_PITCH, CHIP_H = 162, 26, 22
# The static picture and the Reset state: car on column 3, the ring behind it
# loaded, the column ahead still loading (frac < .45 in the animation).
STATIC_CAR_COL, STATIC_FRAC = 3, 0.2
STATIC_MIB = 60  # cache use shown in the picture and at Reset, out of the 96 MiB cap


def text(x, y, s, cls="", anchor="start", extra=""):
    a = f' text-anchor="{anchor}"' if anchor != "start" else ""
    c = f' class="{cls}"' if cls else ""
    return f'<text x="{x}" y="{y}"{a}{c}{extra}>{escape(s)}</text>'


def node_open(nid, title, tooltip, interactive):
    attrs = f'id="n-{nid}" class="node" data-id="{nid}"'
    if interactive:
        attrs += f' tabindex="0" role="button" aria-label="{escape(title)}: {escape(tooltip)}"'
    return f'<g {attrs}><title>{escape(title)} — {escape(tooltip)}</title>'


def box_rect(nid, cls="box"):
    x, y, w, h = BOX[nid]
    return f'<rect class="{cls}" x="{x}" y="{y}" width="{w}" height="{h}" rx="10"/>'


def stage(nid):
    return next(s for s in STAGES if s["id"] == nid)


def mini_grid() -> str:
    """7 × 3 tile cells plus the car, in the mid-drive state the animation also resets to."""
    out = []
    car_col = STATIC_CAR_COL
    for r in range(GRID_ROWS):
        for c in range(GRID_COLS):
            cls = "cell"
            if c == car_col + 1:
                cls += " is-loading" if STATIC_FRAC < 0.45 else " is-loaded"
            elif abs(c - car_col) <= 1:
                cls += " is-loaded"
            x = GRID_X + c * (CELL + GAP)
            y = GRID_Y + r * (CELL + GAP)
            out.append(f'<rect id="cell-{c}-{r}" class="{cls}" x="{x}" y="{y}" width="{CELL}" height="{CELL}" rx="3"/>')
    cx = round(GRID_X + (car_col + STATIC_FRAC) * (CELL + GAP) + 4, 1)
    cy = GRID_Y + 1 * (CELL + GAP) + 8
    out.append(f'<rect id="car" class="car" x="{cx}" y="{cy}" width="14" height="8" rx="2"/>')
    return "".join(out)


def gauge(static_fill: float) -> str:
    x, y, w, h = GAUGE
    fill = int(w * static_fill)
    return (
        f'<rect class="gauge-track" x="{x}" y="{y}" width="{w}" height="{h}" rx="5"/>'
        f'<rect id="gauge-fill" class="gauge-fill" x="{x}" y="{y}" width="{fill}" height="{h}" rx="5"/>'
    )


def render_svg(interactive: bool, with_heading: bool) -> str:
    p = []
    role = "group" if interactive else "img"
    p.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
             f'role="{role}" aria-labelledby="svg-title svg-desc" class="diagram-svg">')
    p.append('<title id="svg-title">How SimCruise works</title>')
    p.append('<desc id="svg-desc">Player input goes to the Godot client, which streams a 3 by 3 ring of map tiles around the car '
             'by running the atlas helper, a Bun binary that fetches fifteen public data layers into a 96 MiB cache. '
             'WorldTile builds terrain, roads, buildings and trees from each tile; Powertrain drives the Miata on it; '
             'the Mobile renderer and procedural audio return picture and sound to the player.</desc>')
    if not interactive:
        p.append("<style>" + svg_css(standalone=True) + "</style>")
    p.append('<defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
             'orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z"/></marker>'
             '<pattern id="hatch" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
             '<rect width="6" height="6" class="hatch-bg"/><rect width="3" height="6" class="hatch-fg"/></pattern></defs>')
    if not interactive:
        p.append(f'<rect width="{W}" height="{H}" class="bg"/>')
    if with_heading:
        p.append(text(24, 46, "How SimCruise works", "h-title"))
        p.append(text(24, 72, "Your hands → Godot client → tile grid → atlas helper → public data → WorldTile → Powertrain → your eyes and ears", "h-sub"))
        p.append(text(W - 24, 72, "Interactive version: docs/how-it-works/index.html", "h-sub", anchor="end"))

    # Edges first so nodes sit on top.
    for e in EDGES:
        marker = ' marker-end="url(#arrow)"' + (' marker-start="url(#arrow)"' if e.get("both") else "")
        p.append(f'<path id="{e["id"]}" class="edge" d="{e["d"]}"{marker}/>')
        p.append(text(e["lx"], e["ly"], e["label"], "edge-label", anchor=e["anchor"]))
        if e.get("label2"):
            p.append(text(e["lx"], e["ly"] + 14, e["label2"], "edge-label", anchor=e["anchor"]))

    # Player (tall).
    s = stage("player")
    x, y, w, h = BOX["player"]
    p.append(node_open("player", s["title"], s["tooltip"], interactive))
    p.append(box_rect("player"))
    p.append(text(x + w / 2, y + 30, "Player", "n-title", anchor="middle"))
    p.append(text(x + w / 2, y + 50, "keyboard · Xbox", "n-sub", anchor="middle"))
    p.append(text(x + w / 2, y + 68, "W A S D · sticks · triggers", "n-line", anchor="middle"))
    p.append(text(x + w / 2, y + h / 2 + 4, "you", "n-you", anchor="middle"))
    p.append(text(x + w / 2, y + h - 48, "eyes · ears", "n-sub", anchor="middle"))
    p.append(text(x + w / 2, y + h - 30, "driver · chase · scenic", "n-line", anchor="middle"))
    p.append(text(x + w / 2, y + h - 16, "HUD Off · Glance · Full", "n-line", anchor="middle"))
    p.append("</g>")

    # Godot client.
    s = stage("client")
    x, y, w, h = BOX["client"]
    p.append(node_open("client", s["title"], s["tooltip"], interactive))
    p.append(box_rect("client"))
    p.append(text(x + 12, y + 26, "Godot client", "n-title"))
    p.append(text(x + 12, y + 44, "Drive.gd · Godot 4.7.2 · 60 Hz", "n-sub"))
    p.append(text(x + 12, y + 66, "owns the scene and worker threads", "n-line"))
    p.append(text(x + 12, y + 82, "picks the next tile · builds it", "n-line"))
    p.append(text(x + 12, y + 98, "parks the car on the nearest road", "n-line"))
    p.append(text(x + 12, y + 114, "camera · HUD · weather · sun", "n-line"))
    p.append(text(x + 12, y + 130, "Jolt physics · Mobile renderer", "n-line"))
    p.append(text(x + 12, y + 146, "waits for the road ahead to exist", "n-line"))
    p.append("</g>")

    # Tile grid with the mini grid.
    s = stage("grid")
    x, y, w, h = BOX["grid"]
    p.append(node_open("grid", s["title"], s["tooltip"], interactive))
    p.append(box_rect("grid"))
    p.append(text(x + 12, y + 26, "Tile grid", "n-title"))
    p.append(text(x + 12, y + 44, "3 × 3 ring · ≈ 486 m tiles", "n-sub"))
    p.append(mini_grid())
    p.append(text(x + 12, y + h - 10, "load ahead · free behind · re-centre", "n-line"))
    p.append("</g>")

    # Atlas helper with the cache gauge.
    s = stage("helper")
    x, y, w, h = BOX["helper"]
    p.append(node_open("helper", s["title"], s["tooltip"], interactive))
    p.append(box_rect("helper"))
    p.append(text(x + 12, y + 26, "Atlas helper", "n-title"))
    p.append(text(x + 12, y + 44, "Bun binary atlas · one process per job", "n-sub"))
    p.append(text(x + 12, y + 66, "fetch, decode, convert, print JSON", "n-line"))
    p.append(text(x + 12, y + 82, "map-cache ≤ 96 MiB · LRU eviction", "n-line"))
    p.append(gauge(STATIC_MIB / 96))
    gx, gy, gw, gh = GAUGE
    p.append(text(gx + gw, gy - 5, f"{STATIC_MIB} MiB", "gauge-label", anchor="end", extra=' id="gauge-text"'))
    p.append(text(x + 12, y + 130, "jobs: tile · aerial · canopy · buildings", "n-line"))
    p.append(text(x + 12, y + 146, "roofs · landscape · conditions · search …", "n-line"))
    p.append("</g>")

    # Public data column with chips.
    s = stage("sources")
    x, y, w, h = BOX["sources"]
    p.append(node_open("sources", s["title"], s["tooltip"], interactive))
    p.append(box_rect("sources", "box box-data"))
    p.append(text(x + 12, y + 26, "Public data", "n-title"))
    p.append(text(x + 12, y + 44, f"{len(LAYERS)} layers · no keys · own licences", "n-sub"))
    p.append("</g>")
    for i, layer in enumerate(LAYERS):
        cy = CHIP_Y0 + i * CHIP_PITCH
        attrs = f'id="n-{layer["id"]}" class="node chip" data-id="{layer["id"]}"'
        if interactive:
            attrs += f' tabindex="0" role="button" aria-label="{escape(layer["title"])}: {escape(layer["tooltip"])}"'
        p.append(f'<g {attrs}><title>{escape(layer["title"])} — {escape(layer["tooltip"])}</title>')
        p.append(f'<rect class="chip-box" x="{x + 6}" y="{cy}" width="{w - 12}" height="{CHIP_H}" rx="6"/>')
        p.append(text(x + 14, cy + 15, layer["title"], "chip-title"))
        p.append(text(x + w - 14, cy + 15, layer["tag"], "chip-tag", anchor="end"))
        p.append("</g>")

    # WorldTile builder.
    s = stage("worldtile")
    x, y, w, h = BOX["worldtile"]
    p.append(node_open("worldtile", s["title"], s["tooltip"], interactive))
    p.append(box_rect("worldtile"))
    p.append(text(x + 12, y + 26, "WorldTile builder", "n-title"))
    p.append(text(x + 12, y + 44, "WorldTile.gd · on the worker thread", "n-sub"))
    p.append(text(x + 12, y + 68, "65 × 65 heights → terrain mesh", "n-line"))
    p.append(text(x + 12, y + 84, "road corridors smoothed · lane paint", "n-line"))
    p.append(text(x + 12, y + 100, "buildings · roof tags · trees · grass", "n-line"))
    p.append(text(x + 12, y + 116, "collision trimesh · grip per tyre", "n-line"))
    p.append(text(x + 12, y + 132, "later passes swap roofs and paint", "n-line"))
    p.append("</g>")

    # Powertrain.
    s = stage("powertrain")
    x, y, w, h = BOX["powertrain"]
    p.append(node_open("powertrain", s["title"], s["tooltip"], interactive))
    p.append(box_rect("powertrain"))
    p.append(text(x + 12, y + 26, "Powertrain", "n-title"))
    p.append(text(x + 12, y + 44, "1999 Miata Sport · 1,043 kg · Jolt", "n-sub"))
    p.append(text(x + 12, y + 68, "4 raycast corners · springs · dampers", "n-line"))
    p.append(text(x + 12, y + 84, "slip-angle tyres · shared grip budget", "n-line"))
    p.append(text(x + 12, y + 100, "5-speed · clutch · diff · brakes", "n-line"))
    p.append(text(x + 12, y + 116, "0–60 7.97 s · 70–0 173 ft · 0.89 g", "n-line n-strong"))
    p.append(text(x + 12, y + 132, "aero drag · speed-capped steering", "n-line"))
    p.append("</g>")

    # Picture & sound.
    s = stage("render")
    x, y, w, h = BOX["render"]
    p.append(node_open("render", s["title"], s["tooltip"], interactive))
    p.append(box_rect("render"))
    p.append(text(x + 12, y + 26, "Picture & sound", "n-title"))
    p.append(text(x + 12, y + 44, "Mobile on Metal · 22.05 kHz synthesis", "n-sub"))
    p.append(text(x + 12, y + 68, "one sun · real clock · 180 m shadows", "n-line"))
    p.append(text(x + 12, y + 84, "head camera · HUD levels · one haze", "n-line"))
    p.append(text(x + 12, y + 100, "engine at rpm/30 Hz · limiter · pops", "n-line"))
    p.append(text(x + 12, y + 116, "tyre hiss · gravel · rumble · wind", "n-line"))
    p.append(text(x + 12, y + 132, "Master ← Cabin ← Engine / Road / Wind", "n-line"))
    p.append("</g>")

    if interactive:
        p.append('<circle id="pulse" class="pulse" r="5" cx="-20" cy="-20"/>')
        gx0, gy0, gw0, gh0 = BOX["grid"]
        p.append(text(gx0 + 6, gy0 + gh0 + 18, "", "play-caption", extra=' id="play-caption"'))
    if with_heading:
        p.append(text(24, H - 16, "The interactive page explains every stage and layer: files, sources, licences, limits. Generated from the code by docs/how-it-works/build.py.", "h-foot"))
    p.append("</svg>")
    return "".join(p)


def svg_css(standalone: bool) -> str:
    """Styles for the diagram. Standalone adds the colour variables and theme media query."""
    sel = "svg" if standalone else ".diagram-svg"
    vars_ = ""
    if standalone:
        vars_ = (
            f"svg{{{css_vars(LIGHT)}}}"
            f"@media (prefers-color-scheme: dark){{svg{{{css_vars(DARK)}}}}}"
        )
    return vars_ + f"""
{sel}{{font-family:{FONT};}}
{sel} .bg{{fill:var(--bg);}}
{sel} .box{{fill:var(--node);stroke:var(--node-stroke);stroke-width:1.4;}}
{sel} .box-data{{fill:var(--data-soft);stroke:var(--data);}}
{sel} .edge{{fill:none;stroke:var(--edge);stroke-width:1.8;}}
{sel} #arrow path{{fill:var(--edge);}}
{sel} .edge-label{{font-size:9.5px;fill:var(--muted);font-style:italic;}}
{sel} .n-title{{font-size:15px;font-weight:650;fill:var(--ink);}}
{sel} .n-sub{{font-size:10.5px;fill:var(--accent);font-weight:600;}}
{sel} .n-line{{font-size:10px;fill:var(--ink);}}
{sel} .n-strong{{font-weight:650;}}
{sel} .n-you{{font-size:26px;fill:var(--node-stroke);font-weight:700;letter-spacing:.06em;}}
{sel} .h-title{{font-size:24px;font-weight:700;fill:var(--ink);}}
{sel} .h-sub{{font-size:12px;fill:var(--muted);}}
{sel} .h-foot{{font-size:10px;fill:var(--muted);}}
{sel} .cell{{fill:var(--unloaded);stroke:var(--node-stroke);stroke-width:.8;}}
{sel} .cell.is-loaded{{fill:var(--loaded);}}
{sel} .cell.is-loading{{fill:url(#hatch);}}
{sel} .hatch-bg{{fill:var(--loading);}}
{sel} .hatch-fg{{fill:var(--node);fill-opacity:.55;}}
{sel} .car{{fill:var(--car);}}
{sel} .gauge-track{{fill:var(--cache-track);}}
{sel} .gauge-fill{{fill:var(--cache);}}
{sel} .gauge-label{{font-size:9.5px;fill:var(--cache);font-family:{MONO};}}
{sel} .chip-box{{fill:var(--chip);stroke:var(--data);stroke-width:.9;}}
{sel} .chip-title{{font-size:11px;fill:var(--chip-ink);font-weight:600;}}
{sel} .chip-tag{{font-size:9px;fill:var(--tag);font-family:{MONO};}}
{sel} .pulse{{fill:var(--cache);opacity:0;}}
{sel} .play-caption{{font-size:9.5px;fill:var(--muted);font-style:italic;}}
"""


PAGE_CSS = """
:root{%LIGHT%}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){%DARK%}}
:root[data-theme="dark"]{%DARK%}
*{box-sizing:border-box;}
html{color-scheme:light dark;}
body{margin:0;background:var(--bg);color:var(--ink);font-family:%FONT%;line-height:1.5;-webkit-text-size-adjust:100%;}
a{color:var(--data);}
code{font-family:%MONO%;font-size:.92em;background:var(--chip);color:var(--chip-ink);padding:.05em .35em;border-radius:4px;}
.wrap{max-width:1240px;margin:0 auto;padding:0 16px;}
header.top{padding:28px 0 8px;}
header.top h1{margin:0 0 6px;font-size:clamp(1.5rem,3.4vw,2.1rem);line-height:1.15;}
header.top p{margin:0;max-width:72ch;color:var(--muted);}
.toolbar{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin:16px 0 6px;}
.btn{appearance:none;border:1px solid var(--node-stroke);background:var(--node);color:var(--ink);font:inherit;font-weight:600;padding:8px 14px;border-radius:999px;cursor:pointer;min-height:40px;}
.btn:hover{background:var(--node-hover);}
.btn:focus-visible,.node:focus-visible,select:focus-visible,a:focus-visible{outline:3px solid var(--focus);outline-offset:2px;}
.btn[aria-pressed="true"]{background:var(--accent-soft);border-color:var(--accent);}
.toolbar label{display:flex;gap:6px;align-items:center;color:var(--muted);font-size:.95rem;}
.toolbar select{font:inherit;padding:6px 8px;border-radius:8px;border:1px solid var(--node-stroke);background:var(--node);color:var(--ink);min-height:40px;}
.status{font-size:.9rem;color:var(--muted);min-height:1.4em;margin:0 0 10px;}
.hint{display:none;font-size:.9rem;color:var(--muted);margin:-4px 0 10px;}
@media (max-width:913px){.hint{display:block;}}
.layout{display:grid;grid-template-columns:minmax(0,1fr);gap:18px;align-items:start;}
@media (min-width:1000px){.layout.panel-open{grid-template-columns:minmax(0,1fr) 340px;}}
.stage{background:var(--panel);border:1px solid var(--node-stroke);border-radius:14px;padding:10px;overflow-x:auto;overscroll-behavior-x:contain;}
.stage svg{display:block;width:100%%;height:auto;min-width:860px;}
.node{cursor:pointer;}
.node:hover .box,.node:hover .chip-box{fill:var(--node-hover);}
.node.is-selected .box,.node.is-selected .chip-box,.node:focus-visible .box,.node:focus-visible .chip-box{stroke:var(--focus);stroke-width:2.4;}
.panel{background:var(--panel);border:1px solid var(--node-stroke);border-radius:14px;padding:18px 18px 22px;position:sticky;top:12px;max-height:calc(100vh - 24px);overflow:auto;}
.panel[hidden]{display:none;}
.panel .close{float:right;margin:-6px -6px 0 0;}
.panel h2{margin:0 0 2px;font-size:1.25rem;line-height:1.2;padding-right:70px;}
.panel .kicker{margin:0 0 12px;color:var(--accent);font-weight:600;font-size:.92rem;}
.panel h3{margin:16px 0 4px;font-size:.8rem;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);}
.panel p{margin:0;}
.panel ul{margin:0;padding-left:18px;}
.panel li{margin:2px 0;}
.panel .lic{color:var(--muted);}
.panel .empty{color:var(--muted);font-style:italic;}
@media (max-width:999px){
 .panel{position:fixed;left:0;right:0;bottom:0;top:auto;max-height:72vh;border-radius:16px 16px 0 0;box-shadow:0 -8px 30px rgba(0,0,0,.25);z-index:10;}
 body.panel-open{padding-bottom:40vh;}
}
.index{margin:18px 0;}
.index summary{cursor:pointer;font-weight:600;}
.index ol{columns:2;column-gap:28px;padding-left:20px;margin:8px 0 0;}
@media (max-width:640px){.index ol{columns:1;}}
.index button{appearance:none;background:none;border:0;padding:2px 0;font:inherit;color:var(--data);text-decoration:underline;cursor:pointer;text-align:left;}
.legend{display:flex;flex-wrap:wrap;gap:14px;font-size:.88rem;color:var(--muted);margin:8px 0 0;}
.legend span::before{content:"";display:inline-block;width:12px;height:12px;border-radius:3px;margin-right:6px;vertical-align:-1px;border:1px solid var(--node-stroke);}
.legend .l-loaded::before{background:var(--loaded);}
.legend .l-loading::before{background:var(--loading);}
.legend .l-unloaded::before{background:var(--unloaded);}
.legend .l-cache::before{background:var(--cache);}
footer.foot{margin:34px 0 40px;color:var(--muted);font-size:.9rem;border-top:1px solid var(--node-stroke);padding-top:14px;}
footer.foot p{margin:4px 0;}
@media (prefers-reduced-motion: reduce){.cell,.gauge-fill,.car{transition:none !important;}}
@media (prefers-reduced-motion: no-preference){.cell{transition:fill .35s ease;}.gauge-fill{transition:width .35s ease;}}
"""

PAGE_JS = r"""
(function () {
  'use strict';
  var MODEL = JSON.parse(document.getElementById('model').textContent);
  var byId = {};
  MODEL.stages.concat(MODEL.layers).forEach(function (n) { byId[n.id] = n; });

  var panel = document.getElementById('panel');
  var layout = document.getElementById('layout');
  var panelBody = document.getElementById('panel-body');
  var closeBtn = document.getElementById('panel-close');
  var lastOpener = null;
  var selected = null;

  function el(tag, cls, textContent) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (textContent != null) e.textContent = textContent;
    return e;
  }
  function section(title, node) {
    var h = el('h3', null, title);
    panelBody.appendChild(h);
    panelBody.appendChild(node);
  }
  function render(entry) {
    panelBody.textContent = '';
    var h2 = el('h2', null, entry.title);
    h2.id = 'panel-title';
    h2.tabIndex = -1;
    panelBody.appendChild(h2);
    panelBody.appendChild(el('p', 'kicker', entry.kicker));
    section('What it does', el('p', null, entry.what));
    var files = el('ul');
    entry.files.forEach(function (f) { var li = el('li'); var c = el('code', null, f); li.appendChild(c); files.appendChild(li); });
    section('Files', files);
    if (entry.sources && entry.sources.length) {
      var src = el('ul');
      entry.sources.forEach(function (s) {
        var li = el('li');
        if (s.url) { var a = el('a', null, s.name); a.href = s.url; a.rel = 'noopener'; a.target = '_blank'; li.appendChild(a); }
        else li.appendChild(document.createTextNode(s.name));
        li.appendChild(document.createTextNode(' — '));
        li.appendChild(el('span', 'lic', s.licence));
        if (s.note) li.appendChild(document.createTextNode(' · ' + s.note));
        src.appendChild(li);
      });
      section('Source, licence, attribution', src);
    } else if (entry.kind === 'layer') {
      section('Source, licence, attribution', el('p', 'empty', 'See licenses/Road Atlas.txt'));
    } else {
      section('Source, licence, attribution', el('p', 'empty', 'Original SimCruise code, MIT licence (LICENSE).'));
    }
    if (entry.bytes) section('Sizes, budgets, checks', el('p', null, entry.bytes));
    section('Limits', el('p', null, entry.limits));
    var note = el('p', 'lic', 'Paths are relative to the repository root. Text verified against the code named above; see docs/how-it-works/README.md.');
    note.style.marginTop = '14px';
    note.style.fontSize = '.85rem';
    panelBody.appendChild(note);
  }
  function select(id) {
    if (selected) selected.classList.remove('is-selected');
    selected = document.getElementById('n-' + id);
    if (selected) selected.classList.add('is-selected');
  }
  function openPanel(id, opener) {
    var entry = byId[id];
    if (!entry) return;
    lastOpener = opener || null;
    render(entry);
    select(id);
    panel.hidden = false;
    document.body.classList.add('panel-open');
    layout.classList.add('panel-open');
    var h = document.getElementById('panel-title');
    if (h) h.focus({ preventScroll: window.innerWidth >= 1000 });
    try { history.replaceState(null, '', '#' + id); } catch (e) {}
  }
  function closePanel() {
    panel.hidden = true;
    document.body.classList.remove('panel-open');
    layout.classList.remove('panel-open');
    if (selected) selected.classList.remove('is-selected');
    selected = null;
    try { history.replaceState(null, '', location.pathname + location.search); } catch (e) {}
    if (lastOpener && lastOpener.focus) lastOpener.focus();
  }
  closeBtn.addEventListener('click', closePanel);
  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape' && !panel.hidden) { e.preventDefault(); closePanel(); }
  });
  Array.prototype.forEach.call(document.querySelectorAll('.node[data-id]'), function (node) {
    var id = node.getAttribute('data-id');
    node.addEventListener('click', function () { openPanel(id, node); });
    node.addEventListener('keydown', function (e) {
      if (e.key === 'Enter' || e.key === ' ' || e.key === 'Spacebar') { e.preventDefault(); openPanel(id, node); }
    });
  });
  Array.prototype.forEach.call(document.querySelectorAll('.index button[data-id]'), function (b) {
    b.addEventListener('click', function () { openPanel(b.getAttribute('data-id'), b); });
  });
  if (location.hash && byId[location.hash.slice(1)]) {
    openPanel(location.hash.slice(1), document.getElementById('n-' + location.hash.slice(1)));
  }

  // Theme: Auto follows prefers-color-scheme; Light/Dark pin it.
  var themeSel = document.getElementById('theme');
  function applyTheme(v) {
    if (v === 'light' || v === 'dark') document.documentElement.setAttribute('data-theme', v);
    else document.documentElement.removeAttribute('data-theme');
  }
  try {
    var saved = localStorage.getItem('simcruise-how-theme');
    if (saved) { themeSel.value = saved; applyTheme(saved); }
  } catch (e) {}
  themeSel.addEventListener('change', function () {
    applyTheme(themeSel.value);
    try { localStorage.setItem('simcruise-how-theme', themeSel.value); } catch (e) {}
  });

  // GitHub Pages serves only /docs, where ../../README.md does not exist, so on
  // a *.github.io host the link goes to the repository's README on github.com.
  var back = document.getElementById('back');
  if (back && /\.github\.io$/i.test(location.hostname)) {
    var segs = location.pathname.split('/').filter(Boolean);
    var at = segs.indexOf('how-it-works');
    back.href = 'https://github.com/' + location.hostname.split('.')[0] + '/' +
      (at > 0 ? segs[at - 1] : location.hostname) + '#readme';
  }

  // Phone width: the hint's link opens the list of stages and layers.
  var list = document.getElementById('all');
  document.getElementById('show-list').addEventListener('click', function (e) {
    e.preventDefault();
    list.open = true;
    list.scrollIntoView({ block: 'start' });
    var summary = list.querySelector('summary');
    if (summary) summary.focus({ preventScroll: true });
  });

  // Play: a car crosses the 7 × 3 mini grid; the 3 × 3 ring loads ahead and
  // frees behind, each new tile runs a helper job (the pulse) and adds to the
  // cache until the 96 MiB trim. Mirrors Drive.stream(), Drive.rebase() and
  // helper/cache-eviction.mjs in miniature; not a measurement.
  var COLS = %COLS%, ROWS = %ROWS%, CELL = %CELL%, GAP = %GAP%, GX = %GX%, GY = %GY%;
  var GAUGE_W = %GAUGE_W%, CAP = 96, PER_TILE = 6.4, TRIM_TO = 76;
  var START_COL = %START_COL%, START_FRAC = %START_FRAC%, START_MIB = %START_MIB%;
  var COLUMN_S = 2.2;
  var reduce = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var playBtn = document.getElementById('play');
  var resetBtn = document.getElementById('reset');
  var status = document.getElementById('status');
  var caption = document.getElementById('play-caption');
  var car = document.getElementById('car');
  var gaugeFill = document.getElementById('gauge-fill');
  var gaugeText = document.getElementById('gauge-text');
  var pulse = document.getElementById('pulse');
  var paths = { exec: document.getElementById('e-exec'), fetch: document.getElementById('e-fetch'), json: document.getElementById('e-json') };
  var playing = false, raf = 0, t0 = 0, elapsed = 0, lastCol = -1, lastTrim = -1;

  function cell(c, r) { return document.getElementById('cell-' + c + '-' + r); }
  // Colour the 7 × 3 cells for a car on carCol; returns how many tiles the ring holds.
  function setCells(carCol, frac) {
    var ring = 0;
    for (var c = 0; c < COLS; c++) {
      var state = '';
      if (Math.abs(c - carCol) <= 1) {
        state = (c === carCol + 1 && frac < 0.45) ? 'is-loading' : 'is-loaded';
        if (state === 'is-loaded') ring += ROWS;
      }
      for (var r = 0; r < ROWS; r++) {
        var e = cell(c, r);
        e.classList.toggle('is-loaded', state === 'is-loaded');
        e.classList.toggle('is-loading', state === 'is-loading');
      }
    }
    return ring;
  }
  // The cache keeps filling across passes (a rebase shifts coordinates, it does
  // not clear the disk): every column entered since Play adds a 3-tile column,
  // and each time the total passes 96 MiB the oldest district is trimmed away.
  function cacheFor(columnsEntered, frac) {
    var tiles = 3 * (columnsEntered + (frac >= 0.45 ? 1 : 0));
    var mib = START_MIB + tiles * PER_TILE;
    var trims = 0;
    while (mib > CAP) { mib = TRIM_TO + (mib - CAP); trims++; }
    return { mib: mib, trims: trims, tiles: tiles };
  }
  function pointAlong(path, k, reverse) {
    var len = path.getTotalLength();
    return path.getPointAtLength(reverse ? len * (1 - k) : len * k);
  }
  function movePulse(frac) {
    // 0–.15 grid→helper, .15–.30 helper→data, .30–.45 data→helper, .45–.60 helper→WorldTile.
    var segs = [[paths.exec, false], [paths.fetch, false], [paths.fetch, true], [paths.json, false]];
    if (frac >= 0.6) { pulse.style.opacity = 0; return; }
    var i = Math.min(3, Math.floor(frac / 0.15));
    var k = (frac - i * 0.15) / 0.15;
    var pt = pointAlong(segs[i][0], k, segs[i][1]);
    pulse.setAttribute('cx', pt.x);
    pulse.setAttribute('cy', pt.y);
    pulse.style.opacity = 1;
  }
  function frame(now) {
    if (!playing) return;
    var t = elapsed + (now - t0) / 1000;
    // Time runs from the Reset pose; the car wraps to column 0 after the last column.
    var p = (START_COL + START_FRAC + t / COLUMN_S) % COLS;
    var carCol = Math.floor(p);
    var frac = p - carCol;
    var columnsEntered = Math.floor(START_FRAC + t / COLUMN_S);
    var x = GX + (reduce ? carCol : p) * (CELL + GAP) + 4;
    car.setAttribute('x', x.toFixed(1));
    var ring = setCells(carCol, frac);
    var c = cacheFor(columnsEntered, frac);
    gaugeFill.setAttribute('width', (GAUGE_W * c.mib / CAP).toFixed(1));
    gaugeText.textContent = Math.round(c.mib) + ' MiB';
    if (!reduce && carCol + 1 < COLS) movePulse(frac); else pulse.style.opacity = 0;
    var text;
    if (c.trims > lastTrim && lastTrim >= 0 && frac < 0.9) text = 'cache over 96 MiB → oldest district evicted (LRU)';
    else if (carCol === COLS - 1 && frac > 0.6) text = 'edge of the picture → origin re-centred (rebase every 6 tiles)';
    else if (frac < 0.45 && carCol + 1 < COLS) text = 'tile ahead: atlas tile X Y → fetch → cache → JSON → WorldTile';
    else text = 'ring loaded · tiles two steps behind are freed';
    caption.textContent = text;
    if (carCol !== lastCol) {
      lastCol = carCol;
      status.textContent = 'Car on column ' + (carCol + 1) + ' of ' + COLS + ' · ' + ring + ' tiles in the ring · cache ' + Math.round(c.mib) + ' MiB' + (c.trims ? ' after ' + c.trims + ' trim' + (c.trims > 1 ? 's' : '') : '');
    }
    if (c.trims > lastTrim || lastTrim < 0) lastTrim = c.trims;
    raf = window.requestAnimationFrame(frame);
  }
  function play() {
    if (playing) return;
    playing = true;
    playBtn.setAttribute('aria-pressed', 'true');
    playBtn.textContent = 'Pause';
    t0 = window.performance.now();
    raf = window.requestAnimationFrame(frame);
  }
  function pause() {
    if (!playing) return;
    playing = false;
    elapsed += (window.performance.now() - t0) / 1000;
    window.cancelAnimationFrame(raf);
    playBtn.setAttribute('aria-pressed', 'false');
    playBtn.textContent = 'Play';
    status.textContent = status.textContent ? status.textContent + ' · paused' : 'Paused';
  }
  function reset() {
    pause();
    elapsed = 0; lastCol = -1; lastTrim = -1;
    setCells(START_COL, START_FRAC);
    car.setAttribute('x', (GX + (START_COL + START_FRAC) * (CELL + GAP) + 4).toFixed(1));
    gaugeFill.setAttribute('width', (GAUGE_W * START_MIB / CAP).toFixed(1));
    gaugeText.textContent = START_MIB + ' MiB';
    pulse.style.opacity = 0;
    caption.textContent = '';
    status.textContent = 'Press Play to watch tiles stream around the car.';
  }
  playBtn.addEventListener('click', function () { if (playing) pause(); else play(); });
  resetBtn.addEventListener('click', reset);
  document.addEventListener('visibilitychange', function () { if (document.hidden) pause(); });
  status.textContent = 'Press Play to watch tiles stream around the car.';
})();
"""

PAGE_HTML = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>How SimCruise works</title>
<meta name="description" content="Interactive diagram of SimCruise: player input, the Godot client, the tile grid, the atlas helper and its cache, fifteen public data layers, the WorldTile builder, the Miata powertrain, and the picture and sound that come back.">
<meta name="color-scheme" content="light dark">
<style>%PAGE_CSS%%SVG_CSS%</style>
</head>
<body>
<div class="wrap">
<header class="top">
 <h1>How SimCruise works</h1>
 <p>One loop: your hands go in at the left, the picture and sound come back out. In between, a Godot client streams a 3 × 3 ring of map tiles around the car by running a small native helper that fetches fifteen free public data layers into a 96 MiB cache, and a tile builder turns each one into road, terrain, buildings and trees for the Miata to drive on. Click or tab to any stage or layer for what it does, the files that implement it, its data source and licence, and its limits.</p>
 <div class="toolbar">
  <button class="btn" id="play" type="button" aria-pressed="false" aria-describedby="status">Play</button>
  <button class="btn" id="reset" type="button">Reset</button>
  <label for="theme">Theme <select id="theme"><option value="auto">Auto</option><option value="light">Light</option><option value="dark">Dark</option></select></label>
  <a id="back" href="../../README.md">Back to the README</a>
 </div>
 <p class="status" id="status" aria-live="polite"></p>
 <p class="hint">The diagram is wider than this screen: swipe it sideways, or <a href="#all" id="show-list">open the list of stages and layers</a>.</p>
</header>
<main class="layout" id="layout">
 <section class="stage" aria-label="Diagram">
%SVG%
 </section>
 <aside class="panel" id="panel" hidden aria-labelledby="panel-title">
  <button class="btn close" id="panel-close" type="button" aria-label="Close details">Close</button>
  <div id="panel-body"></div>
 </aside>
</main>
<p class="legend" aria-hidden="true"><span class="l-loaded">tile loaded</span><span class="l-loading">tile loading</span><span class="l-unloaded">not loaded</span><span class="l-cache">cache use</span></p>
<details class="index" id="all">
 <summary>All stages and layers as a list</summary>
 <ol>%INDEX%</ol>
</details>
<footer class="foot">
 <p>Built by <code>docs/how-it-works/build.py</code> from the SimCruise source; the static picture in the README (<code>diagram.svg</code>) comes from the same model. Everything on this page was read from the files it names, with no external requests, fonts or scripts. Data licences are listed per layer and in <code>licenses/</code>; the code is MIT.</p>
</footer>
</div>
<script id="model" type="application/json">%MODEL%</script>
<script>%PAGE_JS%</script>
</body>
</html>
"""


def render_html() -> str:
    model = {
        "stages": [dict(s, kind="stage") for s in STAGES],
        "layers": [dict(l, kind="layer") for l in LAYERS],
    }
    model_json = json.dumps(model, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
    index = "".join(
        f'<li><button type="button" data-id="{n["id"]}">{escape(n["title"])}</button></li>'
        for n in STAGES + LAYERS
    )
    js = (PAGE_JS.replace("%COLS%", str(GRID_COLS)).replace("%ROWS%", str(GRID_ROWS))
          .replace("%CELL%", str(CELL)).replace("%GAP%", str(GAP)).replace("%GX%", str(GRID_X))
          .replace("%GY%", str(GRID_Y)).replace("%GAUGE_W%", str(GAUGE[2]))
          .replace("%START_COL%", str(STATIC_CAR_COL)).replace("%START_FRAC%", str(STATIC_FRAC))
          .replace("%START_MIB%", str(STATIC_MIB)))
    css = (PAGE_CSS.replace("%LIGHT%", css_vars(LIGHT)).replace("%DARK%", css_vars(DARK))
           .replace("%FONT%", FONT).replace("%MONO%", MONO).replace("%%", "%"))
    return (PAGE_HTML.replace("%PAGE_CSS%", css).replace("%SVG_CSS%", svg_css(standalone=False))
            .replace("%SVG%", render_svg(interactive=True, with_heading=False))
            .replace("%INDEX%", index).replace("%MODEL%", model_json).replace("%PAGE_JS%", js))


def render_static_svg() -> str:
    return '<?xml version="1.0" encoding="UTF-8"?>\n' + render_svg(interactive=False, with_heading=True) + "\n"


def main(argv):
    outputs = {HERE / "index.html": render_html(), HERE / "diagram.svg": render_static_svg()}
    if "--check" in argv:
        stale = [p.name for p, s in outputs.items() if not p.exists() or p.read_text(encoding="utf-8") != s]
        if stale:
            print("stale: " + ", ".join(stale) + " (run python3 docs/how-it-works/build.py)")
            return 1
        print("docs/how-it-works is up to date")
        return 0
    for path, content in outputs.items():
        path.write_text(content, encoding="utf-8")
        print(f"wrote {path.relative_to(HERE.parent.parent)} ({len(content.encode('utf-8')):,} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
