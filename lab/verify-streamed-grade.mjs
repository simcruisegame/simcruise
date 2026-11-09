// Usage: node lab/verify-streamed-grade.mjs /path/to/cache [/path/to/other-cache]
// Empirical input fixtures are ordinary provider cache entries. No fixture
// coordinates or city-specific exceptions enter the production road model.
import fs from 'node:fs';
import path from 'node:path';
import {gunzipSync} from 'node:zlib';
import {performance} from 'node:perf_hooks';
import assert from 'node:assert/strict';
import {VectorTile} from '@mapbox/vector-tile';
import Pbf from 'pbf';
import {decode} from 'fast-png';
import {streamedTerrain} from '../helper/road-grade.mjs';

const dirs=process.argv.slice(2);
const widths={motorway:19,trunk:16,primary:12,secondary:11,tertiary:10,minor:8,service:4.6,track:3};
function read(key){
 for(const d of dirs){
  const p=path.join(d,key);
  if(fs.existsSync(p))return fs.readFileSync(p);
 }
 throw Error('Missing input '+key);
}
const dem=(x,y)=>read('h-'+x+'-'+y);
function vector(x,y){
 const b=read('v-'+x+'-'+y);
 return new VectorTile(new Pbf(b[0]===31?gunzipSync(b):b));
}
async function field(X,Y,source){
 return streamedTerrain(X,Y,source,decode(dem(Math.floor(X/8),Math.floor(Y/8))),widths,dem);
}
function tri(h,grid,x,y){
 const gx=x*grid,gy=y*grid,i=Math.min(grid-1,Math.floor(gx)),j=Math.min(grid-1,Math.floor(gy));
 const fx=gx-i,fy=gy-j,o=j*(grid+1)+i;
 const a=h[o],b=h[o+1],c=h[o+grid+1],d=h[o+grid+2];
 return fx+fy<=1?a+(b-a)*fx+(c-a)*fy:d+(c-d)*(1-fx)+(b-d)*(1-fy);
}
const pct=(v,p)=>v.sort((a,b)=>a-b)[Math.floor((v.length-1)*p)];
const sampleSites=[['Vermont',4883,5927],['Alaska',1369,4644],['Hawaii',1008,7198],['Arizona',3091,6574],['Kansas',3633,6332],['Tennessee',4372,6463]];
let totalSamples=0;
for(const [label,mx,my] of sampleSites){
 const data=vector(mx,my),baseTile={layers:{}},layer=data.layers.transportation;
 const scale=40075016.68557849/65536*Math.cos(Math.atan(Math.sinh(Math.PI*(1-2*(my+.5)/16384))));
 const values=[[],[]];let conditioned=0,protectedCount=0,maxSeam=0;const result=[];const started=performance.now();
 for(let cy=1;cy<=2;cy++){
  result[cy]=[];
  for(let cx=1;cx<=2;cx++){
   const X=4*mx+cx,Y=4*my+cy;
   const updated=await field(X,Y,data),raw=await field(X,Y,baseTile);
   result[cy][cx]=updated;conditioned+=updated.road_grade.conditioned;protectedCount+=updated.road_grade.structureProtected;
   for(let k=0;k<layer.length;k++){
    const f=layer.feature(k),p=f.properties;
    if(!['motorway','trunk','primary','secondary','tertiary','minor'].includes(p.class)||p.brunnel||Number(p.layer??0)!==0||
       ['unpaved','gravel','dirt','ground','sand','grass'].includes(p.surface))continue;
    for(const line of f.loadGeometry())for(let j=1;j<line.length;j++){
     const a=line[j-1],b=line[j],dx=(b.x-a.x)*4/f.extent,dy=(b.y-a.y)*4/f.extent,len=Math.hypot(dx,dy);
     if(len*scale<2)continue;
     const steps=Math.max(1,Math.ceil(len*scale/9));
     for(let n=0;n<steps;n++){
      const f0=(n+.5)/steps,x=a.x*4/f.extent-cx+dx*f0,y=a.y*4/f.extent-cy+dy*f0;
      const tx=-dy/len/scale,ty=dx/len/scale;
      if(Math.min(x,y,1-x,1-y)<.02)continue;
      const points=[x+tx,y+ty,x-tx,y-ty];
      for(const [i,mesh] of [raw,updated].entries()){
       const d=tri(mesh.h,mesh.grid,points[0],points[1])-tri(mesh.h,mesh.grid,points[2],points[3]);
       values[i].push(Math.abs(Math.atan(d/2)*180/Math.PI));
      }
     }
    }
   }
  }
 }
 for(let axis=0;axis<2;axis++)for(let p=1;p<=2;p++)for(let i=0;i<=64;i++){
  const a=axis?result[1][p].h[64*65+i]:result[p][1].h[i*65+64];
  const b=axis?result[2][p].h[i]:result[p][2].h[i*65];
  maxSeam=Math.max(maxSeam,Math.abs(a-b));
 }
 assert.equal(maxSeam,0);assert(conditioned>0);
 totalSamples+=values[0].length;
 console.log(JSON.stringify({site:label,crossSlopeSamples:values[0].length,beforeP95:pct(values[0],.95),afterP95:pct(values[1],.95),conditioned,protectedCount,maxSeam,timeMs:Math.round(performance.now()-started)}));
}
// Shared z13 DEM/z14 road boundary at Stowe; the two tiles were generated
// independently from different vector and elevation source files.
const east=await field(4884*4,5927*4+2,vector(4884,5927));
const west=await field(4884*4-1,5927*4+2,vector(4883,5927));
for(let i=0;i<=64;i++)assert.equal(west.h[i*65+64],east.h[i*65]);
console.log('PASS cross-source seam across 65 vertices; total mapped-road cross slopes',totalSamples);
