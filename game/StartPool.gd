extends RefCounted
## Where a drive begins. data/start-pool.json lists verified US roads: each
## point is an OpenStreetMap node on the named way, with the way's bearing
## (provenance in licenses/Road Atlas.txt). Drive.gd starts a normal launch on
## a random entry; when that jump fails (no map helper, offline, or no road
## inside the search ring) it tries RETRIES more entries and then FALLBACK,
## whose base tiles ship in data/ and load with no network. Check modes start
## at FALLBACK directly so their runs repeat.
const POOL_PATH := "res://data/start-pool.json"
const RETRIES := 3
const FALLBACK := {"lon":-80.13025,"lat":25.78000,"title":"Ocean Drive, Miami Beach · Art Deco District"}

# Entries that cannot place a car are dropped, so a damaged file only shrinks the choice.
static func load_pool(path: String = POOL_PATH) -> Array:
 var pool := []
 if not FileAccess.file_exists(path):return pool
 var parsed = JSON.parse_string(FileAccess.get_file_as_string(path))
 if not parsed is Array:return pool
 for entry in parsed:
  if valid(entry):pool.append(entry)
 return pool

static func valid(entry) -> bool:
 if not entry is Dictionary or str(entry.get("title","")).strip_edges().is_empty():return false
 for key in ["lon","lat","bearing"]:
  if not (entry.get(key) is float or entry.get(key) is int):return false
 return absf(float(entry.lat))<=82. and absf(float(entry.lon))<=180. and float(entry.bearing)>=0. and float(entry.bearing)<360.

# Up to count distinct entries in random order (a partial Fisher-Yates shuffle
# driven by rng). The entry titled avoid is left out while any other remains.
static func sequence(pool: Array, rng: RandomNumberGenerator, count: int, avoid: String = "") -> Array:
 var order := []
 for entry in pool:
  if str(entry.title)!=avoid:order.append(entry)
 if order.is_empty():order=pool.duplicate()
 var picks := []
 for i in mini(count,order.size()):
  var j := rng.randi_range(i,order.size()-1)
  var chosen = order[j];order[j]=order[i];order[i]=chosen
  picks.append(chosen)
 return picks
