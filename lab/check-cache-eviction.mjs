import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {trimSourceCache} from '../helper/cache-eviction.mjs';
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'sunday-driver-evict-'));
const time=Date.now(),past=(time-3_600_000)/1000;
function file(name,used=past,written=past){
 const item=path.join(dir,name);
 fs.writeFileSync(item,'0123456789');
 fs.utimesSync(item,used,written);
 return item;
}
try{
 fs.mkdirSync(path.join(dir,'.source-state'));
 const staleRoad=file('v-1732-1801'),hotRoad=file('v-1733-1801',past+30);
 const aerial=file('a-7-17.jpg'),photo=file('kv1-frame-300.jpg');
 const note=file('my-own-photo.jpg'),locked=file('h-114-931');
 const lock=path.join(dir,'.source-state',path.basename(locked)+'.lock');
 fs.mkdirSync(lock);
 trimSourceCache(dir,40,{aerial:path.basename(aerial),frames:[{imagePath:photo}]},time);
 assert.ok(!fs.existsSync(staleRoad));
 for(const item of [hotRoad,aerial,photo,note,locked])assert.ok(fs.existsSync(item));
 assert.ok(Math.abs(fs.statSync(photo).mtimeMs-past*1000)<2);
 trimSourceCache(dir,0,{},time+25_000);
 for(const item of [aerial,photo,note,locked])assert.ok(fs.existsSync(item));
 assert.ok(!fs.existsSync(hotRoad));
 fs.rmdirSync(lock);
 trimSourceCache(dir,0,{},time+90_000);
 for(const item of [aerial,photo,locked])assert.ok(!fs.existsSync(item));
 assert.ok(fs.existsSync(note));
 console.log('Cache eviction passed recent aerial/photo handoff, stale-first eviction, fetch timestamp, active writer and unrelated file checks.');
}finally{fs.rmSync(dir,{recursive:true,force:true})}
