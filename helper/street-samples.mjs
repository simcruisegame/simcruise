// Sample one compact sequence per driving tile. All on-disk names are derived
// from validated integers. Only processed KartaView thumbnails leave the CDN.
import fs from 'node:fs';
import path from 'node:path';
import {cacheReader} from './atlas-cache.mjs';

const day = 86400000;
const radius = 230;
const api = 'https://api.kartaview.org';
const cdn = 'https://cdn.kartaview.org';
const licenseUrl = 'https://creativecommons.org/licenses/by-sa/4.0/';
const stateAge = file => Date.now() - (fs.statSync(file,{throwIfNoEntry:false})?.mtimeMs ?? 0);
const remember = file => fs.writeFileSync(file,'-');
const meters = (a,b) => Math.hypot((a.lon-b.lon)*111195*Math.cos((a.lat+b.lat)*Math.PI/360),(a.lat-b.lat)*111195);
const angle = (a,b) => Math.abs(((a-b+540)%360)-180);
const idText = value => /^\d{1,16}$/.test(String(value)) ? String(value) : null;
function number(value,min,max) {
 if (value===null || value===undefined || value==='') return null;
 const n=Number(value);
 return Number.isFinite(n) && n>=min && n<=max ? n : null;
}
function reportedDate(value) {
 // The legacy API supplies local time without a UTC offset. Never substitute
 // date_added, timestamp or an upload-directory date as the camera time.
 if(typeof value!=='string' || !/^\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d\d:\d\d)?$/.test(value))return null;
 const date=value.replace(' ','T');
 const ms=Date.parse(date),year=Number(date.slice(0,4));
 return year>=1990 && Number.isFinite(ms) && ms<=Date.now()+day?date:null;
}
function samplePhoto(row) {
 const id=idText(row.id),sequenceId=idText(row.sequence_id);
 const sequenceIndex=number(row.sequence_index,0,10000000),lat=number(row.lat,-85,85),lon=number(row.lng,-180,180);
 if(!id || !sequenceId || !Number.isInteger(sequenceIndex) || lat===null || lon===null || row.projection!=='PLANE')return null;
 // This is the same processed large-thumbnail link form returned by
 // KartaView's /2.0/photo/ endpoint. Ignore unprocessed and arbitrary paths.
 const file=/^(storage\d{1,3})\/(files\/photo\/\d{4}\/\d{1,2}\/\d{1,2}\/lth\/[a-zA-Z0-9_-]+\.jpg)$/.exec(row.lth_name??'');
 if(!file)return null;
 const nested='https://'+file[1]+'.openstreetcam.org/'+file[2];
 const thumb=cdn+'/pr:sharp/'+Buffer.from(nested).toString('base64url');
 const author=typeof row.username==='string' ? row.username.slice(0,100).trim() : '';
 return {id,sequenceId,sequenceIndex,lat,lon,heading:number(row.heading,0,360),capturedAt:reportedDate(row.shot_date),
  captureTimeSource:reportedDate(row.shot_date)?'KartaView shot_date':null,gpsAccuracyMeters:number(row.gps_accuracy,0.01,10000),
  cameraType:'PLANE',author:author||'KartaView contributor',sourceUrl:'https://kartaview.org/details/'+sequenceId+'/'+sequenceIndex,
  licenseUrl,thumb};
}
function validJpeg(data) {
 if(data.length<16||data[0]!==255||data[1]!==216||data.lastIndexOf(Buffer.from([255,217]))<data.length-256)return false;
 for(let i=2;i+9<data.length;){
  if(data[i++]!==255)return false;
  let mark=data[i++];while(mark===255 && i<data.length)mark=data[i++];
  if(mark===0xd9||mark===0xda)return false;
  const length=data.readUInt16BE(i);if(length<2||i+length>data.length)return false;
  if([0xc0,0xc1,0xc2].includes(mark)){
   const h=data.readUInt16BE(i+3),w=data.readUInt16BE(i+5);
   return w>=120 && h>=90 && w<=2048 && h<=2048 && w*h<=2100000;
  }
  i+=length;
 }
 return false;
}
async function limitedFetch(url,options) {
 const endpoint=new URL(url);
 const list=endpoint.origin===api && endpoint.pathname==='/1.0/list/nearby-photos/';
 const image=endpoint.origin===cdn && /^\/pr:sharp\/[A-Za-z0-9_-]+$/.test(endpoint.pathname) && !endpoint.search;
 if(!list&&!image)throw Error('Unknown street-image source');
 const limit=list?384*1024:320*1024;
 const init={...options,redirect:'error'};
 if(list){
  init.method='POST';init.headers={...init.headers,'Content-Type':'application/x-www-form-urlencoded'};
  init.body=endpoint.searchParams.toString();endpoint.search='';
 }
 const response=await fetch(endpoint,init);
 if(!response.ok){await response.body?.cancel();return response}
 if(Number(response.headers.get('content-length'))>limit){await response.body?.cancel();throw Error('Oversize street sample')}
 const reader=response.body.getReader(),blocks=[];let size=0;
 while(true){
  const {value,done}=await reader.read();if(done)break;
  size+=value.byteLength;
  if(size>limit){await reader.cancel();throw Error('Oversize street sample')}
  blocks.push(value);
 }
 let bytes=Buffer.concat(blocks,size);
 if(list){
  const payload=JSON.parse(bytes.toString()),rows=payload.currentPageItems??payload.osv?.photos;
  if(Number(payload.status?.apiCode)!==600 || !Array.isArray(rows))throw Error('Street source did not return a complete catalog');
  const photos=rows.slice(0,100).map(samplePhoto).filter(Boolean);
  bytes=Buffer.from(JSON.stringify({scanned:rows.length,photos}));
 }else if(!validJpeg(bytes))throw Error('Invalid or oversized street thumbnail');
 return new Response(bytes,{status:200});
}

