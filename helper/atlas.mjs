import {observedCloud} from './observed-cloud.mjs';
import {fuseSky} from './sky-fusion.mjs';
import {photoWeather} from './photo-weather.mjs';
import {streamedRoofs} from './roof-stream.mjs';
import {cachedLaneLayout,fetchLaneDistrict} from './road-layout.mjs';
import {cachedBankSurface,fetchRoadBanks} from './bank-source.mjs';
import {applyRoadBanks} from './road-banks.mjs';
import {ridgeImagery} from './ridge-imagery.mjs';
import {summitSnow} from './summit-snow.mjs';
import {regionalSnow} from './regional-snow.mjs';
import {forestForm} from './forest-form.mjs';
import {shrubCover} from './shrub-cover.mjs';
import {spectralGround} from './spectral-ground.mjs';
import {trimSourceCache} from './cache-eviction.mjs';
import {aerialSource,regionalPhotograph} from './regional-aerial.mjs';
import {regionalTerrain} from './regional-terrain.mjs';
import {measuredGround,elevationGate} from './usgs-ground.mjs';
import {streamedTerrain} from './road-grade.mjs';
import {pedestrianSurfaces} from './pedestrian.mjs';
import {cartography} from './cartography.mjs';
import {conditions} from './conditions.mjs';
import {fetchObjects} from './street-objects.mjs';
import {mappedGuardRails} from './guard-rails.mjs';
import {panoraReferences} from './panoramax.mjs';
import {streetSample} from './street-samples.mjs';
import {estimateStreetAppearance} from './street-appearance.mjs';
import {recent} from './recent.mjs';
import {canopyObservation} from './canopy.mjs';
import {signalControls} from './signal-controls.mjs';
import {applyMunicipalCrowns} from './municipal-crowns.mjs';
import {anchorCrowns,cityInventory} from './civic-trees.mjs';
import {cacheReader} from './atlas-cache.mjs';
import {buildingUses} from './nsi-buildings.mjs';
import {buildingRows,buildingEnrichment,localBuildingTile,buildingFingerprint} from './overture-buildings.mjs';
import {vectorTile} from './vector-tile.mjs';
import {hasFootprintSurvey} from './verified-footprints.mjs';
import {roadWidths as widths} from './road-widths.mjs';
import {decode} from 'fast-png';
import {inflateSync,gunzipSync} from 'node:zlib';
import fs from 'node:fs';
import path from 'node:path';
const [command,cache,...args]=process.argv.slice(2);
if(command==='archive-cloud-read'){
 try{const {readCloud}=await import('./cloud-reader.mjs');const result=await readCloud(+cache,+args[0],+args[1]);fs.writeSync(1,JSON.stringify({result})+'\n');}
 catch{process.exitCode=1;}
 process.exit(process.exitCode??0);
}
fs.mkdirSync(cache,{recursive:true});
const day=86400000, cap=96*1024*1024;
let requests=0;
const readCached=cacheReader(cache,()=>requests++);
const data=(id,url,ttl=30*day)=>readCached(id,url,ttl);
const merc=(lon,lat)=>[(lon+180)/360*65536,(1-Math.asinh(Math.tan(lat*Math.PI/180))/Math.PI)/2*65536];
const rounded=n=>Math.round(n*100000)/100000;
function clip(a,b){let t0=0,t1=1,dx=b[0]-a[0],dy=b[1]-a[1];for(const [p,q] of [[-dx,a[0]],[dx,1-a[0]],[-dy,a[1]],[dy,1-a[1]]]){
 if(p===0){if(q<0)return null;continue}const t=q/p;if(p<0)t0=Math.max(t0,t);else t1=Math.min(t1,t);if(t0>=t1)return null
}return [[rounded(a[0]+t0*dx),rounded(a[1]+t0*dy)],[rounded(a[0]+t1*dx),rounded(a[1]+t1*dy)]]}

