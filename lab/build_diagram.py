"""Build the standalone source diagram against an actual Sunday Driver release source ZIP."""
from pathlib import Path
from html import escape
import argparse, json, zipfile, re, base64
from datetime import date
D=Path(__file__).resolve().parents[1]
arg=argparse.ArgumentParser()
arg.add_argument('--source',type=Path,default=D/'outputs/Sunday Driver - Miata Sport Source.zip')
arg.add_argument('--out',type=Path,default=D/'outputs')
arg.add_argument('--examples',type=Path,default=Path(__file__).parent/'diagram-assets')
cfg=arg.parse_args()
with zipfile.ZipFile(cfg.source) as release:
    names=release.namelist()
    def code(filename):
        matches=[p for p in names if p.endswith('/'+filename)]
        return release.read(matches[0]).decode() if matches else ''
    required=[('atlas.mjs','photon.komoot.io'),('atlas.mjs','openfreemap.org'),('atlas.mjs','terrarium'),('atlas.mjs','USGSImageryOnly'),('street-objects.mjs','SIMCRUISE_OVERPASS_URL'),('conditions.mjs','aviationweather.gov'),('recent.mjs','earth-search'),('canopy.mjs','dataforgood'),('street-samples.mjs','kartaview.org'),('Drive.gd','street_detail.tick'),('Powertrain.gd','clutch_load')]
    for filename,term in required:
        if term not in code(filename):raise ValueError(f'Review diagram before publishing. Shipped {filename} no longer references {term}')
    upgraded='differential_torque' in code('Powertrain.gd') and 'SIDE_RELAXATION_M' in code('Powertrain.gd')
    panoramax = bool(code('panoramax.mjs')) and 'panora' in code('atlas.mjs').lower()
    paths = bool(code('pedestrian.mjs')) and 'pedestrian' in code('WorldTile.gd')
    regional = bool(code('regional-aerial.mjs'))
    plants = bool(code('low-vegetation-18166-27914.json'))
    enriched='Overture' in code('BuildingStream.gd') and 'BuildingStream.gd' in code('Drive.gd')
    has_uses = 'usesStatus' in code('atlas.mjs') and 'enriched_uses' in code('WorldTile.gd')
    uses_cooked_roofs = 'LidarRoof.gd' in code('WorldTile.gd') and bool(code('LidarRoof.gd'))
    miami = json.loads(code('lidar-roofs-18166-27914.json') or '{}') if uses_cooked_roofs else {}
    has_landmark='face_finish' in code('LidarRoof.gd') and bool(code('lidar-roofs-17907-25198.json'))
    has_tree_sources='cityInventory' in code('atlas.mjs') and bool(code('civic-trees.mjs'))
    has_finishes='enriched_finishes' in code('BuildingStream.gd') and 'surfaceUpdates' in code('overture-buildings.mjs')
    gravel = 'has_mapped_unpaved' in code('WorldTile.gd') and bool(code('SustainedSurfaceCheck.gd'))
    parking = 'PARKING_RACK_RAD' in code('Powertrain.gd') and bool(code('TurningCircle.gd'))
    spectral = bool(code('spectral-ground.mjs')) and 'spectral_ground' in code('Drive.gd')
    additions = bool(code('verified-footprints.mjs')) and 'admitted_footprints' in code('BuildingStream.gd')
    distant = bool(code("regional-terrain.mjs")) and "regional_terrain.tick" in code("Drive.gd")
    facades = bool(code("PhotoFacades.gd"))
    paint = bool(code("PaintEvidence.gd"))
    witnesses = bool(code("corroborated-outlines.json"))
    locking = 'slide_reaction' in code('Powertrain.gd') and bool(code('LockedBrakeTurn.gd'))
    shrub = bool(code('shrub-cover.mjs')) and 'rangeland.tick' in code('Drive.gd')
    soil = bool(code('bare-reflectance.mjs')) and 'has_soil' in code('terrain.gdshader')
    forest = bool(code('forest-form.mjs')) and bool(code('ForestFormStream.gd'))
    banks = bool(code('bank-source.mjs')) and 'road_banks.tick' in code('Drive.gd')
    capitol = bool(code('cheyenne/runtime-check.json'))
    seasonal = bool(code('summit-snow.mjs')) and bool(code('ridge-imagery.mjs')) and 'summit.tick' in code('RegionalTerrain.gd')
    stripes = bool(code('utah/striping/manifest.json'))
    streamed_roofs = 'streamedRoofs' in code('atlas.mjs') and 'fetch_roofs' in code('BuildingStream.gd')
    surveyed_forest = bool(code('SurveyForest.gd')) and bool(code('als-forest-index.json'))
    stereo_stone = bool(code('MeasuredGranite.gd')) and any(n.endswith('/needles-granite.meshdata') for n in names)
    live_lane_paint = 'prepare_lane_update' in code('WorldTile.gd') and 'fetchLaneDistrict' in code('atlas.mjs')
    municipal_crowns = bool(code('municipal-crowns.mjs'))
    engine_sound = bool(code('InlineFourSound.gd'))
    historic_glass = bool(code('HistoricAnchorage.gd'))
    signal_source = 'signalControls' in code('atlas.mjs') and bool(code('SignalControls.gd'))
    store_source = bool(code('MappedStorefront.gd'))
    ground13 = 'measuredGround' in code('atlas.mjs') and bool(code('usgs-elevation-products.json'))
    archive_sky = bool(code('photo-weather.mjs')) and bool(code('ArchiveWeather.gd'))
    observed_walls = bool(code('ObservedWallPatches.gd')) and bool(code('survey-wall-16104-24411.json'))
    crossfall = bool(code('road-crossfall.mjs')) and bool(code('RoadCrossfallGround.gd'))
    road_checks=json.loads(code('us-road-samples.json') or '[]')
    assert not road_checks or len({r['state'] for r in road_checks})==len(road_checks)
    tick=re.search(r'physics_ticks_per_second=(\d+)',code('project.godot')).group(1)
