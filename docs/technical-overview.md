# Technical overview

SimCruise is two programs and a cache:

```
Unreal Engine 5.8 client (Source/SimCruise)     map helper (Bun, helper/*.mjs)             public data services
world streamer · Chaos vehicle · environment    one process per job, no server             OSM/OpenFreeMap, USGS, USDA,
 worker thread ── "atlas tile X Y" ────────────▶ fetch · decode · convert ─── HTTPS ───▶   Overture, NOAA, NASA, …
               ◀── one JSON document on stdout ─ write PNG/JSON into the cache
data/tiles: the bundled offline start            %LOCALAPPDATA%/SimCruise/map-cache
```

**The client** is an Unreal Engine 5.8 C++ project (`Source/SimCruise`), rendered with Lumen and virtual shadow maps under a physically based sky. Everything it draws of the world is built at run time from data: there are no hand-placed levels. The details of every part, with what it looks like against photographs and what is still missing, are in [unreal-architecture.md](unreal-architecture.md).

**The helper** (`helper/atlas.mjs` and its modules) is JavaScript compiled by Bun into one native executable. The client never opens a network socket: for each tile it runs the helper, which fetches and converts the public data and answers with one JSON document. The offline start (24 tiles around Ocean Drive, Miami Beach) is bundled in `data/tiles`.

**The world.** Web Mercator zoom-16 tiles (about 550 m at Miami Beach) stream in a ring around the car, built on worker threads and installed one per frame; a floating origin keeps the car near the origin. Each tile becomes one procedural mesh with a section per layer: terrain conditioned under the roads, road ribbons from the helper's fitted road profiles (crown, curbs, gutters, sidewalks), markings, water, buildings, props and foliage. Buildings follow a regional recipe (Art Deco in the Miami Beach district), palms grow where coconut palms grow, street lights stand behind the curbs. Surface look comes from code-built materials reading CC0 detail photographs and the aerial photograph, with world-anchored coordinates so nothing repeats at tile seams.

**The car.** The physics is Chaos at a fixed 360 Hz. A physics-thread callback evaluates each wheel's contact, suspension, tyre slip (combined), engine, clutch, gearbox and differential in SI units from the canonical car files in `data/cars`, and applies one force and torque to the body. Automated benches (`SimCruise.Vehicle.*`) hold every car to its published 0–60 mph, 70–0 mph and skidpad figures, check static loads and lift-off stability, prove convergence from 60 to 720 Hz and repeat whole runs bit for bit. The car you see is a downloaded glTF model rigged at load time (wheels, steering wheel, lamps, materials by role). Car data and calibration are described in [cars.md](cars.md).

**Light.** The sun and moon are computed for the place and time; illuminance is in real units (a clear Miami noon is about EV 15.6) and the camera exposes like a photographer's within a band around a grey-card reading. Night has street lights (a pool of real lights near the car), lit windows, neon and low beams with a photometric cut-off.

**Privacy.** No telemetry, analytics, accounts, keys or crash reporting. The only network traffic is the helper's requests to the data services listed in [data-sources.md](data-sources.md). Nothing about you or where you drive is sent anywhere else.

The Godot client this replaced (1.x releases) is at the git tag `godot-final`; [unreal-migration-ledger.md](unreal-migration-ledger.md) maps every Godot file to its Unreal successor.