async function photograph(mx,my){
 const source=aerialSource(mx,my);
 if(!source)return {requests};
 if(source.regional){
  const observed=await regionalPhotograph(source,cache,()=>requests++);
  return {...observed,requests};
 }
 const size=40075016.68557849/16384,west=mx*size-20037508.342789244,north=20037508.342789244-my*size;
 const id='a-'+mx+'-'+my+'.jpg',bounds=[west,north-size,west+size,north].join(',');
 const url='https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/export?bbox='+bounds+'&bboxSR=3857&imageSR=3857&size=1024,1024&format=jpg&f=image';
 const miss=path.join(cache,id+'.miss');
 if(Date.now()-(fs.statSync(miss,{throwIfNoEntry:false})?.mtimeMs??0)<3600000)return {requests};
 try{
  const photo=await data(id,url);
  if(photo[0]!==255||photo[1]!==216){fs.rmSync(path.join(cache,id),{force:true});throw Error('Invalid aerial image')}
  return {aerial:id,requests};
 }catch{fs.writeFileSync(miss,'');return {requests}}
}
async function chunk(X,Y){
 const mx=Math.floor(X/4),my=Math.floor(Y/4),ex=Math.floor(X/8),ey=Math.floor(Y/8);
 const meta=JSON.parse((await data('manifest','https://tiles.openfreemap.org/planet',day)).toString());
 const url=meta.tiles[0].replace('{z}','14').replace('{x}',mx).replace('{y}',my);
 const aerial_key=aerialSource(mx,my)?.key??'';
 const aerial=aerial_key&&fs.existsSync(path.join(cache,aerial_key))?aerial_key:'';
 const [buf,elev,street_objects,measured,mapped_rails]=await Promise.all([data('v-'+mx+'-'+my,url),data('h-'+ex+'-'+ey,'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/13/'+ex+'/'+ey+'.png'),fetchObjects(X,Y,cache,readCached),measuredGround(X,Y,cache,readCached),(args.includes('--mapped-guard-rails')?mappedGuardRails(X,Y,cache,readCached).catch(()=>({status:'unavailable',rows:[]})):Promise.resolve({status:'off',rows:[]}))]);
 const vector=buf[0]===31?gunzipSync(buf):buf;
 const tile=vectorTile(vector),dem=decode(elev);
 const ground=elevationGate(cache,X,Y,measured);
 // A neighbouring z14 vector tile is read only when a road crosses into it near
 // this tile; the 3 × 3 ring would request it moments later anyway.
 const readVector=async(nx,ny)=>{const raw=await data('v-'+nx+'-'+ny,meta.tiles[0].replace('{z}','14').replace('{x}',nx).replace('{y}',ny));return vectorTile(raw[0]===31?gunzipSync(raw):raw)};
 // Bridge end heights shared between the tiles a deck crosses (tiny local files).
 const memo={get:key=>{try{return JSON.parse(fs.readFileSync(path.join(cache,'bridge-end-v1-'+key+'.json'),'utf8')).z}catch{return undefined}},
  set:(key,z)=>{try{fs.writeFileSync(path.join(cache,'bridge-end-v1-'+key+'.json'),JSON.stringify({z:Math.round(z*100)/100}))}catch{}}};
 const terrain=await streamedTerrain(X,Y,tile,dem,widths,(x,y)=>data('h-'+x+'-'+y,'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/13/'+x+'/'+y+'.png'),ground,readVector,memo);
 if(measured?.unavailable)terrain.road_grade.elevation_fallback=measured.reason;
 if(!args.includes('--without-road-banks'))applyRoadBanks(X,Y,tile,widths,terrain,cachedBankSurface(X,Y,cache));
 const roads=[],buildings=buildingRows(tile.layers.building,X,Y),land=[],pois=[];
 const each=(name,fn)=>{const layer=tile.layers[name];if(!layer)return;for(let i=0;i<layer.length;i++){const f=layer.feature(i);const box=f.bbox(),t=f.extent/4,sx=(X%4)*t,sy=(Y%4)*t;if(box[2]<sx||box[0]>sx+t||box[3]<sy||box[1]>sy+t)continue;fn(f,p=>[rounded((p.x-sx)/t),rounded((p.y-sy)/t)])}};
 const names=[];
 each('transportation_name',(f,conv)=>{for(const l of f.loadGeometry()){const pts=l.map(conv);names.push([pts,f.properties.name||''])}});
 each('transportation',(f,conv)=>{const p=f.properties;if(f.type!==2||p.brunnel==='tunnel'||!widths[p.class])return;
  for(const line of f.loadGeometry()){const pts=line.map(conv);let best=0.012**2,name=p.name||'';
   if(!name){
    let longest=0,aa=pts[0],bb=pts[1];for(let j=1;j<pts.length;j++){let d=Math.hypot(pts[j][0]-pts[j-1][0],pts[j][1]-pts[j-1][1]);if(d>longest){longest=d;aa=pts[j-1];bb=pts[j]}}
    const cx=(aa[0]+bb[0])/2,cy=(aa[1]+bb[1])/2;
    for(const [q,n] of names){if(!n)continue;for(let j=1;j<q.length;j++){const a=q[j-1],b=q[j],dx=b[0]-a[0],dy=b[1]-a[1],ll=dx*dx+dy*dy;if(!ll)continue;
     const align=Math.abs((dx*(bb[0]-aa[0])+dy*(bb[1]-aa[1]))/(Math.sqrt(ll)*longest));if(align<.8)continue;
     const t=Math.max(0,Math.min(1,((cx-a[0])*dx+(cy-a[1])*dy)/ll));const d=(cx-a[0]-dx*t)**2+(cy-a[1]-dy*t)**2;if(d<best){best=d;name=n}
    }}
   }
   let run=[];
   const commit=()=>{
    if(run.length>1){
     const road=[p.class,p.oneway||0,name,run,widths.of(f)];
     const bridge=p.brunnel==='bridge'||Number(p.layer??0)>0;
     if(p.surface==='unpaved'||p.surface==='paved'||bridge)road.push(p.surface==='unpaved'||p.surface==='paved'?p.surface:'');
     // Index 6 marks a structure: the deck comes from `bridges`, not the ground.
     if(bridge)road.push('bridge');
     roads.push(road);
    }
    run=[];
   };
   for(let j=1;j<pts.length;j++){const v=clip(pts[j-1],pts[j]);if(!v){commit();continue}if(!run.length)run.push(v[0]);else if(run.at(-1)[0]!==v[0][0]||run.at(-1)[1]!==v[0][1]){commit();run.push(v[0])}run.push(v[1])}commit()
  }});
 for(const name of ['landcover','park','water'])each(name,(f,conv)=>{if(f.type===3)land.push([name,f.properties.class||'',f.loadGeometry().map(r=>r.map(conv))])});
 each('poi',(f,conv)=>{if(f.type===1&&f.properties.name){const p=conv(f.loadGeometry()[0][0]);if(p.every(v=>v>=0&&v<1))pois.push([p,f.properties.name,String(f.properties.class||'')])}});
 return {id:[X,Y],aerial,aerial_key,street_objects,guard_rails:mapped_rails.rows,guard_rails_status:mapped_rails.status,live_canopy:true,footprint_survey:hasFootprintSurvey(X,Y)?buildingFingerprint(vector):'',...terrain,roads,pedestrian:pedestrianSurfaces(each,clip),buildings,land,pois,trees:[],requests};
}
try{
 let result;
 if(command==='tile'){
  result=await chunk(+args[0],+args[1]);
  const layout=cachedLaneLayout(+args[0],+args[1],result.roads,cache);
  if(layout)result.road_layout=layout;
 }
 else if(command==='roofs'){result={...await streamedRoofs(...args.slice(0,5).map(Number),cache,readCached),requests};}
 else if(command==='road-layout'){
  const x=+args[0],y=+args[1];
  result={...await fetchLaneDistrict(x,y,cache,readCached),requests};
  if(result.status==='checked' && args[2] && args[2].length<=80000){
   const roads=JSON.parse(Buffer.from(args[2],'base64').toString('utf8'));
   if(Array.isArray(roads))result.layout=cachedLaneLayout(x,y,roads,cache);
  }
 }
 else if(command==='road-banks'){result=await fetchRoadBanks(Number(args[0]),Number(args[1]),cache)}
 else if(command==='forest-form'){result={...await forestForm(Number(args[0]),Number(args[1]),cache,readCached),requests}}
 else if(command==='ground-cover'){result=await spectralGround(+args[0],+args[1],cache)}
 else if(command==='ridge'){result={...await ridgeImagery(...args.slice(0,6).map(Number),cache,readCached),requests}}
 else if(command==='summit-snow'){
  try {result={...await summitSnow(+args[0],+args[1],args[2],cache,readCached),requests};}
  catch(error){result={x:+args[0],y:+args[1],date:args[2],status:'temporarily-unavailable',requests};}
 }
 else if(command==='snow'){result={...await regionalSnow(+args[0],+args[1],args[2],readCached),requests}}
 else if(command==='landscape'){
  try{result={...await regionalTerrain(+args[0],+args[1],cache,readCached),requests};}
  catch(error){result={status:'unavailable',region:[+args[0],+args[1]],reason:error.message,requests};}
 }
 else if(command==='buildings'){
  const x=+args[0],y=+args[1];
  result={id:[x,y],status:'unavailable',usesStatus:'unavailable',release:null,attribution:'',updates:[],uses:[],additions:[],additionsStatus:'unavailable',requests:0,bytes:0};
  let baseline;
  try{baseline=localBuildingTile(x,y,cache)}catch{}
  if(baseline){
   const [heights,occupancy]=await Promise.allSettled([buildingEnrichment(x,y,cache,fetch,baseline),buildingUses(x,y,cache,fetch,baseline)]);
   if(heights.status==='fulfilled')Object.assign(result,heights.value);
   result.requests=baseline.stats.requests;result.bytes=baseline.stats.bytes;
   if(occupancy.status==='fulfilled'){
    result.uses=occupancy.value.uses;result.usesStatus=occupancy.value.status;
    result.requests+=occupancy.value.requests;result.bytes+=occupancy.value.bytes;
   }
  }
 }
 else if(command==='map'){result=await cartography(+args[0],+args[1],+args[2],data)}
 else if(command==='recent'){result={...await recent(+args[0],+args[1],readCached),requests}}
 else if(command==='shrub-cover'){
  try{result={...await shrubCover(+args[0],+args[1],cache,readCached),requests};}
  catch{result={status:'unavailable',id:[+args[0],+args[1]],requests};}
 }
 else if(command==='archive-weather'){
  const [lat,lon,epoch,elevation]=args.map(Number);
  const station=await photoWeather(lat,lon,epoch,elevation,readCached);
  if(station.reason==='archive_format'&&station.key)fs.rmSync(path.join(cache,station.key),{force:true});
  let swath;
  if(station.reports?.length){
   try{swath=await observedCloud(lat,lon,epoch,readCached);}catch{}
  }
  result={...fuseSky(station,swath),requests};
 }
 else if(command==='conditions'){result=await conditions(+args[0],+args[1],data)}
 else if(command==='street-sample'){
  const samples=await streetSample(+args[0],+args[1],cache,()=>requests++);
  const appearance=await estimateStreetAppearance(samples.frames);
  const {references,...panoramax}=samples.status==='available' ? {references:[],status:'not_requested'} :
   await panoraReferences(+args[0],+args[1],cache,()=>requests++,fetch,Number(args[2] ?? .5),Number(args[3] ?? .5));
  result={...samples,appearance,panoramax,references,requests};
 }
 else if(command==='canopy'){
  const x=+args[0],y=+args[1];
  const [base,survey,controls]=await Promise.all([
   canopyObservation(x,y,readCached),
   cityInventory(x,y,cache,()=>requests++).catch(()=>({source:null,stems:[]})),
   args.includes('--signal-controls')?signalControls(x,y,cache,readCached):null,
  ]);
  const side=611.496/Math.cosh(Math.PI*(1-2*(y+.5)/65536));
  const merged=anchorCrowns(base.crowns,survey.stems,side,base.grid);
  const geometry=await applyMunicipalCrowns(x,y,merged.trees,survey.stems,cache,()=>requests++);
  result={...merged,...geometry,inventorySource:survey.source,...(controls?{signalControls:controls}:{}),requests};
 }
 else if(command==='aerial'){result=await photograph(+args[0],+args[1])}
 else if(command==='search'){
  let q=args[0].trim().slice(0,200);let j=JSON.parse((await data('s-'+Bun.hash(q).toString(16),'https://photon.komoot.io/api/?limit=6&lang=en&q='+encodeURIComponent(q),7*day)).toString());
  const norm=s=>String(s??'').toLowerCase().replace(/[^a-z0-9 ]/g,' ').trim().replace(/\s+/g,' ');
  const parts=q.split(/\s+in\s+|,/i).map(norm);
  const streetType={rd:'road',dr:'drive',ln:'lane',st:'street',ct:'court',cir:'circle',hwy:'highway',ave:'avenue',blvd:'boulevard'};
  const street=s=>norm(s).split(' ').map(w=>streetType[w]||w).join(' ');
  const typedStreet=/\b(road|drive|lane|street|court|circle|highway|avenue|boulevard|way|trail)$/.test(street(parts[0]));
  function distance(a,b){let row=Array.from({length:b.length+1},(_,i)=>i);for(let i=0;i<a.length;i++){let next=[i+1];for(let k=0;k<b.length;k++)next.push(Math.min(next[k]+1,row[k+1]+1,row[k]+Number(a[i]!==b[k])));row=next}return row[b.length]}
  result=j.features.filter(f=>{
   const p=f.properties;if(p.countrycode?.toUpperCase()!=='US')return false;
   if(typedStreet&&![p.street,p.name].some(n=>street(n)===street(parts[0])))return false;
   return parts.slice(1).filter(Boolean).every(v=>[p.city,p.county,p.district,p.state,p.statecode,'us','usa'].filter(Boolean).some(n=>{
    const a=v.replace(/ county$/,''),b=norm(n).replace(/ county$/,'');return a===b||(a.length>=5&&distance(a,b)<=2)
   }));
  }).map(f=>{const p=f.properties;return {title:[p.name||p.street,p.city,p.county,p.state].filter((a,i,v)=>a&&v.indexOf(a)===i).join(', '),street:typedStreet?(p.street||p.name):'',lon:f.geometry.coordinates[0],lat:f.geometry.coordinates[1]}});
 }else throw Error('Unknown map request');
 trimSourceCache(cache,cap,result);console.log(JSON.stringify(result));
}catch(e){console.error(e.message);process.exitCode=1}