release_name=cfg.source.name.removesuffix(' Source.zip')
items=[
('search','Place search','Street name or coordinates → a road and starting direction.',[('Photon','https://photon.komoot.io/'),('OpenStreetMap','https://www.openstreetmap.org/copyright')],'On search'),
('roads','Roads & building outlines','Road paths, intersections, footprints and available building attributes. Bundled and cached observations can add signals and building finishes. Operators can connect their own Overpass service.',[('OpenFreeMap','https://openfreemap.org/'),('OpenStreetMap','https://www.openstreetmap.org/copyright'),('Overpass service guidance','https://dev.overpass-api.de/overpass-doc/en/preface/commons.html')],'Streamed'),
('ground','Terrain & aerial photos','The driving surface uses the USGS 3DEP survey where a local block is available. Terrarium also supplies elevation. USGS and USDA aerial images provide ground color.' if ground13 else 'Terrarium supplies general elevations. USGS aerial imagery adds ground color where available. The bundled Miami Beach tiles were built from the same sources ahead of time.',[('Terrarium elevation archive','https://registry.opendata.aws/terrain-tiles/'),('USGS aerial imagery','https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer')],'Streamed'),
('trees','Trees & canopy','A machine-learned canopy height map helps place full 3D trees. Individual tree species and shapes are usually inferred.',[('Meta and WRI canopy heights','https://registry.opendata.aws/dataforgood-fb-forestsv2/'),('Poly Haven 3D assets','https://polyhaven.com/')],'Streamed'),
('photos','Street-level photos','Up to three nearby KartaView views can adjust local road or vegetation color when the views agree. A drive can continue without them. Capture dates vary.',[('KartaView','https://kartaview.org/'),('Photo license and attribution','https://kartaview.org/terms')],'Optional'),
('weather','Sky & season','Aviation weather observations can change sky and visibility. Clear Sentinel-2 observations supply broad seasonal surface color.',[('NOAA METAR','https://aviationweather.gov/data/api/'),('Copernicus via Earth Search','https://earth-search.aws.element84.com/v1/collections/sentinel-2-c1-l2a')],'Optional'),
('local','Bundled start area','Ocean Drive and the Flamingo Park blocks of Miami Beach ship as 24 prepared tiles (OpenStreetMap roads and footprints, USGS 3DEP elevation, USGS aerial photographs and the canopy map), so the game starts without a network.',[('USGS 3DEP','https://www.usgs.gov/3d-elevation-program'),('USGS The National Map imagery','https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer')],'Bundled'),
('car','Miata specifications & tests','The US 1999 Sport five-speed uses four tire contacts, suspension forces and a simulated driveline. The published road test supplies calibration targets. The individual car’s damper and tire curves are unverified.',[('Period instrumented road test','https://www.caranddriver.com/reviews/a15142268/1999-mazda-mx-5-miata-road-test-review/'),('Fat Cat Motorsports suspension data','https://www.fatcatmotorsports.com/FCM_Ride_Harmony_NB_Mazda_Miata_Online/FCM_Ride_Harmony_NB_Mazda_Miata_Online.htm'),('Jolt Physics','https://github.com/jrouwe/JoltPhysics')],'Local model')]
if enriched:items.insert(2,('overture','Building dimensions','Additional attributed heights are admitted where a footprint can be matched confidently.',[('Overture buildings','https://docs.overturemaps.org/guides/buildings/')],'Optional'))
if has_uses:
    items.insert(3,('usace','Buildings and their uses','An isolated inventory point inside a matched footprint can identify a single-family home, duplex, multifamily building or nonresidential structure. USACE models the occupancy and number of stories. Ambiguous buildings keep their original classification.',[('USACE National Structure Inventory','https://www.hec.usace.army.mil/confluence/nsi/technicalreferences/2026/technical-documentation')],'Optional'))
if miami:
    items.insert(4,('lidar','Miami roof shapes',f"Surveyed roof points define {len(miami['roofs'])} full 3D roofs in the South Miami Avenue demo. The flight took place in 2021. Each mesh is tied to its exact footprint and checked against spatially withheld laser points.",[('Miami-Dade lidar metadata','https://www.fisheries.noaa.gov/inport/item/76028')],'Bundled'))
if paths:
    items.append(('paths','Mapped sidewalks and paths','An existing road tile also supplies mapped outdoor paved footways and cycle paths. They are drawn in the surface material and excluded from drivable road searches.',[('OpenMapTiles transportation layer','https://openmaptiles.org/schema/#transportation')],'Streamed'))
if regional:
    items.append(('aerial-regional','Hawaii and Alaska aerial coverage','Hawaii uses federal NAIP aerial imagery where available. The Alaska Forest Service imagery covers selected forest areas. The downtown Juneau test had real pixels. The Anchorage response was transparent.',[('Hawaii NAIP','https://www.fisheries.noaa.gov/inport/item/69406'),('USFS Alaska imagery','https://imagery.geoplatform.gov/iipp/rest/services/Aerial_Imagery/RGBI_post2000_USFS_R10_Alaska_multiRes_Public/ImageServer/info/iteminfo')],'Streamed'))
