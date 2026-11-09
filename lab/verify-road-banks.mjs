import assert from 'node:assert/strict';
import os from 'node:os';import fs from 'node:fs';import path from 'node:path';
import {applyRoadBanks} from '../helper/road-banks.mjs';
import {fetchRoadBanks,selectBareEarth} from '../helper/bank-source.mjs';
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'sd-bank-'));
const x=12744,y=25485,side=40075016.68557849/65536/Math.cosh(Math.PI*(1-2*(y+.5)/65536));
const edge=65,baseline=Array(edge*edge).fill(1500);
function feature(type,props,points){
 const vertices=points.map(([a,b])=>({x:a*1024,y:b*1024}));
 return {type,properties:props,extent:4096,loadGeometry:()=>[vertices],bbox:()=>[0,0,4096,4096]};
}
const surface={source:'Synthetic terrain',z:Float32Array.from({length:129*129},(_,k)=>1500+(Math.floor(k/129)/128-.5)*16)};
const adjust=(bridged=false)=>{
 const line=feature(2,{class:'primary',oneway:1,...(bridged?{brunnel:'bridge'}:{})},[[0,1.5],[1,1.5]]);
 const minor=feature(2,{class:'minor'},[[.25,1],[.25,2]]);
 const vec={layers:{transportation:{length:2,feature:i=>[line,minor][i]}}};
 const out={grid:64,h:[...baseline]};applyRoadBanks(x,y,vec,{primary:12,minor:7},out,surface);return out;
};
try{
 const adjusted=adjust();assert.ok(adjusted.road_banks.vertices>50);assert.deepEqual(adjust(true).h,baseline);
 // A coarse-only consumer can still use the original heights. The optional
 // fine packet contains sorted millimetre displacements outside road triangles.
 assert.deepEqual(adjusted.h,baseline);
 assert.equal(adjusted.road_banks.detailGrid,128);
 const packet=adjusted.road_banks.deltas;assert.equal(packet.length,2*adjusted.road_banks.vertices);
 let last=-1,nearerThanPreviousGuard=false;
 for(let k=0;k<packet.length;k+=2){
  const idx=packet[k],i=idx%129,j=Math.floor(idx/129),mm=packet[k+1];
  assert.ok(idx>last&&Number.isInteger(idx));last=idx;
  assert.ok(Number.isInteger(mm)&&Math.abs(mm)<=6000);
  assert.ok(i>0&&j>0&&i<128&&j<128);
  const primary=Math.abs(j/128-.5)*side-6,minor=Math.abs(i/128-.25)*side-3.5;
  assert.ok(Math.min(primary,minor)>Math.SQRT2*side/128+.75);
  if(primary<Math.SQRT2*side/64+2)nearerThanPreviousGuard=true;
 }
 assert.ok(nearerThanPreviousGuard,"The survey reaches the measured shoulder outside the protected carriageway.");
 const missing={grid:64,h:[...baseline]};
 applyRoadBanks(x,y,{layers:{}},{primary:12},missing,null);
 assert.deepEqual(missing,{grid:64,h:baseline});
 const source={OBJECTID:12,Name:'QA source',Source:'USGS',ProductName:'USGS_3DEP',DEM_Type:1,VerticalDatum:'North American Vertical Datum of 1988 (NAVD 88)',LowPS:1};
 assert.equal(selectBareEarth([{attributes:{...source,LowPS:3}},{attributes:source}]).OBJECTID,12);
 for(const override of [{LowPS:3},{DEM_Type:2},{VerticalDatum:'Unknown'},{Source:'Unknown'}])assert.equal(selectBareEarth([{attributes:{...source,...override}}]),undefined);
 let hits=0;const noSource=async()=>{hits++;return Response.json({features:[]})};
 assert.equal((await fetchRoadBanks(x,y,temp,noSource)).status,'unsupported');
 const warm=await fetchRoadBanks(x,y,temp,noSource);assert.equal(hits,1);assert.equal(warm.requests,0);
 const invalid=await fetchRoadBanks(-1,y,temp,noSource);assert.equal(invalid.requests,0);
 console.log('Protected road, bridge, minor road, edge, source selection and cached fallback passed');
}finally{fs.rmSync(temp,{recursive:true,force:true})}
