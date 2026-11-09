import fs from 'node:fs';
import path from 'node:path';
import {decode} from 'fast-png';
import {cacheReader} from './atlas-cache.mjs';

const half = 20037508.342789244;
const step = 2 * half / 16384;
const pixels = 512;
const limit = 2 * 1024 * 1024;
const day = 86400000;
const base = 'https://imagery.geoplatform.gov/iipp/rest/services/';
const sources = [
 {code:'hi-naip', credit:'USDA Farm Service Agency, Aerial Photography Field Office', service:'NAIP/NAIP2021_Hawaii', extent:[-17846192.4,2139892.2,-17226396.6,2541816]},
 {code:'ak-usfs', credit:'USDA Forest Service, Alaska Region and Geospatial Technology and Applications Center', service:'Aerial_Imagery/RGBI_post2000_USFS_R10_Alaska_multiRes_Public', extent:[-16720531.8,7288900.5,-14461393.2,8771224.8]},
];
export function aerialSource(mx,my) {
 if (![mx,my].every(n=>Number.isInteger(n)&&n>=0&&n<16384)) return null;
 const west=mx*step-half, north=half-my*step;
 const bounds=[west,north-step,west+step,north];
 const source=sources.find(({extent:e})=>west<e[2] && west+step>e[0] && north>e[1] && north-step<e[3]);
 if (source) {
  const query=new URLSearchParams({bbox:bounds.join(','),bboxSR:'3857',imageSR:'3857',size:pixels+','+pixels,format:'png32',bandIds:'0,1,2',f:'image'});
  return {key:'a-'+source.code+'-'+mx+'-'+my+'.png',regional:true,credit:source.credit,url:base+source.service+'/ImageServer/exportImage?'+query};
 }
 const lon=(mx+.5)/16384*360-180,lat=Math.atan(Math.sinh(Math.PI*(1-2*(my+.5)/16384)))*180/Math.PI;
 if (lon < -125 || lon > -66.6 || lat < 24.4 || lat > 49.5) return null;
 return {key:'a-'+mx+'-'+my+'.jpg',regional:false};
}

export function validOrthophoto(bytes) {
 if (bytes.length<33 || bytes.length>limit || !bytes.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10]))) return false;
 if (bytes.readUInt32BE(16)!==pixels || bytes.readUInt32BE(20)!==pixels || bytes[24]!==8 || ![2,6].includes(bytes[25])) return false;
 const {data,channels}=decode(bytes);
 let sum=0,squares=0;
 for (let i=0;i<data.length;i+=channels) {
  if (channels===4 && data[i+3]!==255) return false; // The terrain shader samples RGB, so holes would print black.
  const intensity=.2126*data[i]+.7152*data[i+1]+.0722*data[i+2];
  sum+=intensity;squares+=intensity*intensity;
 }
 const count=pixels*pixels;
 return squares/count-(sum/count)**2>=9;
}

export async function regionalPhotograph(source,cache,onRequest,fetcher=fetch) {
 const miss=path.join(cache,source.key+'.miss');
 const noData=()=>Number(fs.readFileSync(miss,'utf8'));
 try { if(noData()>Date.now()) return null; } catch {}
 const validatedFetch=async(url,options)=>{
  // Repeat the check under the cache reader's per-image lock for concurrent misses.
  let cooldown=0;try {cooldown=noData()} catch {}
  if (cooldown>Date.now()) throw Error('Cached coverage gap');
  onRequest();
  const response=await fetcher(url,options);
  if(!response.ok) return response;
  if (Number(response.headers.get('content-length'))>limit) {await response.body?.cancel();throw Error('Aerial byte limit')}
  const reader=response.body.getReader(),parts=[];
  let length=0;
  while(true) {
   const {done,value}=await reader.read();if(done)break;
   length+=value.length;
   if(length>limit){await reader.cancel();throw Error('Aerial byte limit')}
   parts.push(value);
  }
  const bytes=Buffer.concat(parts,length);
  if(!validOrthophoto(bytes)){
   fs.writeFileSync(miss,String(Date.now()+day));
   throw Error('Incomplete, empty or invalid orthophoto');
  }
  return new Response(bytes);
 };
 const load=cacheReader(cache,()=>{},validatedFetch);
 try {
  await load(source.key,source.url,30*day,null,5000);
  return {aerial:source.key,attribution:source.credit};
 } catch {
  if(!fs.existsSync(miss) || noData()<=Date.now())fs.writeFileSync(miss,String(Date.now()+3600000));
  return null;
 }
}
