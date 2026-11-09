import {cacheReader} from '../helper/atlas-cache.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'drive-cache-'));
try {
 let calls=0;let limited=false;
 const fetcher=async()=>{await new Promise(r=>setTimeout(r,25));return limited?new Response('',{status:429,headers:{'Retry-After':'120'}}):new Response('tile')};
 const get=cacheReader(dir,()=>calls++,fetcher);
 const url='https://example.org/data';
 await Promise.all([get('v1',url,3600000),get('v1',url,3600000)]);
 assert.equal(calls,1);
 limited=true;
 assert.equal((await get('v1',url,0)).toString(),'tile');assert.equal(calls,2);
 await assert.rejects(get('v2',url,0),/pause/);assert.equal(calls,2);
 assert.equal((await get('v1',url,0)).toString(),'tile');assert.equal(calls,2);
 let started=0,active=0,peak=0;
 const limitedGet=cacheReader(dir,()=>started++,async()=>{active++;peak=Math.max(peak,active);await new Promise(r=>setTimeout(r,35));active--;return new Response('tile')});
 await Promise.all(Array.from({length:9},(_,i)=>limitedGet('of-'+i,'https://tiles.openfreemap.org/'+i,60000)));
 assert.equal(started,9);assert.equal(peak,4);
 await limitedGet('wx1','https://aviationweather.gov/api/data/metar',60000);
 await assert.rejects(limitedGet('wx2','https://aviationweather.gov/api/data/metar',60000),/interval/);
 assert.equal(started,10);
 console.log('PASS concurrent miss, warm read, stale fallback and provider-wide Retry-After');
} finally {fs.rmSync(dir,{recursive:true,force:true})}
