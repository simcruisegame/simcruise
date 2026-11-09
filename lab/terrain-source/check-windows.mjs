import assert from 'node:assert/strict';
import {measuredGround} from '../../helper/usgs-ground.mjs';
import {cacheReader} from '../../helper/atlas-cache.mjs';
const state=process.argv[2];
if(!state)throw Error('Pass a directory for the elevation source cache');
const sites=[['degree join',40,-97],['Mount Washington',44.267,-71.302],['Puerto Rico',18.446,-66.079],['Aleutian island',51.880,-176.658],['Guam',13.476,144.75],['Pacific outside the index',12,-139]];
const projected=(lat,lon)=>[(lon+180)/360*65536,(1-Math.asinh(Math.tan(lat*Math.PI/180))/Math.PI)*32768];
const report=[];
for(const [name,lat,lon] of sites){
 let requests=0;const reads=cacheReader(state,()=>requests++);const [u,v]=projected(lat,lon),x=Math.floor(u),y=Math.floor(v);const a=await measuredGround(x,y,state,reads);const n=requests;const again=await measuredGround(x,y,state,reads);
 let sample=a?.sample(u,v)??null;
 assert.equal(requests,n);assert.equal(again?.sample(u,v)??null,sample);
 let delta=null;if(name==='degree join'){
  const east=await measuredGround(x+1,y,state,reads),south=await measuredGround(x,y+1,state,reads);delta=0;
  for(let i=0;i<=64;i++){const f=i/64;for(const [wx,wy,other] of [[x+1,y+f,east],[x+f,y+1,south]]){const ha=a.sample(wx,wy),hb=other.sample(wx,wy);assert.ok(ha!=null&&hb!=null);delta=Math.max(delta,Math.abs(ha-hb));}}assert.ok(delta<.000001);
 }
 if(name!=='Pacific outside the index')assert.ok(sample!=null);else assert.equal(n,0);
 report.push({name,cold:n,warm:0,products:a?.products??[],elevation_m:sample,shared_edge_error_m:delta});console.log(JSON.stringify(report.at(-1)));
}

