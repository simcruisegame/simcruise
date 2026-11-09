import assert from 'node:assert/strict';
import {encode} from 'fast-png';
import {correctHeightOutliers} from '../helper/regional-height-outliers.mjs';

function pixel(img,x,y,value){
 const q=Math.round((value+32768)*256),offset=(y*256+x)*3;
 img.data.set([q>>16,q>>8&255,q&255],offset);
}
function elevation(img,x,y){
 const offset=(y*256+x)*3;
 return img.data[offset]*256+img.data[offset+1]+img.data[offset+2]/256-32768;
}
function plane(value){
 const img={width:256,height:256,depth:8,channels:3,data:new Uint8Array(256*256*3)};
 for(let y=0;y<256;y++)for(let x=0;x<256;x++)pixel(img,x,y,value);
 return img;
}
const low=plane(45), witness=plane(42),z=10,tx=300,ty=400;
const u=(tx+.5)/2**z,v=(ty+.5)/2**z,extent=.5/2**z;
const local=new Map([[z+'/'+tx+'/'+ty,low]]);
let calls=0;
const fresh=()=>Date.now()+3000;
const valid=()=>{calls++;return encode(witness)};
assert.deepEqual(await correctHeightOutliers(local,z,u,v,extent,valid,fresh()),{attempts:0,corrected:0});
for(let y=99;y<101;y++)for(let x=100;x<102;x++)pixel(low,x,y,-667);
const unchanged=low.data.slice();
assert.equal((await correctHeightOutliers(local,z,u,v,extent,()=>{throw Error('unavailable')},fresh())).corrected,0);
assert.deepEqual(low.data,unchanged);
assert.equal((await correctHeightOutliers(local,z,u,v,extent,()=>encode(plane(-667)),fresh())).corrected,0);
assert.deepEqual(low.data,unchanged);
assert.deepEqual(await correctHeightOutliers(local,z,u,v,extent,valid,0),{attempts:0,corrected:0});
assert.deepEqual(await correctHeightOutliers(local,z,u,v,extent,valid,fresh()),{attempts:1,corrected:4});
assert.equal(elevation(low,100,99),42);assert.equal(elevation(low,98,96),45);
assert.equal(calls,1);
const saltFlat=plane(-80),negative=new Map([[z+'/'+tx+'/'+ty,saltFlat]]);
assert.deepEqual(await correctHeightOutliers(negative,z,u,v,extent,valid,fresh()),{attempts:0,corrected:0});
assert.equal(elevation(saltFlat,100,100),-80);
console.log('Coarse hole correction, fine-corroborated depression, missing witness, completed deadline, untouched neighbors, below-sea-level basin passed.');