export function selectSequence(rows,center,excluded=new Set()) {
 const groups=new Map(),seen=new Set();
 for(const row of rows){
  if(excluded.has(row.id)||seen.has(row.id)||meters(row,center)>radius+1)continue;
  seen.add(row.id);if(!groups.has(row.sequenceId))groups.set(row.sequenceId,[]);
  groups.get(row.sequenceId).push(row);
 }
 const candidates=[];
 for(const group of groups.values()){
  let score=Infinity,best=[];
  group.sort((a,b)=>a.sequenceIndex-b.sequenceIndex);
  for(let i=0;i<group.length;i++)for(let j=i+1;j<group.length&&j<=i+16;j++){
   const a=group[i],b=group[j],ab=meters(a,b);
   if(ab<5||ab>39||b.sequenceIndex-a.sequenceIndex>24)continue;
   for(let k=j+1;k<group.length&&k<=j+16;k++){
    const c=group[k],bc=meters(b,c),span=meters(a,c);
    if(bc<5||bc>39||span<12||span>72||c.sequenceIndex-b.sequenceIndex>24||span<.91*(ab+bc))continue;
    const headings=[a,b,c].map(f=>f.heading).filter(v=>v!==null);
    if(headings.some(v=>angle(v,headings[0])>25))continue;
    const cost=meters(center,b)+.18*(meters(center,a)+meters(center,c))+.37*Math.abs(span-32)+.4*Math.abs(ab-bc);
    if(cost<score){score=cost;best=[a,b,c]}
   }
  }
  if(best.length){
   // Compare capture *days*; the provider often omits the local UTC offset.
   // Conservatively date a triplet by its oldest recorded day. An undated
   // frame leaves the triplet undated, even if its upload time is known.
   const days=best.map(f=>typeof f.capturedAt==='string'?Date.parse(f.capturedAt.slice(0,10)+'T00:00:00Z')/day:NaN);
   const observed=days.every(d=>Number.isFinite(d)&&d<=Date.now()/day+1)?Math.min(...days):null;
   candidates.push({frames:best,geometry:score,observed});
  }
 }
 if(!candidates.length)return [];
 const closest=Math.min(...candidates.map(c=>c.geometry));
 // Units are metres of the existing geometric cost. Freshness may break a
 // close tie; even a decades-newer capture cannot overcome an 8 m penalty.
 const local=candidates.filter(c=>c.geometry<=closest+8);
 const dated=local.filter(c=>c.observed!==null);
 const latest=dated.length?Math.max(...dated.map(c=>c.observed)):null;
 const cost=c=>c.geometry+(latest===null?0:c.observed===null?2:Math.min(8,(latest-c.observed)/365.25*1.5));
 local.sort((a,b)=>cost(a)-cost(b) || a.geometry-b.geometry || a.frames[0].sequenceId.localeCompare(b.frames[0].sequenceId));
 return local[0].frames;
}

