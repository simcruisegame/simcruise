// A lightweight second catalog for areas without a compatible KartaView sequence.
// Store only the license, producer, capture time and pose. Images stay on the host.
import {cacheReader} from './atlas-cache.mjs';

const day=86400000;
const uuid=/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
const licenses=new Map([
 ['CC-BY-SA-4.0','https://creativecommons.org/licenses/by-sa/4.0/'],
 ['CC-BY-4.0','https://creativecommons.org/licenses/by/4.0/'],
 ['CC0-1.0','https://creativecommons.org/publicdomain/zero/1.0/']
]);
const toLat=t=>Math.atan(Math.sinh(Math.PI*(1-2*t/65536)))*180/Math.PI;
const toLon=t=>t*360/65536-180;
export function publicReferences(doc,x,y) {
 if(!Array.isArray(doc?.features)||(doc.type!=null && doc.type!=='FeatureCollection'))throw Error('Invalid Panoramax response');
 const records=[];
 for(const f of doc.features.slice(0,40)){
  if(!f || typeof f!=='object')continue;
  const prop=f.properties??{},coords=f.geometry?.coordinates;
  const author=Array.isArray(f.providers) && f.providers.find(s=>Array.isArray(s?.roles) && s.roles.includes('producer'))?.name;
  const license=licenses.get(prop.license);
  if(!license || !uuid.test(f.id) || !uuid.test(f.collection) || !Array.isArray(coords) || f.geometry.type!=='Point' || typeof author!=='string' || !author || author.length>120)continue;
  if(!Array.isArray(f.links) || !f.links.some(l=>l?.rel==='license' && l.href===license))continue;
  const [lon,lat]=coords;
  if(!Number.isFinite(lat)||!Number.isFinite(lon)||lon<toLon(x-.3)||lon>toLon(x+1.3)||lat<toLat(y+1.3)||lat>toLat(y-.3))continue;
  const accuracy=Number(prop['quality:horizontal_accuracy']);
  if(!Number.isFinite(accuracy)||accuracy<=0||accuracy>20)continue;
  const time=prop.datetime;
  if(typeof time!=='string'||!/(?:Z|[+-]\d\d:\d\d)$/.test(time))continue;
  const ms=Date.parse(time);
  if(!Number.isFinite(ms)||ms<Date.UTC(1990,0,1)||ms>Date.now()+day)continue;
  const rawHeading=prop['view:azimuth'],fov=prop['pers:interior_orientation']?.field_of_view;
  const heading=Number.isFinite(rawHeading)&&rawHeading>=0&&rawHeading<360?rawHeading:null;
  records.push({id:f.id,source:'Panoramax',author,lon,lat,accuracyM:accuracy,capturedAt:time,captureUnix:Math.floor(ms/1000),heading,
   horizontalFov:Number.isFinite(fov)&&fov>=30&&fov<=120?fov:null,license:prop.license,licenseUrl:license,
   sourceUrl:'https://panoramax.xyz/#focus=pic&pic='+f.id});
 }
 return records;
}
export async function panoraReferences(x,y,cache,onRequest,fetcher=fetch,u=.5,v=.5) {
 if(![x,y].every(n=>Number.isInteger(n)&&n>=0&&n<65536) || ![u,v].every(n=>Number.isFinite(n)&&n>=0&&n<=1))return {status:'unavailable',reason:'invalid_location',references:[]};
 // The federated catalog orders this bounded result around the bbox center.
 // An entire tile can fill its first page with images on another block.
 const cx=Math.min(15,Math.floor(u*16)),cy=Math.min(15,Math.floor(v*16));
 const ax=x+(cx+.5)/16,ay=y+(cy+.5)/16;
 const key='panoramax-v2-'+x+'-'+y+'-'+cx+'-'+cy+'.json';
 let attempted=false,validated=false;
 const url=new URL('https://api.panoramax.xyz/api/search');
 url.search=new URLSearchParams({bbox:[toLon(ax-.22),toLat(ay+.22),toLon(ax+.22),toLat(ay-.22)].map(n=>n.toFixed(6)).join(','),limit:'24'}).toString();
 const filtered=async(address,opts)=>{
  attempted=true;
  const response=await fetcher(address,{...opts,redirect:'error'});
  if(!response.ok)return response;
  const max=480*1024;
  if(Number(response.headers.get('content-length'))>max||!response.body){await response.body?.cancel();throw Error('Catalog above budget')}
  const reader=response.body.getReader(),chunks=[];
  let sum=0;
  for(;;){const {done,value}=await reader.read();if(done)break;sum+=value.length;if(sum>max){await reader.cancel();throw Error('Catalog above budget')}chunks.push(value)}
  const filtered=publicReferences(JSON.parse(Buffer.concat(chunks)),x,y);
  validated=true;
  return new Response(JSON.stringify(filtered),{headers:{'Content-Type':'application/json'}});
 };
 const read=cacheReader(cache,onRequest,filtered);
 try{
  const references=JSON.parse((await read(key,url.href,14*day,null,5000)).toString());
  if(!Array.isArray(references))throw Error('Invalid Panoramax response');
  return {status:references.length?'available':'empty',via:validated?'provider':attempted?'stale_cache':'cache',references};
 }catch(error){
  const msg=String(error.message);
  const reason=/429|503|pause|minimum request|concurrency/i.test(msg)?'provider_backoff':/time|abort/i.test(msg)?'timeout':/budget/.test(msg)?'above_budget':/Invalid Panoramax|JSON/i.test(msg)?'invalid_response':'request_failed';
  return {status:'unavailable',reason,references:[]};
 }
}
