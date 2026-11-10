// Road widths from OpenStreetMap lane counts.
//
// The OpenMapTiles vector tiles carry a road's class and one-way flag but not
// its lanes, so every trunk used to be 16 m wide: a divided highway mapped as
// two one-way carriageways 12-16 m apart became two overlapping 16 m strips
// (Vidalia GA, US-280), and a one-way couplet split overlapped its own approach
// (Eureka CA, US-101). Here a road's carriageway is its OSM `lanes` x 3.6 m (or
// its OSM `width`), plus paved shoulders by class, and a one-way carriageway of
// a divided road gets an outside and a median shoulder instead of two outside
// ones. Lanes come from the OSM highways layer Esri publishes for North America
// (the same source as road-layout.mjs), read once per zoom-14 vector tile and
// matched to the vector roads by position and direction, so the width of a road
// is a function of the map alone and both tiles of a seam agree. Where the
// source cannot be read, the class and one-way defaults below apply.
import fs from 'node:fs';
import path from 'node:path';

const EARTH=40075016.68557849,HALF=EARTH/2,SPAN=EARTH/16384;
const DAY=86400000,LIFETIME=28*DAY;
const HOST='https://services6.arcgis.com/Do88DoK2xjTUCXd1/ArcGIS/rest/services/OSM_NA_Highways/FeatureServer/0/query';
export const LANE_M=3.6;
const MAJOR=['motorway','trunk','primary','secondary','tertiary'];
// Paved shoulder per side, metres: [outside, median side of a one-way carriageway].
// AASHTO-style rural values; on urban streets they stand in for the gutter and
// parking edge. Two-way roads get the outside shoulder on both sides.
const SHOULDER={motorway:[3,1.2],trunk:[2.4,.9],primary:[1.8,.6],secondary:[1.5,.6],tertiary:[1.2,.6]};
const RAMP_SHOULDER=[1.8,.6];
// Lanes when OSM has no count: [two-way, one-way].
const DEFAULT_LANES={motorway:[4,2],trunk:[2,2],primary:[2,2],secondary:[2,1],tertiary:[2,1]};
// Classes without lane data keep their fixed widths (residential streets include parking).
export const FIXED={minor:8,service:4.6,track:3};
const MATCH_M=6,PAGES=4,PAGE=2000;

export function laneWidth(cls,oneway,ramp,lanes=null,carriageway=null){
 if(!MAJOR.includes(cls))return FIXED[cls];
 const one=Boolean(oneway)||Boolean(ramp);
 const [outside,inside]=ramp?RAMP_SHOULDER:SHOULDER[cls];
 let road=Number.isFinite(carriageway)&&carriageway>=2.5&&carriageway<=30?carriageway:null;
 if(road===null){
  let n=Number.isInteger(lanes)&&lanes>0?lanes:(ramp?1:DEFAULT_LANES[cls][one?1:0]);
  n=Math.min(n,one?6:8);
  if(!one)n=Math.max(n,2);
  road=n*LANE_M;
 }
 return Math.round((road+(one?outside+inside:2*outside))*100)/100;
}
// The class table: the width of a road with no lane data (two-way, default lanes).
export const classWidths={...Object.fromEntries(MAJOR.map(c=>[c,laneWidth(c,0,0)])),...FIXED};

function query(px,py,offset){
 const pad=40,x0=px*SPAN-HALF-pad,x1=(px+1)*SPAN-HALF+pad,y1=HALF-py*SPAN+pad,y0=HALF-(py+1)*SPAN-pad;
 return HOST+'?'+new URLSearchParams({f:'json',where:"highway IN ('motorway','motorway_link','trunk','trunk_link','primary','primary_link','secondary','secondary_link','tertiary','tertiary_link')",
  inSR:'3857',outSR:'3857',geometryType:'esriGeometryEnvelope',geometry:[x0,y0,x1,y1].join(','),spatialRel:'esriSpatialRelIntersects',
  outFields:'highway,oneway,lanes,width',orderByFields:'OBJECTID',resultOffset:String(offset),resultRecordCount:String(PAGE),
  returnGeometry:'true',maxAllowableOffset:'1',geometryPrecision:'1'});
}
const positiveInt=v=>{const n=parseInt(String(v??''),10);return Number.isInteger(n)&&n>0&&n<=12?n:null};
const metres=v=>{const m=String(v??'').trim().match(/^(\d+(?:\.\d+)?)\s*(m)?$/);return m?Number(m[1]):null};
function decode(raw){
 const doc=JSON.parse(raw);
 if(doc.error||!Array.isArray(doc.features)||doc.features.length>PAGE)throw Error('Invalid lane observation');
 const ways=[];
 for(const f of doc.features){
  const a=f.attributes??{},paths=f.geometry?.paths;
  if(typeof a.highway!=='string'||!Array.isArray(paths))continue;
  const cls=a.highway.replace(/_link$/,'');
  if(!MAJOR.includes(cls))continue;
  for(const pts of paths){
   if(!Array.isArray(pts)||pts.length<2||pts.some(p=>!Array.isArray(p)||!Number.isFinite(p[0])||!Number.isFinite(p[1])))continue;
   ways.push({cls,link:a.highway.endsWith('_link'),lanes:positiveInt(a.lanes),width:metres(a.width),pts});
  }
 }
 return {ways,more:Boolean(doc.exceededTransferLimit)};
}

