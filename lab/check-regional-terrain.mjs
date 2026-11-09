// Usage: bun lab/check-regional-terrain.mjs /path/to/dedicated/cache Utah Alaska
// Repeats each real district while blocking all network on the second pass.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {cacheReader} from '../helper/atlas-cache.mjs';
import {oceanSampler} from '../helper/regional-ocean.mjs';
import {regionalTerrain} from '../helper/regional-terrain.mjs';
import {trimSourceCache} from '../helper/cache-eviction.mjs';
const isOcean=oceanSampler();
assert(isOcean(-157.98,21.16));assert(isOcean(-149.98,61.015));
assert(!isOcean(-116.767,36.228));assert(!isOcean(-110.62409,43.75382));
const [directory,...places]=process.argv.slice(2);
if(!directory)throw Error('Provide a dedicated test cache directory');
fs.mkdirSync(directory,{recursive:true});
const sites={Utah:[37.101444,-109.990814],Wyoming:[43.753821,-110.62409],Alaska:[61.184,-149.895],Hawaii:[21.303,-157.862],Nebraska:[40.56724,-96.5909],Virginia:[37.37027,-77.90695],DeathValley:[36.228,-116.767]};
// Sample the actual simplified triangle at two independently surveyed positions.
function interpolatedHeight(mesh,vertexCount,indexCount,pointX,pointZ) {
 const indexStart=8+vertexCount*12;
 for(let index=0;index<indexCount;index+=3) {
  const corners=[];
  for(let j=0;j<3;j++) {
   const offset=8+12*mesh.readUInt32LE(indexStart+4*(index+j));
   corners.push([mesh.readFloatLE(offset),mesh.readFloatLE(offset+4),mesh.readFloatLE(offset+8)]);
  }
  const [[ax,ay,az],[bx,by,bz],[cx,cy,cz]]=corners;
  const area=(bz-cz)*(ax-cx)+(cx-bx)*(az-cz);
  const a=((bz-cz)*(pointX-cx)+(cx-bx)*(pointZ-cz))/area;
  const b=((cz-az)*(pointX-cx)+(ax-cx)*(pointZ-cz))/area;
  if(a>=-.00001 && b>=-.00001 && a+b<=1.00001)return a*ay+b*by+(1-a-b)*cy;
 }
 throw Error('Survey witness outside the mesh');
}
const independentHeights={Virginia:[37.61072270,-77.80661509,42.35],Nebraska:[40.76571018,-96.85459177,373.63]};
for(const place of places.length?places:Object.keys(sites)){
 const [lat,lon]=sites[place],x=Math.floor((lon+180)/360*8192),y=Math.floor((1-Math.asinh(Math.tan(lat*Math.PI/180))/Math.PI)*4096);
 let calls=0,bytes=0;
 const reader=cacheReader(directory,()=>calls++,async(url,options)=>{
  const result=await fetch(url,options);
  const body=result.body.pipeThrough(new TransformStream({transform(chunk,controller){bytes+=chunk.byteLength;controller.enqueue(chunk);}}));
  return new Response(body,{headers:result.headers,status:result.status,statusText:result.statusText});
 });
 const started=performance.now(),record=await regionalTerrain(x,y,directory,reader);
 trimSourceCache(directory,96*1024*1024,record);
 const file=path.join(directory,record.grid),mesh=fs.readFileSync(file);
 const vertices=mesh.readUInt32LE(0),indices=mesh.readUInt32LE(4);
 assert.equal(mesh.length,8+vertices*12+indices*4);
 assert(vertices>=4 && indices>0 && indices<=420000);
 let min=Infinity,max=-Infinity;
 for(let i=0;i<vertices;i++){
  const x=mesh.readFloatLE(8+12*i),h=mesh.readFloatLE(12+12*i),z=mesh.readFloatLE(16+12*i);
  assert(Math.abs(x)<=45000&&Math.abs(z)<=45000&&h>=-10500&&h<7000);
  min=Math.min(min,h);max=Math.max(max,h);
 }
 for(let i=0;i<indices;i++)assert(mesh.readUInt32LE(8+vertices*12+i*4)<vertices);
 let witnessHeight=null;
 if(independentHeights[place]) {
  const [lat,lon,truth]=independentHeights[place];
  const u=(lon+180)/360, v=(1-Math.asinh(Math.tan(lat*Math.PI/180))/Math.PI)/2;
  const px=(u-(x+.5)/8192)*record.metresPerWorld, pz=(v-(y+.5)/8192)*record.metresPerWorld;
  witnessHeight=interpolatedHeight(mesh,vertices,indices,px,pz);
  assert(record.heightCorrections>0 && Math.abs(witnessHeight-truth)<record.maxError+3);
 }
 if(place==="DeathValley")assert(min < -70 && !record.seaMask);
 if(place==="Hawaii")assert(record.seaSamples>100000 && record.seaMask);
 const ms=performance.now()-started,repeat=performance.now();
 const again=await regionalTerrain(x,y,directory,async()=>{throw Error('Warm scene attempted a network call');});
 assert(again.reused&&again.grid===record.grid);
 assert(mesh.equals(fs.readFileSync(file)));
 console.log(JSON.stringify({place,region:[x,y],requests:calls,transferredBytes:bytes,firstMs:Math.round(ms),warmMs:Math.round(performance.now()-repeat),warmRequests:0,meshBytes:mesh.length,vertices,triangles:indices/3,heightRangeM:[min,max],toleranceM:record.maxError,witnessHeight,correctedPixels:record.heightCorrections,verificationTiles:record.verificationTiles,oceanSamples:record.seaSamples,detailPhotos:record.photos.length,middle:!!record.middle,basePhoto:!!record.aerial,meshSHA256:crypto.createHash('sha256').update(mesh).digest('hex')}));
}