if plants:
    items.append(('low-plants','Measured low vegetation','The South Miami Avenue demonstration includes 12 small patches where independent lidar strips agreed on height and aerial infrared supported vegetation. Mature shrubs between these patches remain unresolved.',[('Miami-Dade flight metadata','https://www.fisheries.noaa.gov/inport/item/76028')],'Bundled'))
if panoramax:
    items.append(('panoramax','Street photo references','A second, license-filtered catalog can return a dated camera location when the primary street sequence is missing. The map can link to the original photo. Optional enrichment requests run outside the frame loop.',[('Panoramax federated catalog','https://docs.panoramax.fr/federated-catalog/overview/')],'Optional'))
if has_tree_sources:
    items.append(('street-trees','Municipal street trees','Lincoln, Nebraska and Cambridge, Massachusetts publish tree inventories with trunk coordinates. The game joins these to plausible canopy crowns, preserves observed trunk diameter where available and estimates missing crown dimensions from nearby measured peers. Private-yard coverage is incomplete.',[('Lincoln parks tree inventory','https://gis.lincoln.ne.gov/public/rest/services/Parks/Trees/MapServer'),('Cambridge street trees','https://www.cambridgema.gov/GIS/gisdatadictionary/Environmental/ENVIRONMENTAL_StreetTrees')],'Streamed'))
if has_landmark:
    items.append(('landmark','Charleston’s Clay Center','A 2018–2019 public lidar survey and two independent flight strips establish the curved roof and its location. The prepared mesh loads at the mapped landmark. Two withheld street pictures check the skyline, and the gallery includes the old and new renders.',[('USGS 3DEP','https://www.usgs.gov/3d-elevation-program'),('Street photograph','https://kartaview.org/details/3485953/2861')],'Bundled'))
if has_finishes:
    items.append(('finishes','Mapped building finishes','The same Overture tile used for dimensions sometimes carries wall or roof material, color and roof shape. The game admits supported OpenStreetMap properties when a unique footprint and source record match. Released examples include concrete gables in Honolulu and glass in Anchorage.',[('Overture building schema','https://docs.overturemaps.org/schema/reference/buildings/building/'),('PMTiles range access','https://docs.protomaps.com/pmtiles/')],'Streamed'))
if gravel:
    items.append(('gravel','Pavement and gravel','Mapped unpaved roads use a separate surface at each wheel and ground color sampled from the aerial tile. Lancaster County independently identifies the Princeton Road test segment as gravel. Two farther views agree more closely with the aerial image. Street-level color there remains unverified.',[('Lancaster County Engineer','https://gis.lincoln.ne.gov/public/rest/services/CountyEngineer/RoadSurfaceType/MapServer'),('USGS aerial imagery','https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer')],'While driving'))
if spectral:
    items.append(('spectral','Grass and other low cover','The optional USDA ground cover setting uses a dated four-band NAIP image. Near-infrared distinguishes living vegetation from similarly colored ground. The game combines it with tree height and a road mask. Two eastern US sites supplied usable imagery in testing. The result depends on the local flight and season.',[('USDA NAIP mosaic and source catalog','https://imagery.geoplatform.gov/iipp/rest/services/NAIP/NAIP_plus/ImageServer')],'Optional · background'))
if additions:
    items.append(('footprint-gaps','Buildings missing from the base map','An observed roof in an airborne survey can verify a second-source building outline. Overlapping, moved and ambiguous structures are withheld. The engine checks source geometry before appending a verified footprint to the scene.',[('Overture Buildings','https://docs.overturemaps.org/guides/buildings/'),('NOAA Digital Coast','https://coast.noaa.gov/dataviewer/'),('USGS 3DEP','https://www.usgs.gov/3d-elevation-program')],'Surveyed examples'))
if distant:
    items.append(('distance','Distant hills, coast and roads','A background worker requests elevation around the drive and spends extra detail on the visible skyline. Martini reduces the mesh before Godot receives it. The patch extends 45 km from its regional center in each map direction. USGS aerial imagery colors the terrain. A cached Natural Earth ocean mask distinguishes open sea from land below sea level. The near road still supplies the four tire contacts.',[('Mapzen elevation','https://registry.opendata.aws/terrain-tiles/'),('Martini','https://github.com/mapbox/martini'),('Natural Earth oceans','https://www.naturalearthdata.com/downloads/10m-physical-vectors/10m-ocean/'),('USGS imagery','https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer')],'Streamed and cached'))
if paint:
    items.append(('local-paint','Lane arrows and stripes','At the Key Largo study site, repeated orthophotos establish the polygon of each accepted arrow, dash and edge stripe. The running game checks the road alignment before applying the paint.',[('Monroe County GIS','https://mcgis4.monroecounty-fl.gov/public/rest/services/Images')],'Verified site'))
if facades:
    items.append(('local-facade','Building faces and porches','At Davidson Hall in Bethel, Maine four registered wall planes use a licensed photograph. Independent airborne returns constrain the main roof and porch. Other buildings use the evidence available at that location.',[('Davidson Hall photograph','https://commons.wikimedia.org/wiki/File:Gould_Academy_Davidson_Hall.JPG')],'Verified site'))
