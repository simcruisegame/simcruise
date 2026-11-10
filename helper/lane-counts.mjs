// Mapped lane counts for the paved major roads of one z16 tile.
//
// The OpenMapTiles vector tiles the roads come from carry no lane tags, so
// the game used to guess every marking from the road class. This reads the
// OpenStreetMap `lanes`, `lanes:forward`, `lanes:backward`, `oneway` and
// `maxspeed` tags for every major way near the tile from Esri's North America
// OSM highways service (the same service road-layout.mjs already uses), and
// matches each road segment of the tile to the nearest way that runs the same
// way. The game (LaneMarkings.gd) turns the counts into MUTCD markings.
//
// The service has no `turn:lanes` field, so turn lanes are only seen as extra
// lanes on the short approach ways that carry them.
//
// Output (tile JSON `road_lanes`): {version, status, source, snapshot, roads}
// where roads holds [road index, [[first segment, last segment, lanes,
// forward, backward, mph], ...]] and forward/backward are counted along the
// tile road's own point order (0 = not mapped). Segments with no confident
// match are left out; the game uses US defaults for them.
import fs from 'node:fs';
import path from 'node:path';

const day=86400000, lifetime=28*day, span=40075016.68557849/65536;
const host='https://services6.arcgis.com/Do88DoK2xjTUCXd1/ArcGIS/rest/services/OSM_NA_Highways/FeatureServer/0/query';
const HIGHWAYS=['motorway','trunk','primary','secondary','tertiary','motorway_link','trunk_link','primary_link','secondary_link','tertiary_link'];
export const LANE_CLASSES=new Set(['motorway','trunk','primary','secondary','tertiary']);
const MAX_WAYS=2000, MAX_POINTS=80000;
// A z14 road line sits within a few metres of the OSM way it was drawn from;
// the other carriageway of a divided road is normally 10 m or more away.
const MATCH_M=6, ALIGN=.94, CELL=32;
const group=(x,y)=>[x-(x%2),y-(y%2)];
export const laneCountKey=(x,y)=>'lane-counts-v1-'+group(x,y).join('-');
const validId=(x,y)=>[x,y].every(n=>Number.isInteger(n)&&n>=0&&n<65536);

