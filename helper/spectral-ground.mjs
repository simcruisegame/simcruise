// Optional USDA NAIP low-vegetation evidence, paired with the already-streamed canopy-height tile.
import fs from 'node:fs';
import path from 'node:path';
import {inflateSync} from 'node:zlib';
import {decode,encode} from 'fast-png';
import {cacheReader} from './atlas-cache.mjs';

const service='https://imagery.geoplatform.gov/iipp/rest/services/NAIP/NAIP_plus/ImageServer';
const day=86400000;
const edge=256;
const requestBudget=200*1024;
function eligibleDate(date,now){
 const stamp=Date.parse(date+'T00:00:00Z');
 return Number.isFinite(stamp)&&new Date(stamp).toISOString().startsWith(date)&&stamp<=now&&now-stamp<=5*365.25*day&&date.slice(5,7)>='05'&&date.slice(5,7)<='10';
}
function sourceDate(name){
 const match=/^m_[0-9]{7}_(?:ne|nw|se|sw)_[0-9]{2}_(?:[0-9]{3}|1)_([0-9]{4})([0-9]{2})([0-9]{2})(?:_[0-9]{8})?$/i.exec(name);
 return match?match.slice(1,4).join('-'):'';
}
function inside(x,y,ring){
 let hit=false;
 for(let i=0,j=ring.length-1;i<ring.length;j=i++){
  const [a,b]=ring[i],[c,d]=ring[j];
  if((b>y)!==(d>y)&&x<(c-a)*(y-b)/(d-b)+a)hit=!hit;
 }
 return hit;
}
export function selectRaster(catalog,bounds,now){
 if(catalog.exceededTransferLimit||!Array.isArray(catalog.features))return null;
 const [w,s,e,n]=bounds;
 const candidates=[];
 for(const f of catalog.features){
  const a=f.attributes??{},date=sourceDate(a.name);
  if(!date||!eligibleDate(date,now)||a.year!==Number(date.slice(0,4))||a.category!==1||!Number.isSafeInteger(a.objectid)||a.lowps<=0||a.lowps>1)continue;
  const rings=f.geometry?.rings;
  if(rings?.length!==1||![[w,s],[w,n],[e,s],[e,n]].every(([x,y])=>inside(x,y,rings[0])))continue;
  candidates.push({id:a.objectid,date,name:a.name,gsd:a.lowps});
 }
 return candidates.sort((a,b)=>b.date.localeCompare(a.date)||a.id-b.id)[0]??null;
}
export function groundMask(bytes,canopy){
 if(!Buffer.isBuffer(bytes)||bytes.length<33||!bytes.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10]))||bytes.readUInt32BE(16)!==edge||bytes.readUInt32BE(20)!==edge||bytes[24]!==8||bytes[25]!==6||canopy.length!==512*512)return null;
 const im=decode(bytes);const mask=new Uint8Array(edge*edge);
 let contrasting=0,usable=0,positive=0;
 for(let y=0;y<edge;y++)for(let x=0;x<edge;x++){
  const i=y*edge+x,k=i*4;
  const nir=im.data[k],red=im.data[k+1],green=im.data[k+2];
  if(im.data[k+3]!==255)return null;
  // Ignore clipped shadows/highlights and near-black ratios. A copy of RGB
  // with a missing/duplicated NIR band must never manufacture a lawn.
  if(Math.min(nir,red,green)<4||Math.max(nir,red,green)>251||nir+red<55)continue;
  usable++;
  if(Math.abs(nir-red)>6)contrasting++;
  const ch=y*2*512+x*2;
  if(Math.max(canopy[ch],canopy[ch+1],canopy[ch+512],canopy[ch+513])>1)continue;
  const ndvi=(nir-red)/(nir+red);
  const t=Math.max(0,Math.min(1,(ndvi-.08)/.12));
  mask[i]=Math.round(255*t*t*(3-2*t));if(mask[i])positive++;
 }
 if(usable<edge*edge*.70||contrasting<usable*.02||positive<16)return null;
 return Buffer.from(encode({width:edge,height:edge,channels:1,depth:8,data:mask}));
}
function write(file,data){
 const tmp=file+'.'+process.pid+'.'+crypto.randomUUID()+'.tmp';
 try{fs.writeFileSync(tmp,data);fs.renameSync(tmp,file)}finally{fs.rmSync(tmp,{force:true})}
}
function cachedJson(file){try{return JSON.parse(fs.readFileSync(file,'utf8'))}catch{return null}}
function withinBudget(fetcher,stats){
 return async (url,options)=>{
  const res=await fetcher(url,options);
  if(!res.ok)return res;
  const limit=Math.min(requestBudget-stats.bytes,String(url).includes('/exportImage?')?180*1024:16*1024);
  if(Number(res.headers.get('content-length'))>limit){await res.body?.cancel();throw Error('Observation exceeds byte budget')}
  const reader=res.body.getReader(),parts=[];let bytes=0;
  try{
   for(;;){const {done,value}=await reader.read();if(done)break;bytes+=value.byteLength;stats.bytes+=value.byteLength;
    if(bytes>limit){await reader.cancel();throw Error('Observation exceeds byte budget')}parts.push(Buffer.from(value))}
  }finally{reader.releaseLock()}
  return new Response(Buffer.concat(parts),{status:res.status,headers:res.headers});
 };
}
export async function spectralGround(x,y,cache,fetcher=fetch,now=Date.now()){
 const stats={requests:0,bytes:0},base={id:[x,y],status:'unavailable'};
 const result=(status,extra={})=>({...base,status,...extra,...stats});
 if(![x,y].every(v=>Number.isInteger(v)&&v>=0&&v<65536))return result('unsupported');
 const key='naip-low-v1-'+x+'-'+y,png=key+'.png',manifest=path.join(cache,key+'.json');
 const cached=cachedJson(manifest);
 if(cached?.until>now){
  if(cached.status!=='available')return result(cached.status);
  if(eligibleDate(cached.date,now)&&fs.existsSync(path.join(cache,png)))return result('available',{png,date:cached.date,source:cached.source});
 }
 const done=(status,days,extra={})=>{write(manifest,JSON.stringify({status,until:now+days*day,...extra}));return result(status,extra)};
 const cooldown=path.join(cache,'.source-state','spectral-ground.until');
 if(Number(cachedJson(cooldown))>now)return result('unavailable');
 const coverFile=path.join(cache,'c2-'+x+'-'+y);
 if(!fs.existsSync(coverFile))return result('waiting_canopy');
 const half=20037508.342789244,side=2*half/65536,w=x*side-half,n=half-y*side,bounds=[w,n-side,w+side,n];
 const lon=(x+.5)/65536*360-180,lat=Math.atan(Math.sinh(Math.PI*(1-2*(y+.5)/65536)))*180/Math.PI;
 if(lon< -125||lon> -66.5||lat<24.3||lat>49.6)return done('unsupported',30);
 const get=cacheReader(cache,()=>stats.requests++,withinBudget(fetcher,stats));
 const url=(suffix,params)=>service+suffix+'?'+new URLSearchParams(params);
 try{
  const canopy=inflateSync(fs.readFileSync(coverFile),{maxOutputLength:512*512});
  if(canopy.length!==512*512)return result('waiting_canopy');
  const query=url('/query',{f:'pjson',geometry:bounds.join(','),geometryType:'esriGeometryEnvelope',inSR:'3857',outSR:'3857',where:'category=1',spatialRel:'esriSpatialRelIntersects',outFields:'objectid,name,year,state,category,lowps',returnGeometry:'true',maxAllowableOffset:'5',resultRecordCount:'4'});
  const catalog=JSON.parse(await get(key+'-catalog',query,30*day,null,10000));
  if(catalog.error||!Array.isArray(catalog.features))throw Error('Catalog unavailable');
  const source=selectRaster(catalog,bounds,now);if(!source)return done('unsupported',30);
  const meta=JSON.parse(await get('naip-low-info-'+source.id,url('/'+source.id+'/info',{f:'pjson'}),30*day,null,10000));
  if(meta.error)throw Error('Raster metadata unavailable');
  if(meta.bandCount!==4||meta.pixelType!=='U8')return done('unsupported',30);
  const exportUrl=url('/exportImage',{f:'image',bbox:bounds.join(','),bboxSR:'3857',imageSR:'3857',size:edge+','+edge,format:'png32',pixelType:'U8',interpolation:'RSP_BilinearInterpolation',bandIds:'3,0,1',renderingRule:JSON.stringify({rasterFunction:'None'}),mosaicRule:JSON.stringify({mosaicMethod:'esriMosaicLockRaster',lockRasterIds:[source.id]})});
  const raw=await get(key+'-'+source.id+'-cir',exportUrl,30*day,null,12000);
  const mask=groundMask(raw,canopy);if(!mask)return done('unsupported',30);
  write(path.join(cache,png),mask);
  return done('available',30,{png,date:source.date,source:source.name});
 }catch{
  write(cooldown,JSON.stringify(now+60000));
  return done('unavailable',15/1440);
 }
}
