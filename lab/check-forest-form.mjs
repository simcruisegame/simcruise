import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';
import {cacheReader} from '../helper/atlas-cache.mjs';
import {forestForm} from '../helper/forest-form.mjs';

const run=process.argv[2];
if(!run)throw Error('Supply a dedicated writable cache directory');
fs.mkdirSync(run,{recursive:true});
// The offline forest-form tiles the Godot client bundled stay at the godot-final tag.
const offlineFile=name=>execFileSync('git',['show',`godot-final:game/data/${name}`],{cwd:fileURLToPath(new URL('..',import.meta.url)),encoding:'utf8',maxBuffer:64*1024*1024});
for(const [x,y,positive] of [[3157,5973,true],[2199,5149,true],[4970,5932,true],[3792,6154,false],[3186,6371,false]]){
 let requests=0;
 const result=await forestForm(x,y,run,cacheReader(run,()=>requests++));
 assert.equal(result.status,positive?'available':'no_coverage',JSON.stringify(result));
 if(positive){
  const offline=JSON.parse(offlineFile(`forest-form-${x}-${y}.json`));
  for(const key of ['grid','bits','evtYear','nlcdYear'])assert.equal(result[key],offline[key]);
 }
 let retryReads=0;
 assert.deepEqual(await forestForm(x,y,run,()=>{retryReads++;throw Error('Network disabled')}),result);
 assert.equal(retryReads,0);
 console.log(JSON.stringify({x,y,status:result.status,accepted:result.accepted,firstRequests:requests,revisitRequests:retryReads}));
}
const empty=path.join(run,'failed-control');
fs.mkdirSync(empty,{recursive:true});
let failed=0;
const bad=()=>{failed++;throw Error('offline test')};
const r=await forestForm(3157,5973,empty,bad);
assert.equal(r.status,'unavailable');
assert.equal((await forestForm(3157,5973,empty,bad)).status,'unavailable');
assert.ok(failed<=1);
