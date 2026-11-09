// USGS seamless 1/3-arc-second bare earth. Read only the GeoTIFF directory and
// compressed raster blocks touching this tile and its road-grading apron.
import {GeoTIFF} from 'geotiff';
import fs from 'node:fs';
import path from 'node:path';
import productNames from './usgs-elevation-products.json' with {type:'json'};
const products=new Set(productNames);

const ROOT='https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/13/TIFF/current';
const DAY=86400000;
const HEADER=32768;
const BUNDLED=[[18584,25420,18589,25426],[18635,25418,18639,25423],[18661,25380,18666,25385]];
const longitude=x=>x/65536*360-180;
const latitude=y=>Math.atan(Math.sinh(Math.PI*(1-2*y/65536)))*180/Math.PI;
const bufferCopy=a=>Uint8Array.from(a).buffer;
const clamp=(n,lo,hi)=>Math.max(lo,Math.min(hi,n));
function degree(lon,lat){
 const west=Math.floor(lon),north=Math.ceil(lat);
 return (north<0?'s':'n')+String(Math.abs(north)).padStart(2,'0')+(west<0?'w':'e')+String(Math.abs(west)).padStart(3,'0');
}
// Authored terrain has its own survey, buildings and alignment. Approach its
// perimeter on the same legacy height field on which that scenery was built.
function authoredWeight(wx,wy){
 let d=Infinity;
 for(const [x0,y0,x1,y1] of BUNDLED){
  d=Math.min(d,Math.hypot(Math.max(x0-wx,0,wx-x1),Math.max(y0-wy,0,wy-y1)));
 }
 const t=clamp(d/1.25,0,1);return t*t*(3-2*t);
}
async function patch(file,bounds,cache,read,budget){
 const url=ROOT+'/'+file+'/USGS_13_'+file+'.tif';
 const missing=path.join(cache,'usgs-dem-miss-'+file);
 if(Date.now()-(fs.statSync(missing,{throwIfNoEntry:false})?.mtimeMs??0)<7*DAY)throw Error('No USGS elevation product');
 // The current product can be replaced in place. Keep its ETag together with
 // the header in one atomic cache entry, and pin all subsequent Range reads.
 let tagged;
 try{tagged=await read('usgs-dem-head-'+file,url,14*DAY,[0,HEADER-1],5500,HEADER,{includeETag:true});}
 catch(err){
  if(/returned (404|403)/.test(String(err)))fs.writeFileSync(missing,'unavailable');
  throw err;
 }
 const line=tagged.indexOf(10),etag=tagged.subarray(0,line).toString();
 if(line<2||line>128||!/^"[a-f\d]{32}(?:-\d+)?"$/i.test(etag))throw Error('Invalid USGS product identity');
 const head=tagged.subarray(line+1),version=etag.slice(1,-1);
 const memo=new Map();
 const source={fetch:slices=>Promise.all(slices.map(({offset,length})=>{
  if(!Number.isSafeInteger(offset)||!Number.isSafeInteger(length)||length<=0||offset<0)throw Error('Invalid TIFF range');
  if(offset+length<=head.length)return bufferCopy(head.subarray(offset,offset+length));
  const id='usgs-dem-data-'+file+'-'+version+'-'+offset+'-'+length;
  if(!memo.has(id)){
   budget.bytes+=length;budget.ranges+=1;
   if(length>1500000||budget.bytes>5800000||budget.ranges>10)throw Error('USGS elevation window exceeds read budget');
   memo.set(id,read(id,url,180*DAY,[offset,offset+length-1],6000,length,{ifMatch:etag}).then(bufferCopy).catch(err=>{
    if(/returned 412|product changed/.test(String(err))){
     const filePath=path.join(cache,'usgs-dem-head-'+file);
     // A different helper may already have installed the newer header.
     try{if(fs.readFileSync(filePath).equals(tagged))fs.unlinkSync(filePath);}catch{}
    }
    throw err;
   }));
  }
  return memo.get(id);
 }))};
 const raster=await (await GeoTIFF.fromSource(source)).getImage();
 const keys=raster.getGeoKeys(),[west,north]=raster.getOrigin(),[dx,dy]=raster.getResolution();
 if(keys.GeographicTypeGeoKey!==4269||keys.GTRasterTypeGeoKey!==1||!(dx>0&&dx<.001&&dy<0&&dy>-.001))throw Error('Unsupported USGS elevation reference');
 const u=lon=>(lon-west)/dx-.5,v=lat=>(lat-north)/dy-.5;
 const [left,bottom,right,top]=bounds;
 const x0=Math.floor(u(left)),y0=Math.floor(v(top)),x1=Math.floor(u(right))+2,y1=Math.floor(v(bottom))+2;
 if(x0<0||y0<0||x1>raster.getWidth()||y1>raster.getHeight()||(x1-x0)*(y1-y0)>90000)throw Error('Incomplete USGS product window');
 const values=await raster.readRasters({window:[x0,y0,x1,y1],samples:[0],interleave:true});
 const stride=x1-x0,nodata=raster.getGDALNoData();
 const sample=(lon,lat)=>{
  const px=u(lon)-x0,py=v(lat)-y0,x=Math.floor(px),y=Math.floor(py),a=px-x,b=py-y;
  if(x<0||y<0||x+1>=stride||y+1>=y1-y0)return null;
  const i=y*stride+x,A=values[i],B=values[i+1],C=values[i+stride],D=values[i+stride+1];
  if(![A,B,C,D].every(h=>Number.isFinite(h)&&h!==nodata&&h>-450&&h<6500))return null;
  return (1-b)*((1-a)*A+a*B)+b*((1-a)*C+a*D);
 };
 sample.etag=version;
 return sample;
}
export async function measuredGround(X,Y,cache,read){
 const side=611.4962263*Math.cos(latitude(Y+.5)*Math.PI/180),apron=74/side;
 const left=longitude(X-apron),right=longitude(X+1+apron),top=latitude(Y-apron),bottom=latitude(Y+1+apron);
 // 3DEP coverage includes US territories. A cached per-degree negative avoids
 // querying a missing island or a neighboring country's file on every tile.
 if(bottom>73||top< -15)return null;
 if(BUNDLED.some(([x0,y0,x1,y1])=>X-apron>=x0&&X+1+apron<=x1&&Y-apron>=y0&&Y+1+apron<=y1))return null;
 const budget={bytes:0,ranges:0},maps=new Map();
 try{
  for(let west=Math.floor(left);west<=Math.floor(right);west++){
   for(let north=Math.ceil(bottom);north<=Math.ceil(top);north++){
    const key=degree(west+.5,north-.5);
    if(!products.has(key))continue;
    const bounds=[Math.max(left,west),Math.max(bottom,north-1),Math.min(right,west+1),Math.min(top,north)];
    maps.set(key,await patch(key,bounds,cache,read,budget));
   }
  }
  if(!maps.size)return null;
  return {source:'USGS 3DEP 1/3 arc-second',products:[...maps.keys()],versions:Object.fromEntries([...maps].map(([id,f])=>[id,f.etag])),weight:authoredWeight,
   sample:(x,y)=>{const lon=longitude(x),lat=latitude(y);return maps.get(degree(lon,lat))?.(lon,lat)??null;}};
 }catch{return null;}
}
