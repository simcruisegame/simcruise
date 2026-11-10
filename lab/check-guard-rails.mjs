// Offline test of helper/guard-rails.mjs against a hand-written, synthetic
// Overpass-shaped answer (lab/fixtures/guard-rails-synthetic-overpass.json).
// No network: every request goes to an injected fetcher.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {cacheReader} from '../helper/atlas-cache.mjs';
import {guardRailRows,mappedGuardRails,MIRRORS} from '../helper/guard-rails.mjs';
const fixture=new URL('./fixtures/guard-rails-synthetic-overpass.json',import.meta.url);
const sample=fs.readFileSync(fixture,'utf8');
const X=18000,Y=26000;
const env={};
const cache=fs.mkdtempSync(path.join(os.tmpdir(),'guard-rails-'));
try{
 // Projection, filtering, de-duplication and clipping.
 const rows=guardRailRows(JSON.parse(sample).elements,X,Y);
 assert.equal(rows.length,2);
 assert.equal(rows[0][0],'guard_rail');assert.equal(rows[0][1].length,3);
 assert.ok(Math.abs(rows[0][1][0][0]-.2)<1e-4&&Math.abs(rows[0][1][0][1]-.3)<1e-4);
 assert.equal(rows[1][0],'jersey_barrier');
 assert.ok(Math.abs(rows[1][1].at(-1)[0]-1.02)<1e-6,'clipped at the tile margin');

 // Every mirror saturated: rotate through them inside the budget, back off,
 // then answer at once from the negative cache.
 let hosts=[],t=1e12,waits=[];
 const clock={now:()=>t,delay:async ms=>{waits.push(ms);t+=ms}};
 const busy=async url=>{hosts.push(new URL(url).hostname);t+=300;return new Response('# 502 at capacity',{status:502})};
 const readBusy=cacheReader(cache,()=>{},busy);
 let r=await mappedGuardRails(X,Y,cache,readBusy,{env,...clock,budget:6000});
 assert.deepEqual(r,{status:'unavailable',rows:[]});
 assert.equal(hosts.length,MIRRORS.length);
 assert.equal(new Set(hosts).size,MIRRORS.length,'each mirror once');
 assert.deepEqual(waits,[250,500,1000],'exponential backoff');
 hosts=[];
 r=await mappedGuardRails(X+1,Y,cache,readBusy,{env,...clock});
 assert.equal(r.status,'unavailable');assert.equal(hosts.length,0,'neighbourhood negative cache');
 r=await mappedGuardRails(X+8,Y,cache,readBusy,{env,...clock});
 assert.equal(hosts.length,0,'provider-wide pause covers other neighbourhoods');
 // Hard budget: slow servers cannot hold a tile for long.
 for(const f of fs.readdirSync(cache))if(f.endsWith('.miss'))fs.rmSync(path.join(cache,f));
 hosts=[];const before=t;
 const slow=async url=>{hosts.push(url);t+=4000;return new Response('',{status:504})};
 await mappedGuardRails(X,Y,cache,cacheReader(cache,()=>{},slow),{env,...clock,budget:6000});
 assert.ok(t-before<=6000+4000,'stays within budget plus one attempt');
 assert.ok(hosts.length<=2);

 // Overpass "remark" (partial answer) is a failure and is not cached.
 for(const f of fs.readdirSync(cache))if(f.endsWith('.miss'))fs.rmSync(path.join(cache,f));
 const partial=async()=>new Response(JSON.stringify({elements:[],remark:'runtime error: Query timed out in "query" at line 1 after 21 seconds.'}));
 r=await mappedGuardRails(X,Y,cache,cacheReader(cache,()=>{},partial),{env,...clock});
 assert.equal(r.status,'unavailable');
 assert.ok(!fs.existsSync(path.join(cache,'guard-rails-v1-4500-6500')),'partial answer not cached');

 // A healthy mirror: rows come back, are cached for the neighbourhood and
 // the negative cache is cleared.
 for(const f of fs.readdirSync(cache))if(f.endsWith('.miss'))fs.rmSync(path.join(cache,f));
 let calls=0;
 const ok=async url=>{calls++;assert.match(decodeURIComponent(new URL(url).search),/barrier~"\^\(guard_rail\|jersey_barrier\)\$"/);return new Response(sample)};
 r=await mappedGuardRails(X,Y,cache,cacheReader(cache,()=>{},ok),{env,...clock});
 assert.equal(r.status,'mapped');assert.equal(r.rows.length,2);assert.equal(calls,1);
 r=await mappedGuardRails(X+1,Y+1,cache,cacheReader(cache,()=>{},ok),{env,...clock});
 assert.equal(r.status,'mapped');assert.equal(calls,1,'one query per z14 neighbourhood');
 // Offline mode and the fixture hook never touch the network.
 r=await mappedGuardRails(X+8,Y,cache,readBusy,{env:{SIMCRUISE_GUARD_RAILS:'offline'},...clock});
 assert.equal(r.status,'offline');
 r=await mappedGuardRails(X+8,Y,cache,readBusy,{env:{SIMCRUISE_GUARD_RAILS_FIXTURE:fixture.pathname.replace(/^\/(\w:)/,'$1')},...clock});
 assert.equal(r.status,'fixture');
 console.log('PASS mapped rails: projection and clipping, mirror rotation with backoff, budget, negative cache, partial answers, neighbourhood cache');
}finally{fs.rmSync(cache,{recursive:true,force:true})}
