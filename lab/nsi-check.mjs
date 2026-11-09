import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {buildingUses,matchedBuildingUses} from '../helper/nsi-buildings.mjs';

const ring=(l,t,r,b)=>[[l,t],[r,t],[r,b],[l,b]];
const row=(outline,extra=[])=>[[outline,...extra],[],5,0,.5,.5,'',''];
const shape=row(ring(.1,.1,.9,.9)),point=[.45,.45,'single_family',2];
const tag={building:'yes',center:[.52,.47],amenity:null};
function match(rows=[shape],points=[point],tags=[tag],blocks=[]){
 return matchedBuildingUses(rows,points,new Map(tags.map((v,i)=>[String(i),v])),.002,blocks);
}
assert.deepEqual(match()[0],{baseIndex:0,center:[.5,.5],use:'single_family',stories:2,source:'USACE NSI',basis:'modeled occupancy'});
for(const [rows,points,tags,blocks] of [
 [[shape],[point,point]], [[shape],[point,[.44,.45,null,null]]], [[shape],[point],[]], [[shape],[point],[tag,tag]],
 [[shape,shape]],[ [row(ring(.1,.1,.9,.9),[ring(.4,.4,.6,.6)])] ],[[shape],[[.101,.45,'single_family',1]]],
 [[row(ring(0,.1,.9,.9))]],[[[...shape.slice(0,6),'school','']]],[[shape],[point],[{...tag,building:'apartments'}]],
 [[shape],[point],[{...tag,shop:'supermarket'}]], [[shape],[point],[{...tag,usage:'commercial'}]],
 [[shape],[point],[tag],[{rings:[ring(.4,.4,.6,.6)],box:[.4,.4,.6,.6],index:-1}]]
]){
 assert.equal(match(rows,points,tags,blocks).length,0);
}
const x=18166,y=27914;
function feature(u,v,occ='RES1-2SNB',stories=2){
 const lon=(x+u)/65536*360-180,lat=Math.atan(Math.sinh(Math.PI*(1-2*(y+v)/65536)))*180/Math.PI;
 return {type:'Feature',geometry:{type:'Point',coordinates:[lon,lat]},properties:{occtype:occ,num_story:stories,pop2amu65:1234,val_struct:'SENSITIVE_MARKER'}};
}
const directory=fs.mkdtempSync(path.join(os.tmpdir(),'nsi-check-'));
try{
 const tags=async()=>new Map([['1',tag]]);
 const base={rows:[shape],getBuildingTags:tags};
 for(const [i,[occ,want]] of [['RES1-SLWB','single_family'],['RES3A','duplex'],['RES3F','multi_family'],['COM1','nonresidential'],['RES6',null]].entries()){
  const dir=path.join(directory,String(i));
  const fetcher=async()=>Response.json({type:'FeatureCollection',features:[feature(.45,.45,occ,9999)]});
  const cold=await buildingUses(x,y,dir,fetcher,base);
  assert.equal(cold.status,want?'available':'empty');
  assert.equal(cold.uses[0]?.use??null,want);assert.equal(cold.uses[0]?.stories??null,null);assert.equal(cold.requests,1);
  const warm=await buildingUses(x,y,dir,()=>{throw Error('unexpected network request')},base);assert.equal(warm.requests,0);assert.deepEqual(warm.uses,cold.uses);
  const saved=fs.readFileSync(path.join(dir,'nsi-uses-v1-'+x+'-'+y),'utf8');
  assert(!/pop2amu65|val_struct|SENSITIVE_MARKER|occtype|geometry/.test(saved));assert.deepEqual(JSON.parse(saved)[0].slice(2),[want,null]);
 }
 // OpenMapTiles groups disconnected buildings into one feature. Only rings
 // intersecting this z16 tile may count against its geometry safety budget.
 const far=ring(10,10,15,15).map(([x,y])=>({x,y}));
 far.push(far[0]);
 const xOffset=x%4*1024, yOffset=y%4*1024;
 const local=ring(xOffset+102,yOffset+102,xOffset+922,yOffset+922).map(([x,y])=>({x,y}));
 local.push(local[0]);
 const layer={length:1,feature:()=>({type:3,extent:4096,properties:{},bbox:()=>[0,0,4096,4096],loadGeometry:()=>[...Array(1500).fill(far),local]})};
 const grouped=await buildingUses(x,y,path.join(directory,'grouped'),async()=>Response.json({type:'FeatureCollection',features:[feature(.45,.45)]}),{...base,layer});
 assert.equal(grouped.uses.length,1);
 const dir=path.join(directory,'limit');
 const bad=await buildingUses(x,y,dir,async()=>new Response('oversize',{headers:{'content-length':1024*1024}}),base);
 assert.equal(bad.status,'unavailable');assert.equal(bad.requests,1);
 assert.equal((await buildingUses(x,y,dir,()=>{throw Error('retry too early')},base)).requests,0);
}finally{fs.rmSync(directory,{recursive:true,force:true})}
console.log('NSI PASS: unique interior join, source-tag veto, ambiguity, hole, boundary, categories, bounded network and sanitized warm/negative cache.');
