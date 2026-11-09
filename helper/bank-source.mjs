import fs from 'node:fs';
import path from 'node:path';
import {fromArrayBuffer} from 'geotiff';
import {cacheReader} from './atlas-cache.mjs';

const server='https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer';
const day=86400000, span=40075016.68557849/65536, half=20037508.342789244;
const tag=(x,y)=>'banks-v1-'+x+'-'+y;
const read=file=>{try{return JSON.parse(fs.readFileSync(file,'utf8'))}catch{return null}};
function save(file,bytes){
 const tmp=file+'.'+process.pid+'.'+crypto.randomUUID()+'.tmp';
 try{fs.writeFileSync(tmp,bytes);fs.renameSync(tmp,file)}finally{fs.rmSync(tmp,{force:true})}
}
export function selectBareEarth(features){
 if(!Array.isArray(features))return;
 const valid=features.map(f=>f.attributes??{}).filter(a=>a.Source==='USGS'&&a.ProductName==='USGS_3DEP'&&a.DEM_Type===1&&a.LowPS>0&&a.LowPS<=1.01&&Number.isSafeInteger(a.OBJECTID)&&/^North American Vertical Datum of 1988 \(NAVD 88\)$/i.test(a.VerticalDatum)&&typeof a.Name==='string'&&a.Name.length<=128);
 return valid.sort((a,b)=>a.LowPS-b.LowPS||b.OBJECTID-a.OBJECTID)[0];
}
export function bounds(x,y){
 const d=span/128,w=x*span-half,n=half-y*span;
 return [w-d/2,n-span-d/2,w+span+d/2,n+d/2];
}
export async function decodeBankTile(bytes,x,y){
 if(bytes.length>100*1024)throw Error('Oversized TIFF');
 const image=await(await fromArrayBuffer(Uint8Array.from(bytes).buffer)).getImage();
 const boundsActual=image.getBoundingBox(),expected=bounds(x,y);
 if(image.getWidth()!==129||image.getHeight()!==129||image.getSamplesPerPixel()!==1||image.getSampleFormat()!==3||image.getBitsPerSample()!==32||image.getGeoKeys()?.ProjectedCSTypeGeoKey!==3857||expected.some((v,i)=>Math.abs(v-boundsActual[i])>.02))throw Error('Unexpected 3DEP raster registration');
 const z=await image.readRasters({samples:[0],interleave:true});
 const missing=image.getGDALNoData();let valid=0;
 for(let i=0;i<z.length;i++){
  if(!Number.isFinite(z[i])||z[i]===0||z[i]===missing||z[i]<=-90||z[i]>=6500)z[i]=NaN;
  else valid++;
 }
 if(valid<z.length*.8)throw Error('Insufficient bare earth');
 return Buffer.from(z.buffer,z.byteOffset,z.byteLength);
}
export async function fetchRoadBanks(x,y,cache,fetcher=fetch,now=Date.now()){
 const status=(state,extra={})=>({id:[x,y],status:state,requests,bytes,...extra});let requests=0,bytes=0;
 if(![x,y].every(i=>Number.isInteger(i)&&i>=0&&i<65536))return status('unsupported');
 const lon=(x+.5)/65536*360-180,lat=Math.atan(Math.sinh(Math.PI*(1-2*(y+.5)/65536)))*180/Math.PI;
 if(lon< -125||lon> -66.5||lat<24.3||lat>49.6)return status('unsupported');
 const key=tag(x,y),manifest=path.join(cache,key+'.json'),grid=key+'.f32',record=read(manifest);
 if(record?.until>now&&(record.status!=='available'||fs.statSync(path.join(cache,grid),{throwIfNoEntry:false})?.size===129*129*4))return status(record.status,{source:record.source??''});
 const get=cacheReader(cache,()=>requests++,fetcher);
 const store=(state,ttl,info={})=>{save(manifest,JSON.stringify({status:state,until:now+ttl,...info}));return status(state,info)};
 const cooldown=path.join(cache,'.source-state','banks-retry.until');
 if(Number(read(cooldown))>now)return status('unavailable');
 const url=(suffix,params)=>server+suffix+'?'+new URLSearchParams(params);
 try{
  const beforeCatalog=requests;
  const catalog=await get(key+'-catalog',url('/query',{f:'json',geometry:lon.toFixed(6)+','+lat.toFixed(6),geometryType:'esriGeometryPoint',inSR:'4326',spatialRel:'esriSpatialRelIntersects',where:'Category=1 AND LowPS<=1.01 AND DEM_Type=1',outFields:'OBJECTID,Name,LowPS,VerticalDatum,Source,DEM_Type,ProductName',returnGeometry:'false',resultRecordCount:'8'}),30*day,null,14000,16*1024);
  if(requests>beforeCatalog)bytes+=catalog.length;
  let j;try{j=JSON.parse(catalog)}catch{}
  if(!j||j.error||!Array.isArray(j.features)){fs.rmSync(path.join(cache,key+'-catalog'),{force:true});throw Error('Unavailable catalog')}
  const source=j.exceededTransferLimit?null:selectBareEarth(j.features);
  if(!source)return store('unsupported',30*day);
  const beforeGrid=requests,sourceKey=key+'-'+source.OBJECTID+'-tif';
  const raw=await get(sourceKey,url('/exportImage',{bbox:bounds(x,y).map(v=>v.toFixed(8)).join(','),bboxSR:'3857',imageSR:'3857',size:'129,129',adjustAspectRatio:'false',pixelType:'F32',format:'tiff',compression:'LZ77',interpolation:'RSP_BilinearInterpolation',renderingRule:JSON.stringify({rasterFunction:'None'}),mosaicRule:JSON.stringify({mosaicMethod:'esriMosaicLockRaster',lockRasterIds:[source.OBJECTID]}),f:'image'}),30*day,null,14000,100*1024);
  if(requests>beforeGrid)bytes+=raw.length;
  let pixels;try{pixels=await decodeBankTile(raw,x,y)}catch(e){fs.rmSync(path.join(cache,sourceKey),{force:true});throw e}
  save(path.join(cache,grid),pixels);
  return store('available',30*day,{source:source.Name,oid:source.OBJECTID,datum:'NAVD88',grid});
 }catch{save(cooldown,JSON.stringify(now+60000));return store('unavailable',15*60000)}
}
export function cachedBankSurface(x,y,cache){
 const key=tag(x,y),record=read(path.join(cache,key+'.json'));if(!record||record.status!=='available'||record.until<=Date.now())return;
 try{const data=fs.readFileSync(path.join(cache,key+'.f32'));if(data.length===129*129*4)return {z:new Float32Array(Uint8Array.from(data).buffer),source:record.source}}catch{}
}
