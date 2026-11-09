extends SceneTree
# godot --headless --path game --script res://checks/StartPoolCheck.gd
# The launch pool (data/start-pool.json through StartPool.gd), without a scene:
# every entry can place a car and carries only the seven fields the game reads,
# titles are unique, a seed repeats its launch sequence of four distinct roads,
# clock-like seeds spread the first pick over the whole pool and fresh
# generators seeded by randomize() (the launch's path) reach many roads, Surprise me never
# repeats the current place, malformed entries are refused, and the fallback
# place has bundled base tiles. Prints one summary line and PASS or FAIL.
const POOL=preload("res://StartPool.gd")
const FIELDS=["title","street","lon","lat","bearing","state","kind"]
var failures=[]

func expect(ok:bool,label:String)->void:
 if not ok:failures.append(label)

static func titles(places:Array)->Array:
 var out=[]
 for place in places:out.append(place.title)
 return out

func _initialize():
 var raw=JSON.parse_string(FileAccess.get_file_as_string(POOL.POOL_PATH))
 var pool=POOL.load_pool()
 expect(raw is Array and pool.size()==raw.size(),"every pool entry is valid")
 expect(pool.size()>=30,"at least 30 pool roads")
 var seen={};var states={};var kinds={}
 for entry in pool:
  seen[entry.title]=true;states[entry.state]=true;kinds[entry.kind]=true
  expect(str(entry.get("street","")).strip_edges()!="","street named: "+entry.title)
  expect(float(entry.lat)>18. and float(entry.lat)<72. and float(entry.lon)>-170. and float(entry.lon)<-65.,"inside the United States: "+entry.title)
  var keys=entry.keys();keys.sort();var wanted=FIELDS.duplicate();wanted.sort()
  expect(keys==wanted,"only the seven game fields: "+entry.title)
 expect(seen.size()==pool.size(),"titles are unique")
 # A seed repeats its launch: four distinct places.
 var a=RandomNumberGenerator.new();a.seed=20261108
 var b=RandomNumberGenerator.new();b.seed=20261108
 var first=POOL.sequence(pool,a,1+POOL.RETRIES);var again=POOL.sequence(pool,b,1+POOL.RETRIES)
 var distinct={}
 for title in titles(first):distinct[title]=true
 expect(titles(first)==titles(again),"the same seed gives the same launch sequence")
 expect(first.size()==1+POOL.RETRIES and distinct.size()==first.size(),"a launch tries four distinct roads")
 # Clock-like seeds: every entry comes first, none much more often than another.
 var counts={};var rng=RandomNumberGenerator.new()
 for i in 7200:
  rng.seed=1762600000+i*37
  var pick=POOL.sequence(pool,rng,1)[0].title;counts[pick]=counts.get(pick,0)+1
 var least=1<<30;var most=0
 for title in seen:
  least=mini(least,counts.get(title,0));most=maxi(most,counts.get(title,0))
 expect(counts.size()==pool.size(),"every road can be the first pick")
 expect(least>0 and float(most)/least<1.8,"first picks are spread evenly (%d..%d of 7200)"%[least,most])
 # The launch's own path: a fresh generator seeded from the clock by randomize().
 var clocked={}
 for i in 300:
  var fresh=RandomNumberGenerator.new();fresh.randomize()
  clocked[POOL.sequence(pool,fresh,1)[0].title]=true
  OS.delay_usec(37)
 expect(clocked.size()>=20,"clock-seeded launches reach many roads (%d of %d in 300)"%[clocked.size(),pool.size()])
 # Surprise me skips the current place; small and empty pools still behave.
 var repeats=0
 for i in 500:
  rng.seed=i
  for place in POOL.sequence(pool,rng,1+POOL.RETRIES,pool[0].title):
   if place.title==pool[0].title:repeats+=1
 expect(repeats==0,"Surprise me never offers the current place")
 expect(POOL.sequence([],rng,4).is_empty(),"an empty pool gives no places")
 expect(POOL.sequence([pool[0]],rng,4,pool[0].title).size()==1,"a one-road pool still starts on it")
 expect(POOL.sequence(pool,rng,1000).size()==pool.size(),"a long sequence stops at the pool size")
 expect(POOL.load_pool("res://data/no-such-pool.json").is_empty(),"a missing pool file gives an empty pool")
 for bad in [{"lon":0.,"lat":0.,"bearing":0.},{"title":"x","lon":0.,"lat":95.,"bearing":0.},{"title":"x","lon":"1","lat":0.,"bearing":0.},{"title":"x","lon":0.,"lat":0.,"bearing":360.},"x"]:
  expect(not POOL.valid(bad),"malformed entry refused: "+str(bad))
 # The fallback loads offline: its base tile is bundled.
 var lat=deg_to_rad(float(POOL.FALLBACK.lat))
 var tile=Vector2i(floori((float(POOL.FALLBACK.lon)+180.)/360.*65536.),floori((1.-asinh(tan(lat))/PI)/2.*65536.))
 expect(FileAccess.file_exists("res://data/%d_%d.json"%[tile.x,tile.y]),"the fallback's base tile is bundled")
 print("STARTPOOL entries=%d states=%d kinds=%d seeded_launch=%s first_pick_range=%d..%d/7200 clock_seeded_roads=%d/300 fallback_tile=%s"%[pool.size(),states.size(),kinds.size(),str(titles(first)),least,most,clocked.size(),str(tile)])
 if failures.is_empty():print("STARTPOOL PASS")
 else:print("STARTPOOL FAIL ",failures)
 quit(0 if failures.is_empty() else 1)
