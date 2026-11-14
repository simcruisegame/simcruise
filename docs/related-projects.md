# Related projects

Not quite this combination, as far as we know; the pieces exist separately:

- **Microsoft Flight Simulator** streams the whole planet from Bing aerial imagery and photogrammetry, which is the scale OSDS aims at, but it is proprietary, needs an account, and you fly over the roads rather than drive them. **FlightGear** is the open-source counterpart with scenery generated from public elevation and OpenStreetMap data, still from the air.
- **Forza Horizon**, **The Crew** and **American Truck Simulator** drive through real regions, but condensed, rearranged or scaled (ATS uses roughly 1:20), hand-built from reference rather than generated from the map, and all are paid, closed titles.
- **BeamNG.drive** and **Assetto Corsa** have the vehicle fidelity (soft-body physics, laser-scanned circuits) but on fictional or closed-course maps. Open-source racers such as **Speed Dreams** or **Rigs of Rods** have fictional maps too.
- **Streets GL**, **OSM2World** and similar open-source renderers draw real OpenStreetMap towns in 3D, and research simulators such as **CARLA** (open source, Unreal) simulate driving for autonomous-vehicle work, but on synthetic towns, not where you live.
- Engine plugins like **Cesium for Unreal/Unity** or the Mapbox and ArcGIS SDKs stream real-world 3D tiles into a game engine, which is the closest technical cousin, but they depend on a vendor account and API key, and someone still has to build the game.

OSDS's niche is the intersection: real roads anywhere in the US, generated at runtime from public data with recorded provenance, one physically modelled real car with published calibration targets, and an MIT code base with no keys or accounts. If you know a project that does this, open an issue so we can link it here.