// Ways of one zoom-14 tile, or null when the source cannot be read completely.
async function parentWays(px,py,cache,read){
 const key='lane-widths-v1-'+px+'-'+py,miss=path.join(cache,key+'.miss');
 if(Date.now()-(fs.statSync(miss,{throwIfNoEntry:false})?.mtimeMs??0)<6*3600000)return null;
 try{
  const ways=[];
  for(let page=0;page<PAGES;page++){
   const id=key+'-'+page,raw=await read(id,query(px,py,page*PAGE),LIFETIME,null,12000,8000000);
   let parsed;
   try{parsed=decode(raw)}catch(error){fs.rmSync(path.join(cache,id),{force:true});throw error}
   ways.push(...parsed.ways);
   if(!parsed.more)return ways;
  }
  throw Error('Lane observation exceeds the page budget');
 }catch{
  try{fs.writeFileSync(miss,'0')}catch{}
  return null;
 }
}

function index(ways){
 const CELL=64,grid=new Map();
 ways.forEach((w,i)=>{for(let k=1;k<w.pts.length;k++){
  const a=w.pts[k-1],b=w.pts[k];
  for(let x=Math.floor(Math.min(a[0],b[0])/CELL);x<=Math.floor(Math.max(a[0],b[0])/CELL);x++)
   for(let y=Math.floor(Math.min(a[1],b[1])/CELL);y<=Math.floor(Math.max(a[1],b[1])/CELL);y++){
    const c=x+','+y;if(!grid.has(c))grid.set(c,[]);grid.get(c).push([i,k]);
   }
 }});
 return (x,y,cls,dx,dy,reach)=>{
  let best=null;
  const seen=new Set();
  for(let gx=Math.floor((x-reach)/CELL);gx<=Math.floor((x+reach)/CELL);gx++)for(let gy=Math.floor((y-reach)/CELL);gy<=Math.floor((y+reach)/CELL);gy++)for(const [i,k] of grid.get(gx+','+gy)??[]){
   const id=i*100000+k;if(seen.has(id))continue;seen.add(id);
   const w=ways[i];if(w.cls!==cls)continue;
   const a=w.pts[k-1],b=w.pts[k],ex=b[0]-a[0],ey=b[1]-a[1],l2=ex*ex+ey*ey;if(l2<1e-6)continue;
   const align=Math.abs(ex*dx+ey*dy)/Math.sqrt(l2*(dx*dx+dy*dy));
   if(align<.9)continue;
   const t=Math.max(0,Math.min(1,((x-a[0])*ex+(y-a[1])*ey)/l2)),d=Math.hypot(x-a[0]-t*ex,y-a[1]-t*ey);
   if(d<=reach&&(!best||d<best.d))best={d,w};
  }
  return best?.w??null;
 };
}

// Width resolver: the class table plus load(px, py) (async, per zoom-14 tile) and
// of(feature, px, py), the width of one vector-tile road of that tile.
export function laneWidths(cache,read){
 const loading=new Map(),matchers=new Map(),memo=new Map();
 const table={...classWidths};
 Object.defineProperties(table,{
  // of() is synchronous: callers await load() for a tile before asking.
  load:{value:(px,py)=>{
   const key=px+','+py;
   if(!loading.has(key))loading.set(key,parentWays(px,py,cache,read).then(ways=>{matchers.set(key,ways?index(ways):null);return ways!==null}));
   return loading.get(key);
  }},
  of:{value:(feature,px,py)=>{
   const p=feature.properties;
   if(!MAJOR.includes(p.class))return FIXED[p.class];
   const key=px+','+py+':'+feature.bbox().join(',')+':'+p.class+':'+(p.oneway??0)+':'+(p.ramp??0);
   if(!memo.has(key))memo.set(key,roadWidth(feature,px,py,matchers.get(px+','+py)));
   return memo.get(key);
  }}
 });
 return table;
}
function roadWidth(feature,px,py,match){
 const p=feature.properties;
 if(!MAJOR.includes(p.class))return FIXED[p.class];
 const ramp=Number(p.ramp??0)===1;
 if(!match)return laneWidth(p.class,p.oneway,ramp);
 const lat=Math.atan(Math.sinh(Math.PI*(1-2*(py+.5)/16384)));
 const reach=MATCH_M/Math.cos(lat),step=8/Math.cos(lat),f=SPAN/feature.extent;
 const votes=new Map();let samples=0;
 for(const line of feature.loadGeometry()){
  const pts=line.map(q=>[px*SPAN-HALF+q.x*f,HALF-py*SPAN-q.y*f]);
  for(let k=1;k<pts.length;k++){
   const a=pts[k-1],b=pts[k],dx=b[0]-a[0],dy=b[1]-a[1],l=Math.hypot(dx,dy);if(l<1e-3)continue;
   for(let s=step/2;s<l;s+=step){
    if(++samples>400)break;
    const w=match(a[0]+dx*s/l,a[1]+dy*s/l,p.class,dx,dy,reach);
    const v=w?(w.lanes?'l'+w.lanes:w.width?'w'+w.width:'-'):null;
    if(v)votes.set(v,(votes.get(v)??0)+1);
   }
  }
 }
 let best=null,n=0;
 for(const [v,c] of votes)if(c>n||(c===n&&v<best)){best=v;n=c}
 if(!best||best==='-')return laneWidth(p.class,p.oneway,ramp);
 return best[0]==='l'?laneWidth(p.class,p.oneway,ramp,Number(best.slice(1))):laneWidth(p.class,p.oneway,ramp,null,Number(best.slice(1)));
}