if witnesses:
    items.append(('outline-witness','Building outlines and heights','In Nevada and Kansas, independent ORNL/FEMA outlines and dated USDA aerials corroborate 20 buildings missing from the base map. Sixteen have Microsoft model heights and four use a provisional 3 m height.',[('Overture attribution','https://overturemaps.org/attribution/'),('USDA NAIP','https://www.usgs.gov/centers/eros/science/usgs-eros-archive-aerial-photography-national-agriculture-imagery-program-naip')],'Verified sites'))
if parking:
    items.append(('parking','Turning circle','The outer front tire follows a complete powered circle in both directions. Four dry-pad measurements put the turning envelope within 12 cm of the published 30.2 ft. The alignment check also measures the inside and outside wheel angles.',[('Mazda workshop manual','https://www.mellens.net/mazda/Mazda-Miata-1999-2001.pdf'),('1999 Miata road review','https://www.theautochannel.com/vehicles/new/reviews/1999/lweitzman_mazdamiata.html')],'Vehicle calibration'))
if locking:
    items.append(('lock-up','Braking with a locked wheel','The tire force follows the direction of sliding tread as a wheel locks. In a flat-pad simulation at 50 mph, applying steering after wheel lock produced 0.33° of heading change. The source includes a straight run and turns in both directions.',[],'Vehicle calibration'))
if forest:
    items.append(('forest-types','Conifer stands','Federal vegetation maps identify conifer-dominated areas. The renderer builds tapered 3D crowns at eligible canopy peaks. In the Washington test the second visit used the local cache.',[('LANDFIRE existing vegetation type','https://landfire.gov/vegetation/evt'),('NLCD land cover','https://www.mrlc.gov/data/legends/national-land-cover-database-class-legend-and-description')],'Streamed'))
if seasonal:
    items.append(('seasonal-ridges','Mountains in the driver’s view','The camera direction and existing terrain identify up to two visible ridge cells. USGS orthophotography supplies rock detail. NASA snow observations guide optional 40 m Sentinel-2 classification and 40–160 m red-band reflectance. Clear satellite pixels can reveal darker faces and remove unsupported snow. Four dated roadside viewpoints also use archived daylight. Repeated visits use local files.',[('NASA Terra snow','https://nsidc.org/data/mod10a1/versions/61'),('Copernicus processing','https://sentiwiki.copernicus.eu/web/s2-processing'),('Element 84 Earth Search','https://github.com/Element84/earth-search')],'Streamed where observed'))
    items.append(('surveyed-streets','Medford and Bartlesville','In Medford, state lane inventory and two aerial acquisitions locate the painted shoulders on one verified carriageway. In Bartlesville, USGS laser returns supply the restaurant roof, entrance tower and adjacent forecourt grade. These small corrections ship with the game.',[('MassGIS','https://www.mass.gov/orgs/massgis-bureau-of-geographic-information'),('USGS 3DEP','https://www.usgs.gov/3d-elevation-program')],'Verified locations'))
if shrub:
    items.append(('rangeland','Open shrub stands','Annual models provide shrub cover and average height on western rangelands. The game places 3D clumps using these stand statistics and local aerial contrast. In the Arizona check the two cold requests totaled 565,186 bytes. A revisit made zero external reads for this layer.',[('USDA RAP cover','https://rangelands.app/support/71-api-documentation'),('USGS RCMAP height','https://www.mrlc.gov/data/rcmap-shrub-height')],'Streamed'))
if soil:
    items.append(('surface-reflectance','Observed bare-ground color','The renderer recovers bare-ground color from the Sentinel-2 image already downloaded for seasonal appearance. The local scan supplies fine grain, height variation and roughness.',[('ESA true-color image encoding','https://sentiwiki.copernicus.eu/web/glossary'),('Sandy Gravel 02 by Dario Barresi, CC0','https://polyhaven.com/a/sandy_gravel_02')],'Streamed + local scan'))
if banks:
    items.append(('cut-banks','Roadside banks','A background job requests fine USGS 3DEP coverage along eligible roads ahead. Where a recent survey passes registration checks, the next tile uses the measured terrain beside the road. The current driving surface stays stable.',[('USGS elevation service','https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer')],'Streamed where surveyed'))
if capitol:
    items.append(('capitol','Capitol roof and facade','In Cheyenne, airborne measurements and architectural elevations locate the dome. Licensed photographs supply wall detail on corroborated central and west faces. The east side retains a provisional finish.',[('USGS 3DEP lidar','https://www.usgs.gov/3d-elevation-program')],'Verified site'))
if streamed_roofs:
    items.append(('roof-stream','Measured rooftops near the car','A USGS work-unit query selects a public Entwine point cloud. The local laz-perf decoder reads a few compressed nodes. Separate ground samples, roof-edge support and withheld points determine which building roofs can be replaced. The cached mesh follows its exact mapped footprint.',[('USGS public lidar','https://registry.opendata.aws/usgs-lidar/'),('Entwine format','https://entwine.io/en/latest/entwine-point-tile.html'),('laz-perf','https://github.com/hobuinc/laz-perf')],'Streamed'))
if surveyed_forest:
    items.append(('airborne-trees','Measured forest and riverbanks','At Snake River Overlook, multiple laser flight lines locate 2,453 crowns and support a detailed ground surface. Their measured area loads from the build when the car approaches. Species, branches and off-survey forest rely on the ordinary vegetation model.',[('Federal laser surveys','https://www.usgs.gov/3d-elevation-program')],'Verified area'))
