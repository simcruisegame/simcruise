// Mapped guard rails and concrete barriers (OSM barrier=guard_rail and
// barrier=jersey_barrier) for one z16 tile, from its z14 neighbourhood.
//
// OSM data is ODbL: it is fetched at runtime into the player's own cache and is
// never committed. The public Overpass servers are run by volunteers and are
// often saturated, so the fetch is built never to slow tile loading:
//   * one query per z14 neighbourhood (16 z16 tiles), cached for 90 days by the
//     shared cache reader (atlas-cache.mjs), with its stale copy as fallback;
//   * a rotating list of public mirrors, tried in turn with exponential backoff,
//     inside a hard time budget (BUDGET_MS) for the whole attempt;
//   * a negative cache: a neighbourhood that failed is not asked again for
//     1 h, doubling per failure up to 7 days, and while every mirror is
//     failing a provider-wide pause (10 min doubling up to 6 h) makes other
//     neighbourhoods skip the network entirely;
//   * a response Overpass marks incomplete (a "remark" with a runtime error or
//     timeout) is treated as a failure and never cached.
// GuardRails.gd draws what this returns and keeps its engineering-warrant
// rails only where nothing is mapped.
//
// SIMCRUISE_OVERPASS_URL replaces the mirror list with one interpreter (for
// operators who run their own). SIMCRUISE_GUARD_RAILS=offline keeps to the
// cache. SIMCRUISE_GUARD_RAILS_FIXTURE=<file> reads an Overpass-shaped JSON
// file instead of the network (tests and captures only; it is not cached).
import fs from 'node:fs';
import path from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';

export const MIRRORS=[
 'https://overpass-api.de/api/interpreter',
 'https://overpass.private.coffee/api/interpreter',
 'https://maps.mail.ru/osm/tools/overpass/api/interpreter',
 'https://overpass.kumi.systems/api/interpreter',
];
const VERSION='guard-rails-v1';
const DAY=86400000,HOUR=3600000,MINUTE=60000;
export const TTL=90*DAY;
export const BUDGET_MS=3000;
const KINDS=new Set(['guard_rail','jersey_barrier']);

const age=file=>Date.now()-(fs.statSync(file,{throwIfNoEntry:false})?.mtimeMs??0);
function readJSON(file){try{return JSON.parse(fs.readFileSync(file,'utf8'))}catch{return null}}
function writeJSON(file,value){try{fs.writeFileSync(file,JSON.stringify(value))}catch{}}

// z14 bounds as south,west,north,east (Overpass bbox order).
export function neighbourhoodBounds(mx,my){
 const lat=k=>Math.atan(Math.sinh(Math.PI*(1-2*k/16384)))*180/Math.PI;
 const lon=k=>k/16384*360-180;
 return [lat(my+1),lon(mx),lat(my),lon(mx+1)].map(n=>n.toFixed(6)).join(',');
}
export function overpassQuery(mx,my){
 return '[out:json][timeout:20];way[barrier~"^(guard_rail|jersey_barrier)$"]('+neighbourhoodBounds(mx,my)+');out geom;';
}

// Overpass elements -> rows [kind, [[x,y],...]] in units of the z16 tile x,y
// (0..1 across the tile), clipped to the tile plus a small margin so a rail
// that runs on across a tile edge is drawn on both sides of it and meets.
export function guardRailRows(elements,x,y,margin=.02){
 const project=p=>[(p.lon+180)/360*65536-x,(1-Math.asinh(Math.tan(p.lat*Math.PI/180))/Math.PI)*32768-y];
 const lo=-margin,hi=1+margin;
 const inside=p=>p[0]>=lo&&p[0]<=hi&&p[1]>=lo&&p[1]<=hi;
 const round=n=>Math.round(n*1e5)/1e5;
 const clip=(a,b)=>{let t0=0,t1=1;const dx=b[0]-a[0],dy=b[1]-a[1];
  for(const [p,q] of [[-dx,a[0]-lo],[dx,hi-a[0]],[-dy,a[1]-lo],[dy,hi-a[1]]]){
   if(p===0){if(q<0)return null;continue}const t=q/p;if(p<0)t0=Math.max(t0,t);else t1=Math.min(t1,t);if(t0>t1)return null}
  return [[a[0]+t0*dx,a[1]+t0*dy],[a[0]+t1*dx,a[1]+t1*dy]]};
 const rows=[],seen=new Set();
 for(const item of elements??[]){
  if(item?.type!=='way'||!Array.isArray(item.geometry)||item.geometry.length<2)continue;
  const kind=item.tags?.barrier;
  if(!KINDS.has(kind))continue;
  if(seen.has(item.id))continue;seen.add(item.id);
  const pts=item.geometry.filter(p=>Number.isFinite(p?.lat)&&Number.isFinite(p?.lon)).map(project);
  let run=[];
  const commit=()=>{if(run.length>1)rows.push([kind,run.map(p=>[round(p[0]),round(p[1])])]);run=[]};
  for(let i=1;i<pts.length;i++){
   const a=pts[i-1],b=pts[i];
   if(inside(a)&&inside(b)){if(!run.length)run.push(a);run.push(b);continue}
   const v=clip(a,b);
   if(!v){commit();continue}
   if(!run.length||run.at(-1)[0]!==v[0][0]||run.at(-1)[1]!==v[0][1]){commit();run.push(v[0])}
   run.push(v[1]);
   if(!inside(b))commit();
  }
  commit();
 }
 return rows;
}

