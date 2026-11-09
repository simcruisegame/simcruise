import fs from 'node:fs';
import path from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';

const minute = 60000;
function read(file) {
 try { return fs.readFileSync(file); }
 catch (error) { if (error.code !== 'ENOENT') throw error; }
}
function atomic(file, bytes) {
 const temp = file + '.' + process.pid + '.' + crypto.randomUUID() + '.tmp';
 try { fs.writeFileSync(temp, bytes); fs.renameSync(temp, file); }
 finally { fs.rmSync(temp, {force: true}); }
}
function age(file) {
 return Date.now() - (fs.statSync(file, {throwIfNoEntry:false})?.mtimeMs ?? 0);
}
// These gates are shared by the native helper processes using this cache.
async function acquireNetworkSlot(state, host) {
 const cap=host==='tiles.openfreemap.org'?4:1;
 const federalImagery=host==='imagery.geoplatform.gov';
 const forestProvider=host==='prd-tnm.s3.amazonaws.com'||host==='index.nationalmap.gov'||host==='usgs-lidar-public.s3.amazonaws.com'||host==='lfps.usgs.gov'||host==='www.mrlc.gov'||host==='elevation.nationalmap.gov'||host==='basemap.nationalmap.gov'||host==='earth-search.aws.element84.com'||host==='gibs.earthdata.nasa.gov';
 const goes=/^noaa-goes(16|17|18|19)\.s3\.amazonaws\.com$/.test(host);
 const spacing=goes?180:(federalImagery||forestProvider)?1000:host==='aviationweather.gov'?60000:host==='mesonet.agron.iastate.edu'?61000:(host==='photon.komoot.io'||host==='api.panoramax.xyz')?1000:0;
 if(!spacing && cap===1)return ()=>{};
 const stamp=path.join(state,host+'.last');
 for(let attempt=0;attempt<100;attempt++) {
  for(let i=0;i<cap;i++) {
   const slot=path.join(state,host+'.slot-'+i);
   try{fs.mkdirSync(slot)}catch(e){
    if(e.code!=='EEXIST')throw e;
    if(age(slot)>60000)fs.rmSync(slot,{recursive:true,force:true});
    continue;
   }
   const release=()=>fs.rmSync(slot,{recursive:true,force:true});
   try{
    if(spacing && age(stamp)<spacing){
     if(goes||federalImagery||forestProvider)await delay(spacing-age(stamp));
     else throw Error('Provider minimum request interval');
    }
    if(spacing)atomic(stamp,'1');
   }catch(e){release();throw e}
   return release;
  }
  await delay(40);
 }
 throw Error('Provider concurrency limit');
}
export function cacheReader(directory, onRequest, fetcher=fetch) {
 const state=path.join(directory,'.source-state');
 fs.mkdirSync(state,{recursive:true});
 return async function data(id,url,ttl,range=null,timeout=9000,byteLimit=null,options={}) {
  const file=path.join(directory,id);
  const lock=path.join(state,id+'.lock');
  const cooldown=path.join(state,new URL(url).hostname+'.until');
  const fresh=()=>age(file)<ttl ? read(file) : undefined;
  let bytes=fresh();
  if(bytes) return bytes;
  for(let waits=0;;waits++) {
   try { fs.mkdirSync(lock); break; }
   catch(error) {
    if(error.code!=='EEXIST') throw error;
    bytes=read(file); if(bytes) return bytes;
    if(age(lock)>minute) { fs.rmSync(lock,{force:true,recursive:true}); continue; }
    if(waits>=100) throw Error('Map download already pending');
    await delay(100);
   }
  }
  let releaseSlot=()=>{};
  try {
   bytes=fresh(); if(bytes) return bytes;
   if(Number(read(cooldown)?.toString())>Date.now()) throw Error('Provider requested a pause');
   releaseSlot=await acquireNetworkSlot(state,new URL(url).hostname);
   if(Number(read(cooldown)?.toString())>Date.now())throw Error('Provider requested a pause');
   onRequest();
   const headers={'User-Agent':'SimCruise/1.1 (open-source driving simulator; interactive map)'};
   if(range)headers.Range='bytes='+range[0]+'-'+range[1];
   if(options.ifMatch)headers['If-Match']=options.ifMatch;
   const response=await fetcher(url,{signal:AbortSignal.timeout(timeout),headers});
   if(response.status===429||response.status===503) {
    const retry=response.headers.get('retry-after');
    const date=retry===null?NaN:(/^\d+$/.test(retry)?Date.now()+Number(retry)*1000:Date.parse(retry));
    const until=Number.isFinite(date)?Math.max(Date.now()+1000,date):Date.now()+minute;
    atomic(cooldown,String(Math.max(until,Number(read(cooldown)?.toString())||0)));
   }
   if(!response.ok) throw Error('Map service returned '+response.status);
   if(options.ifMatch && response.headers.get('etag')!==options.ifMatch){
    await response.body?.cancel();throw Error('Geographic product changed');
   }
   if(range && (response.status!==206 || !response.headers.get('content-range')?.startsWith('bytes '+range[0]+'-'+range[1]+'/'))) {
    await response.body?.cancel();throw Error('Requested geographic range unavailable');
   }
   if (byteLimit == null) {
    bytes=Buffer.from(await response.arrayBuffer());
   } else {
    if (Number(response.headers.get('content-length'))>byteLimit) {
     await response.body?.cancel();throw Error('Map response exceeds byte budget');
    }
    const chunks=[];let total=0;
    for await (const chunk of response.body) {
     total+=chunk.byteLength;
     if (total>byteLimit) throw Error('Map response exceeds byte budget');
     chunks.push(chunk);
    }
    bytes=Buffer.concat(chunks,total);
   }
   if(range && bytes.length!==range[1]-range[0]+1)throw Error('Truncated geographic range');
   if(!bytes.length) throw Error('Empty map response');
   if(options.includeETag){
    const tag=response.headers.get('etag');
    if(!tag||tag.length>128||/[\r\n]/.test(tag))throw Error('Missing geographic product identity');
    bytes=Buffer.concat([Buffer.from(tag+'\n'),bytes]);
   }
   atomic(file,bytes);return bytes;
  } catch(error) {
   bytes=read(file);if(bytes) return bytes;
   throw error;
  } finally { releaseSlot();fs.rmSync(lock,{force:true,recursive:true}); }
 };
}