if stereo_stone:
    items.append(('roadside-rock','Roadside rock faces','At a measured stretch of Needles Highway, licensed photographs from separate positions and airborne laser returns reconstruct the granite formation. A further view checks the skyline.',[('USGS 3DEP','https://www.usgs.gov/3d-elevation-program')],'Verified site'))
if ground13:
    items.append(('ground-13','USGS ground beneath the tires','A small index selects the public elevation product. geotiff.js reads the directory and compressed blocks under and ahead of the car. Product-version checks, the disk cache and shared tile edges keep range reads and heights consistent. The Anchorage K Street test went from rolling backward to clearing the course with four tire contacts throughout.',[('USGS 3DEP products','https://www.usgs.gov/3d-elevation-program/about-3dep-products-services'),('geotiff.js','https://github.com/geotiffjs/geotiff.js')],'Streamed'))
if archive_sky:
    items.append(('archive-sky','Sky at the time of street photography','A licensed, dated picture can select archived local station observations and a small GOES cloud-mask window. The local HDF5 reader examines the sky overhead. Ambiguous conditions keep the ordinary sky. Coverage depends on the date and available observations.',[('NOAA GOES data','https://registry.opendata.aws/noaa-goes/'),('Iowa Environmental Mesonet','https://mesonet.agron.iastate.edu/ASOS/'),('h5wasm','https://github.com/usnistgov/h5wasm')],'Streamed'))
if any(path.endswith('/game/data/lidar-roofs-5480-18579.json') for path in names):
    items.append(('anchorage-towers','Measured downtown office towers','For two Anchorage buildings, the local mesh preserves separately measured upper levels in the matched footprint. The survey corrects mapped height gaps. The native office façade materials remain approximate.',[('USGS public lidar','https://registry.opendata.aws/usgs-lidar/')],'Bundled'))
if observed_walls:
    items.append(('observed-walls','Individual photographed walls','For two verified façades in Iowa City and Minneapolis, observations from separate camera positions locate a real patch of surface. Geometry fingerprints attach the small credited image to the matching wall. The game relights it as part of the building.',[('Panoramax','https://panoramax.fr/en')],'Bundled'))
if live_lane_paint:
    items.append(('lanes','Paint arriving during a drive','The background lane worker reads a bounded OSM district, registers an explicit single lane to the mapped road, and replaces the paint on the currently loaded tile. The car continues on the same physics surface. Washington also has four side markings verified against two city aerial surveys.',[('Road lane data', 'https://wiki.openstreetmap.org/wiki/Key:lanes'), ('DC orthophotography', 'https://opendata.dc.gov/datasets/aerial-photography-orthophoto-2025/about')],'Streamed + surveyed'))
if municipal_crowns:
    items.append(('tree-wells','City tree wells and measured crowns','Where Cambridge publishes LiDAR crown polygons and physical sidewalk tree wells, their boundaries and the city pavement determine where a trunk can stand. A separate street photograph checks the recovered trees.',[('LiDAR crowns', 'https://www.cambridgema.gov/gis/gisdatadictionary/environmental/environmental_treecanopy2024'), ('Road polygons', 'https://www.cambridgema.gov/gis/gisdatadictionary/basemap/basemap_roads'), ('Sidewalk tree wells', 'https://www.cambridgema.gov/gis/gisdatadictionary/basemap/basemap_sidewalks'), ('Survey license', 'https://opendatacommons.org/licenses/pddl/1.0/')],'Municipal survey'))
if signal_source:
    items.append(('traffic-controls','Mapped signalized intersections','An optional worker reads the Overture base display archive near a mapped road junction. Original OSM signals and the road topology establish whether the crossing is controlled. Small unlit heads indicate this in the scene. The exact pole positions and phase are unavailable.',[('Overture infrastructure','https://docs.overturemaps.org/schema/reference/base/infrastructure/'),('OSM traffic signals','https://wiki.openstreetmap.org/wiki/Tag:highway%3Dtraffic_signals')],'Streamed'))
if store_source:
    items.append(('shop-fronts','Street-level entrances','The existing vector-tile query carries the category of each named place. A tagged shop building or a matching retail place within a small footprint can supply an unbranded entrance on the road-facing side. Registered photographs can establish an individual facade.',[('OpenMapTiles points of interest','https://openmaptiles.org/schema/#poi')],'Local + streamed'))
if historic_glass:
    items.append(('historic-glass','Dated building materials','Three archived photographs establish the glass-to-metal banding of the old 601 West Fifth office façade in Anchorage. A historical date enables this surface. The later renovation uses the normal material.',[('Warren LeMay photograph', 'https://commons.wikimedia.org/wiki/File:Downtown,_Anchorage,_AK_along_W_5th_Ave.jpg'), ('Commercial property record', 'https://www.muni.org/Lists/AssemblyListDocuments/Attachments/625369/AIM%20070-2016%20OCR.pdf')],'Dated local material'))
if engine_sound:
    items.append(('engine-acoustics','The Miata under load','RPM sets the four-cylinder firing cadence. Signed crankshaft torque, clutch load and throttle change the synthesized pressure pulses through acceleration, overrun and stalls. M controls sound.',[('Interior recording', 'https://freesound.org/people/TurboTheSergal/sounds/523351/'), ('Exterior recording', 'https://freesound.org/people/TurboTheSergal/sounds/523352/')],'Local synthesis'))
