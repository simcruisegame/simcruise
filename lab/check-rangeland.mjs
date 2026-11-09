// Default is offline. With --live, check one fresh Arizona district and one revisit.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {decode} from 'fast-png';
import {shrubCover} from '../helper/shrub-cover.mjs';

const cache=fs.mkdtempSync(path.join(os.tmpdir(),'sunday-shrub-check-'));
async function rejectRead() {throw Error('Unexpected external read');}
const district=(lat,lon)=>[Math.floor((lon+180)*4096/360),Math.floor((1-Math.asinh(Math.tan(lat*Math.PI/180))/Math.PI)*2048)];
try {
 const virginia=await shrubCover(...district(37.5,-77.5),cache,rejectRead);
 assert.equal(virginia.status,'unsupported');
 await assert.rejects(shrubCover(-1,0,cache,rejectRead),/Invalid shrub district/);
 if(process.argv.includes('--live')) {
  const trace=[];
  async function boundedRead(id,url,ttl,headers,ms,cap) {
   const response=await fetch(url,{signal:AbortSignal.timeout(ms)});
   assert.equal(response.status,200);
   const blocks=[];let length=0;
   for await(const block of response.body) {
    length+=block.length;assert.ok(length<=cap);blocks.push(block);
   }
   trace.push({id,length});return Buffer.concat(blocks);
  }
  const pos=district(35.931225,-111.713203);
  const initial=await shrubCover(...pos,cache,boundedRead);
  const warm=await shrubCover(...pos,cache,rejectRead);
  assert.deepEqual(initial,warm);assert.equal(trace.length,2);
  const im=decode(fs.readFileSync(path.join(cache,warm.png)));
  let supported=0;
  for(let i=0;i<im.width*im.height;i++)if(im.data[i*3]>=4 && im.data[i*3+1]>=8)supported++;
  assert.ok(supported>0);console.log(JSON.stringify({district:pos,trace,supported,warmReads:0}));
 }
 console.log('Rangeland boundary and cache checks passed.');
} finally {fs.rmSync(cache,{recursive:true,force:true});}
