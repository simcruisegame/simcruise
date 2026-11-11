// Optional OSM details from a shared, cacheable z14 neighbourhood.
import fs from 'node:fs';
import path from 'node:path';
import honolulu from '../data/observed/observed-honolulu.json' with {type:'json'};
export function streetObjects(elements,x,y){
 const project=p=>[(p.lon+180)/360*65536-x,(1-Math.asinh(Math.tan(p.lat*Math.PI/180))/Math.PI)*32768-y];
 const owned=p=>p[0]>=0&&p[0]<1&&p[1]>=0&&p[1]<1;
 const objects={signals:[],canopies:[],building_finishes:[]};
 const seen=new Set();
 const stations=elements.filter(e=>e.tags?.amenity==='fuel');
 const centres=stations.map(e=>e.center??(e.geometry?e.geometry[0]:e));
 for(const item of elements){
  const identity=item.type+'/'+item.id;
  if(item.id!=null&&seen.has(identity))continue;
  seen.add(identity);
  const tags=item.tags||{};
  if(item.type==='node'&&tags.highway==='traffic_signals'){
   const p=project(item);if(owned(p))objects.signals.push(p);continue;
  }
  if(item.type!=='way'||!item.geometry||item.geometry.length<4)continue;
  const pts=item.geometry.slice(0,-1).map(project);
  const c=pts.reduce((a,v)=>[a[0]+v[0]/pts.length,a[1]+v[1]/pts.length],[0,0]);
  if(!owned(c))continue;
  // An observation belongs only to its closed way. Skip courtyard relations,
  // parts, outlines from other tiles and ambiguous or unusually complex data.
  const first=item.geometry[0],last=item.geometry.at(-1);
  if(tags.building && !tags['building:part'] && tags.building!=='roof' && first.lat===last.lat && first.lon===last.lon && pts.length<=95 && objects.building_finishes.length<48){
   const details={};
   for(const [key,field] of [['building:material','wall_material'],['building:colour','wall_color'],['roof:material','roof_material'],['roof:colour','roof_color'],['roof:shape','shape']]){
    const value=tags[key];
    if(typeof value==='string'&&value.length<=32&&/^(?:#[0-9a-f]{3}(?:[0-9a-f]{3})?|[a-z][a-z_ -]*)$/i.test(value))details[field]=value.toLowerCase();
   }
   if(details.shape && !['flat','gabled'].includes(details.shape))delete details.shape;
   if(Object.keys(details).length){
    if(details.shape==='gabled'&&/^\d+(?:\.\d+)?(?: m)?$/.test(tags['roof:height']||'')){
     const h=parseFloat(tags['roof:height']);if(h<=40)details.roof_height=h;
    }
    if(details.shape==='gabled'&&tags['roof:orientation']==='across')details.across=true;
    objects.building_finishes.push({way:item.id,ring:pts.map(p=>p.map(n=>+n.toFixed(6))),...details});
   }
  }
  const close=centres.findIndex(p=>p&&Math.hypot(project(p)[0]-c[0],project(p)[1]-c[1])<.14);
  if(close<0)continue;
  const edges=pts.map((p,i)=>Math.hypot(p[0]-pts[(i+1)%pts.length][0],p[1]-pts[(i+1)%pts.length][1]));
  // Some individually mapped fuel islands are classified as generic buildings.
  // Only infer a canopy on an elongated four-sided fuel island.
  if(tags.building!=='roof'&&!(tags.amenity==='fuel'&&pts.length===4&&Math.max(...edges)>2.8*Math.min(...edges)))continue;
  objects.canopies.push({ring:pts,brand:tags.brand||stations[close].tags.brand||'',name:tags.name||stations[close].tags.name||''});
 }
 return objects;
}
export async function fetchObjects(x,y,cache,read,endpoint=process.env.SIMCRUISE_OVERPASS_URL){
 const mx=Math.floor(x/4),my=Math.floor(y/4);
 const previous='street-osm-'+mx+'-'+my;
 const key='street-osm-v2-'+mx+'-'+my,miss=path.join(cache,key+'.miss');
 const cooldown=path.join(cache,'street-osm-v2-provider.miss');
 const age=file=>Date.now()-(fs.statSync(file,{throwIfNoEntry:false})?.mtimeMs??0);
 const fallback=()=>{
  const seed=mx===honolulu.macro[0]&&my===honolulu.macro[1]?honolulu.elements:[];
  for(const file of [key,previous]){
   try{
    const saved=JSON.parse(fs.readFileSync(path.join(cache,file),'utf8'));
    if(Array.isArray(saved.elements)&&!saved.remark)return streetObjects([...saved.elements,...seed],x,y);
   }catch{}
  }
  return streetObjects(seed,x,y);
 };
 // The public Overpass service asks apps to run their own backend. Installed draws
 // reuse existing observations. Operators can supply their own interpreter.
 if(!endpoint)return fallback();
 let server;
 try{server=new URL(endpoint);if(!['http:','https:'].includes(server.protocol))return fallback()}catch{return fallback()}
 if(age(miss)<86400000 || (age(cooldown)<60000 && !fs.existsSync(path.join(cache,key))))return fallback();
 const lat=k=>Math.atan(Math.sinh(Math.PI*(1-2*k/16384)))*180/Math.PI;
 const lon=k=>k/16384*360-180;
 const bounds=[lat(my+1),lon(mx),lat(my),lon(mx+1)].map(n=>n.toFixed(6)).join(',');
 const q='[out:json][timeout:5];(node[highway=traffic_signals]('+bounds+');nwr[amenity=fuel]('+bounds+');way[building=roof]('+bounds+'););out geom;'+
  'way[building][~"^(building:(material|colour)|roof:(material|colour|shape))$"~"."]('+bounds+')(if:is_closed() && count_members()<=96 && count_tags()<=40);out geom 48;';
 try{
  server.searchParams.set('data',q);
  const bytes=await read(key,server.toString(),45*86400000,null,2500);
  const data=JSON.parse(bytes.toString());
  if(!Array.isArray(data.elements)||data.remark)throw Error('incomplete map inventory');
  return streetObjects(data.elements,x,y);
 }catch{fs.writeFileSync(miss,'');fs.writeFileSync(cooldown,'');return fallback()}
}