if crossfall:
    items.append(('crossfall','Camber under each tire','The roof worker reuses the same laser returns. Distinct flight strips must corroborate a measured cross-section and mapped crossings or structures exclude unsuitable road segments. Eligible profiles can inform a subsequent tile build in three prepared districts. The visible road and four wheel rays share its triangles.',[('USGS lidar','https://registry.opendata.aws/usgs-lidar/')],'Local + streamed'))
if stripes:
    items.append(('stripe-survey','Highway stripes','Two Utah corridors use visual traces of marking position with a state survey of stripe type. The game replaces the default markings only on accepted intervals.',[('Utah Open Data','https://opendata.utah.gov/')],'Verified sites'))
S=[]
def rect(x,y,w,h,fill,stroke='none',r=12):
    S.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}"/>')
def label(x,y,words,sz=17,fill='#20312b',weight=500,**attrs):
    at=' '.join(f'{k}="{v}"' for k,v in attrs.items())
    S.append(f'<text x="{x}" y="{y}" font-size="{sz}" font-weight="{weight}" fill="{fill}" {at}>{escape(str(words))}</text>')
def path(d,marker=True,stroke='#668f73'):
    S.append(f'<path d="{d}" fill="none" stroke="{stroke}" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"'+(' marker-end="url(#arrow)"' if marker else '')+'/>')
def block(x,y,w,h,title,lines,fill='#f1f4ea',stroke='#c1d0bf'):
    rect(x,y,w,h,fill,stroke)
    label(x+18,y+31,title,21,weight=700)
    for i,line in enumerate(lines):label(x+18,y+57+i*21,line,15,'#374c42')
S.append('<svg xmlns="http://www.w3.org/2000/svg" width="1130" height="785" viewBox="0 0 1130 785" role="img" aria-labelledby="title desc" font-family="Avenir Next,Helvetica,Arial,sans-serif"><title id="title">How Sunday Driver builds a real-world drive</title><desc id="desc">Geographic data feeds a local 3D scene. Car specifications and player inputs feed the physics model. Both meet in the rendered drive. Downloaded data is cached as the car moves.</desc><defs><marker id="arrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 L2 5 Z" fill="#668f73"/></marker></defs>')
rect(0,0,1130,785,'#f9f8f1',r=0)
label(32,50,'How Sunday Driver builds a real-world drive',35,weight=750)
label(34,80,release_name+(' · 1.0.8' if live_lane_paint else ' · 1.0.7' if ground13 else ' · 1.0.6' if streamed_roofs else ' · 1.0.5' if seasonal else ' · 1.0.4' if shrub and soil else ' · 1.0.3' if distant else ' · 1.0.2' if parking and gravel else '')+'   /   '+date.today().strftime('%d %b %Y'),16,'#55675c')
label(34,134,'Where the data comes from',20,weight=720)
label(482,134,'What happens on your Mac',20,weight=720)
source_rows=[
('search','Type a place','Photon search or coordinates'),
('roads','Roads and buildings','OpenStreetMap / OpenFreeMap · Overture' if enriched else 'OpenStreetMap · OpenFreeMap · Overpass'),
('ground','Relief out to 45 km' if distant else 'Hills and ground color','USGS bare earth · Terrarium · aerial imagery' if ground13 else 'Terrarium · 3DEP banks · USGS aerial imagery' if banks else 'Terrarium · USGS imagery · Natural Earth' if distant else 'Terrarium elevation · USGS / USDA imagery'),
('trees','Tree height and trunks','Canopy heights · LANDFIRE · RAP · USGS RCMAP' if shrub and forest else 'Meta / WRI canopy · municipal inventories' if has_tree_sources else 'Meta / WRI canopy map'),
('photos','Roadside appearance','KartaView photos · Panoramax references' if panoramax else 'KartaView street photos · where available'),
('weather','Weather and seasonal color','NOAA GOES / surface stations · Sentinel-2' if archive_sky else 'NOAA observations · Sentinel-2 imagery'),
('local','Local survey detail','USGS 3DEP · roofs, forest and terrain' if streamed_roofs else 'Miami / Charleston lidar · USACE' if has_landmark else 'Miami-Dade lidar · USACE building uses' if has_uses else 'State and local GIS · city inventories')]
for i,(key,note,detail) in enumerate(source_rows):
    y=153+i*56
    rect(32,y,392,48,'#ffffff','#d5dad0',7)
    label(46,y+20,note,16,weight=700);label(46,y+38,detail,13,'#536359')