function trimSamples(cache,keep) {
 // Independent upper bounds prevent image sampling from consuming the entire
 // shared 96 MiB geographic cache or accumulating unlimited tiny miss files.
 const items=[];
 for(const name of fs.readdirSync(cache)){
  if(!/^kv1-(?:near-\d+-\d+\.json|frame-\d+\.jpg|(?:area-\d+-\d+|frame-\d+)\.miss)$/.test(name))continue;
  const file=path.join(cache,name),stat=fs.statSync(file,{throwIfNoEntry:false});
  if(stat?.isFile())items.push({file,time:stat.mtimeMs,bytes:stat.size});
 }
 items.sort((a,b)=>b.time-a.time);
 let count=0,total=0;
 for(const entry of items.filter(e=>keep.has(e.file))){count++;total+=entry.bytes}
 for(const entry of items){
  if(keep.has(entry.file))continue;
  if(count>=640||total+entry.bytes>32*1024*1024){fs.rmSync(entry.file,{force:true});continue}
  count++;total+=entry.bytes;
 }
}
export async function streetSample(tx,ty,directory,onRequest=()=>{}) {
 if(!Number.isInteger(tx)||!Number.isInteger(ty)||tx<0||ty<0||tx>=65536||ty>=65536)throw Error('Expected a z16 tile');
 const cache=path.resolve(directory),tile=[tx,ty];
 const center={lat:Math.atan(Math.sinh(Math.PI*(1-2*(ty+.5)/65536)))*180/Math.PI,lon:(tx+.5)/65536*360-180};
 const result=(status,reason,frames=[])=>({tile,status,reason,frames,attribution:frames.length?'© Grab and KartaView Contributors. CC BY-SA 4.0.':'',licenseUrl});
 const name='kv1-near-'+tx+'-'+ty+'.json',miss=path.join(cache,'kv1-area-'+tx+'-'+ty+'.miss');
 const pause=path.join(cache,'.source-state','street-sample.until');
 const markers=new Map();
 const read=cacheReader(cache,()=>{},async (source,options)=>{
  const marker=markers.get(source);
  // cacheReader owns the per-key cross-process lock at this point. Check
  // failures here as well, so a queued process cannot repeat a failed fetch.
  if(marker && stateAge(marker.file)<marker.ttl)throw Error('Street sample is cooling down');
  if(marker?.catalog && stateAge(pause)<30000)throw Error('Street catalog is cooling down');
  onRequest();
  try{
   const response=await limitedFetch(source,options);
   if(!response.ok && marker){remember(marker.file);if(marker.catalog)remember(pause)}
   return response;
  }catch(error){
   if(marker){remember(marker.file);if(marker.catalog)remember(pause)}
   throw error;
  }
 });
 if((stateAge(miss)<15*60000 || stateAge(pause)<30000) && !fs.existsSync(path.join(cache,name)))return result('unavailable','provider_cooldown');
 const url=new URL('/1.0/list/nearby-photos/',api);
 url.search=new URLSearchParams({lat:center.lat.toFixed(6),lng:center.lon.toFixed(6),radius:String(radius),ipp:'100'}).toString();
 markers.set(url.href,{file:miss,ttl:15*60000,catalog:true});
 let catalog;
 try{catalog=JSON.parse((await read(name,url.href,14*day,null,5500)).toString())}
 catch{remember(miss);remember(pause);trimSamples(cache,new Set());return result('unavailable','catalog_unavailable')}
 const blocked=new Set(catalog.photos.filter(p=>stateAge(path.join(cache,'kv1-frame-'+p.id+'.miss'))<6*3600000).map(p=>p.id));
 const group=selectSequence(catalog.photos,center,blocked);
 if(!group.length){
  trimSamples(cache,new Set([path.join(cache,name)]));
  if(blocked.size && selectSequence(catalog.photos,center).length)return result('unavailable','thumbnail_cooldown');
  return result('no_coverage',catalog.scanned?'no_usable_planar_sequence':'no_nearby_photos');
 }
 const frames=[];
 for(const {thumb,...frame} of group){
  const file='kv1-frame-'+frame.id+'.jpg';
  markers.set(thumb,{file:path.join(cache,'kv1-frame-'+frame.id+'.miss'),ttl:6*3600000});
  try{
   await read(file,thumb,30*day,null,5500);
   const capturedAt=reportedDate(frame.capturedAt);
   frames.push({...frame,capturedAt,captureTimeSource:capturedAt?'KartaView shot_date':null,imagePath:path.join(cache,file)});
  }catch{remember(path.join(cache,'kv1-frame-'+frame.id+'.miss'))}
 }
 trimSamples(cache,new Set([path.join(cache,name),...frames.map(f=>f.imagePath)]));
 if(!frames.length){remember(miss);return result('unavailable','thumbnails_unavailable')}
 return result('available',frames.length===3?'three_compatible_poses':'partial_thumbnails',frames);
}
