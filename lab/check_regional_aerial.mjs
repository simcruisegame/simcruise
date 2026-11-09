import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {encode} from 'fast-png';
import {aerialSource,regionalPhotograph,validOrthophoto} from '../helper/regional-aerial.mjs';

const honolulu=aerialSource(1007,7199),juneau=aerialSource(2074,4908);
assert.match(honolulu.key,/hi-naip/);
assert.match(juneau.key,/ak-usfs/);
assert.equal(aerialSource(4541,6978).key,'a-4541-6978.jpg');
assert.equal(aerialSource(0,0),null);
assert.equal(aerialSource(-1,7199),null);

const width=512,height=512,pixels=new Uint8Array(width*height*4);
function png(){return Buffer.from(encode({width,height,channels:4,data:pixels}))}
pixels.fill(255);assert.equal(validOrthophoto(png()),false);
for(let i=0;i<pixels.length;i+=4) pixels[i]=(i/4)%255;
const good=png();assert.equal(validOrthophoto(good),true);
pixels[3]=0;const hole=png();assert.equal(validOrthophoto(hole),false);
assert.equal(validOrthophoto(Buffer.from('{"error":1}')),false);
const wrong=Buffer.from(good);wrong.writeUInt32BE(99999,16);assert.equal(validOrthophoto(wrong),false);

const dir=fs.mkdtempSync(path.join(os.tmpdir(),'sunday-driver-regional-'));
try{
 let calls=0;
 const get=(source,bytes)=>regionalPhotograph(source,dir,()=>calls++,async()=>new Response(bytes));
 const rows=await Promise.all([get(honolulu,good),get(honolulu,good)]);
 assert.equal(calls,1);assert.equal(rows[0].aerial,honolulu.key);assert.equal(rows[1].aerial,honolulu.key);
 await get(honolulu,good);assert.equal(calls,1);
 const unavailable=await get(juneau,hole);assert.equal(unavailable,null);assert.equal(calls,2);
 assert.equal(await get(juneau,good),null);assert.equal(calls,2);
 assert.equal(fs.existsSync(path.join(dir,juneau.key)),false);
 console.log('Regional coverage, decode, no-data, cache coalescing and cooldown checks passed.');
}finally{fs.rmSync(dir,{recursive:true,force:true})}