export function laneCountQuery(x,y){
 const [gx,gy]=group(x,y),half=span*32768;
 const bbox=[gx*span-half-60,half-(gy+2)*span-60,(gx+2)*span-half+60,half-gy*span+60];
 return host+'?'+new URLSearchParams({f:'json',where:"highway IN ('"+HIGHWAYS.join("','")+"')",
  inSR:'3857',outSR:'3857',geometryType:'esriGeometryEnvelope',geometry:bbox.join(','),
  outFields:'osm_id2,highway,oneway,lanes,lanes_forward,lanes_backward,maxspeed',
  resultRecordCount:String(MAX_WAYS),returnGeometry:'true',maxAllowableOffset:'.5',geometryPrecision:'1'});
}
// First whole number of a tag value ("2", "2;3", "3.5"); 0 when absent or odd.
export function count(value){
 const m=/^\s*(\d{1,2})/.exec(String(value??''));
 const n=m?Number(m[1]):0;
 return n>=1&&n<=12?n:0;
}
// Posted speed in mph. OSM's plain numbers are km/h; US ways normally say "mph".
export function mph(value){
 const m=/^\s*(\d{1,3})(?:\s*(mph|km\/h|kmh|kph))?\s*$/i.exec(String(value??''));
 if(!m)return 0;
 const n=Number(m[1]),v=m[2]&&m[2].toLowerCase()==='mph'?n:Math.round(n/1.609344);
 return v>=5&&v<=90?v:0;
}
export function onewayOf(value,highway){
 const v=String(value??'').toLowerCase();
 if(v==='yes'||v==='1'||v==='true')return 1;
 if(v==='-1'||v==='reverse')return -1;
 if(v==='no'||v==='0'||v==='false')return 0;
 return highway==='motorway'||highway==='motorway_link'?1:0;
}
export function decodeLaneWays(raw){
 const doc=JSON.parse(raw);
 if(doc.error||doc.exceededTransferLimit||!Array.isArray(doc.features)||doc.features.length>MAX_WAYS)throw Error('Incomplete lane coverage');
 const ways=[];let points=0;
 for(const f of doc.features){
  const a=f.attributes,parts=f.geometry?.paths;
  if(!a||!HIGHWAYS.includes(a.highway)||!Array.isArray(parts)||parts.length>64)throw Error('Invalid lane observation');
  const way={oneway:onewayOf(a.oneway,a.highway),lanes:count(a.lanes),forward:count(a.lanes_forward),backward:count(a.lanes_backward),mph:mph(a.maxspeed)};
  if(!way.lanes&&!way.forward&&!way.backward&&!way.mph)continue;
  for(const pts of parts){
   if(!Array.isArray(pts)||pts.length<2||(points+=pts.length)>MAX_POINTS||pts.some(p=>!Array.isArray(p)||p.length!==2||!p.every(Number.isFinite)))throw Error('Invalid lane geometry');
   ways.push({...way,pts});
  }
 }
 return ways;
}
// Fetches (or reads the 28-day cache of) the ways around the tile; null when
// the service cannot be reached, so the tile still builds with US defaults.
export async function fetchLaneWays(x,y,dir,read){
 if(!validId(x,y))return null;
 const key=laneCountKey(x,y),miss=path.join(dir,key+'.miss');
 const negative=fs.statSync(miss,{throwIfNoEntry:false});
 if(negative&&Date.now()-negative.mtimeMs<6*3600000)return null;
 try{
  const raw=await read(key,laneCountQuery(x,y),lifetime,null,8000,4000000);
  try{return {ways:decodeLaneWays(raw),snapshot:new Date(fs.statSync(path.join(dir,key),{throwIfNoEntry:false})?.mtimeMs??Date.now()).toISOString()};}
  catch(error){
   const file=path.join(dir,key);
   try{if(fs.readFileSync(file).equals(Buffer.from(raw)))fs.unlinkSync(file);}catch{}
   throw error;
  }
 }catch{
  try{fs.writeFileSync(miss,'0');}catch{}
  return null;
 }
}
// Matches every segment of the tile's paved major roads to a mapped way.
// roads: the tile's road rows ([class, oneway, name, points 0..1, width, ...]).
export function matchLaneCounts(x,y,roads,ways){
 const cos=1/Math.cosh(Math.PI*(1-2*(y+.5)/65536)),side=span*cos,west=x*span-span*32768,north=span*32768-y*span;
 const segs=[],grid=new Map();
 for(const w of ways){
  const pts=w.pts.map(p=>[(p[0]-west)*cos,(north-p[1])*cos]);
  for(let k=1;k<pts.length;k++){
   const a=pts[k-1],b=pts[k],dx=b[0]-a[0],dy=b[1]-a[1],len=Math.hypot(dx,dy);
   if(len<.05)continue;
   if(Math.max(a[0],b[0])<-MATCH_M||Math.min(a[0],b[0])>side+MATCH_M||Math.max(a[1],b[1])<-MATCH_M||Math.min(a[1],b[1])>side+MATCH_M)continue;
   const seg={a,dx:dx/len,dy:dy/len,len,way:w},id=segs.push(seg)-1;
   for(let i=Math.floor((Math.min(a[0],b[0])-MATCH_M)/CELL);i<=Math.floor((Math.max(a[0],b[0])+MATCH_M)/CELL);i++)
    for(let j=Math.floor((Math.min(a[1],b[1])-MATCH_M)/CELL);j<=Math.floor((Math.max(a[1],b[1])+MATCH_M)/CELL);j++){
     const key=i+','+j;if(!grid.has(key))grid.set(key,[]);grid.get(key).push(id);
    }
  }
 }
 const out=[];
 roads.forEach((road,index)=>{
  if(!LANE_CLASSES.has(road[0])||road[5]==='unpaved'||!Array.isArray(road[3]))return;
  const oneway=Number(road[1])!==0;
  const runs=[];
  for(let k=1;k<road[3].length;k++){
   const a=road[3][k-1].map(v=>v*side),b=road[3][k].map(v=>v*side);
   const dx=b[0]-a[0],dy=b[1]-a[1],len=Math.hypot(dx,dy);
   if(len<.5)continue;
   const ux=dx/len,uy=dy/len,votes=new Map();
   // A long map segment can span several OSM ways (turn pockets at each
   // block), so the samples vote on the counts they read, not on the way.
   const samples=Math.max(1,Math.min(16,Math.round(len/12)));
   for(let s=0;s<samples;s++){
    const f=(s+.5)/samples,px=a[0]+dx*f,py=a[1]+dy*f;
    let best=null,bestD=MATCH_M;
    for(const id of grid.get(Math.floor(px/CELL)+','+Math.floor(py/CELL))??[]){
     const seg=segs[id],along=ux*seg.dx+uy*seg.dy;
     if(Math.abs(along)<ALIGN)continue;
     const t=Math.max(0,Math.min(seg.len,(px-seg.a[0])*seg.dx+(py-seg.a[1])*seg.dy));
     const d=Math.hypot(px-seg.a[0]-seg.dx*t,py-seg.a[1]-seg.dy*t);
     if(d<bestD){bestD=d;best={way:seg.way,sign:along>0?1:-1};}
    }
    if(!best)continue;
    const w=best.way;let entry;
    if(oneway){
     if(w.oneway===0)continue;
     entry=[w.lanes||w.forward,0,0,w.mph];
     if(entry[0])entry[1]=entry[0];
    }else{
     if(w.oneway!==0)continue;
     const [f,bk]=best.sign>0?[w.forward,w.backward]:[w.backward,w.forward];
     entry=[w.lanes,f,bk,w.mph];
    }
    const key=entry.join('/'),v=votes.get(key)??{n:0,entry};v.n++;votes.set(key,v);
   }
   const ranked=[...votes.values()].sort((p,q)=>q.n-p.n);
   let entry=ranked[0]?.entry;
   // Most samples must agree on the counts, or at least on the lane total
   // (the split then falls back to the game's rule for that total).
   if(!entry)continue;
   if(ranked[0].n*2<=samples){
    const totals=new Map(),speeds=new Map();
    for(const v of ranked){totals.set(v.entry[0],(totals.get(v.entry[0])??0)+v.n);speeds.set(v.entry[3],(speeds.get(v.entry[3])??0)+v.n);}
    const [lanes,n]=[...totals].sort((p,q)=>q[1]-p[1])[0];
    if(!lanes||n*2<=samples)continue;
    entry=[lanes,oneway?lanes:0,0,[...speeds].sort((p,q)=>q[1]-p[1])[0][0]];
   }
   if(!entry[0]&&!entry[1]&&!entry[2]&&!entry[3])continue;
   const j=k-1,last=runs.at(-1);
   if(last&&last[1]===j-1&&last.slice(2).every((v,i)=>v===entry[i]))last[1]=j;
   else runs.push([j,j,...entry]);
  }
  if(runs.length)out.push([index,runs]);
 });
 return out;
}