function validElements(bytes){
 const data=JSON.parse(bytes.toString());
 if(!Array.isArray(data.elements))throw Error('No element list');
 // Overpass answers 200 with a remark when a query ran out of time or memory;
 // the element list is then partial.
 if(data.remark&&/error|timed out|timeout|out of memory/i.test(String(data.remark)))throw Error('Incomplete Overpass answer');
 return data.elements;
}

// Returns {status, rows}: status is 'mapped' (fresh or cached answer),
// 'fixture', or 'unavailable' / 'offline' (rows empty; rails are inferred).
export async function mappedGuardRails(x,y,cache,read,options={}){
 const env=options.env??process.env;
 const now=options.now??Date.now;
 const wait=options.delay??delay;
 const budget=options.budget??BUDGET_MS;
 const mx=Math.floor(x/4),my=Math.floor(y/4);
 const id=VERSION+'-'+mx+'-'+my,file=path.join(cache,id);
 const fixture=env.SIMCRUISE_GUARD_RAILS_FIXTURE;
 if(fixture){
  try{return {status:'fixture',rows:guardRailRows(validElements(fs.readFileSync(fixture)),x,y)}}
  catch{return {status:'unavailable',rows:[]}}
 }
 // A fresh answer needs no network and no negative-cache bookkeeping.
 if(age(file)<TTL){
  try{return {status:'mapped',rows:guardRailRows(validElements(fs.readFileSync(file)),x,y)}}catch{fs.rmSync(file,{force:true})}
 }
 const stale=()=>{try{return {status:'mapped',rows:guardRailRows(validElements(fs.readFileSync(file)),x,y)}}catch{return null}};
 if(env.SIMCRUISE_GUARD_RAILS==='offline')return stale()??{status:'offline',rows:[]};
 const missFile=path.join(cache,id+'.miss'),providerFile=path.join(cache,VERSION+'-provider.miss');
 const miss=readJSON(missFile),provider=readJSON(providerFile);
 if((miss&&now()<miss.until)||(provider&&now()<provider.until))return stale()??{status:'unavailable',rows:[]};
 let mirrors=MIRRORS;
 if(env.SIMCRUISE_OVERPASS_URL){
  try{const u=new URL(env.SIMCRUISE_OVERPASS_URL);if(['http:','https:'].includes(u.protocol))mirrors=[u.toString()]}catch{}
 }
 // Start at a different mirror per neighbourhood so load spreads across them.
 const start=((mx*31+my*17)%mirrors.length+mirrors.length)%mirrors.length;
 const query=overpassQuery(mx,my);
 const deadline=now()+budget;
 let pause=250;
 for(let attempt=0;attempt<mirrors.length;attempt++){
  const left=deadline-now();
  if(left<400)break;
  const server=new URL(mirrors[(start+attempt)%mirrors.length]);
  server.searchParams.set('data',query);
  try{
   const bytes=await read(id,server.toString(),TTL,null,Math.min(left,2500));
   // On a failed request the reader hands back the stale copy: that is a
   // failure for the negative cache, and the stale rows are used below.
   if(age(file)>=TTL)throw Error('Stale copy');
   const rows=guardRailRows(validElements(bytes),x,y);
   fs.rmSync(missFile,{force:true});fs.rmSync(providerFile,{force:true});
   return {status:'mapped',rows};
  }catch{
   // The cache reader keeps whatever bytes a server sent; an invalid answer
   // must not stay cached as if it were the inventory.
   try{validElements(fs.readFileSync(file))}catch{fs.rmSync(file,{force:true})}
  }
  if(attempt+1<mirrors.length&&deadline-now()>pause+400){await wait(pause);pause*=2}
 }
 const failures=(miss?.failures??0)+1;
 writeJSON(missFile,{failures,until:now()+Math.min(7*DAY,HOUR*2**(failures-1))});
 const streak=(provider?.failures??0)+1;
 writeJSON(providerFile,{failures:streak,until:now()+Math.min(6*HOUR,10*MINUTE*2**(streak-1))});
 return stale()??{status:'unavailable',rows:[]};
}