path('M424 177 H450 V205 H482')
path('M424 233 H449 M424 289 H449 M424 345 H449 M424 401 H449 M424 457 H449 M424 513 H449 M449 233 V513',False)
path('M449 349 H482')
block(482,164,280,87,'Locate the car',['Resolve the street and select','the surrounding map tiles.'])
path('M622 251 V289')
block(482,292,280,108,'Stream and cache',['Fetch data near the car.','Reuse it when you revisit.','Load the next tiles as you drive.'])
path('M622 400 V441')
block(482,445,280,111,'Build the 3D street',['Align data by coordinates.','Mesh nearby roads and distant hills.' if distant else 'Build terrain, roads and buildings.','Apply local colors and materials.'])
path('M762 500 H855')
block(860,444,238,120,'Your driving view',['Godot renders the street,','car and lighting on the','Mac’s Metal GPU.'],'#d9eadb','#87a791')
label(34,598,'How the car responds',20,weight=720)
block(32,615,392,96,'Driver input + 1999 Miata Sport',['Xbox or keyboard · throttle, brake and steering','Five-speed, suspension, tires and rear handbrake.'],'#faf0df','#ddc8a5')
path('M424 662 H482')
block(482,615,280,96,'Vehicle physics',['Four tire contacts and suspension','forces update '+tick+' times per second.'],'#faf0df','#ddc8a5')
path('M762 664 H969 V568')
path('M725 556 V610')
label(742,586,'Pavement or gravel' if gravel else 'Road surface',13,'#4b6351')
label(34,754,'Street detail depends on the location, survey date and available camera coverage.',15,'#4c5c52')
S.append('</svg>')
svg=''.join(S)
def safe(s):return escape(str(s),quote=True)
sources=''.join('<tr id="'+key+'"><th scope="row">'+safe(title)+'</th><td>'+safe(mode)+'</td><td>'+safe(desc)+'<div>'+ ' · '.join('<a href="'+safe(url)+'">'+safe(title)+'</a>' for title,url in links)+'</div></td></tr>' for key,title,desc,links,mode in items)
experiment='On South Miami Avenue a small DeepLab vision model labeled three camera views on one CPU thread in roughly one third of a second per photo. A separate two-view depth test explained movement of some distant tree features in a held-out photograph, but their possible positions on the map differed by over 11 m. This test remains outside the playable scenery.'
physics='The playable car models torque transfer between the rear wheels, tire-force buildup and a measured rear Bilstein damping curve.' if upgraded else 'A torque-biasing rear differential and gradual tire-force buildup are in development.'
content='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Sunday Driver · How the world is built</title><style>
*{box-sizing:border-box}body{margin:0 auto;max-width:1240px;padding:clamp(10px,3vw,36px);background:#efefe8;color:#20312b;font:16px/1.5 "Avenir Next",Helvetica,Arial,sans-serif}main{background:#f9f8f1;box-shadow:0 2px 18px #21332512}.diagram{overflow:auto}svg{display:block;width:100%;min-width:760px;height:auto}section{padding:18px clamp(18px,4vw,42px) 30px}h2{font-size:25px;line-height:1.25;margin:0 0 14px}table{border-collapse:collapse;text-align:left;width:100%}td,th{border-top:1px solid #d5dcd1;padding:11px 10px 11px 0;vertical-align:top}th{width:22%}td:nth-child(2){width:14%;color:#566659}td div{font-size:14px}a{color:#246249;text-underline-offset:3px}p{max-width:88ch}figure{margin:20px 0}figure img{max-width:100%;border-radius:6px}figcaption{font-size:14px}footer{font-size:14px;color:#627366} @media(max-width:650px){body{padding:4px}table,tbody,tr,th,td{display:block;width:auto!important}tr{padding:9px 0;border-top:1px solid #d4d9d0}td,th{border:0;padding:1px 0}td:nth-child(2){font-size:13px}}</style><main><div class="diagram">'''+svg+'</div><section><h2>Source directory</h2><table><tbody>'+sources+'</tbody></table></section><section><h2>What the team is testing</h2><p>'+experiment+' '+physics+'</p>'
content+='<p><a href="Sunday Driver - Street comparisons.html">Compare the renderer with street photographs in Charleston, Lincoln, Lewisburg, Bethel, Key Largo, Miami, Monument Valley and the Tetons</a>.</p>'
if streamed_roofs:
    content+='<h3>What changes after the road appears?</h3><p>While the Miata moves, a single background building worker looks for USGS laser measurements around the car. In the Lincoln check it read 0.67 MB of compressed points and reconstructed eight roofs. Returning to the same district used the stored result and made zero roof requests. Separate octree samples checked the geometry. For seven of twelve study districts the measurements were sufficient for at least one roof. <a href="https://registry.opendata.aws/usgs-lidar/">USGS distribution</a> · <a href="https://github.com/hobuinc/laz-perf">laz-perf</a>.</p>'
content+='<h3>Open-source tools in the project</h3><p>Godot and Jolt run the local simulation. Martini simplifies regional elevation meshes. PMTiles range reads fetch the part of an Overture archive needed for nearby buildings. NumPy, SciPy, Shapely and pyproj prepare and check lidar-derived roof geometry. OpenCV and LiteRT are used offline to compare camera frames and label surfaces. Other candidates include PDAL for remote point-cloud queries and Roofer for automatic roof reconstruction from surveyed points and footprint polygons. They are possible authoring backends. <a href="https://godotengine.org/license/">Godot</a> · <a href="https://docs.protomaps.com/pmtiles/">PMTiles</a> · <a href="https://opencv.org/">OpenCV</a> · <a href="https://pdal.io/en/stable/stages/readers.copc.html">PDAL</a> · <a href="https://github.com/3DBAG/roofer">Roofer</a>.</p>'
if road_checks:
    count=sum(r.get('passed',False) for r in road_checks)
    rows=''.join(f'<tr><th scope="row">{safe(r["state"])}</th><td>{safe(r["town"])}</td><td>{r.get("roads",0)}</td><td>{r.get("repeat_requests","—")}</td></tr>' for r in road_checks)
    content+=f'<h3>Where we checked the road loader</h3><p>{count} of {len(road_checks)} sampled districts returned mapped roads and a finite elevation grid on the first visit, then returned the same roads from cache. This measures the geographic loader. It does not measure frame rate, street photography or the accuracy of a particular house.</p><details><summary>Show the locations and request counts</summary><table><thead><tr><th>State</th><th>Sample district</th><th>Road records</th><th>Repeat requests</th></tr></thead><tbody>{rows}</tbody></table></details>'
    content+='<h3>Additional sources under review</h3><p>The Federal Highway Administration publishes bridge inspections and the Department of Transportation collects official local address points. These could improve bridge geometry and ambiguous street searches. <a href="https://www.usgs.gov/3d-elevation-program/about-3dep-products-services">USGS elevation products</a> · <a href="https://www.fhwa.dot.gov/bridge/nbi/">National Bridge Inventory</a> · <a href="https://www.transportation.gov/gis/national-address-database">National Address Database</a>.</p>'
if (cfg.examples/'miami-photo.webp').is_file() and (cfg.examples/'miami-semantic.webp').is_file():
    def photo(name):
        return 'data:image/webp;base64,'+base64.b64encode((cfg.examples/name).read_bytes()).decode()
    palette=json.loads((cfg.examples/'palette.json').read_text())
    legend=' '.join('<span style="display:inline-flex;align-items:center;gap:5px;margin:3px 15px 3px 0"><i style="display:inline-block;width:13px;height:13px;border-radius:3px;background:rgb('+','.join(str(n) for n in palette['palette'][i])+')"></i>'+safe(title)+'</span>' for i,title in palette['index'].items())
    content+='<h3>What AI can see on one real street</h3><p>Move the slider to compare the South Miami Avenue photograph with the regions identified by the local model. Dark gray marks areas withheld from the reconstruction test. A colored region identifies a likely surface class. It still needs depth before we can place it in the game.</p><figure style="max-width:680px"><div style="position:relative;line-height:0"><img style="width:100%" src="'+photo('miami-photo.webp')+'" alt="South Miami Avenue street photograph"><img id="mask" style="position:absolute;inset:0;width:100%;clip-path:inset(0 0 0 50%)" src="'+photo('miami-semantic.webp')+'" alt="Model surface labels"><span id="division" style="position:absolute;left:50%;top:0;bottom:0;border-left:2px solid white"></span></div><label style="display:block;margin:10px 0">Photograph to surface labels <input aria-label="Amount of photograph visible" type="range" value="50" min="0" max="100" style="width:100%" id="blend"></label><div>'+legend+'</div><figcaption>Image adapted from KartaView frame 217, September 2025. © Grab and KartaView contributors, photographed by mrmappenburger. Resized and semantically labeled. <a href="https://kartaview.org/details/11591982/217">Source photograph</a> · <a href="https://creativecommons.org/licenses/by-sa/4.0/">CC BY-SA 4.0</a>. <a href="https://www.kaggle.com/models/spsayakpaul/deeplabv3-mobilenetv2/TfLite/ade20k/2">DeepLab model</a>.</figcaption></figure>'
content+="""</section></main><script>const slider=document.getElementById('blend');if(slider){slider.oninput=()=>{document.getElementById('mask').style.clipPath='inset(0 0 0 '+slider.value+'%)';document.getElementById('division').style.left=slider.value+'%';};}</script></html>"""
cfg.out.mkdir(parents=True,exist_ok=True)
(cfg.out/'Sunday Driver - world and data.html').write_text(content)
(cfg.out/'Sunday Driver - world and data.svg').write_text(svg)

# Also render the identical diagram for chat and apps without an SVG preview.
from PIL import Image, ImageDraw, ImageFont
import xml.etree.ElementTree as ET
import math
scale=2
canvas=Image.new('RGB',(1130*scale,785*scale),'white')
drawing=ImageDraw.Draw(canvas)
font_path='/System/Library/Fonts/Avenir Next.ttc'
def xy(x,y):return round(x*scale),round(y*scale)
for obj in ET.fromstring(svg):
    tag=obj.tag.rsplit('}',1)[-1]; a=obj.attrib
    if tag=='rect':
        x,y,w,h=[float(a[k]) for k in ('x','y','width','height')]
        outline=None if a['stroke']=='none' else a['stroke']
        drawing.rounded_rectangle([xy(x,y),xy(x+w,y+h)],radius=round(float(a['rx'])*scale),fill=a['fill'],outline=outline,width=scale)
    if tag=='text':
        font=ImageFont.truetype(font_path,int(a['font-size'])*scale,index=5 if int(a['font-weight'])<700 else 0)
        drawing.text(xy(float(a['x']),float(a['y'])),obj.text,font=font,fill=a['fill'],anchor='ls')
    if tag=='path':
        pts=[]
        for op,args in re.findall(r'([MHVL])([^MHVL]*)',a['d']):
            nums=list(map(float,args.strip().split()))
            if op=='M':
                if len(pts)>1:drawing.line([xy(*v) for v in pts],fill=a['stroke'],width=5)
                pts=[tuple(nums)]
            elif op=='H':pts.append((nums[0],pts[-1][1]))
            elif op=='V':pts.append((pts[-1][0],nums[0]))
            else:pts.append(tuple(nums))
        drawing.line([xy(*v) for v in pts],fill=a['stroke'],width=5)
        if 'marker-end' in a:
            (ax,ay),(bx,by)=pts[-2:];ang=math.atan2(by-ay,bx-ax)
            tip=[(bx,by)]+[(bx-12*math.cos(ang+d),by-12*math.sin(ang+d)) for d in (-.48,.48)]
            drawing.polygon([xy(*v) for v in tip],fill='#668f73')
canvas.resize((1582,1099),Image.Resampling.LANCZOS).save(cfg.out/'Sunday Driver - world and data.png',optimize=True)
